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
static uint32_t job_addresses[512], descriptor_sizes[512];
int spu_workload_dispatch_job(const uint8_t* image, uint32_t size,uint32_t ea,uint32_t desc) {
    (void)image;(void)size;
    LONG index=InterlockedCompareExchange(&jobs_seen,0,0);
    assert(index>=0 && index<512);
    job_addresses[index]=ea;descriptor_sizes[index]=desc;
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
 // JOBLIST descriptors are published before releasing the guard. Each list
 // carries its own descriptor size; preserve array order and following jobs.
 const u32 list=0x5000,first=0x5100,chain=0xB000,guard=0xA000;
 for(u32 stride=128;stride<=256;stride*=2) {
  LONG initial=jobs_seen;
  vm_write64(commands,guard|15);vm_write64(commands+8,list|6);
  vm_write64(commands+16,job);vm_write64(commands+24,127);
  vm_write64(list,0);vm_write64(list+8,0);
  assert(cellSpursCreateJobChain(0x100,chain,commands,256,1,0,1,1)==CELL_OK);
  assert(cellSpursJobGuardInitialize(chain,guard,1,1,1)==CELL_OK);
  assert(cellSpursRunJobChain(chain)==CELL_OK);
  Sleep(2);assert(jobs_seen==initial);
  for(u32 i=0;i<3;++i) {
   vm_write64(first+i*stride,binary);vm_write32(first+i*stride+8,0x00100000);
  }
  vm_write32(list,3);vm_write32(list+4,stride);vm_write64(list+8,first);
  assert(cellSpursJobGuardNotify(guard)==CELL_OK);
  assert(cellSpursJoinJobChain(chain)==CELL_OK);assert(jobs_seen==initial+4);
  for(u32 i=0;i<3;++i) {
   assert(job_addresses[initial+i]==first+i*stride);
   assert(descriptor_sizes[initial+i]==stride);
  }
  assert(job_addresses[initial+3]==job && descriptor_sizes[initial+3]==256);
 }
 // An all-zero empty list is valid and still reaches its following job.
 vm_write64(list,0);vm_write64(list+8,0);
 vm_write64(commands,list|6);vm_write64(commands+8,job);vm_write64(commands+16,127);
 LONG initial=jobs_seen;
 assert(cellSpursCreateJobChain(0x100,chain,commands,256,1,0,1,1)==CELL_OK);
 assert(cellSpursRunJobChain(chain)==CELL_OK);
 assert(cellSpursJoinJobChain(chain)==CELL_OK);assert(jobs_seen==initial+1);
 // Invalid size or overflowing array must stop before a later job executes.
 const u32 bad_strides[]={0,48,512};
 for(u32 i=0;i<4;++i) {
  initial=jobs_seen;
  vm_write32(list,3);vm_write32(list+4,i<3?bad_strides[i]:256);
  vm_write64(list+8,i<3?first:UINT64_C(0xFFFFFF00));
  assert(cellSpursCreateJobChain(0x100,chain,commands,256,1,0,1,1)==CELL_OK);
  assert(cellSpursRunJobChain(chain)==CELL_OK);
  assert(cellSpursJoinJobChain(chain)==CELL_OK);assert(jobs_seen==initial);
 }
 const u64 bad_lists[]={6,UINT64_C(0x100000006),UINT64_C(0xFFFFFFFFFFFFFFFE)};
 for(u32 i=0;i<3;++i) {
  initial=jobs_seen;vm_write64(commands,bad_lists[i]);
  assert(cellSpursCreateJobChain(0x100,chain,commands,256,1,0,1,1)==CELL_OK);
  assert(cellSpursRunJobChain(chain)==CELL_OK);
  assert(cellSpursJoinJobChain(chain)==CELL_OK);assert(jobs_seen==initial);
 }
 free(vm_base);puts("SPURS guards and ordered job-list dispatch tests passed");return 0;
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
