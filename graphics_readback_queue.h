#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace wkc {
// The render thread owns the GPU. A guest CPU consumer waits until that thread
// has materialized the requested bytes; timed-out requests cannot run later.
class GraphicsReadbackQueue {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> pending_{false};
    bool busy_=false,done_=false,stopped_=false,result_=false;
    uint32_t offset_=0,bytes_=0;
public:
    bool read(uint32_t offset,uint32_t bytes,std::chrono::milliseconds timeout) {
        const auto deadline=std::chrono::steady_clock::now()+timeout;
        std::unique_lock lock(mutex_);
        if(!changed_.wait_until(lock,deadline,[&]{return !busy_||stopped_;})||stopped_) return false;
        busy_=true;done_=false;result_=false;offset_=offset;bytes_=bytes;
        pending_.store(true,std::memory_order_release);
        const bool completed=changed_.wait_until(lock,deadline,[&]{return done_||stopped_;});
        const bool result=completed&&!stopped_&&done_&&result_;
        pending_.store(false,std::memory_order_release);
        busy_=false;lock.unlock();changed_.notify_all();
        return result;
    }
    template<class Read> void service(Read&& read) {
        if(!pending_.load(std::memory_order_acquire)) return;
        std::unique_lock lock(mutex_);
        if(!busy_||done_||stopped_) return;
        // Keep the lock through readback. A deadline cannot release a CPU
        // consumer while a GPU operation is still writing its source bytes.
        result_=read(offset_,bytes_);done_=true;
        pending_.store(false,std::memory_order_release);
        lock.unlock();changed_.notify_all();
    }
    void stop() {
        std::lock_guard lock(mutex_);
        stopped_=true;pending_.store(false,std::memory_order_release);
        changed_.notify_all();
    }
};
}
