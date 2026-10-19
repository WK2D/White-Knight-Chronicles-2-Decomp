// Synthetic concurrency/ABI tests for the actual extracted SDK handlers.
#ifdef NDEBUG
#undef NDEBUG
#endif
#define NOMINMAX
#include "win32_compat.h"
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/error_codes.h"
#include "ppu_lwmutex_token.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>
#include <unordered_map>
#include <vector>

static std::atomic<uint32_t> memory[4096];
static std::unordered_map<uint32_t, void(*)(ppu_context*)> handlers;
extern "C" uint32_t vm_read32(uint64_t a) {
    assert(!(a & 3) && a / 4 < 4096);
    return memory[a / 4].load();
}
extern "C" void vm_write32(uint64_t a, uint32_t v) {
    assert(!(a & 3) && a / 4 < 4096);
    memory[a / 4].store(v);
}
extern "C" uint64_t vm_read64(uint64_t a) {
    return (uint64_t(vm_read32(a)) << 32) | vm_read32(a + 4);
}
extern "C" void vm_write64(uint64_t a, uint64_t v) {
    vm_write32(a, uint32_t(v >> 32)); vm_write32(a + 4, uint32_t(v));
}
extern "C" void ps3_hle_register_ctx(uint32_t nid, const char*, void(*fn)(ppu_context*)) {
    handlers[nid] = fn;
}
extern "C" void ps3_hle_call(uint32_t nid, ppu_context* ctx) { handlers.at(nid)(ctx); }
extern "C" {
volatile long long g_gcm_ref_pub_count = 0;
unsigned long long ppu_thread_cpu_us(unsigned) { return 0; }
unsigned ppu_thread_prof_pc(unsigned) { return 0; }
void wkc2_register_lwcond();
}
// Deterministic early kernel timeouts exercise the production deadline loop.
// Zero-time trylocks and infinite waits keep the real Windows behavior.
static std::atomic<unsigned> forced_early_timeouts{0};
static DWORD test_kernel_wait(HANDLE semaphore, DWORD milliseconds) {
    if (milliseconds && milliseconds != INFINITE && forced_early_timeouts.load()) {
        forced_early_timeouts.fetch_sub(1);
        return WAIT_TIMEOUT;
    }
    return WaitForSingleObject(semaphore, milliseconds);
}
#define WaitForSingleObject test_kernel_wait
#include "lwmutex-fixture.inc"
#undef WaitForSingleObject

