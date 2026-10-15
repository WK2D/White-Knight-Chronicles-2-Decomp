#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <cassert>
#include <cstring>
#include <mutex>
#include <unordered_map>

static uint8_t memory[8192];
extern "C" { uint8_t* vm_base = memory; }
static std::mutex atomic_line;
static std::unordered_map<uint32_t,void(*)(ppu_context*)> handlers;
static unsigned signals;
static uint32_t signalled_ts, signalled_task;
static uint16_t r16(uint32_t a) { return uint16_t(memory[a])<<8 | memory[a+1]; }
static void w16(uint32_t a,uint16_t v) { memory[a]=uint8_t(v>>8); memory[a+1]=uint8_t(v); }
static void w32(uint32_t a,uint32_t v) { w16(a,uint16_t(v>>16)); w16(a+2,uint16_t(v)); }
extern "C" void wkc2_register_lfqueue(void);
extern "C" void ps3_hle_register_ctx(uint32_t id,const char*,void(*f)(ppu_context*)) { handlers[id]=f; }
extern "C" void spu_lockline_lock(void) { atomic_line.lock(); }
extern "C" void spu_lockline_unlock(void) { atomic_line.unlock(); }
extern "C" void spu_coh_notify_write(uint32_t) {}
extern "C" void spu_taskset_signal_task(uint32_t ts,uint32_t task) {
    ++signals; signalled_ts=ts; signalled_task=task;
}
extern "C" int32_t cellSpursCreateTaskset(void* spurs,void* taskset,uint64_t,const uint8_t*,uint32_t) {
    uint32_t ts=uint32_t(uintptr_t(taskset));
    w32(ts+0x60,0); w32(ts+0x64,uint32_t(uintptr_t(spurs))); w32(ts+0x74,2);
    return 0;
}
extern "C" int32_t cellSpursCreateTasksetWithAttribute(void* spurs,void* taskset,const void*) {
    return cellSpursCreateTaskset(spurs,taskset,0,nullptr,0);
}
static uint32_t call(const char* name, uint64_t a,uint64_t b=0,uint64_t c=0,
                     uint64_t d=0,uint64_t e=0,uint64_t f=0) {
    ppu_context ctx{};
    ctx.gpr[3]=a; ctx.gpr[4]=b; ctx.gpr[5]=c; ctx.gpr[6]=d; ctx.gpr[7]=e; ctx.gpr[8]=f;
    handlers.at(ps3_compute_nid(name))(&ctx); return uint32_t(ctx.gpr[3]);
}
int main() {
    constexpr uint32_t q=128, ring=256, item=512, taskset=1024, spurs=2048;
    wkc2_register_lfqueue();
    assert(call("cellSpursCreateTaskset",spurs,taskset)==0);
    memory[q+128-1]=1; memory[q+128]=0xC3;
    assert(call("_cellSpursLFQueueInitialize",spurs|1,q,ring,32,3,3)==0);
    assert(memory[q+128]==0xC3); // Header occupies exactly 128 bytes.
    assert(r16(q+14)==0); // Old SDK erroneously placed depth here.
    assert(r16(q+0x26)==3 && memory[q+0x1F]==1); // Direction and tagged buffer.
    assert(r16(q+0x30)==0xFFFF && r16(q+4)==0);
    std::memset(memory+item,0xA5,32);
    // A real SPU waiter publishes workload 2 / task 7 in slot 0 before parking.
    w16(q+4,1); w16(q+0x32,0x0207);
    assert(call("_cellSpursLFQueuePushBody",q,item,0)==0);
    assert(signals==1 && signalled_ts==taskset && signalled_task==7);
    assert(r16(q+4)==0x401 && r16(q+8)==1 && r16(q+14)==1);
    assert(!std::memcmp(memory+ring,memory+item,32));
    // Consumer acknowledges the signal. Filling the ring must not overwrite it.
    w16(q+4,0x421);
    assert(call("_cellSpursLFQueuePushBody",q,item,0)==0);
    assert(call("_cellSpursLFQueuePushBody",q,item,0)==0);
    assert(call("_cellSpursLFQueuePushBody",q,item,0)==0x80410901);
    assert(signals==1 && r16(q+14)==3);
    // Complete each consumer reservation, then cross both ring and sequence wraps.
    for (unsigned sequence=3;sequence<81;++sequence) {
        w16(q,uint16_t((sequence-2)%6));
        w16(q+6,uint16_t((sequence-2)%6));
        assert(call("_cellSpursLFQueuePushBody",q,item,0)==0);
        assert(r16(q+8)==(sequence+1)%6 && r16(q+14)==(sequence+1)%6);
    }
    assert(call("_cellSpursLFQueuePushBody",q+1,item,0)==0x80410910);
    assert(call("_cellSpursLFQueuePushBody",0,item,0)==0x80410911);
}
