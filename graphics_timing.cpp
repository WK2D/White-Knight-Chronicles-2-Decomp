#include "ppu_recomp.h"

namespace {
uint64_t requested_interval=2;
uint8_t requested_wait=0;
}
// These hooks execute on the game's graphics thread, alongside its original
// interval setter and wait. Never write this state from the host render thread.
// Every frame limit keeps the game's own flip interval; the host vblank clock
// sets the rate. (Unlocked used to clear the interval here.)
extern "C" void wkc2_graphics_interval_requested(ppu_context* ctx) {
    requested_interval=ctx->gpr[4]; requested_wait=uint8_t(ctx->gpr[5]);
}
extern "C" void wkc2_graphics_frame_wait(ppu_context*) {
    const uint32_t state=vm_read32(0x13638e4);
    if (!state) return;
    vm_write64(state+0x18,requested_interval);
    vm_write8(state+0x20,requested_wait);
}
