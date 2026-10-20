// Exercise the actual SDK VM lifecycle and one shared epoch from C and C++.
// The epoch detects stale positive mapping checks; it is not an unmapping lease.
#ifdef NDEBUG
#undef NDEBUG
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "vm.h"
#include "vm_mapping_epoch.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>

#ifndef _WIN32
#error This regression uses the Windows VM and VirtualQuery.
#endif

extern "C" {
std::uint64_t vm_mapping_epoch_test_c_read(void);
void vm_mapping_epoch_test_c_invalidate(void);
std::int32_t vm_mapping_epoch_test_c_init(void);
std::int32_t vm_mapping_epoch_test_c_commit(std::uint32_t, std::uint32_t);
std::int32_t vm_mapping_epoch_test_c_protect(
    std::uint32_t, std::uint32_t, int, int, int);
void vm_mapping_epoch_test_c_shutdown(void);
std::uint64_t vm_mapping_epoch_test_c_shutdown_release_epoch(void);
unsigned vm_mapping_epoch_test_c_shutdown_release_count(void);
int vm_mapping_epoch_test_c_shutdown_release_succeeded(void);
}

namespace {
static_assert(sizeof(void*) == 8, "The SDK VM reserves a 4 GiB guest arena.");

void expect_mapping(const void* address, DWORD state, DWORD protection = 0)
{
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(address, &info, sizeof info) == sizeof info);
    assert(info.State == state);
    if (state == MEM_COMMIT) assert(info.Protect == protection);
}

std::uint64_t shared_epoch()
{
    const auto epoch = vm_mapping_epoch_read();
    assert(vm_mapping_epoch_test_c_read() == epoch);
    return epoch;
}

std::uint64_t changed_epoch(std::uint64_t previous)
{
    const auto epoch = shared_epoch();
    assert(epoch != previous);
    return epoch;
}

