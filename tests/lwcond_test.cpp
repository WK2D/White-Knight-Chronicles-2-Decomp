#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/error_codes.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

extern "C" void wkc2_register_lwcond(void);
static uint8_t memory[4096];
static std::mutex guest_mutex;
static std::mutex memory_mutex;
static std::unordered_map<uint32_t, void (*)(ppu_context*)> handlers;
extern "C" uint32_t vm_read32(uint64_t a) {
    std::lock_guard guard(memory_mutex);
    return (uint32_t(memory[a])<<24)|(uint32_t(memory[a+1])<<16)|
        (uint32_t(memory[a+2])<<8)|memory[a+3];
}
extern "C" void vm_write32(uint64_t a, uint32_t v) {
    std::lock_guard guard(memory_mutex);
    for (int i=0; i<4; ++i) memory[a+i]=(uint8_t)(v>>(24-i*8));
}
extern "C" void ps3_hle_register_ctx(uint32_t nid, const char*, void (*fn)(ppu_context*)) {
    handlers[nid]=fn;
}
extern "C" void ps3_hle_call(uint32_t nid, ppu_context* c) {
    if (nid==ps3_compute_nid("sys_lwmutex_lock")) {
        guest_mutex.lock(); vm_write32(c->gpr[3], (uint32_t)c->thread_id);
        vm_write32(c->gpr[3]+12,1); c->gpr[3]=0;
    } else if (nid==ps3_compute_nid("sys_lwmutex_unlock")) {
        vm_write32(c->gpr[3],0); vm_write32(c->gpr[3]+12,0);
        guest_mutex.unlock(); c->gpr[3]=0;
    } else handlers.at(nid)(c);
}
static uint32_t call(ppu_context& c, const char* fn, uint32_t a, uint64_t b=0) {
    c.gpr[3]=a; c.gpr[4]=b; ps3_hle_call(ps3_compute_nid(fn),&c);
    return (uint32_t)c.gpr[3];
}
int main() {
    wkc2_register_lwcond();
    ppu_context main{}; main.thread_id=1;
    constexpr uint32_t cond=128, mutex=256;
    vm_write32(cond+8,0x12345678);
    assert(call(main,"sys_lwcond_create",cond,mutex)==0);
    assert(vm_read32(cond)==mutex && vm_read32(cond+4)!=0);
    assert(vm_read32(cond+8)==0x12345678); // Adjacent guest data stays intact.
    assert(call(main,"sys_lwmutex_lock",mutex)==0);
    assert(call(main,"sys_lwcond_wait",cond,2000)==CELL_ETIMEDOUT);
    assert(vm_read32(mutex)==1); // A timed wait also reacquires ownership.
    bool done=false;
    std::thread signaller([&] {
        ppu_context worker{}; worker.thread_id=2;
        call(worker,"sys_lwmutex_lock",mutex);
        done=true;
        // Signal happens while holding the mutex, potentially before sleep.
        assert(call(worker,"sys_lwcond_signal",cond)==0);
        call(worker,"sys_lwmutex_unlock",mutex);
    });
    while(!done) assert(call(main,"sys_lwcond_wait",cond,1000000)==0);
    assert(vm_read32(mutex)==1);
    call(main,"sys_lwmutex_unlock",mutex);
    signaller.join();
    assert(call(main,"sys_lwcond_destroy",cond)==0);
    assert(call(main,"sys_lwcond_signal",cond)==CELL_ESRCH);
}
