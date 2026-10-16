/* Exercise the production thread exit/join path, including a delayed host
 * epilogue after the guest exit notification. No game code is involved. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sys_ppu_thread.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;
static HANDLE tail_entered, tail_release;
static volatile LONG join_done;
static int64_t joined;
static uint64_t worker_id;

void ppu_resv_register(ppu_context* ctx) { (void)ctx; }
int ps3_intr_is_handler(unsigned long long tid)
{
    (void)tid;
    SetEvent(tail_entered);
    assert(WaitForSingleObject(tail_release,5000)==WAIT_OBJECT_0);
    return 0;
}
int ps3_intr_wait(unsigned long long tid) { (void)tid; abort(); }
uint32_t vm_read32(uint64_t a) { (void)a; abort(); }

static void guest_entry(ppu_context* ctx)
{
    ctx->gpr[3]=0x12345678u;
    sys_ppu_thread_exit(ctx);
    abort(); /* Guest execution cannot continue after exit. */
}

static DWORD WINAPI join_thread(void* unused)
{
    (void)unused;
    ppu_context ctx;
    ppu_context_init(&ctx);
    ctx.gpr[3]=worker_id;
    ctx.gpr[4]=0x200;
    joined=sys_ppu_thread_join(&ctx);
    InterlockedExchange(&join_done,1);
    return 0;
}

int main(void)
{
    vm_base=VirtualAlloc(NULL,VM_TOTAL_SIZE,MEM_RESERVE,PAGE_READWRITE);
    assert(vm_base);
    assert(VirtualAlloc(vm_base,0x10000,MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc(vm_base+VM_STACK_BASE,0x1000000,MEM_COMMIT,PAGE_READWRITE));
    lv2_syscall_table table={0};
    sys_ppu_thread_init(&table);
    ppu_thread_register_main();
    g_ppu_thread_entry_trampoline=guest_entry;
    tail_entered=CreateEventA(NULL,TRUE,FALSE,NULL);
    tail_release=CreateEventA(NULL,TRUE,FALSE,NULL);
    assert(tail_entered && tail_release);
    for (unsigned i=0;i<128;++i) {
        ResetEvent(tail_entered); ResetEvent(tail_release);
        join_done=0;
        ppu_context ctx;
        ppu_context_init(&ctx);
        ctx.gpr[3]=0x100; ctx.gpr[4]=0x10000; ctx.gpr[7]=0x4000;
        ctx.gpr[8]=1;
        assert(sys_ppu_thread_create(&ctx)==CELL_OK);
        worker_id=0;
        for (unsigned n=0;n<8;++n) worker_id=worker_id*256+vm_base[0x100+n];
        assert(worker_id==2);
        assert(WaitForSingleObject(tail_entered,5000)==WAIT_OBJECT_0);
        HANDLE joiner=CreateThread(NULL,0,join_thread,NULL,0,NULL);
        assert(joiner);
        /* The guest exit event has fired, but the host still owns its slot.
         * Returning from join now permits an old epilogue to overwrite reuse. */
        assert(WaitForSingleObject(joiner,20)==WAIT_TIMEOUT);
        assert(!InterlockedCompareExchange(&join_done,0,0));
        assert(g_ppu_threads[1].state!=PPU_THREAD_STATE_FREE);
        SetEvent(tail_release);
        assert(WaitForSingleObject(joiner,5000)==WAIT_OBJECT_0);
        CloseHandle(joiner);
        assert(joined==CELL_OK && join_done);
        assert(g_ppu_threads[1].state==PPU_THREAD_STATE_FREE);
        assert(!g_ppu_threads[1].host_thread && !g_ppu_threads[1].finish_event);
        assert(vm_base[0x204]==0x12 && vm_base[0x207]==0x78);
    }
    CloseHandle(tail_entered); CloseHandle(tail_release);
    VirtualFree(vm_base,0,MEM_RELEASE);
    puts("PPU join waits for the host epilogue; 128 slot reuses passed");
    return 0;
}
