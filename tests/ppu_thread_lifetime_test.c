/* Exercise the production PPU thread lifetime: creation flags, detached
 * threads releasing their slot exactly once, joins, and guest stack reuse.
 * No game code is involved. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sys_ppu_thread.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sched.h>
#include <sys/mman.h>
#endif

uint8_t* vm_base;
uint32_t ppu_vm_size;

void ppu_resv_register(ppu_context* ctx) { (void)ctx; }
int ps3_intr_is_handler(unsigned long long tid) { (void)tid; return 0; }
int ps3_intr_wait(unsigned long long tid) { (void)tid; abort(); }
uint32_t vm_read32(uint64_t a) { (void)a; abort(); }

enum { EXIT_NESTED = 1, RETURN = 2, WAIT = 3, STACK = 4 };
static volatile int release_wait, stack_was_zero;

static void pause_briefly(void)
{
#ifdef _WIN32
    Sleep(1);
#else
    sched_yield();
#endif
}

static void guest_entry(ppu_context* ctx)
{
    const uint64_t mode = ctx->gpr[3];
    if (mode == WAIT) while (!release_wait) pause_briefly();
    if (mode == STACK) {
        /* A reused stack must look like a fresh one. */
        uint8_t* below = vm_base + (uint32_t)ctx->gpr[1] - 0x1000;
        int zero = 1;
        for (unsigned i = 0; i < 0x1000; i++) zero &= below[i] == 0;
        stack_was_zero = zero;
        memset(below, 0xA5, 0x1000);
    }
    if (mode == RETURN) { ctx->gpr[3] = 0x2222; return; }
    /* Exit from a scratch context, as a guest callback nested in this
     * thread does. The thread's own r3 must not replace the status. */
    ppu_context nested;
    ppu_context_init(&nested);
    nested.thread_id = ctx->thread_id;
    nested.gpr[3] = 0xE000 + mode;
    sys_ppu_thread_exit(&nested);
    abort(); /* Guest execution cannot continue after exit. */
}

static uint64_t create(uint64_t mode, uint64_t flags)
{
    ppu_context ctx;
    ppu_context_init(&ctx);
    ctx.gpr[3] = 0x100; ctx.gpr[4] = 0x10000; ctx.gpr[5] = mode;
    ctx.gpr[6] = 1000; ctx.gpr[7] = 0x10000; ctx.gpr[8] = flags;
    assert(sys_ppu_thread_create(&ctx) == CELL_OK);
    uint64_t id = 0;
    for (unsigned n = 0; n < 8; ++n) id = id * 256 + vm_base[0x100 + n];
    assert(id >= 2 && id <= PPU_THREAD_MAX);
    return id;
}

static int64_t call(int64_t (*fn)(ppu_context*), uint64_t id, uint64_t self)
{
    ppu_context ctx;
    ppu_context_init(&ctx);
    ctx.thread_id = self;
    ctx.gpr[3] = id; ctx.gpr[4] = 0x200;
    return fn(&ctx);
}

static uint64_t joined_status(void)
{
    uint64_t v = 0;
    for (unsigned n = 0; n < 8; ++n) v = v * 256 + vm_base[0x200 + n];
    return v;
}

static void wait_free(uint64_t id)
{
    for (unsigned n = 0; g_ppu_threads[id - 1].state != PPU_THREAD_STATE_FREE; ++n) {
        /* An exit that left a detached slot FINISHED never frees it. */
        assert(n < 2000000);
        pause_briefly();
    }
}

static volatile uint64_t join_id;
static volatile int64_t join_rc = -1;
#ifdef _WIN32
static DWORD WINAPI joiner(void* unused) { (void)unused; join_rc = call(sys_ppu_thread_join, join_id, 1); return 0; }
#else
static void* joiner(void* unused) { (void)unused; join_rc = call(sys_ppu_thread_join, join_id, 1); return NULL; }
#endif

