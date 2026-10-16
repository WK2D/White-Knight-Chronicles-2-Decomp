#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/guest_call.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>
#include <vector>

static std::vector<uint8_t> memory(64*1024*1024);
extern "C" {
uint8_t* vm_base=memory.data();
uint32_t ppu_vm_size=64*1024*1024;
int g_resv_store_active=0;uint32_t g_ww_lo=0,g_ww_hi=0;
void ppu_resv_break_store(uint64_t){}void ps3_ww_report_inline(uint32_t,uint64_t,int){}
int spu_coh_is_reserved(uint32_t){return 0;}void spu_coh_notify_write(uint32_t){}
void spu_lockline_lock(){}void spu_lockline_unlock(){}
void wkc2_register_vdec();void wkc2_shutdown_vdec();
}
static std::map<uint32_t,void(*)(ppu_context*)> handlers;
extern "C" void ps3_hle_register_ctx(uint32_t n,const char*,void(*f)(ppu_context*)){handlers[n]=f;}
static void w32(uint32_t a,uint32_t v){for(unsigned i=0;i<4;++i)memory[a+i]=uint8_t(v>>(24-i*8));}
static void w64(uint32_t a,uint64_t v){w32(a,uint32_t(v>>32));w32(a+4,uint32_t(v));}
static uint32_t r32(uint32_t a){return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3];}
static uint64_t r64(uint32_t a){return uint64_t(r32(a))<<32|r32(a+4);}
static uint32_t call(const char* name,std::initializer_list<uint64_t> args){ppu_context c{};unsigned n=3;for(auto a:args)c.gpr[n++]=a;handlers.at(ps3_compute_nid(name))(&c);return uint32_t(c.gpr[3]);}
static std::atomic<unsigned> audone=0,picout=0,seqdone=0;
static void callback(uint32_t opd,uint64_t h,uint64_t type,uint64_t status,uint64_t user,uint64_t,uint64_t,uint64_t,uint64_t){
    assert(opd==0x1234&&h==0x10020&&user==0xABCD&&status==0);
    if(type==0)++audone;else if(type==1)++picout;else{assert(type==2);++seqdone;}
}
extern "C"{ps3_guest_caller_fn g_ps3_guest_caller=callback;}
template<class P>static void wait_until(P p){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!p()){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}}
static std::vector<uint8_t> read(const char* file){std::ifstream f(file,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    assert(argc==7);auto es=read(argv[1]),ref=read(argv[2]);unsigned width=std::atoi(argv[3]),height=std::atoi(argv[4]),rate_num=std::atoi(argv[5]),rate_den=std::atoi(argv[6]);
    size_t frame_size=size_t(width)*height*3/2;assert(!ref.empty()&&ref.size()%frame_size==0);
    std::vector<size_t> offsets;size_t pending=0;bool have_header=false;
    for(size_t i=0;i+4<es.size();++i)if(!es[i]&&!es[i+1]&&es[i+2]==1){
        if((es[i+3]==0xB3||es[i+3]==0xB8)&&!have_header){pending=i;have_header=true;}
        if(es[i+3]==0){offsets.push_back(have_header?pending:i);have_header=false;}
    }
    offsets.push_back(es.size());assert(offsets.size()>1&&offsets[0]==0);
    wkc2_register_vdec();assert(handlers.size()==8);w32(0x1000,0);w32(0x1004,0);
    assert(call("cellVdecQueryAttr",{0x1000,0x1100})==0&&memory[0x1104]==4);
    w32(0x1200,0x10020);w32(0x1204,r32(0x1100));w32(0x1300,0x1234);w32(0x1304,0xABCD);
    auto rc=call("cellVdecOpen",{0x1000,0x1200,0x1300,0x1400});if(rc==0x80610180)return 77;
    assert(rc==0&&r32(0x1400)==0x10020);w32(0x1500,3);w32(0x1504,0);memory[0x1508]=255;
    for(unsigned pass=0;pass<2;++pass){
        assert(call("cellVdecStartSeq",{0x10020})==0);auto base_a=audone.load(),base_p=picout.load(),base_s=seqdone.load();
        std::atomic<bool> producer_done=false;unsigned received=0;
        std::thread producer([&]{
            for(unsigned n=0;n+1<offsets.size();++n){
                auto size=offsets[n+1]-offsets[n];assert(size<0x400000);std::memcpy(vm_base+0x100000,es.data()+offsets[n],size);
                w32(0x1600,0x100000);w32(0x1604,uint32_t(size));w64(0x1608,n?~uint64_t(0):90000);w64(0x1610,~uint64_t(0));w64(0x1618,0x55550000+n);
                wait_until([&]{auto r=call("cellVdecDecodeAu",{0x10020,0,0x1600});assert(!r||r==0x80610103);return !r;});
                // The submitted record and compressed bytes can be reused now:
                // the asynchronous implementation owns a copy of both.
                std::memset(vm_base+0x100000,0xA5,size);std::memset(vm_base+0x1600,0x5A,32);
            }
            assert(call("cellVdecEndSeq",{0x10020})==0);producer_done=true;
        });
        // A longer private sample deliberately fills output then pending input.
        if(offsets.size()>24)wait_until([&]{return picout.load()>=base_p+16;});
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!producer_done.load()||seqdone.load()==base_s||received<ref.size()/frame_size){
            assert(std::chrono::steady_clock::now()<deadline);
            auto r=call("cellVdecGetPicItem",{0x10020,0x1700});
            if(r==0x80610104){std::this_thread::yield();continue;}assert(r==0);
            auto item=r32(0x1700),info=r32(item+72);assert(r32(item)==0&&memory[item+12]==1);
            assert(r64(item+16)==90000+uint64_t(received)*90000*rate_den/rate_num);
            assert(memory[info+5]&&memory[info+20]==3);
            assert(call("cellVdecGetPicture",{0x10020,0x1500,0x800000})==0);
            assert(size_t(received+1)*frame_size<=ref.size());
            assert(std::memcmp(vm_base+0x800000,ref.data()+received*frame_size,frame_size)==0);++received;
        }
        producer.join();wait_until([&]{return audone.load()==base_a+offsets.size()-1&&picout.load()==base_p+received;});
        assert(received==ref.size()/frame_size);std::printf("async pass %u: %u frames match, all callbacks accounted for\n",pass,received);
    }
    assert(call("cellVdecClose",{0x10020})==0);wkc2_shutdown_vdec();
}
