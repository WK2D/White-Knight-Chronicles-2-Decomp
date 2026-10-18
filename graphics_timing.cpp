#include "ppu_recomp.h"
#include "graphics_settings.h"

namespace {
uint64_t requested_interval=2;
uint8_t requested_wait=0;
void apply_interval(uint32_t state) {
    if (!state) return;
    const bool unlocked=wkc::graphics_frame_limit(wkc::graphics_snapshot())==0;
    vm_write64(state+0x18,unlocked ? 0 : requested_interval);
    vm_write8(state+0x20,unlocked ? 0 : requested_wait);
}
}
// These hooks execute on the game's graphics thread, alongside its original
// interval setter and wait. Never write this state from the host render thread.
extern "C" void wkc2_graphics_interval_requested(ppu_context* ctx) {
    requested_interval=ctx->gpr[4]; requested_wait=uint8_t(ctx->gpr[5]);
    if (wkc::graphics_frame_limit(wkc::graphics_snapshot())==0) {
        ctx->gpr[4]=0; ctx->gpr[5]=0;
    }
}
extern "C" void wkc2_graphics_frame_wait(ppu_context*) {
    apply_interval(vm_read32(0x13638e4));
}
