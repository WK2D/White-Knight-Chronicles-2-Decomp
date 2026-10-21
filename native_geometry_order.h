// Ordered write log for pipelined geometry jobs.
// A job copies its sources before earlier jobs have committed. Before it
// commits, any commit made after that copy that wrote one of its source ranges
// makes the copy stale; the job must then be copied and prepared again.
// Project-written; no SDK, guest or SPU types.
#ifndef WKC_NATIVE_GEOMETRY_ORDER_H
#define WKC_NATIVE_GEOMETRY_ORDER_H
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace wkc::geometry::order {
// Half-open guest byte range [begin, end).
struct ByteRange {std::uint64_t begin=0,end=0;};

inline bool overlaps(const std::vector<ByteRange>& a,const std::vector<ByteRange>& b) {
    for(const auto& x:a)for(const auto& y:b)if(x.begin<y.end&&y.begin<x.end)return true;
    return false;
}

class WriteLog {
public:
    explicit WriteLog(std::size_t limit=8192):limit_(limit?limit:1) {}
    // Sequence number of the latest commit; a copy records it before reading.
    std::uint64_t sequence()const {std::lock_guard<std::mutex> lock(mutex_);return sequence_;}
    // True when a commit after `after` wrote any of `reads`. A log that no
    // longer reaches back to `after` cannot prove otherwise and reports true.
    bool written_since(std::uint64_t after,const std::vector<ByteRange>& reads)const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(sequence_==after)return false;
        if(log_.empty()||log_.front().first>after+1)return true;
        for(auto it=log_.rbegin();it!=log_.rend()&&it->first>after;++it)
            if(overlaps(it->second,reads))return true;
        return false;
    }
    void commit(std::vector<ByteRange> writes) {
        std::lock_guard<std::mutex> lock(mutex_);
        log_.emplace_back(++sequence_,std::move(writes));
        if(log_.size()>limit_)log_.pop_front();
    }
    // A commit whose writes are unknown (for example a compiled fallback job):
    // every copy made before it is stale.
    void commit_unknown() {commit({ByteRange{0,std::uint64_t(1)<<32}});}
private:
    mutable std::mutex mutex_;
    std::uint64_t sequence_=0;
    std::deque<std::pair<std::uint64_t,std::vector<ByteRange>>> log_;
    std::size_t limit_;
};
}
#endif