static uint32_t call(ppu_context& ctx, const char* name, uint32_t address, uint64_t second = 0) {
    ctx.gpr[3] = address; ctx.gpr[4] = second;
    ps3_hle_call(ps3_compute_nid(name), &ctx);
    return uint32_t(ctx.gpr[3]);
}
static void register_handlers() {
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_create"), "", sys_lwmutex_create);
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_lock"), "", sys_lwmutex_lock);
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_trylock"), "", sys_lwmutex_trylock);
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_unlock"), "", sys_lwmutex_unlock);
    wkc2_register_lwcond();
}
static void slot_publication() {
    constexpr unsigned threads = 8, collisions = 32;
    std::atomic<LwmSlot*> observed[collisions]{};
    std::vector<std::thread> workers;
    for (unsigned n = 0; n < threads; ++n) workers.emplace_back([&] {
        for (unsigned i = 0; i < collisions; ++i) {
            const uint32_t address = 0x80001000u + i * LWM_HASH;
            auto* slot = lwm_slot(address);
            assert(slot && lwm_find(address) == slot);
            assert(slot->addr.load() == address);
            auto* previous = observed[i].exchange(slot);
            assert(!previous || previous == slot);
            if (lwm_kernel_backend()) assert(slot->sem);
            else assert(slot->token.free.load() == 1);
        }
    });
    for (auto& worker : workers) worker.join();
}
static void token_wake_and_deadline() {
    PpuLwmutexToken token;
    assert(token.try_acquire() && !token.try_acquire());
    auto blocked = std::async(std::launch::async, [&] {
        assert(token.acquire(UINT32_MAX)); token.release(); return true;
    });
    const auto prepared_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!token.waiters.load()) {
        assert(std::chrono::steady_clock::now() < prepared_deadline);
        std::this_thread::yield();
    }
    { std::lock_guard<std::mutex> guard(token.sleep_mutex); assert(token.waiters.load() == 1); }
    token.release(); // Also exercises release from a different thread.
    assert(blocked.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(blocked.get() && token.waiters.load() == 0);
    token.release(); token.release(); // Over-release remains binary.
    assert(token.try_acquire() && !token.try_acquire());
    std::atomic<bool> done{false};
    std::thread spurious([&] {
        while (!done.load()) { token.changed.notify_all(); std::this_thread::yield(); }
    });
    const auto start = std::chrono::steady_clock::now();
    assert(!token.acquire(25));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    done.store(true); spurious.join();
    assert(elapsed >= std::chrono::milliseconds(25) && elapsed < std::chrono::seconds(1));
    assert(token.waiters.load() == 0 && !token.try_acquire());
    token.release();
}
static void guest_abi() {
    ppu_context owner{}, other{}; owner.thread_id = 1; other.thread_id = 2;
    constexpr uint32_t mutex = 256;
    assert(call(owner, "sys_lwmutex_create", mutex) == 0);
    assert(call(owner, "sys_lwmutex_lock", mutex) == 0);
    assert(call(owner, "sys_lwmutex_trylock", mutex) == 0);
    assert(vm_read32(mutex) == 1 && vm_read32(mutex + 12) == 2);
    assert(call(other, "sys_lwmutex_trylock", mutex) == 0x8001000bu);
    assert(call(owner, "sys_lwmutex_unlock", mutex) == 0);
    if (lwm_kernel_backend()) forced_early_timeouts.store(3);
    const auto start = std::chrono::steady_clock::now();
    assert(call(other, "sys_lwmutex_lock", mutex, 15000) == 0x8001000bu);
    assert(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(15));
    assert(forced_early_timeouts.load() == 0);
    assert(vm_read32(mutex) == 1 && vm_read32(mutex + 12) == 1);
    // A different host/guest thread can release a transferred lock.
    std::thread cross([&] { assert(call(other, "sys_lwmutex_unlock", mutex) == 0); }); cross.join();
    assert(call(owner, "sys_lwmutex_unlock", mutex) == 0); // Free over-release.
    assert(call(other, "sys_lwmutex_lock", mutex) == 0);
    assert(call(owner, "sys_lwmutex_create", mutex) == 0); // Reused address resets token.
    assert(call(owner, "sys_lwmutex_trylock", mutex) == 0);
    assert(call(owner, "sys_lwmutex_unlock", mutex) == 0);
    assert(lwm_timeout_ms(0) == INFINITE && lwm_timeout_ms(1) == 1);
    assert(lwm_timeout_ms(1000) == 1 && lwm_timeout_ms(1001) == 2);
    assert(lwm_timeout_ms(UINT64_MAX) == 0xfffffffeu);
}
static void contention_and_conditions() {
    constexpr uint32_t mutex = 512, cond = 128;
    ppu_context main{}; main.thread_id = 1;
    assert(call(main, "sys_lwmutex_create", mutex) == 0);
    std::atomic<unsigned> active{0}; uint64_t total = 0;
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) workers.emplace_back([&, i] {
        ppu_context ctx{}; ctx.thread_id = i + 2;
        for (unsigned n = 0; n < 3000; ++n) {
            assert(call(ctx, "sys_lwmutex_lock", mutex) == 0);
            assert(active.fetch_add(1) == 0);
            const auto value = total;
            if (!(n & 127)) std::this_thread::yield();
            total = value + 1;
            assert(active.fetch_sub(1) == 1);
            assert(call(ctx, "sys_lwmutex_unlock", mutex) == 0);
        }
    });
    for (auto& worker : workers) worker.join();
    assert(total == 24000 && active.load() == 0);
    assert(call(main, "sys_lwcond_create", cond, mutex) == 0);
    assert(call(main, "sys_lwmutex_lock", mutex) == 0);
    assert(call(main, "sys_lwcond_wait", cond, 2000) == CELL_ETIMEDOUT);
    assert(vm_read32(mutex) == 1 && vm_read32(mutex + 12) == 1);
    bool signalled = false;
    std::thread signal([&] {
        ppu_context other{}; other.thread_id = 2;
        assert(call(other, "sys_lwmutex_lock", mutex) == 0);
        signalled = true;
        assert(call(other, "sys_lwcond_signal", cond) == 0);
        assert(call(other, "sys_lwmutex_unlock", mutex) == 0);
    });
    while (!signalled) assert(call(main, "sys_lwcond_wait", cond, 1000000) == 0);
    assert(vm_read32(mutex) == 1);
    assert(call(main, "sys_lwmutex_unlock", mutex) == 0);
    signal.join();
    assert(call(main, "sys_lwcond_destroy", cond) == 0);
}
static void recreate_pending() {
    if (lwm_kernel_backend()) return;
    constexpr uint32_t mutex = 768;
    ppu_context main{}; main.thread_id = 1;
    assert(call(main, "sys_lwmutex_create", mutex) == 0);
    assert(call(main, "sys_lwmutex_lock", mutex) == 0);
    auto waiting = std::async(std::launch::async, [&] {
        ppu_context other{}; other.thread_id = 2;
        assert(call(other, "sys_lwmutex_lock", mutex) == 0);
        assert(call(other, "sys_lwmutex_unlock", mutex) == 0);
        return true;
    });
    auto* slot = lwm_find(mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!slot->token.waiters.load()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    { std::lock_guard<std::mutex> guard(slot->token.sleep_mutex); assert(slot->token.waiters.load() == 1); }
    assert(call(main, "sys_lwmutex_create", mutex) == 0);
    assert(waiting.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(waiting.get() && slot->token.waiters.load() == 0);
}
static void set_test_backend(const char* value) {
#ifdef _WIN32
    assert(_putenv_s("PS3RECOMP_LWMUTEX_KERNEL",value)==0);
#else
    assert(setenv("PS3RECOMP_LWMUTEX_KERNEL",value,1)==0);
#endif
}
int main(int argc, char**) {
#ifdef _WIN32
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _CALL_REPORTFAULT);
#endif
    set_test_backend(argc > 1 ? "1" : "0");
    register_handlers();
    token_wake_and_deadline(); slot_publication(); guest_abi(); recreate_pending(); contention_and_conditions();
    // Changing the environment after slots exist cannot switch live tokens.
    const bool original = lwm_kernel_backend();
    set_test_backend(original ? "0" : "1");
    assert(lwm_kernel_backend() == original);
    puts(argc > 1 ? "Kernel backend: ABI, concurrency and actual project lwcond pass"
                  : "User token: ABI, concurrency and actual project lwcond pass");
}
