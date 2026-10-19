#include "ppu_recomp.h"
#include "graphics_readback_queue.h"
#include "graphics_settings.h"
#include <cstdio>

extern "C" uint32_t cellGcmResolveLocated(int,uint32_t);
extern "C" int rsx_draw_engine_guest_buffer_read(uint32_t,uint32_t,uint32_t);
extern "C" int rsx_live_draw_guest_buffer_read(uint32_t,uint32_t,uint32_t);
namespace { wkc::GraphicsReadbackQueue portrait_readback; }

// This is the game's raw avatar-image copy boundary. Loading an existing
// portrait uses ordinary CPU memory; finishing the creator reads an RSX target.
extern "C" int wkc2_avatar_portrait_copy(ppu_context* ctx) {
    const uint32_t source=uint32_t(ctx->gpr[4]),bytes=uint32_t(ctx->gpr[5]);
    if(!source||!bytes||bytes>0x32000u) return 1; // retain original validation
    const uint32_t local=cellGcmResolveLocated(1,0);
    if(!local||local==UINT32_MAX||source<local||uint64_t(source)-local>=0x10000000u) return 1;
    const uint32_t offset=source-local;
    if(uint64_t(offset)+bytes>0x10000000u) return 0;
    if(portrait_readback.read(offset,bytes,std::chrono::seconds(5))) return 1;
    std::fprintf(stderr,"[avatar portrait] GPU readback failed; empty capture was not marked ready\n");
    return 0;
}
extern "C" void wkc2_portrait_readback_tick() {
    portrait_readback.service([](uint32_t offset,uint32_t bytes) {
        if(wkc::graphics_active_renderer())
            return rsx_live_draw_guest_buffer_read(0,offset,bytes)!=0;
        return rsx_draw_engine_guest_buffer_read(0,offset,bytes)!=0;
    });
}
extern "C" void wkc2_portrait_readback_stop() { portrait_readback.stop(); }
