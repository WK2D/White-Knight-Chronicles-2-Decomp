// Exercise the actual SDK coherency TU with synthetic mapped bytes. Runtime
// taskset/wake boundaries are stubs; the lock, ticket registry and atomics are real.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "spu_coherency.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {
using Line=std::array<std::uint8_t,128>;
constexpr unsigned ea=0x10000,other_ea=ea+128;
std::atomic<unsigned> wakes{0},taskset_notifications{0},line_notifications{0};
std::atomic<bool> hook_valid{true};
bool compare_hook_bytes=true;
Line expected;
unsigned expected_address=ea,notification_stage=0;

Line pattern(unsigned seed) {
    Line value;for(unsigned i=0;i<value.size();++i)value[i]=std::uint8_t(seed+37u*i);
    return value;
}
void line_committed(unsigned address);
}
extern "C" {
std::uint8_t* vm_base=nullptr;
void (*g_spu_line_commit_hook)(std::uint32_t)=line_committed;
void spu_ch_wake(spu_context*){wakes.fetch_add(1,std::memory_order_relaxed);}
void spu_taskset_line_committed(std::uint32_t address) {
    // Reacquiring the real lock also detects callbacks invoked before unlock.
    spu_lockline_lock();spu_lockline_unlock();
    if(address!=expected_address)hook_valid.store(false);
    if(compare_hook_bytes) {
        if(notification_stage!=0)hook_valid.store(false);
        notification_stage=1;
    }
    taskset_notifications.fetch_add(1,std::memory_order_relaxed);
}
}
namespace {
void line_committed(unsigned address) {
    spu_lockline_lock();
    if(address!=expected_address||
       (compare_hook_bytes&&std::memcmp(vm_base+address,expected.data(),128)))hook_valid.store(false);
    spu_lockline_unlock();
    if(compare_hook_bytes) {
        if(notification_stage!=1)hook_valid.store(false);
        notification_stage=0;
    }
    line_notifications.fetch_add(1,std::memory_order_relaxed);
}
struct Ticket {
    std::uint64_t value=0;
    ~Ticket(){spu_native_release_reservation(value);}
};
struct Observer {
    std::unique_ptr<spu_context> context=std::make_unique<spu_context>();
    Observer(){spu_context_init(context.get(),1);}
    ~Observer(){spu_coh_unregister(context.get());}
    void reserve(unsigned address) {
        spu_lockline_lock();
        spu_coh_reserve(context.get(),address);
        context->resv_ea=address;context->resv_valid=1;context->event_status=0;
        spu_lockline_unlock();
    }
};
void reset(const Line& value) {
    spu_lockline_lock();std::memcpy(vm_base+ea,value.data(),128);
    if(spu_coh_is_reserved(ea))spu_coh_notify_write(ea);
    spu_lockline_unlock();
    expected_address=ea;expected=value;notification_stage=0;
    wakes=0;taskset_notifications=0;line_notifications=0;hook_valid=true;
}
void coherent_write(unsigned address,const Line& value,bool except) {
    spu_lockline_lock();std::memcpy(vm_base+address,value.data(),128);
    if(except)spu_coh_notify_write_except(address,nullptr);
    else spu_coh_notify_write(address);
    spu_lockline_unlock();
}
void protocol() {
    const auto initial=pattern(3),next=pattern(17),third=pattern(93);
    Line read{};
    reset(initial);
    {
        Ticket ticket;Observer peer;
        assert(spu_native_getllar(ea,read.data(),&ticket.value)==4&&ticket.value&&read==initial);
        peer.reserve(ea);expected=next;
        assert(spu_native_putllc(ea,next.data(),ticket.value)==0);
        assert(std::memcmp(vm_base+ea,next.data(),128)==0);
        assert(!peer.context->resv_valid&&(peer.context->event_status&SPU_EVENT_LR));
        assert(wakes==1&&taskset_notifications==1&&line_notifications==1&&hook_valid);
        assert(spu_native_putllc(ea,third.data(),ticket.value)==1);
        assert(taskset_notifications==1&&line_notifications==1);
    }
    // Native generations must reject ABA even after all128 bytes return to the
    // captured value. Both production writer-notify entry points participate.
    for(bool except:{false,true}) {
        reset(initial);Ticket ticket;
        assert(spu_native_getllar(ea,read.data(),&ticket.value)==4);
        coherent_write(ea,next,except);coherent_write(ea,initial,except);
        assert(spu_native_putllc(ea,third.data(),ticket.value)==1);
        assert(std::memcmp(vm_base+ea,initial.data(),128)==0);
        assert(taskset_notifications==0&&line_notifications==0);
    }
    // Peer snapshots conflict, but a write to an unrelated marked line does
    // not introduce a global-epoch false conflict.
    reset(initial);
    {
        Ticket a,b;assert(spu_native_getllar(ea,read.data(),&a.value)==4);
        assert(spu_native_getllar(ea,read.data(),&b.value)==4&&a.value!=b.value);
        coherent_write(other_ea,third,false);expected=next;
        assert(spu_native_putllc(ea,next.data(),a.value)==0);
        assert(spu_native_putllc(ea,third.data(),b.value)==1);
        assert(taskset_notifications==1&&line_notifications==1&&hook_valid);
    }
    reset(initial);
    {
        Ticket ticket;Observer peer;assert(spu_native_getllar(ea,read.data(),&ticket.value)==4);
        peer.reserve(ea);expected=next;
        assert(spu_native_putlluc(ea,next.data())==2);
        assert(!peer.context->resv_valid&&(peer.context->event_status&SPU_EVENT_LR)&&wakes==1);
        assert(spu_native_putllc(ea,third.data(),ticket.value)==1);
        assert(taskset_notifications==1&&line_notifications==1&&hook_valid);
    }
    reset(initial);
    {
        Ticket wrong;
        assert(spu_native_getllar(ea,read.data(),&wrong.value)==4);
        assert(spu_native_putllc(other_ea,next.data(),wrong.value)==1);
        assert(spu_native_putllc(ea,next.data(),wrong.value)==1);
        assert(!taskset_notifications&&!line_notifications);
    }
    // Release invalidated/unused tickets: slot ownership is independent from
    // this bounded architectural reservation registry.
    reset(initial);
    std::vector<std::uint64_t> tickets;
    for(unsigned i=0;i<256;++i) {
        std::uint64_t token=0;assert(spu_native_getllar(ea,read.data(),&token)==4);
        tickets.push_back(token);
    }
    Line sentinel=pattern(0xd3),saved=sentinel;std::uint64_t token=0x12345678;
    assert(spu_native_getllar(ea,sentinel.data(),&token)==-1&&sentinel==saved&&token==0x12345678);
    for(auto live:tickets)spu_native_release_reservation(live);
    Ticket reusable;assert(spu_native_getllar(ea,read.data(),&reusable.value)==4);
    assert(spu_native_getllar(ea+4,sentinel.data(),&token)==-1&&sentinel==saved&&token==0x12345678);
    assert(spu_native_putllc(ea+4,next.data(),reusable.value)==-1);
    assert(spu_native_putlluc(ea+4,next.data())==-1);
    assert(spu_native_getllar(ea,nullptr,&token)==-1&&spu_native_getllar(ea,read.data(),nullptr)==-1);
    assert(spu_native_putllc(ea,nullptr,reusable.value)==-1&&spu_native_putlluc(ea,nullptr)==-1);
    assert(!taskset_notifications&&!line_notifications&&std::memcmp(vm_base+ea,initial.data(),128)==0);
}

struct Shared {
    std::atomic<unsigned> ready{0};std::atomic<bool> start{false};
};
struct Worker {Shared* shared;unsigned commits=0,conflicts=0,errors=0;};
DWORD WINAPI increment(void* argument) {
    auto& worker=*static_cast<Worker*>(argument);auto& shared=*worker.shared;
    Line line{};std::uint64_t token=0;
    const auto first=spu_native_getllar(ea,line.data(),&token);
    shared.ready.fetch_add(1,std::memory_order_release);
    while(!shared.start.load(std::memory_order_acquire))SwitchToThread();
    if(first!=4){worker.errors=1;return 1;}
    for(unsigned attempts=0;worker.commits<100&&attempts<100000;++attempts) {
        const auto value=(unsigned(line[0])<<24)|(unsigned(line[1])<<16)|(unsigned(line[2])<<8)|line[3];
        const auto next=value+1;
        line[0]=std::uint8_t(next>>24);line[1]=std::uint8_t(next>>16);line[2]=std::uint8_t(next>>8);line[3]=std::uint8_t(next);
        const int status=spu_native_putllc(ea,line.data(),token);token=0;
        if(status==0)++worker.commits;
        else if(status==1)++worker.conflicts;
        else {++worker.errors;break;}
        if(worker.commits==100)break;
        if(spu_native_getllar(ea,line.data(),&token)!=4){++worker.errors;break;}
    }
    spu_native_release_reservation(token);
    if(worker.commits!=100)++worker.errors;
    return worker.errors?1:0;
}
unsigned concurrent() {
    unsigned conflicts=0;compare_hook_bytes=false;
    for(unsigned round=0;round<16;++round) {
        auto initial=pattern(round+61);std::fill_n(initial.begin(),4,0);reset(initial);
        Shared shared;Worker workers[2]{{&shared},{&shared}};
        HANDLE threads[2]{CreateThread(nullptr,0,increment,&workers[0],0,nullptr),
                          CreateThread(nullptr,0,increment,&workers[1],0,nullptr)};
        assert(threads[0]&&threads[1]);
        while(shared.ready.load(std::memory_order_acquire)!=2)SwitchToThread();
        shared.start.store(true,std::memory_order_release);
        assert(WaitForMultipleObjects(2,threads,TRUE,10000)==WAIT_OBJECT_0);
        for(auto thread:threads){DWORD code=1;assert(GetExitCodeThread(thread,&code)&&!code);CloseHandle(thread);}
        Line final{};Ticket receipt;assert(spu_native_getllar(ea,final.data(),&receipt.value)==4);
        const auto value=(unsigned(final[0])<<24)|(unsigned(final[1])<<16)|(unsigned(final[2])<<8)|final[3];
        assert(value==200&&std::equal(final.begin()+4,final.end(),initial.begin()+4));
        assert(workers[0].commits==100&&workers[1].commits==100&&!workers[0].errors&&!workers[1].errors);
        assert(workers[0].conflicts+workers[1].conflicts>=1);
        assert(taskset_notifications==200&&line_notifications==200&&hook_valid);
        conflicts+=workers[0].conflicts+workers[1].conflicts;
    }
    return conflicts;
}
}
int main() {
    vm_base=static_cast<std::uint8_t*>(std::calloc(1,2u<<20));assert(vm_base);
    protocol();const auto conflicts=concurrent();
    std::free(vm_base);vm_base=nullptr;
    std::printf("Native atomic production TU: CAS/ABA/both writer notifications/peer LR/256-ticket exhaustion/rejection passed; 3200 concurrent commits, %u real conflicts.\n",conflicts);
    return 0;
}
