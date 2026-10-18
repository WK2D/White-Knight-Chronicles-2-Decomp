/* Exercise the production detached-worker ownership registry. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "spu_async_lifetime.h"
#include "spu_async_fixture.inc"
static HANDLE start_event, release_event;
static volatile LONG tried, winners, coalesced;
static unsigned reservation_wakes;
/* The real coherence registry calls this host-wake boundary. No SPU is parked
 * in this fixture; reservation validity and LR are updated by production code. */
void spu_ch_wake(spu_context* ctx) {(void)ctx;++reservation_wakes;}
static HANDLE completion_entered,completion_done;
static DWORD WINAPI complete_task(LPVOID opaque) {
    SetEvent(completion_entered);
    spu_taskset_task_exited((spu_async_job*)opaque);
    SetEvent(completion_done);return 0;
}
static void coherent_completion(void) {
    const uint32_t ea=0xb0000;
    vm_base=(uint8_t*)calloc(1,2u<<20);assert(vm_base);
    assert(spu_workload_taskset_begin(ea));
    spu_async_job job={0};job.taskset_ea=ea;job.taskid=0;
    assert(async_owner_claim(SPU_ASYNC_TASK,ea,0,0,&job.lifetime)==1);
    vm_base[ea+0x10]=vm_base[ea+0x20]=vm_base[ea+0x30]=0x80;
    completion_entered=CreateEvent(NULL,TRUE,FALSE,NULL);
    completion_done=CreateEvent(NULL,TRUE,FALSE,NULL);assert(completion_entered && completion_done);
    /* Model an SPU paused AFTER PUTLLC's comparison, before its line copy.
     * The actual lock-line lock must exclude the actual completion function
     * for that whole window. The peer sets task 1 in the same bitset bytes. */
    spu_lockline_lock();
    uint8_t commit[128];memcpy(commit,vm_base+ea,sizeof commit);
    commit[0x10]|=0x40;commit[0x20]|=0x40;commit[0x30]|=0x40;
    HANDLE worker=CreateThread(NULL,0,complete_task,&job,0,NULL);assert(worker);
    assert(WaitForSingleObject(completion_entered,5000)==WAIT_OBJECT_0);
    /* Owner release happens before the bit update; wait for that point so
     * the test cannot pass merely because the worker was never scheduled. */
    for(unsigned n=0;n<5000 && async_owner_active(SPU_ASYNC_TASK,ea,0,0);n++) Sleep(1);
    assert(!async_owner_active(SPU_ASYNC_TASK,ea,0,0));
    DWORD early=WaitForSingleObject(completion_done,50);
    memcpy(vm_base+ea,commit,sizeof commit);
    spu_lockline_unlock();
    assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    assert(early==WAIT_TIMEOUT);
    /* No retired task can be resurrected by the peer's stale full-line copy;
     * no newly posted sibling bit can be lost by the completion's clear. */
    assert(vm_base[ea+0x10]==0x40 && vm_base[ea+0x20]==0x40 && vm_base[ea+0x30]==0x40);
    CloseHandle(worker);CloseHandle(completion_entered);CloseHandle(completion_done);
    /* Independently prove real reservation invalidation and LR delivery,
     * including a task index in the last bitset byte. */
    spu_context* observer=(spu_context*)malloc(sizeof *observer);assert(observer);
    spu_context_init(observer,0);observer->resv_ea=ea;observer->resv_valid=1;
    spu_lockline_lock();spu_coh_reserve(observer,ea);spu_lockline_unlock();
    job.taskid=127;
    assert(async_owner_claim(SPU_ASYNC_TASK,ea,0,127,&job.lifetime)==1);
    vm_base[ea+0x1f]=vm_base[ea+0x2f]=vm_base[ea+0x3f]=1;
    unsigned before=reservation_wakes;
    spu_taskset_task_exited(&job);
    assert(!observer->resv_valid && (observer->event_status&SPU_EVENT_LR));
    assert(reservation_wakes==before+1);
    assert(!vm_base[ea+0x1f] && !vm_base[ea+0x2f] && !vm_base[ea+0x3f]);
    spu_coh_unregister(observer);free(observer);
    assert(spu_workload_taskset_close(ea));free(vm_base);vm_base=NULL;
}
static DWORD WINAPI contend(LPVOID ignored) {
    (void)ignored;
    WaitForSingleObject(start_event,INFINITE);
    spu_async_ticket ticket={0};
    int result=async_owner_claim(SPU_ASYNC_TASK,0x10000,0,7,&ticket);
    assert(result>=0);
    if (result) InterlockedIncrement(&winners);
    else InterlockedIncrement(&coalesced);
    InterlockedIncrement(&tried);
    if (result) {
        WaitForSingleObject(release_event,INFINITE);
        assert(async_owner_finish(&ticket)==1);
        assert(async_owner_finish(&ticket)==0);
    }
    return 0;
}
int main(void) {
    assert(async_owner_begin(SPU_ASYNC_TASK,0x10000,0));
    start_event=CreateEvent(NULL,TRUE,FALSE,NULL);
    release_event=CreateEvent(NULL,TRUE,FALSE,NULL);
    assert(start_event && release_event);
    HANDLE workers[16];
    for(int i=0;i<16;i++) { workers[i]=CreateThread(NULL,0,contend,NULL,0,NULL);assert(workers[i]); }
    SetEvent(start_event);
    for(int i=0;i<2000 && tried!=16;i++) Sleep(1);
    assert(tried==16 && winners==1 && coalesced==15);
    assert(!async_owner_begin(SPU_ASYNC_TASK,0x10000,0));
    assert(!async_owner_close(SPU_ASYNC_TASK,0x10000,0));
    for(int i=0;i<10000;i++) {
        spu_async_ticket duplicate={0};
        assert(async_owner_claim(SPU_ASYNC_TASK,0x10000,0,7,&duplicate)==0);
        assert(!duplicate.owner);
    }
    SetEvent(release_event);
    assert(WaitForMultipleObjects(16,workers,TRUE,5000)==WAIT_OBJECT_0);
    for(int i=0;i<16;i++) CloseHandle(workers[i]);
    CloseHandle(start_event);CloseHandle(release_event);
    /* Completed slots are reusable; stale/duplicate cleanup cannot retire a
     * newer claim at the same guest task id, even within one taskset lifetime. */
    for(int i=0;i<1000;i++) {
        spu_async_ticket first={0},next={0};
        assert(async_owner_claim(SPU_ASYNC_TASK,0x10000,0,7,&first)==1);
        spu_async_ticket stale=first;
        assert(async_owner_finish(&first)==1);
        assert(async_owner_claim(SPU_ASYNC_TASK,0x10000,0,7,&next)==1);
        assert(async_owner_finish(&stale)==0);
        assert(async_owner_active(SPU_ASYNC_TASK,0x10000,0,7));
        assert(async_owner_finish(&next)==1);
    }
    spu_async_ticket a={0},b={0},c={0},d={0};
    assert(async_owner_claim(SPU_ASYNC_TASK,0x10000,0,1,&a)==1);
    assert(async_owner_claim(SPU_ASYNC_TASK,0x10000,0,2,&b)==1);
    assert(async_owner_claim(SPU_ASYNC_TASK,0x20000,0,1,&c)==1);
    assert(async_owner_claim(SPU_ASYNC_WORKLOAD,0x10000,1,0,&d)==1);
    assert(async_owner_finish(&a) && async_owner_finish(&b) && async_owner_finish(&c) && async_owner_finish(&d));
    /* Workloads key by SPURS owner plus workload id, not image/arg addresses. */
    assert(async_owner_begin(SPU_ASYNC_WORKLOAD,0x30000,4));
    assert(async_owner_claim(SPU_ASYNC_WORKLOAD,0x30000,4,0,&a)==1);
    assert(async_owner_claim(SPU_ASYNC_WORKLOAD,0x30000,4,0,&b)==0);
    assert(async_owner_claim(SPU_ASYNC_WORKLOAD,0x30000,5,0,&c)==1);
    assert(!async_owner_close(SPU_ASYNC_WORKLOAD,0x30000,4));
    assert(async_owner_finish(&a) && async_owner_finish(&c));
    assert(async_owner_close(SPU_ASYNC_WORKLOAD,0x30000,4));
    assert(async_owner_claim(SPU_ASYNC_WORKLOAD,0x30000,4,0,&a)==-1);
    assert(async_owner_begin(SPU_ASYNC_WORKLOAD,0x30000,4));
    assert(async_owner_claim(SPU_ASYNC_WORKLOAD,0x30000,4,0,&a)==1);
    assert(async_owner_finish(&a));
    /* There is no old eight-taskset cap. */
    for(uint32_t i=1;i<=64;i++) {
        uint32_t ea=0x40000+i*0x1000;
        assert(async_owner_begin(SPU_ASYNC_TASK,ea,0));
        assert(async_owner_claim(SPU_ASYNC_TASK,ea,0,127,&a)==1);
        assert(async_owner_finish(&a));
        assert(async_owner_close(SPU_ASYNC_TASK,ea,0));
    }
    actual_dispatch_cases();
    coherent_completion();
    puts("SPU async lifetime: one active owner, slot reuse and stale cleanup passed");
    return 0;
}
