/* Production registration, guest endian output and boundary regressions.
 * No game code, platform settings changes or real device access. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sys_platform_queries.h"
#include "ppu_syscall_coverage.h"
#include <assert.h>
#include <stdlib.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
#ifndef _WIN32
#include <pthread.h>
#endif
static ppu_syscall_coverage coverage;
static unsigned wins[8];
#ifdef _WIN32
static DWORD WINAPI coverage_worker(void* arg)
#else
static void* coverage_worker(void* arg)
#endif
{
    unsigned index=(unsigned)(uintptr_t)arg;
    for(unsigned repeat=0;repeat<20;repeat++)for(unsigned id=0;id<1024;id++)
        wins[index]+=(unsigned)ppu_syscall_first_unhandled(&coverage,id);
    return 0;
}
static void coverage_checks(void) {
    ppu_syscall_coverage local={0};
    assert(ppu_syscall_first_unhandled(&local,144));
    for(unsigned i=0;i<1000;i++)assert(!ppu_syscall_first_unhandled(&local,144));
    assert(ppu_syscall_first_unhandled(&local,631)); /* survives the old global budget */
    assert(ppu_syscall_first_unhandled(&local,1023));
    assert(ppu_syscall_first_unhandled(&local,UINT64_MAX));
    assert(!ppu_syscall_first_unhandled(&local,1024));
#ifdef _WIN32
    HANDLE workers[8];
    for(unsigned i=0;i<8;i++){workers[i]=CreateThread(NULL,0,coverage_worker,(void*)(uintptr_t)i,0,NULL);assert(workers[i]);}
    assert(WaitForMultipleObjects(8,workers,TRUE,INFINITE)==WAIT_OBJECT_0);
    for(unsigned i=0;i<8;i++)CloseHandle(workers[i]);
#else
    pthread_t workers[8];
    for(unsigned i=0;i<8;i++)assert(!pthread_create(&workers[i],NULL,coverage_worker,(void*)(uintptr_t)i));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(workers[i],NULL));
#endif
    unsigned total=0;for(unsigned i=0;i<8;i++)total+=wins[i];assert(total==1024);
}
static uint8_t memory[4096];
static lv2_syscall_table table;
static ppu_context context;
static uint32_t read32(unsigned at) {
    return (uint32_t)memory[at]<<24|(uint32_t)memory[at+1]<<16|
           (uint32_t)memory[at+2]<<8|memory[at+3];
}
static int32_t call(unsigned number,uint64_t a,uint64_t b) {
    memset(&context,0,sizeof context);context.gpr[11]=number;
    context.gpr[3]=a;context.gpr[4]=b;lv2_syscall_dispatch(&table,&context);
    return (int32_t)context.gpr[3];
}
static void unchanged(void) {
    for(unsigned i=0;i<sizeof memory;i++)assert(memory[i]==0xA5);
}
int main(void) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif
    coverage_checks();
    vm_base=memory;ppu_vm_size=sizeof memory;lv2_syscall_table_init(&table);
    lv2_platform_queries_register(&table);
    assert(table.handlers[144]!=lv2_syscall_unimplemented);
    assert(table.handlers[630]!=lv2_syscall_unimplemented);
    assert(table.handlers[631]!=lv2_syscall_unimplemented);
    memset(memory,0xA5,sizeof memory);
    lv2_platform_store32(64,-330);lv2_platform_store32(68,345);
    const uint8_t expected[]={0xFF,0xFF,0xFE,0xB6,0,0,1,0x59};
    assert(!memcmp(memory+64,expected,sizeof expected));
    int32_t zone,summer;assert(lv2_platform_timezone_host(&zone,&summer));
    memset(memory,0xA5,sizeof memory);assert(call(144,128,132)==CELL_OK);
    assert((int32_t)read32(128)==zone && (int32_t)read32(132)==summer);
    for(unsigned i=0;i<sizeof memory;i++)if(i<128 || i>=136)assert(memory[i]==0xA5);
    const uint32_t invalid32[]={0,sizeof memory-3,sizeof memory,UINT32_MAX};
    for(unsigned i=0;i<sizeof invalid32/sizeof *invalid32;i++) {
        memset(memory,0xA5,sizeof memory);
        assert(call(144,128,invalid32[i])==(int32_t)CELL_EFAULT);unchanged();
        assert(call(144,invalid32[i],128)==(int32_t)CELL_EFAULT);unchanged();
    }
    assert(call(144,sizeof memory-4,128)==CELL_OK);
    for(unsigned device=1;device<=2;device++) {
        memset(memory,0xA5,sizeof memory);assert(call(631,device,128)==CELL_OK);
        for(unsigned i=0;i<sizeof memory;i++)assert(memory[i]==(i>=128&&i<136?0:0xA5));
        const uint32_t invalid64[]={0,sizeof memory-7,sizeof memory,UINT32_MAX};
        for(unsigned i=0;i<sizeof invalid64/sizeof *invalid64;i++) {
            memset(memory,0xA5,sizeof memory);
            assert(call(631,device,invalid64[i])==(int32_t)CELL_EFAULT);unchanged();
        }
        assert(call(631,device,sizeof memory-8)==CELL_OK);
    }
    for(unsigned device=0;device<=3;device+=3) {
        memset(memory,0xA5,sizeof memory);
        assert(call(631,device,128)==(int32_t)CELL_ESRCH);unchanged();
    }
    assert(call(630,1,~UINT64_C(0))==CELL_OK);
    assert(call(630,2,0)==(int32_t)CELL_EINVAL);
    assert(call(630,3,0)==(int32_t)CELL_ESRCH);
    memset(memory,0xA5,sizeof memory);vm_base=NULL;
    assert(call(144,128,132)==(int32_t)CELL_EFAULT);
    assert(call(631,1,128)==(int32_t)CELL_EFAULT);unchanged();
    puts("Registered timezone/GPIO outputs, endian encoding, retail semantics and bounds pass.");return 0;
}
