#include "graphics_readback_queue.h"
#include "ppu_recomp.h"
#include <array>
#include <future>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace std::chrono_literals;
static void require(bool ok,const char* message) { if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);} }
extern "C" int wkc2_avatar_portrait_copy(ppu_context*);
extern "C" void wkc2_portrait_readback_tick();
extern "C" void wkc2_portrait_readback_stop();
static unsigned bridge_reads=0;
static bool bridge_result=true;
static uint32_t bridge_offset=0,bridge_bytes=0;
extern "C" uint32_t cellGcmResolveLocated(int local,uint32_t offset) {
    require(local==1,"Unexpected address space");return 0xc0000000u+offset;
}
extern "C" int rsx_draw_engine_guest_buffer_read(uint32_t location,uint32_t offset,uint32_t bytes) {
    require(location==0,"Portrait was read from the wrong GPU location");
    ++bridge_reads;bridge_offset=offset;bridge_bytes=bytes;return bridge_result;
}
int main() {
    wkc::GraphicsReadbackQueue queue;
    std::array<uint8_t,64> gpu{},cpu{};
    for(unsigned i=0;i<gpu.size();++i) gpu[i]=uint8_t(i*3+7);
    auto consume=std::async(std::launch::async,[&] {
        if(!queue.read(16,32,2s)) return false;
        for(unsigned i=16;i<48;++i) if(cpu[i]!=gpu[i]) return false;
        return true;
    });
    const auto renderer=std::this_thread::get_id();
    unsigned serviced=0;
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(consume.wait_for(0ms)!=std::future_status::ready&&std::chrono::steady_clock::now()<deadline) {
        queue.service([&](uint32_t offset,uint32_t bytes) {
            require(std::this_thread::get_id()==renderer,"Readback escaped the renderer thread");
            require(offset==16&&bytes==32,"Readback range changed");
            for(unsigned i=offset;i<offset+bytes;++i) cpu[i]=gpu[i];
            ++serviced;return true;
        });
        std::this_thread::yield();
    }
    require(consume.get()&&serviced==1,"CPU copied before its GPU source was available");
    require(cpu[15]==0&&cpu[48]==0,"Readback changed bytes outside its range");
    require(!queue.read(0,4,2ms),"Unserviced read did not time out");
    queue.service([&](uint32_t,uint32_t){++serviced;return true;});
    require(serviced==1,"A cancelled readback executed later");
    auto failed=std::async(std::launch::async,[&]{return queue.read(4,4,2s);});
    while(failed.wait_for(0ms)!=std::future_status::ready) {
        queue.service([](uint32_t,uint32_t){return false;});std::this_thread::yield();
    }
    require(!failed.get(),"Backend readback failure was reported as success");
    auto stopped=std::async(std::launch::async,[&]{return queue.read(0,4,2s);});
    queue.stop();require(!stopped.get(),"Shutdown did not unblock the reader");
    require(!queue.read(0,4,2s),"Stopped queue accepted another reader");
    ppu_context ctx{};
    ctx.gpr[4]=0x61000000;ctx.gpr[5]=0x32000;
    require(wkc2_avatar_portrait_copy(&ctx)==1&&bridge_reads==0,"Loading a stored CPU portrait issued a GPU readback");
    ctx.gpr[4]=0xcffffff0;ctx.gpr[5]=32;
    require(wkc2_avatar_portrait_copy(&ctx)==0&&bridge_reads==0,"Wrapping local-memory span was accepted");
    ctx.gpr[4]=0xce940000;ctx.gpr[5]=0x24000;
    const ppu_context preserved=ctx;
    auto bridge=std::async(std::launch::async,[&]{return wkc2_avatar_portrait_copy(&ctx);});
    while(bridge.wait_for(0ms)!=std::future_status::ready) {wkc2_portrait_readback_tick();std::this_thread::yield();}
    require(bridge.get()==1&&bridge_reads==1,"Portrait copy did not wait for its actual readback");
    require(bridge_offset==0xe940000&&bridge_bytes==0x24000,"Portrait readback span changed");
    require(std::memcmp(&preserved,&ctx,sizeof(ctx))==0,"Readback damaged the guest CPU context");
    bridge_result=false;
    auto rejected=std::async(std::launch::async,[&]{return wkc2_avatar_portrait_copy(&ctx);});
    while(rejected.wait_for(0ms)!=std::future_status::ready) {wkc2_portrait_readback_tick();std::this_thread::yield();}
    require(rejected.get()==0,"Failed readback still allowed a portrait-ready copy");
    wkc2_portrait_readback_stop();
    require(wkc2_avatar_portrait_copy(&ctx)==0,"Closed renderer accepted a portrait capture");
    return 0;
}
