/* PPU producer for the guest CellSpursLFQueue consumed by lifted SPU code.
 * All queue mutations share the SPU atomic-line lock. The PPU copies an item
 * before publishing its sequence; a consumer cannot commit an older snapshot
 * over that publication. Guest halfword indices wrap at twice the ring depth.
 * ANY2ANY waiter packs contain three modulo-30 cursors: registered (bits 0..4),
 * resumed (5..9), and signalled (10..14), with 15 shared waiter-id slots.
 * Layout/dataflow checked against WKC2's SPU functions 0x9A30 and 0x9EC0.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <chrono>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
void spu_lockline_lock(void);
void spu_lockline_unlock(void);
void spu_coh_notify_write(uint32_t);
void spu_taskset_signal_task(uint32_t, uint32_t);
int32_t cellSpursCreateTaskset(void*, void*, uint64_t, const uint8_t*, uint32_t);
int32_t cellSpursCreateTasksetWithAttribute(void*, void*, const void*);
}

namespace {
constexpr uint32_t again = 0x80410901, invalid = 0x80410902;
constexpr uint32_t permission = 0x80410909, alignment = 0x80410910;
constexpr uint32_t null_pointer = 0x80410911, state_error = 0x8041090F;
std::mutex directory_lock;
std::map<std::pair<uint32_t,uint32_t>, uint32_t> tasksets;

uint16_t r16(uint32_t a) { const auto* p = vm_base + a; return uint16_t(p[0]) << 8 | p[1]; }
uint32_t r32(uint32_t a) { return uint32_t(r16(a)) << 16 | r16(a+2); }
uint64_t r64(uint32_t a) { return uint64_t(r32(a)) << 32 | r32(a+4); }
void w16(uint32_t a, uint16_t v) { auto* p = vm_base + a; p[0] = uint8_t(v>>8); p[1] = uint8_t(v); }
void w32(uint32_t a, uint32_t v) { w16(a,uint16_t(v>>16)); w16(a+2,uint16_t(v)); }
void w64(uint32_t a, uint64_t v) { w32(a,uint32_t(v>>32)); w32(a+4,uint32_t(v)); }
void result(ppu_context* c, uint32_t rc) { c->gpr[3] = uint64_t(int64_t(int32_t(rc))); }
void* ea(uint64_t v) { return reinterpret_cast<void*>(uintptr_t(uint32_t(v))); }

void record_taskset(uint32_t ts) {
    std::lock_guard guard(directory_lock);
    tasksets[{uint32_t(r64(ts+0x60)),r32(ts+0x74)}] = ts;
}
void create_taskset(ppu_context* c) {
    uint32_t ts = uint32_t(c->gpr[4]);
    auto rc = cellSpursCreateTaskset(ea(c->gpr[3]),ea(ts),c->gpr[5],
        static_cast<const uint8_t*>(ea(c->gpr[6])),uint32_t(c->gpr[7]));
    if (!rc) record_taskset(ts);
    result(c,uint32_t(rc));
}
void create_taskset_attribute(ppu_context* c) {
    uint32_t ts = uint32_t(c->gpr[4]);
    auto rc = cellSpursCreateTasksetWithAttribute(ea(c->gpr[3]),ea(ts),ea(c->gpr[5]));
    if (!rc) record_taskset(ts);
    result(c,uint32_t(rc));
}

void signal(uint32_t owner, uint16_t waiter) {
    uint32_t ts = owner;
    uint32_t tid = waiter & 0xFF;
    if (owner & 1) {
        std::lock_guard guard(directory_lock);
        auto it = tasksets.find({owner & ~1u, uint32_t(waiter >> 8)});
        if (it == tasksets.end()) {
            fprintf(stderr,"[lfqueue] no taskset for spurs=%08X waiter=%04X\n",owner,waiter);
            return;
        }
        ts = it->second;
    }
    spu_taskset_signal_task(ts,tid);
}

void initialize(ppu_context* c) {
    uint32_t owner = uint32_t(c->gpr[3]), q = uint32_t(c->gpr[4]);
    uint32_t buffer = uint32_t(c->gpr[5]), size = uint32_t(c->gpr[6]);
    uint32_t depth = uint32_t(c->gpr[7]), direction = uint32_t(c->gpr[8]);
    if (!q || (size && !buffer)) { result(c,null_pointer); return; }
    if ((q & 127) || (buffer & 15)) { result(c,alignment); return; }
    if (!depth || depth > 0x7FFF || size > 0x4000 || (size & 15) || direction > 3 ||
        uint64_t(buffer) + uint64_t(size)*depth > 0x100000000ULL) {
        result(c,invalid); return;
    }
    spu_lockline_lock();
    std::memset(vm_base+q,0,128);
    w32(q+0x10,size); w32(q+0x14,depth);
    w64(q+0x18,uint64_t(buffer | (direction == 3 ? 1u : 0u)));
    w32(q+0x24,direction);
    w64(q+0x70,owner);
    vm_base[q+0x20] = 0xFF; vm_base[q+0x21] = 0xFF;
    if (direction == 3) {
        w32(q+0x28,0xFFFFFFFF); w16(q+0x30,0xFFFF); w16(q+0x50,0xFFFF);
    } else {
        vm_base[q+0x22] = 0xFF; vm_base[q+0x23] = 0xFF;
    }
    spu_coh_notify_write(q);
    spu_lockline_unlock();
    fprintf(stderr,"[lfqueue] init q=%08X buffer=%08X size=%u depth=%u direction=%u\n",
        q,buffer,size,depth,direction);
    result(c,0);
}

void push(ppu_context* c) {
    uint32_t q = uint32_t(c->gpr[3]), data = uint32_t(c->gpr[4]);
    bool blocking = c->gpr[5] != 0;
    if (!q || !data) { result(c,null_pointer); return; }
    if ((q & 127) || (data & 15)) { result(c,alignment); return; }
    for (;;) {
        spu_lockline_lock();
        uint32_t direction = r32(q+0x24), depth = r32(q+0x14), size = r32(q+0x10);
        uint32_t buffer = uint32_t(r64(q+0x18)) & ~1u;
        if (direction != 2 && direction != 3) {
            spu_lockline_unlock(); result(c,permission); return;
        }
        if (!depth || depth > 0x7FFF || size > 0x4000 || (size & 15) ||
            uint64_t(buffer) + uint64_t(depth)*size > 0x100000000ULL) {
            spu_lockline_unlock(); result(c,invalid); return;
        }
        uint32_t tail = r16(q+14), head = r16(q), committed = r16(q+8);
        uint32_t modulus = depth*2;
        /* All PPU producers serialize here. An outstanding SPU producer is
         * allowed to finish its reservation before we publish a later item. */
        uint32_t occupied = (tail + modulus - head) % modulus;
        if (tail >= modulus || head >= modulus || committed >= modulus) {
            spu_lockline_unlock(); result(c,state_error); return;
        }
        if (occupied >= depth || committed != tail) {
            spu_lockline_unlock();
            if (!blocking) { result(c,again); return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        uint32_t slot = tail % depth;
        std::memcpy(vm_base + buffer + size_t(slot)*size,vm_base+data,size);
        uint16_t next = uint16_t((tail+1)%modulus);
        w16(q+14,next); w16(q+8,next);
        uint32_t pack_ea = q + (direction == 3 ? 4 : 0x30);
        uint16_t pack = r16(pack_ea);
        unsigned registered = pack & 31, sent = (pack>>10)&31;
        bool notify = sent != registered && sent < 30 && registered < 30;
        uint16_t waiter = 0;
        if (notify) {
            waiter = r16(q+0x32+2*(sent%15));
            sent = (sent+1)%30;
            w16(pack_ea,uint16_t((pack & 0x83FF) | (sent<<10)));
        }
        uint32_t owner = uint32_t(r64(q+0x70));
        spu_coh_notify_write(q);
        spu_lockline_unlock();
        if (notify) signal(owner,waiter);
        static std::atomic<unsigned> logs{0};
        if (logs.fetch_add(1) < 12) fprintf(stderr,"[lfqueue] push q=%08X slot=%u sequence=%u->%u notify=%d waiter=%04X\n",
            q,slot,tail,next,int(notify),waiter);
        result(c,0); return;
    }
}
}

extern "C" void wkc2_register_lfqueue(void) {
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursCreateTaskset"),"cellSpursCreateTaskset",create_taskset);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursCreateTasksetWithAttribute"),"cellSpursCreateTasksetWithAttribute",create_taskset_attribute);
    ps3_hle_register_ctx(ps3_compute_nid("_cellSpursLFQueueInitialize"),"_cellSpursLFQueueInitialize",initialize);
    ps3_hle_register_ctx(0x8A85674D,"_cellSpursLFQueuePushBody",push);
}
