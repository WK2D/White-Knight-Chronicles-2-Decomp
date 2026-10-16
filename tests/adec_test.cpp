#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/guest_call.h"
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>

static std::array<uint8_t,2*1024*1024> memory;
extern "C" {uint8_t* vm_base=memory.data(); void wkc2_register_adec(); void wkc2_shutdown_adec();}
static std::map<uint32_t,void(*)(ppu_context*)> handlers;
extern "C" void ps3_hle_register_ctx(uint32_t n,const char*,void(*f)(ppu_context*)) {handlers[n]=f;}
static void w32(uint32_t a,uint32_t v) {for(unsigned i=0;i<4;++i) memory[a+i]=uint8_t(v>>(24-i*8));}
static void w64(uint32_t a,uint64_t v) {w32(a,uint32_t(v>>32)); w32(a+4,uint32_t(v));}
static uint32_t r32(uint32_t a) {return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3];}
static uint64_t r64(uint32_t a) {return uint64_t(r32(a))<<32|r32(a+4);}
static uint32_t call(const char* name,std::initializer_list<uint64_t> args) {
    ppu_context c{}; unsigned i=3; for(auto a:args) c.gpr[i++]=a;
    handlers.at(ps3_compute_nid(name))(&c); return uint32_t(c.gpr[3]);
}
static std::atomic<unsigned> consumed=0,produced=0,ended=0;
static void callback(uint32_t opd,uint64_t h,uint64_t type,uint64_t data,uint64_t user,uint64_t,uint64_t,uint64_t,uint64_t) {
    assert(opd==0x11223344 && h==0x10000 && user==0x12345678);
    if(type==0) {assert(data==0x1600); ++consumed;}
    else if(type==1) {assert(!data); ++produced;}
    else {assert(type==3 && !data); ++ended;}
}
extern "C" {ps3_guest_caller_fn g_ps3_guest_caller=callback;}
template<class P> static void wait_until(P p) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!p()) {assert(std::chrono::steady_clock::now()<end); std::this_thread::yield();}
}
static void start(unsigned channels,unsigned depth,unsigned payload) {
    w32(0x1400,channels); w32(0x1404,1); w32(0x1408,depth); w32(0x140C,payload);
    assert(call("cellAdecStartSeq",{0x10000,0x1400})==0);
}
static void submit(unsigned size,uint64_t pts=90000) {
    w32(0x1600,0x1800); w32(0x1604,size); w64(0x1608,pts); w64(0x1610,0x1020304050607080ull);
    assert(call("cellAdecDecodeAu",{0x10000,0x1600})==0);
}
static uint32_t item() {
    wait_until([] {auto rc=call("cellAdecGetPcmItem",{0x10000,0x1700}); assert(!rc || rc==0x80610005); return !rc;});
    return r32(0x1700);
}
static void end() {
    auto e=ended.load(); assert(call("cellAdecEndSeq",{0x10000})==0);
    wait_until([&]{return ended.load()==e+1;});
}
int main(int argc,char** argv) {
    wkc2_register_adec(); assert(handlers.size()==8);
    w32(0x1000,1); assert(call("cellAdecQueryAttr",{0x1000,0x1100})==0);
    auto required=r32(0x1100); assert(required>=0x80000 && required<0x100000);
    w32(0x1200,required); w32(0x1204,0x10000); w32(0x1300,0x11223344); w32(0x1304,0x12345678);
    assert(call("cellAdecOpen",{0x1000,0x1200,0x1300,0x1500})==0 && r32(0x1500)==0x10000);
    if(argc==3) {
        // Optional private fixture: packed stereo s16be, 240 samples per AU.
        std::ifstream input(argv[1],std::ios::binary); std::ofstream output(argv[2],std::ios::binary);
        assert(input && output); start(3,1,960); unsigned count=0;
        while(input.read(reinterpret_cast<char*>(memory.data()+0x1800),960)) {
            submit(960,count ? ~uint64_t(0) : 90000);
            auto a=item(); assert(r32(a+12)==1920 && r64(a+32)==90000+uint64_t(count)*450);
            assert(call("cellAdecGetPcm",{0x10000,0x3000})==0);
            output.write(reinterpret_cast<char*>(memory.data()+0x3000),1920); ++count;
        }
        assert(input.eof() && !input.gcount() && count); end();
        assert(call("cellAdecClose",{0x10000})==0); wkc2_shutdown_adec(); return 0;
    }
    assert(call("cellAdecDecodeAu",{0x10000,0x1600})==0x80610002);
    start(3,1,4);
    // Distinct queued records retain their samples, source info and timestamps.
    for(unsigned n=0;n<16;++n) {
        memory[0x1800]=uint8_t(n); memory[0x1801]=0; memory[0x1802]=0x80; memory[0x1803]=0;
        submit(4,n ? ~uint64_t(0) : 90000);
    }
    assert(call("cellAdecDecodeAu",{0x10000,0x1600})==0x80610004);
    wait_until([] {return produced.load()==16;});
    std::array<uint32_t,16> items;
    for(unsigned n=0;n<16;++n) {
        auto a=items[n]=item(); assert(r32(a+12)==8 && r32(a+4)==0);
        assert(r32(a+16)==a+0x40 && r32(a+0x40)==3 && r32(a+0x44)==1 && r32(a+0x48)==8);
        assert(r32(a+24)==0x1800 && r32(a+28)==4 && r64(a+40)==0x1020304050607080ull);
        assert(r64(a+32)==90000+uint64_t(n)*90000/48000);
        if(n) assert(a!=items[n-1]);
    }
    assert(call("cellAdecGetPcmItem",{0x10000,0x1700})==0x80610005);
    for(unsigned n=0;n<16;++n) {
        memory[0x2008]=0xA5; assert(call("cellAdecGetPcm",{0x10000,0x2000})==0);
        assert(std::bit_cast<float>(r32(0x2000))==float(n)/128);
        assert(r32(0x2004)==0xBF800000 && memory[0x2008]==0xA5);
    }
    assert(call("cellAdecGetPcm",{0x10000,0})==0x80610005); end();
    // 24-bit mono ignores the dummy source channel and retains full precision.
    start(1,3,6); memory[0x1800]=0x7F; memory[0x1801]=memory[0x1802]=0xFF;
    memory[0x1803]=0x80; memory[0x1804]=memory[0x1805]=0;
    submit(6); assert(r32(item()+12)==4); assert(call("cellAdecGetPcm",{0x10000,0x2000})==0);
    assert(std::bit_cast<float>(r32(0x2000))==8388607.0f/8388608.0f); end();
    // Channel order for 5.1 is L,C,R,SL,SR,LFE rather than source L,R,C,SL,SR,LFE.
    start(9,1,12);
    for(unsigned n=0;n<6;++n) {memory[0x1800+n*2]=uint8_t(n+1); memory[0x1801+n*2]=0;}
    submit(12); assert(r32(item()+12)==24); assert(call("cellAdecGetPcm",{0x10000,0x2000})==0);
    unsigned order[]={1,3,2,4,5,6};
    for(unsigned n=0;n<6;++n) assert(std::bit_cast<float>(r32(0x2000+n*4))==float(order[n])/128); end();
    // Concurrent reader/producer catches dropped notifications and slot reuse.
    start(3,1,4); auto first=produced.load();
    std::thread reader([&] {
        for(unsigned n=0;n<1000;++n) {
            auto a=item(); assert(r32(a+12)==8);
            assert(call("cellAdecGetPcm",{0x10000,0x2000})==0);
            assert(std::bit_cast<float>(r32(0x2000))==float(n%128)/128);
        }
    });
    for(unsigned n=0;n<1000;++n) {
        memory[0x1800]=uint8_t(n%128); memory[0x1801]=memory[0x1802]=memory[0x1803]=0;
        w32(0x1600,0x1800); w32(0x1604,4); w64(0x1608,90000+n*2);
        wait_until([] {auto rc=call("cellAdecDecodeAu",{0x10000,0x1600}); assert(!rc || rc==0x80610004); return !rc;});
    }
    reader.join(); wait_until([&]{return produced.load()==first+1000;}); end();
    assert(call("cellAdecClose",{0x10000})==0);
    assert(call("cellAdecGetPcm",{0x10000,0})==0x80610003);
    assert(consumed.load()==produced.load()); wkc2_shutdown_adec();
}