void lifecycle()
{
    assert(vm_base == nullptr && ppu_vm_size == 0);
    auto epoch = shared_epoch();
    assert(vm_mapping_epoch_test_c_protect(VM_MAIN_MEM_BASE, VM_PAGE_SIZE,
                                         1, 0, 0) == CELL_EFAULT);
    assert(vm_protect(VM_MAIN_MEM_BASE, VM_PAGE_SIZE, 1, 0, 0) == CELL_EFAULT);
    assert(vm_mapping_epoch_test_c_commit(VM_MAIN_MEM_BASE, VM_PAGE_SIZE) == CELL_EFAULT);
    vm_mapping_epoch_test_c_shutdown();
    assert(vm_mapping_epoch_test_c_shutdown_release_count() == 0);
    assert(shared_epoch() == epoch);

    // This is the actual SDK allocation: 4 GiB reserved, 512 MiB committed.
    assert(vm_mapping_epoch_test_c_init() == CELL_OK);
    const auto first_init_epoch = changed_epoch(epoch);
    epoch = first_init_epoch;
    assert(vm_base != nullptr && VM_READY);
    assert((reinterpret_cast<std::uintptr_t>(vm_base) & (VM_TOTAL_SIZE - 1)) == 0);
    expect_mapping(vm_base + VM_MAIN_MEM_BASE, MEM_COMMIT, PAGE_READWRITE);
    expect_mapping(vm_base + VM_STACK_BASE, MEM_COMMIT, PAGE_READWRITE);
    assert(vm_init() == CELL_OK);
    assert(vm_mapping_epoch_test_c_init() == CELL_OK);
    assert(shared_epoch() == epoch); // Idempotent init keeps existing mappings.

    vm_mapping_epoch_invalidate();
    assert(shared_epoch() == epoch + 1);
    epoch = shared_epoch();
    vm_mapping_epoch_test_c_invalidate();
    assert(shared_epoch() == epoch + 1);
    epoch = shared_epoch();

    // Committing reserved pages only adds access and keeps positive checks valid.
    constexpr std::uint32_t committed = 0x60000000u;
    constexpr std::uint32_t uncommitted = committed + 2 * VM_PAGE_SIZE;
    expect_mapping(vm_base + committed, MEM_RESERVE);
    assert(vm_mapping_epoch_test_c_commit(committed, VM_PAGE_SIZE) == CELL_OK);
    assert(shared_epoch() == epoch);
    expect_mapping(vm_base + committed, MEM_COMMIT, PAGE_READWRITE);
    assert(vm_commit(committed, VM_PAGE_SIZE) == CELL_OK);
    assert(shared_epoch() == epoch);

    constexpr std::uint32_t page = VM_MAIN_MEM_BASE + 2 * VM_PAGE_SIZE;
    vm_base[page] = 0x5a;
    assert(vm_mapping_epoch_test_c_protect(page, VM_PAGE_SIZE, 1, 0, 0) == CELL_OK);
    epoch = changed_epoch(epoch);
    expect_mapping(vm_base + page, MEM_COMMIT, PAGE_READONLY);
    assert(vm_base[page] == 0x5a);

    assert(vm_protect(page, VM_PAGE_SIZE, 0, 0, 0) == CELL_OK);
    epoch = changed_epoch(epoch);
    expect_mapping(vm_base + page, MEM_COMMIT, PAGE_NOACCESS);

    assert(vm_mapping_epoch_test_c_protect(page, VM_PAGE_SIZE, 1, 1, 0) == CELL_OK);
    epoch = changed_epoch(epoch);
    expect_mapping(vm_base + page, MEM_COMMIT, PAGE_READWRITE);
    assert(vm_base[page] == 0x5a);
    vm_base[page] = 0xa7;
    assert(vm_base[page] == 0xa7);

    // Windows rejects protection changes on reserved, uncommitted pages.
    expect_mapping(vm_base + uncommitted, MEM_RESERVE);
    assert(vm_mapping_epoch_test_c_protect(uncommitted, VM_PAGE_SIZE,
                                         1, 0, 0) == CELL_EFAULT);
    assert(shared_epoch() == epoch);
    assert(vm_protect(uncommitted, VM_PAGE_SIZE, 0, 0, 0) == CELL_EFAULT);
    assert(shared_epoch() == epoch);
    expect_mapping(vm_base + uncommitted, MEM_RESERVE);

    const auto released_base = vm_base;
    vm_mapping_epoch_test_c_shutdown();
    const auto shutdown_epoch = changed_epoch(epoch);
    assert(vm_mapping_epoch_test_c_shutdown_release_count() == 1);
    assert(vm_mapping_epoch_test_c_shutdown_release_succeeded());
    // The C shim samples the real epoch immediately before calling VirtualFree.
    assert(vm_mapping_epoch_test_c_shutdown_release_epoch() == shutdown_epoch);
    assert(vm_base == nullptr && !VM_READY);
    expect_mapping(released_base, MEM_FREE);
    vm_shutdown();
    assert(shared_epoch() == shutdown_epoch);

    // The generation must change on reinitialization, including base reuse.
    assert(vm_init() == CELL_OK);
    epoch = changed_epoch(shutdown_epoch);
    assert(epoch != first_init_epoch);
    expect_mapping(vm_base + page, MEM_COMMIT, PAGE_READWRITE);
    assert(vm_base[page] == 0); // Real vm_init zeroes main memory again.
    const auto reinitialized_base = vm_base;
    vm_shutdown();
    changed_epoch(epoch);
    assert(vm_base == nullptr && !VM_READY);
    expect_mapping(reinitialized_base, MEM_FREE);
}

void concurrent_c_and_cpp_invalidations()
{
    constexpr unsigned thread_count = 4;
    constexpr unsigned invalidations_per_thread = 100;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false}, monotonic{true};
    std::array<std::thread, thread_count> workers;
    const auto before = shared_epoch();
    for (unsigned worker = 0; worker < thread_count; ++worker) {
        workers[worker] = std::thread([&, worker] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            auto observed = vm_mapping_epoch_read();
            for (unsigned i = 0; i < invalidations_per_thread; ++i) {
                if ((worker + i) & 1u) vm_mapping_epoch_test_c_invalidate();
                else vm_mapping_epoch_invalidate();
                const auto next = ((worker + i) & 1u)
                    ? vm_mapping_epoch_read() : vm_mapping_epoch_test_c_read();
                if (next <= observed) monotonic.store(false, std::memory_order_relaxed);
                observed = next;
            }
        });
    }
    while (ready.load(std::memory_order_acquire) != thread_count) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    assert(monotonic.load(std::memory_order_relaxed));
    assert(shared_epoch() == before + thread_count * invalidations_per_thread);
}
}

int main()
{
    lifecycle();
    concurrent_c_and_cpp_invalidations();
    std::puts("VM mapping epoch lifecycle, C/C++ sharing, and concurrency passed");
    return 0;
}
