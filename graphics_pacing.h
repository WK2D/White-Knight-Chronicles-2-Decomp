#ifndef WKC_GRAPHICS_PACING_H
#define WKC_GRAPHICS_PACING_H
#include <cstdint>
namespace wkc {
// Unlocked flips receive an immediate tick; idle/boot still gets a 59.94 Hz
// heartbeat. FIFO fence polling inside one frame must not create extra ticks.
class UnlockedVblankClock {
    double deadline_=0;
    uint32_t flips_=0;
    bool started_=false;
public:
    bool ready(double now,uint32_t flips) const { return !started_ || flips!=flips_ || now>=deadline_; }
    double delay(double now,uint32_t flips) const { return ready(now,flips)?0:deadline_-now; }
    bool take(double now,uint32_t flips) {
        if(!ready(now,flips)) return false;
        started_=true;flips_=flips;deadline_=now+1000.0*1001.0/60000.0;return true;
    }
    void reset() { started_=false; }
};
}
#endif