int main(void)
{
#ifdef _WIN32
    vm_base = VirtualAlloc(NULL, VM_TOTAL_SIZE, MEM_RESERVE, PAGE_READWRITE);
    assert(vm_base);
    assert(VirtualAlloc(vm_base, 0x10000, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(vm_base + VM_STACK_BASE, 0x4000000, MEM_COMMIT, PAGE_READWRITE));
#else
    vm_base = mmap(NULL, VM_TOTAL_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    assert(vm_base != MAP_FAILED);
#endif
    lv2_syscall_table table = {0};
    sys_ppu_thread_init(&table);
    ppu_thread_register_main();
    g_ppu_thread_entry_trampoline = guest_entry;

    /* Threads created without JOINABLE are detached: join is EINVAL, and
     * each one frees its slot and stack when it ends, by exit or return. */
    uint64_t id = create(WAIT, 0);
    assert(g_ppu_threads[id - 1].state == PPU_THREAD_STATE_DETACHED);
    assert(call(sys_ppu_thread_join, id, 1) == (int64_t)(int32_t)CELL_EINVAL);
    assert(call(sys_ppu_thread_detach, id, 1) == (int64_t)(int32_t)CELL_EINVAL);
    release_wait = 1;
    wait_free(id);
    const uint32_t stack_end = g_vm_stack_alloc.next_addr;
    for (unsigned i = 0; i < 3 * PPU_THREAD_MAX; ++i) wait_free(create(i & 1 ? RETURN : EXIT_NESTED, 0));
    assert(g_vm_stack_alloc.next_addr == stack_end);

    /* Joinable threads report the status passed to exit, then free their
     * slot and stack for the next thread. */
    for (unsigned i = 0; i < 2 * PPU_THREAD_MAX; ++i) {
        const uint64_t mode = i & 1 ? RETURN : EXIT_NESTED;
        id = create(mode, SYS_PPU_THREAD_CREATE_JOINABLE);
        assert(call(sys_ppu_thread_join, id, 1) == CELL_OK);
        assert(joined_status() == (mode == RETURN ? 0x2222 : 0xE000 + mode));
        assert(g_ppu_threads[id - 1].state == PPU_THREAD_STATE_FREE);
    }
    assert(g_vm_stack_alloc.next_addr == stack_end);

    /* A thread cannot join itself, and a thread being joined cannot be
     * detached out from under its joiner. */
    release_wait = 0;
    id = create(WAIT, SYS_PPU_THREAD_CREATE_JOINABLE);
    assert(call(sys_ppu_thread_join, id, id) == (int64_t)(int32_t)CELL_EDEADLK);
    join_id = id;
#ifdef _WIN32
    HANDLE host = CreateThread(NULL, 0, joiner, NULL, 0, NULL);
    assert(host);
#else
    pthread_t host;
    assert(pthread_create(&host, NULL, joiner, NULL) == 0);
#endif
    while (!g_ppu_threads[id - 1].joining) pause_briefly();
    assert(call(sys_ppu_thread_detach, id, 1) == (int64_t)(int32_t)CELL_EBUSY);
    assert(call(sys_ppu_thread_join, id, 1) == (int64_t)(int32_t)CELL_EINVAL);
    release_wait = 1;
#ifdef _WIN32
    assert(WaitForSingleObject(host, 5000) == WAIT_OBJECT_0);
    CloseHandle(host);
#else
    assert(pthread_join(host, NULL) == 0);
#endif
    assert(join_rc == CELL_OK && joined_status() == 0xE000 + WAIT);

    /* Detaching a running joinable thread hands its release to the thread. */
    release_wait = 0;
    id = create(WAIT, SYS_PPU_THREAD_CREATE_JOINABLE);
    assert(call(sys_ppu_thread_detach, id, 1) == CELL_OK);
    release_wait = 1;
    wait_free(id);

    /* A reused stack is zeroed. */
    for (unsigned i = 0; i < 3; ++i) {
        stack_was_zero = 0;
        id = create(STACK, SYS_PPU_THREAD_CREATE_JOINABLE);
        assert(call(sys_ppu_thread_join, id, 1) == CELL_OK);
        assert(stack_was_zero);
    }
    assert(g_vm_stack_alloc.next_addr == stack_end);
    puts("PPU thread lifetime: detached release, joins and stack reuse passed");
    return 0;
}
