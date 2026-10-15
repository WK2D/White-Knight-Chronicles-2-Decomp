/* Keep the SDK thread-table layout out of the generated PPU header's TU. */
#include "sys_ppu_thread.h"
#include <cstdio>
extern "C" void wkc2_dump_threads(void) {
    for (int slot=1; slot<PPU_THREAD_MAX; ++slot) {
        const auto& t = g_ppu_threads[slot];
        if (t.state != PPU_THREAD_STATE_RUNNING && t.state != PPU_THREAD_STATE_DETACHED) continue;
        fprintf(stderr,"[threads] tid=%u state=%d lr=%08X ctr=%08X r3=%08X r4=%08X r28=%08X r29=%08X r30=%08X r31=%08X name=%s\n",
            slot+1,t.state,(uint32_t)t.ctx.lr,(uint32_t)t.ctx.ctr,
            (uint32_t)t.ctx.gpr[3],(uint32_t)t.ctx.gpr[4],
            (uint32_t)t.ctx.gpr[28],(uint32_t)t.ctx.gpr[29],
            (uint32_t)t.ctx.gpr[30],(uint32_t)t.ctx.gpr[31],t.name);
    }
}
