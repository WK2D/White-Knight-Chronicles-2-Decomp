#include "spu_dma.h"
#include <assert.h>

extern int spu_run_with_halt(void (*)(spu_context*),spu_context*);
static int completed;
static mfc_engine engine;
extern void spu_wrch(spu_context*,uint32_t,u128);
extern u128 spu_rdch(spu_context*,uint32_t);
extern uint32_t spu_rchcnt(spu_context*,uint32_t);
extern void spu_mfc_release(spu_context*);

static void channel_write(spu_context* ctx,uint32_t channel,uint32_t value) {
    u128 v={{0}};v._u32[0]=value;spu_wrch(ctx,channel,v);
}

static uint32_t tag_status(spu_context* ctx,uint32_t mask) {
    channel_write(ctx,MFC_WrTagMask,mask);
    channel_write(ctx,MFC_WrTagUpdate,MFC_TAG_UPDATE_IMMEDIATE);
    return spu_rdch(ctx,MFC_RdTagStat)._u32[0];
}

/* A pure stall notification followed by a real GET. The tag stays incomplete
 * until this context acknowledges its list, independently of other contexts. */
static void park_list(spu_context* ctx,uint32_t tag) {
    spu_ls_write32(ctx,0x200,0x80000000u);
    spu_ls_write32(ctx,0x204,0);
    spu_ls_write32(ctx,0x208,16);
    spu_ls_write32(ctx,0x20C,0x20002000);
    memset(ctx->ls+0x400,0,16);
    channel_write(ctx,MFC_LSA,0x400);
    channel_write(ctx,MFC_EAH,0);
    channel_write(ctx,MFC_EAL,0x200);
    channel_write(ctx,MFC_Size,16);
    channel_write(ctx,MFC_TagID,tag);
    channel_write(ctx,MFC_Cmd,MFC_GETL_CMD);
    assert(ctx->list_stall_mask==(1u<<tag));
    assert(spu_rchcnt(ctx,MFC_RdListStallStat)==1);
    assert(tag_status(ctx,1u<<tag)==0);
}

static DWORD WINAPI resume_on_another_thread(void* arg) {
    spu_context* ctx=arg;
    assert(tag_status(ctx,8)==0);
    channel_write(ctx,MFC_WrListStallAck,3);
    assert(tag_status(ctx,8)==8);
    assert(memcmp(ctx->ls+0x400,vm_base+0x20002000,16)==0);
    return 0;
}

static void context_mfc_ownership(void) {
    /* More than eight logical contexts are legitimate over a game's life.
     * The old table handed the ninth and later contexts one shared engine. */
    spu_context* contexts[12];
    for(unsigned i=0;i<12;i++) {
        contexts[i]=malloc(sizeof(*contexts[i]));assert(contexts[i]);
        spu_context_init(contexts[i],i+1);
        assert(tag_status(contexts[i],0xFFFFFFFFu)==0xFFFFFFFFu);
    }
    for(unsigned i=0;i<16;i++) vm_base[0x20002000+i]=(uint8_t)(0x40+i);
    park_list(contexts[8],3);
    assert(tag_status(contexts[9],8)==8);
    park_list(contexts[9],4);
    channel_write(contexts[9],MFC_WrListStallAck,4);
    assert(tag_status(contexts[9],16)==16);
    assert(tag_status(contexts[8],8)==0);
    assert(memcmp(contexts[9]->ls+0x400,vm_base+0x20002000,16)==0);

    /* Handoff is sequential: no MFC channel accesses overlap on one context. */
    HANDLE worker=CreateThread(NULL,0,resume_on_another_thread,contexts[8],0,NULL);
    assert(worker && WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
    assert(CloseHandle(worker));
    assert(tag_status(contexts[8],8)==8);

    /* Reusing exactly the same context address starts with fresh tag state. */
    park_list(contexts[8],3);
    spu_context_init(contexts[8],9);
    assert(tag_status(contexts[8],8)==8);
    park_list(contexts[8],3);
    spu_mfc_release(contexts[8]);spu_mfc_release(contexts[8]);
    assert(tag_status(contexts[8],8)==8);
    spu_mfc_release(NULL);
    for(unsigned i=0;i<12;i++) { spu_mfc_release(contexts[i]);free(contexts[i]); }
    puts("Twelve independent MFC contexts, stalled-list isolation, reuse and host handoff passed");
}

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
        context_mfc_ownership();
    }
    free(ctx);assert(VirtualFree(vm_base,0,MEM_RELEASE));vm_base=NULL;
    puts(expect_halt ? "Explicit DMA repeat diagnostic cutoff passed" :
         "1024 valid DMA label polls and later producer publication passed");
    return 0;
}
