#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include "cellSpurs.h"
#include "ppu_memory.h"
uint8_t* vm_base;
uint32_t g_spurs_job_ls_handle;
static volatile LONG jobs_seen;
int spu_workload_dispatch_job(const uint8_t* image, uint32_t size,uint32_t ea,uint32_t desc) {
    (void)image;(void)size;(void)ea;(void)desc;
    InterlockedIncrement(&jobs_seen);return 0;
}
extern s32 cellSpursCreateJobChain(u64,u64,u64,u32,u32,u64,u32,u32);
extern s32 cellSpursRunJobChain(u64);
extern s32 cellSpursShutdownJobChain(u64);
extern s32 cellSpursJoinJobChain(u64);
extern s32 cellSpursJobGuardInitialize(u64,u64,u32,u32,u32);
extern s32 cellSpursJobGuardNotify(u64);
extern s32 cellSpursJobGuardReset(u64);
static void wait_jobs(LONG n) {
    for (int i=0;i<2000 && InterlockedCompareExchange(&jobs_seen,0,0)!=n;++i) Sleep(1);
    assert(InterlockedCompareExchange(&jobs_seen,0,0)==n);
}
int main(void) {
 vm_base=calloc(1,0x100000);assert(vm_base);
 const u32 commands=0x1000,job=0x1100,binary=0x1200;
 vm_write64(job,binary);vm_write32(job+8,0x00100000);
 for(int i=0;i<100;++i) {
  u32 chain=0x10000+i*0x200,guard=chain+0x80;
  vm_write64(commands,guard|15);vm_write64(commands+8,job);vm_write64(commands+16,commands|3);
  assert(cellSpursCreateJobChain(0x100,chain,commands,128,1,0,1,1)==CELL_OK);
  assert(cellSpursJobGuardInitialize(chain,guard,2,1,1)==CELL_OK);
  assert(cellSpursRunJobChain(chain)==CELL_OK);
  Sleep(2);assert(jobs_seen==i*2);
  assert(cellSpursJobGuardNotify(guard)==CELL_OK);
  Sleep(2);assert(jobs_seen==i*2);
  // Reset clears the outstanding notification before arming a new pair.
  assert(cellSpursJobGuardReset(guard)==CELL_OK);
  assert(cellSpursJobGuardNotify(guard)==CELL_OK);
  Sleep(2);assert(jobs_seen==i*2);
  assert(cellSpursJobGuardNotify(guard)==CELL_OK);wait_jobs(i*2+1);
  // Credits arriving between laps survive the worker's consumption.
  for(int n=0;n<2;++n)assert(cellSpursJobGuardNotify(guard)==CELL_OK);
  wait_jobs(i*2+2);
  assert(cellSpursShutdownJobChain(chain)==CELL_OK);
  assert(cellSpursJoinJobChain(chain)==CELL_OK);
  assert(cellSpursJobGuardNotify(guard)!=CELL_OK);
 }
 // More than 32 guards can be live on a chain at the same time.
 assert(cellSpursCreateJobChain(0x100,0x7000,commands,128,1,0,1,1)==CELL_OK);
 for(int i=0;i<128;++i)
  assert(cellSpursJobGuardInitialize(0x7000,0x40000+i*128,1,1,1)==CELL_OK);
 for(int i=0;i<128;++i)assert(cellSpursJobGuardNotify(0x40000+i*128)==CELL_OK);
 assert(cellSpursShutdownJobChain(0x7000)==CELL_OK);
 assert(cellSpursJoinJobChain(0x7000)==CELL_OK);
 for(int i=0;i<128;++i)assert(cellSpursJobGuardNotify(0x40000+i*128)!=CELL_OK);
 // An absent guard must never release an unprepared job.
 vm_write64(commands,0x9000|15);vm_write64(commands+8,job);vm_write64(commands+16,127);
 assert(cellSpursCreateJobChain(0x100,0x8000,commands,128,1,0,1,1)==CELL_OK);
 assert(cellSpursRunJobChain(0x8000)==CELL_OK);
 assert(cellSpursJoinJobChain(0x8000)==CELL_OK);assert(jobs_seen==200);
 free(vm_base);puts("SPURS guard lifetime and wait tests passed");return 0;
}
/* Guard tests use PPU memory with no running SPU reservations. Other SPURS
 * subsystems are outside this test and must never be invoked. */
uint32_t g_ww_lo, g_ww_hi;
int g_resv_store_active;
void ps3_ww_report_inline(uint32_t a,uint64_t v,int w) { abort(); }
int spu_coh_is_reserved(uint32_t a) { return 0; }
void spu_lockline_lock(void) { abort(); }
void spu_lockline_unlock(void) { abort(); }
void spu_coh_notify_write(uint32_t a) { abort(); }
void ppu_resv_break_store(uint64_t a) { abort(); }
void (*g_spu_line_commit_hook)(uint32_t);
volatile long g_spurs_pm_polls;
#define UNEXPECTED(name) void name(void) { fprintf(stderr,"Unexpected dependency: " #name "\n"); abort(); }
UNEXPECTED(spu_taskset_signal_task)
UNEXPECTED(spu_ef_bits_post)
UNEXPECTED(spu_taskset_signal_parked_obj)
UNEXPECTED(spu_taskset_latch_wake)
UNEXPECTED(sys_event_queue_cancel_by_id)
UNEXPECTED(sys_event_queue_uncancel_by_id)
UNEXPECTED(spurs_taskset_init)
UNEXPECTED(spurs_taskset_add_task)
UNEXPECTED(spu_elf_image_size)
UNEXPECTED(spu_workload_dispatch_async)
UNEXPECTED(spu_workload_fingerprint)
UNEXPECTED(spu_workload_find_img)
UNEXPECTED(spu_run_policy_module)
