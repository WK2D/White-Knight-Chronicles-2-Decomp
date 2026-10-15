#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/guest_call.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

static std::array<uint8_t,1024*1024> memory;
extern "C" { uint8_t* vm_base=memory.data(); void wkc2_register_dmux(void); void wkc2_shutdown_dmux(void); }
static std::map<uint32_t,void(*)(ppu_context*)> handlers;
extern "C" void ps3_hle_register_ctx(uint32_t n,const char*,void(*f)(ppu_context*)) {handlers[n]=f;}
static void w32(uint32_t a,uint32_t v) {for(unsigned i=0;i<4;++i) memory[a+i]=uint8_t(v>>(24-i*8));}
static uint32_t r32(uint32_t a) {return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3];}
static uint64_t r64(uint32_t a) {return uint64_t(r32(a))<<32|r32(a+4);}
static uint32_t call(const char* name,std::initializer_list<uint64_t> args) {
    ppu_context c{}; unsigned i=3; for(auto a:args) c.gpr[i++]=a;
    handlers.at(ps3_compute_nid(name))(&c); return uint32_t(c.gpr[3]);
}
static std::atomic<unsigned> done=0,found=0,flushed=0,errors=0;
static void callback(uint32_t opd,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t,uint64_t,uint64_t,uint64_t) {
    assert(a==0x10000);
    if(opd==0x1110) {
        assert(c==0xAABBCCDD && b>=0x10000 && b+16<=0x10100);
        if(r32(uint32_t(b))==0) {assert(r64(uint32_t(b)+8)==0x1122334455667788ull); ++done;}
        else ++errors;
    } else {
        assert(opd==0x2220 && (b==0x20000 || b==0x20010 || b==0x30000) && d==0x88776655);
        assert(c==b+0x50);
        if(r32(uint32_t(c))==0) ++found;
        else {assert(r32(uint32_t(c))==1); ++flushed;}
    }
}
extern "C" {ps3_guest_caller_fn g_ps3_guest_caller=callback;}
template<class P> static void wait_until(P predicate) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()) {assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield();}
}
static std::vector<uint8_t> pes(uint8_t id,std::vector<uint8_t> data) {
    unsigned n=3+unsigned(data.size()); std::vector<uint8_t> b={0,0,1,id,uint8_t(n>>8),uint8_t(n),0x80,0,0};
    b.insert(b.end(),data.begin(),data.end()); return b;
}
static void setup(uint32_t video_base=0x20000) {
    w32(0x1000,1); w32(0x1100,0x10000); w32(0x1104,0x10000);
    w32(0x1200,0x1110); w32(0x1204,0xAABBCCDD); w32(0x1304,0xA5A5A5A5);
    assert(call("cellDmuxOpen",{0x1000,0x1100,0x1200,0x1300})==0);
    assert(r32(0x1300)==0x10000 && r32(0x1304)==0xA5A5A5A5);
    w32(0x1400,0xE0); w32(0x1404,0); w32(0x1408,0); w32(0x140C,0);
    w32(0x1500,video_base); w32(0x1504,4096);
    w32(0x1600,0x2220); w32(0x1604,0x88776655);
    assert(call("cellDmuxEnableEs",{0x10000,0x1400,0x1500,0x1600,0,0x1700})==0);
    assert(r32(0x1700)==video_base);
}
int main() {
    wkc2_register_dmux(); assert(handlers.size()==11);
    assert(handlers.count(0xA2D4189B) && handlers.count(0x02170D1A)); setup();
    w32(0x1A0C,0xA5A5A5A5);
    assert(call("cellDmuxQueryAttr",{0x1000,0x1A00})==0 && r32(0x1A00)==4096 && r32(0x1A0C)==0xA5A5A5A5);
    assert(call("cellDmuxQueryEsAttr",{0x1000,0x1400,0,0x1A00})==0 && r32(0x1A00)>0x12A800);
    memory[0x1FFFF]=0x5A; memory[0x21000]=0xA5;
    std::vector<uint8_t> input;
    for(unsigned i=0;i<20;++i) {
        std::vector<uint8_t> frame(2000,uint8_t(0x50+i));
        frame[0]=frame[1]=0; frame[2]=1; frame[3]=0; frame[4]=0; frame[5]=8;
        auto p=pes(0xE0,frame); input.insert(input.end(),p.begin(),p.end());
    }
    std::memcpy(memory.data()+0x40000,input.data(),input.size());
    assert(call("cellDmuxSetStream",{0x10000,0x40000,input.size(),1,0x1122334455667788ull})==0);
    wait_until([]{return found.load()>=1;});
    assert(done.load()==0); // Full resource stalls the producer without faking completion.
    assert(call("cellDmuxSetStream",{0x10000,0x40000,input.size(),0,0})==0x80610203);
    unsigned received=0;
    auto drain_one=[&] {
        uint32_t rc=call("cellDmuxPeekAu",{0x20000,0x1800,0x1804});
        if(rc==0x80610204) return false;
        assert(rc==0); uint32_t a=r32(0x1800); assert(r32(0x1804)==0);
        assert(call("cellDmuxGetAu",{0x20000,0x1800,0x1804})==0 && r32(0x1800)==a);
        assert(a>=0x20080 && a+128+2000<=0x21000 && r32(a)==a+128 && r32(a+4)==2000);
        assert(r64(a+16)==0x1122334455667788ull && r64(a+24)==UINT64_MAX && memory[a+12]==1);
        assert(memory[r32(a)+6]==0x50+received);
        assert(call("cellDmuxGetAu",{0x20000,0x1808,0})==0x80610204);
        // Acquired AU remains valid until release, even while the worker is waiting.
        assert(memory[r32(a)+1999]==0x50+received);
        assert(call("cellDmuxReleaseAu",{0x20000})==0); ++received; return true;
    };
    wait_until([&]{drain_one(); return received==19 && done.load()==1;});
    assert(call("cellDmuxFlushEs",{0x20000})==0);
    wait_until([&]{drain_one(); return received==20 && flushed.load()==1;});
    assert(memory[0x1FFFF]==0x5A && memory[0x21000]==0xA5);
    assert(call("cellDmuxReleaseAu",{0x20000})==0x80610202);
    // LPCM-specific information is returned through a guest pointer, not a count.
    w32(0x1400,0xBD); w32(0x1404,0x40); w32(0x1500,0x30000); w32(0x1504,8192);
    w32(0x1900,48000); w32(0x1904,2); w32(0x1908,16);
    assert(call("cellDmuxEnableEs",{0x10000,0x1400,0x1500,0x1600,0x1900,0x1700})==0);
    std::vector<uint8_t> audio(1924,0x33); audio[0]=0x40; audio[1]=0x31; audio[2]=0x40; audio[3]=0;
    input=pes(0xBD,audio); std::memcpy(memory.data()+0x40000,input.data(),input.size());
    assert(call("cellDmuxSetStream",{0x10000,0x40000,input.size(),1,0x1122334455667788ull})==0);
    wait_until([]{return done.load()==2;});
    for(unsigned i=0;i<2;++i) {
        assert(call("cellDmuxGetAu",{0x30000,0x1800,0x1804})==0);
        uint32_t a=r32(0x1800),info=r32(0x1804); assert(info==a+40 && r32(a+4)==960);
        assert(memory[info]==3 && memory[info+1]==1 && memory[info+2]==1);
        assert(memory[r32(a)]==0x33 && memory[r32(a)+959]==0x33);
        assert(call("cellDmuxReleaseAu",{0x30000})==0);
    }
    assert(call("cellDmuxDisableEs",{0x30000})==0);
    assert(call("cellDmuxClose",{0x10000})==0);
    assert(call("cellDmuxGetAu",{0x30000,0x1800,0})==0x80610201);
    // Closing a producer parked on a full AU resource must join it promptly.
    setup(0x20010); input.clear();
    for(unsigned i=0;i<20;++i) {auto f=pes(0xE0,std::vector<uint8_t>{0,0,1,0,0,8,1,2,3}); input.insert(input.end(),f.begin(),f.end());}
    // Fifteen 256-byte allocations fit; the sixteenth must wait for a release.
    std::memcpy(memory.data()+0x40000,input.data(),input.size());
    unsigned before=found.load();
    assert(call("cellDmuxSetStream",{0x10000,0x40000,input.size(),1,0x1122334455667788ull})==0);
    wait_until([&]{return found.load()>=before+15;});
    assert(done.load()==2);
    assert(call("cellDmuxClose",{0x10000})==0);
    assert(errors.load()==0); wkc2_shutdown_dmux();
    std::puts("Demux guest callback, backpressure, AU lifetime and shutdown checks passed");
}
