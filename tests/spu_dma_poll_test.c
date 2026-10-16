#include "spu_dma.h"
#include <assert.h>

extern int spu_run_with_halt(void (*)(spu_context*),spu_context*);
static int completed;
static mfc_engine engine;

/* A producer may publish a completion label after hundreds of valid reads.
 * Exercise the production DMA implementation without game data. */
static void poll_label(spu_context* ctx) {
    for (unsigned poll=0;poll<1024;++poll) {
        if (poll==768) {
            vm_base[0x20000D80]=0x12;vm_base[0x20000D81]=0x34;
            vm_base[0x20000D82]=0x56;vm_base[0x20000D83]=0x78;
        }
        ctx->mfc_lsa=0x100;
        ctx->mfc_eah=0;ctx->mfc_eal=0x20000D80;
        ctx->mfc_size=4;ctx->mfc_tag=1;
        assert(mfc_submit(&engine,ctx,MFC_GET_CMD)==0);
        const uint8_t* expected=vm_base+0x20000D80;
        assert(memcmp(ctx->ls+0x100,expected,4)==0);
        assert(engine.tag_completed&(1u<<1));
    }
    completed=1;
}

int wkc2_test_spu_dma_poll(int expect_halt) {
    assert(!vm_base);
    vm_base=VirtualAlloc(NULL,(SIZE_T)UINT64_C(0x100000000),MEM_RESERVE,PAGE_READWRITE);
    assert(vm_base);
    assert(VirtualAlloc(vm_base+0x20000000,0x10000,MEM_COMMIT,PAGE_READWRITE));
    spu_context* ctx=malloc(sizeof(*ctx));assert(ctx);
    spu_context_init(ctx,0);ctx->image_id=19;ctx->pc=0x2C58;
    ctx->status=SPU_STATUS_RUNNING;
    mfc_engine_init(&engine);
    completed=0;
    int halted=spu_run_with_halt(poll_label,ctx);
    assert(halted==expect_halt);
    assert(completed==!expect_halt);
    if (!expect_halt) {
        assert(ctx->status==SPU_STATUS_RUNNING);
        assert(ctx->ls[0x100]==0x12 && ctx->ls[0x103]==0x78);
    }
    free(ctx);assert(VirtualFree(vm_base,0,MEM_RELEASE));vm_base=NULL;
    puts(expect_halt ? "Explicit DMA repeat diagnostic cutoff passed" :
         "1024 valid DMA label polls and later producer publication passed");
    return 0;
}
