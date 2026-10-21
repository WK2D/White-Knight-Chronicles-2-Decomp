// Native execution of SPU image 21, the SPURS event-flag set job. See
// native_event_flag.h for the line layout and the update rules.
//
// The job descriptor carries the flag's 64-bit address at +0x30 and the bit
// number at +0x3C. The update runs as a native reservation loop over the same
// coherent line operations SPU atomics use (patch 0125). A set that would wake
// an SPU task declines before any store, so the compiled job keeps that path:
// it needs the SPURS kernel context, which this runtime does not stage for jobs.
#include "native_event_flag.h"
#include <cstdio>
#include <cstring>

#include "spu_coherency.h"
#include "spu_workload.h"
extern "C" {
extern std::uint8_t* vm_base;
std::uint32_t spurs_port_queue(std::uint32_t port);
int sys_event_queue_push_by_id(std::uint32_t queue_id,std::uint64_t source,std::uint64_t data1,
                               std::uint64_t data2,std::uint64_t data3);
#ifdef _WIN32
extern __declspec(thread) int g_spurs_job_user_events;
#else
extern __thread int g_spurs_job_user_events;
#endif
}

namespace wkc::event_flag {
namespace {
std::uint32_t be32(const std::uint8_t* p) {
    return std::uint32_t(p[0])<<24|std::uint32_t(p[1])<<16|std::uint32_t(p[2])<<8|p[3];
}
// What the job's interrupt-mailbox write does for a SPURS job context (no lv2
// group, SPU id 0): a no-reply user event on the port's attached queue.
void deliver_event(std::uint32_t value,std::uint32_t data) {
    const unsigned port=(value>>24)&63;
    const std::uint32_t queue=spurs_port_queue(port);
    if(!queue) {
        static int reported=0;
        if(reported++<16)std::fprintf(stderr,"[spu-evt] SPURS user event on unbound port %u dropped (img=21 native)\n",port);
        return;
    }
    const int rc=sys_event_queue_push_by_id(queue,0xFFFFFFFF53505501ull,0,
        (std::uint64_t(port)<<32)|(value&0xFFFFFFu),data);
    if(rc==0&&port>=16)++g_spurs_job_user_events; // the chain then skips its own completion event
}
EventSink sink=deliver_event;
}

void set_event_sink(EventSink next) {sink=next?next:deliver_event;}

int execute_job(std::uint32_t job_ea,std::uint32_t descriptor_size) {
    if(!vm_base||descriptor_size<0x40)return SPU_NATIVE_JOB_DECLINED;
    const std::uint8_t* descriptor=vm_base+job_ea;
    const std::uint32_t high=be32(descriptor+0x30),ea=be32(descriptor+0x34);
    const std::uint32_t shift=be32(descriptor+0x3C)&63;
    const std::uint16_t bits=std::uint16_t(shift<32?(1u<<shift):0u);
    if(!high&&!ea)return SPU_NATIVE_JOB_COMPLETED;   // null flag: the job returns an error, no effects
    if(ea&127)return SPU_NATIVE_JOB_COMPLETED;       // misaligned flag: likewise
    if(high)return SPU_NATIVE_JOB_DECLINED;          // beyond the 32-bit guest space
    SetResult result;
    for(;;) {
        std::uint8_t line[128];std::uint64_t token=0;
        if(spu_native_getllar(ea,line,&token)<0)return SPU_NATIVE_JOB_DECLINED;
        result=apply_set(line,bits);
        if(result.rejected){spu_native_release_reservation(token);return SPU_NATIVE_JOB_COMPLETED;}
        if(result.woken_tasks){spu_native_release_reservation(token);return SPU_NATIVE_JOB_DECLINED;}
        const int stored=spu_native_putllc(ea,line,token);
        if(stored==0)break;
        if(stored<0)return SPU_NATIVE_JOB_DECLINED;
    }
    if(result.wake_ppu&&result.port<64)sink(0x40000000u|std::uint32_t(result.port)<<24,0);
    return SPU_NATIVE_JOB_COMPLETED;
}
}
