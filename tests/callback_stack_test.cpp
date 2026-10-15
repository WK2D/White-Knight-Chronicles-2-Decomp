#include "guest_callback_stack.hpp"
#include <atomic>
#include <cassert>
#include <set>
#include <thread>
#include <vector>
int main() {
    constexpr unsigned count=24;
    std::atomic<unsigned> ready=0;
    uint32_t tops[count]{};
    std::vector<std::thread> threads;
    for(unsigned i=0;i<count;++i) threads.emplace_back([&,i] {
        ps3recomp::GuestCallbackStack outer; tops[i]=outer.top;
        {ps3recomp::GuestCallbackStack inner; assert(inner.top==outer.top-0x10000);}
        {ps3recomp::GuestCallbackStack inner; assert(inner.top==outer.top-0x10000);}
        ++ready;
        while(ready.load()!=count) std::this_thread::yield();
        for(unsigned j=0;j<count;++j) if(j!=i) {
            uint32_t distance=tops[i]>tops[j] ? tops[i]-tops[j] : tops[j]-tops[i];
            assert(distance>=0x100000);
        }
    });
    for(auto& t:threads) t.join();
    // Sequential host threads return their slots, rather than consuming the pool.
    for(unsigned i=0;i<256;++i) {std::thread t([]{ps3recomp::GuestCallbackStack stack; assert(stack.top==0xCFFF0000);}); t.join();}
}
