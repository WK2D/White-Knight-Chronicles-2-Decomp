// Project-written replacement for SPU image 21 (`zz_raw_job_012A1980`), the
// SPU-side SPURS event-flag set job.
//
// The job ORs one bit into a 128-byte CellSpursEventFlag line with an atomic
// read-modify-write, releases waiters whose conditions are now met, and wakes
// a waiting PPU thread with an SPU thread event on the flag's port. Layout:
//
//   +0x00 be16 events            +0x08 be16 spuTaskUsedWaitSlots
//   +0x02 be16 spuTaskPendingRecv +0x0A be16 spuTaskWaitMode (bit set = AND)
//   +0x04 be16 ppuWaitMask        +0x0C u8 spuPort, +0x0D u8 isIwl
//   +0x06 u8 ppuWaitSlotAndMode   +0x0E u8 direction, +0x0F u8 clearMode
//   +0x07 u8 ppuPendingRecv       +0x10 be16 spuTaskWaitMask[16]
//   +0x30 be16 pendingRecvTaskEvents[16]  +0x50 waitingTaskId[16]
//   +0x60 waitingTaskWklId[16]    +0x70 be64 taskset address
//
// Slot i corresponds to mask bit 0x8000 >> i. Directions: 0 SPU-to-SPU,
// 1 SPU-to-PPU, 2 PPU-to-SPU (rejected), 3 any-to-any. Wait mode 0 is OR,
// clear mode 0 is automatic.
#pragma once
#include <cstdint>

namespace wkc::event_flag {

struct SetResult {
    bool rejected=false;          // invalid direction: the job ends with no store
    bool wake_ppu=false;          // send the PPU wake-up event on `port`
    std::uint8_t port=0;
    std::uint16_t woken_tasks=0;  // SPU task slots released by this set
};

inline std::uint16_t load16(const std::uint8_t* p) {return std::uint16_t(p[0]<<8|p[1]);}
inline void store16(std::uint8_t* p,std::uint16_t v) {p[0]=std::uint8_t(v>>8);p[1]=std::uint8_t(v);}

// One pass of the job's reservation loop over `line`, which is modified in
// place exactly as the job modifies its local-store copy before PUTLLC.
inline SetResult apply_set(std::uint8_t line[128],std::uint16_t bits) {
    SetResult r;
    const std::uint8_t direction=line[0x0E];
    if(direction==2||direction>3){r.rejected=true;return r;}
    const std::uint16_t events=std::uint16_t(load16(line)|bits);
    std::uint16_t consumed=0;
    if(direction==0||direction==3) {
        const std::uint16_t candidates=std::uint16_t(load16(line+0x08)&~load16(line+0x02));
        const std::uint16_t and_mode=load16(line+0x0A);
        for(int slot=0;slot<16;++slot) {
            const std::uint16_t bit=std::uint16_t(0x8000u>>slot);
            if(!(candidates&bit))continue;
            const std::uint16_t wanted=load16(line+0x10+2*slot);
            const std::uint16_t have=std::uint16_t(events&wanted);
            if(have!=wanted&&(have==0||(and_mode&bit)))continue;
            r.woken_tasks|=bit;
            consumed|=have;
            store16(line+0x30+2*slot,std::uint16_t(load16(line+0x30+2*slot)|have));
        }
        store16(line+0x02,std::uint16_t(load16(line+0x02)|r.woken_tasks));
    }
    const std::uint16_t ppu_wanted=load16(line+0x04);
    if((direction==1||direction==3)&&ppu_wanted) {
        const std::uint16_t have=std::uint16_t(ppu_wanted&events);
        const bool or_mode=(line[0x06]&0x0F)==0;
        if(have==ppu_wanted||(or_mode&&have)) {
            consumed|=have;
            const unsigned slot=line[0x06]>>4;
            store16(line+0x30+2*slot,have);
            line[0x07]=1;
            store16(line+0x04,0);
            r.wake_ppu=true;
            r.port=line[0x0C];
        }
    }
    store16(line,line[0x0F]==0?std::uint16_t(events&~consumed):events);
    return r;
}

// Runtime entry for an image-21 job (native_event_flag.cpp): returns the SDK's
// SPU_NATIVE_JOB_COMPLETED or SPU_NATIVE_JOB_DECLINED.
int execute_job(std::uint32_t job_ea,std::uint32_t descriptor_size);
// Where the PPU wake-up goes: the SPURS port's event queue by default. Job
// replay substitutes a recorder. `value` is the interrupt-mailbox word.
using EventSink=void(*)(std::uint32_t value,std::uint32_t data);
void set_event_sink(EventSink sink);

}
