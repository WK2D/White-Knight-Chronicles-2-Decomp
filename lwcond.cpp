/* Guest sysPrxForUser lightweight conditions. ABI: two BE32 words, mutex EA
 * and queue ID. Host state stays outside guest memory and uses portable C++.
 * Enqueue and mutex release are covered by the same lock as signalling, so a
 * signal between releasing the guest mutex and sleeping cannot be lost. */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/error_codes.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

extern "C" void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
extern "C" void ps3_hle_call(uint32_t, ppu_context*);

namespace {
struct Waiter {
    uint64_t tid;
    bool signalled = false;
    std::condition_variable cv;
};
struct Condition {
    uint32_t mutex;
    std::list<Waiter*> waiters;
};
std::mutex state_lock;
std::unordered_map<uint32_t, std::shared_ptr<Condition>> conditions;
uint32_t next_id = 0x4c000001;

void result(ppu_context* c, uint32_t code) {
    c->gpr[3] = (uint64_t)(int64_t)(int32_t)code;
}
auto find_condition(uint32_t ea) {
    return conditions.find(vm_read32(ea + 4));
}
void create(ppu_context* c) {
    uint32_t ea = (uint32_t)c->gpr[3], mutex = (uint32_t)c->gpr[4];
    if (!ea || !mutex) { result(c, CELL_EFAULT); return; }
    if ((ea & 3) || (mutex & 3)) { result(c, CELL_EALIGN); return; }
    std::lock_guard guard(state_lock);
    auto cond = std::make_shared<Condition>();
    cond->mutex = mutex;
    uint32_t id = next_id++;
    conditions.emplace(id, std::move(cond));
    vm_write32(ea, mutex);
    vm_write32(ea + 4, id);
    result(c, CELL_OK);
}
void destroy(ppu_context* c) {
    std::lock_guard guard(state_lock);
    uint32_t ea = (uint32_t)c->gpr[3];
    auto it = find_condition(ea);
    if (it == conditions.end()) { result(c, CELL_ESRCH); return; }
    if (!it->second->waiters.empty()) { result(c, CELL_EBUSY); return; }
    conditions.erase(it);
    vm_write32(ea + 4, 0);
    result(c, CELL_OK);
}
void signal_impl(ppu_context* c, bool all, bool targeted) {
    std::lock_guard guard(state_lock);
    auto it = find_condition((uint32_t)c->gpr[3]);
    if (it == conditions.end()) { result(c, CELL_ESRCH); return; }
    bool found = false;
    for (auto* waiter : it->second->waiters) {
        if (waiter->signalled || (targeted && waiter->tid != c->gpr[4])) continue;
        waiter->signalled = true;
        waiter->cv.notify_one();
        found = true;
        if (!all) break;
    }
    result(c, targeted && !found ? CELL_EPERM : CELL_OK);
}
void signal_one(ppu_context* c) { signal_impl(c, false, false); }
void signal_all(ppu_context* c) { signal_impl(c, true, false); }
void signal_to(ppu_context* c) { signal_impl(c, false, true); }
void wait(ppu_context* c) {
    uint32_t ea = (uint32_t)c->gpr[3];
    uint64_t timeout = c->gpr[4];
    std::unique_lock guard(state_lock);
    auto it = find_condition(ea);
    if (it == conditions.end()) { result(c, CELL_ESRCH); return; }
    auto cond = it->second;
    if (vm_read32(cond->mutex) != (uint32_t)c->thread_id) {
        result(c, CELL_EPERM); return;
    }
    if (vm_read32(cond->mutex + 12) != 1) { result(c, CELL_EDEADLK); return; }
    Waiter waiter{c->thread_id};
    cond->waiters.push_back(&waiter);
    c->gpr[3] = cond->mutex;
    ps3_hle_call(ps3_compute_nid("sys_lwmutex_unlock"), c);
    bool woke = true;
    if (timeout) {
        const auto bounded = std::min<uint64_t>(timeout, INT64_MAX);
        woke = waiter.cv.wait_for(guard, std::chrono::microseconds(bounded),
            [&] { return waiter.signalled; });
    } else {
        waiter.cv.wait(guard, [&] { return waiter.signalled; });
    }
    cond->waiters.remove(&waiter);
    guard.unlock();
    c->gpr[3] = cond->mutex;
    c->gpr[4] = 0; // Reacquire without a timeout, including after a timed wait.
    ps3_hle_call(ps3_compute_nid("sys_lwmutex_lock"), c);
    c->gpr[4] = timeout;
    result(c, woke ? CELL_OK : CELL_ETIMEDOUT);
}
} // namespace

extern "C" void wkc2_register_lwcond(void) {
    struct Entry { const char* name; void (*fn)(ppu_context*); } entries[] = {
        {"sys_lwcond_create", create}, {"sys_lwcond_destroy", destroy},
        {"sys_lwcond_wait", wait}, {"sys_lwcond_signal", signal_one},
        {"sys_lwcond_signal_all", signal_all}, {"sys_lwcond_signal_to", signal_to},
    };
    for (auto& entry : entries)
        ps3_hle_register_ctx(ps3_compute_nid(entry.name), entry.name, entry.fn);
}
