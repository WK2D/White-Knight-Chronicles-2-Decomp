#include "guest_callback_stack.hpp"
#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>
#include <vector>

// Stand-in for sys_ppu_thread.c: guarded 1 MiB blocks from the stack region.
static std::mutex allocator_mutex;
static constexpr uint32_t first_block=0xD0001000u, block_stride=0x101000u;
static uint32_t next_block=first_block;
static unsigned allocations=0;
extern "C" uint32_t ppu_callback_stack_allocate(uint32_t size) {
    std::lock_guard l(allocator_mutex);
    assert(size==0x100000);
    const uint32_t base=next_block;
    next_block+=block_stride;   // the block and the next one's guard page
    ++allocations;
    return base;
}
static void check_placement(uint32_t top) {
    // RSX local memory is 0xC0000000-0xCFFFFFFF: callback frames must not land there.
    assert(top>=0xD0000000u && top<0xE0000000u && !(top&0xF));
    const uint32_t base=first_block+(top-first_block)/block_stride*block_stride;
    // The callee's LR/CR saves above the incoming SP stay inside the block.
    assert(top>base && top+0x100<=base+0x100000);
}
int main() {
    constexpr unsigned count=24;
    std::atomic<unsigned> ready=0;
    uint32_t tops[count]{};
    std::vector<std::thread> threads;
    for(unsigned i=0;i<count;++i) threads.emplace_back([&,i] {
        ps3recomp::GuestCallbackStack outer; tops[i]=outer.top;
        check_placement(outer.top);
        {ps3recomp::GuestCallbackStack inner; assert(inner.top==outer.top-0x10000); check_placement(inner.top);}
        {ps3recomp::GuestCallbackStack inner; assert(inner.top==outer.top-0x10000);}
        ++ready;
        while(ready.load()!=count) std::this_thread::yield();
        for(unsigned j=0;j<count;++j) if(j!=i) {
            uint32_t distance=tops[i]>tops[j] ? tops[i]-tops[j] : tops[j]-tops[i];
            assert(distance>=0x100000);
        }
    });
    for(auto& t:threads) t.join();
    assert(allocations==count);
    // Sequential host threads return their slots, and a returned slot keeps its memory.
    uint32_t reused=0;
    for(unsigned i=0;i<256;++i) {std::thread t([&]{ps3recomp::GuestCallbackStack stack; if(!reused) reused=stack.top; assert(stack.top==reused);}); t.join();}
    assert(allocations==count);
}
