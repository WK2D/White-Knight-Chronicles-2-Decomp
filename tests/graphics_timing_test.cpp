#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ppu_recomp.h"
#include "graphics_settings.h"
#include "graphics_pacing.h"
#include <cassert>
#include <filesystem>
extern "C" void wkc2_graphics_interval_requested(ppu_context*);
extern "C" void wkc2_graphics_frame_wait(ppu_context*);
static uint64_t interval;
static uint8_t wait_flag;
static uint32_t state=0x100;
extern "C" uint32_t vm_read32(uint64_t a) { assert(a==0x13638e4);return state; }
extern "C" void vm_write64(uint64_t a,uint64_t v) { assert(a==state+0x18);interval=v; }
extern "C" void vm_write8(uint64_t a,uint8_t v) { assert(a==state+0x20);wait_flag=v; }
int main() {
    wkc::UnlockedVblankClock clock;
    assert(clock.take(0,0));
    for(unsigned i=0;i<10000;++i) assert(!clock.take(i*.001,0));
    assert(clock.delay(10,0)>6 && clock.delay(10,0)<7);
    assert(clock.take(11,1)); // Actual new flip needs no heartbeat delay.
    assert(!clock.take(11,1));
    assert(clock.take(28,1)); // Idle heartbeat keeps startup alive.
    for(unsigned i=2;i<1002;++i) assert(clock.take(28+i*.001,i)); // No fixed FPS ceiling.
    clock.reset();assert(clock.take(29,1001));
    const auto path=std::filesystem::temp_directory_path()/"wkc2-timing-test.ini";
    std::filesystem::remove(path);wkc::graphics_initialize(path);
    ppu_context ctx{};ctx.gpr[4]=2;ctx.gpr[5]=1;
    wkc2_graphics_interval_requested(&ctx);
    wkc2_graphics_frame_wait(&ctx);assert(interval==2 && wait_flag==1);
    for(unsigned i=0;i<5;i++) assert(wkc::graphics_change(wkc::GraphicsOption::frame_limit,1));
    wkc2_graphics_frame_wait(&ctx);assert(interval==0 && wait_flag==0);
    ctx.gpr[4]=3;ctx.gpr[5]=1;wkc2_graphics_interval_requested(&ctx);
    assert(ctx.gpr[4]==0 && ctx.gpr[5]==0);
    assert(wkc::graphics_change(wkc::GraphicsOption::frame_limit,1));
    wkc2_graphics_frame_wait(&ctx);assert(interval==3 && wait_flag==1);
    state=0;wkc2_graphics_frame_wait(&ctx); // Before guest graphics allocation.
    std::filesystem::remove(path);
}
