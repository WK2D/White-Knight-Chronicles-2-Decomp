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
    puts("SPU async lifetime: one active owner, slot reuse and stale cleanup passed");
    return 0;
}
