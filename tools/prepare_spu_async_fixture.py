"""Build a synthetic fixture from the current detached-worker runtime.

The production launch, owner claims, task scheduling, completion and stack
cleanup run unchanged. Only host thread creation, lifted SPU execution, image
lookup and guest storage are fixture boundaries; no game input is needed.
"""
import argparse
from pathlib import Path

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("sdk", type=Path)
ap.add_argument("output", type=Path)
a = ap.parse_args()
s = (a.sdk / "runtime/spu/spu_workload.c").read_text(encoding="utf-8")

def section(start, end):
    begin = s.index(start)
    return s[begin:s.index(end, begin)]

struct_begin = s.index("typedef struct {", s.index("/* Async dispatch:"))
struct_end = s.index("} spu_async_job;", struct_begin) + len("} spu_async_job;")
taskset = section("static volatile LONG s_ts_lock;", "static void spu_async_run(")
completion = section("static void spu_async_finished(", "int spu_workload_dispatch_job(")
dispatch = section("static int spu_dispatch_async_owned(", "/* Unowned async services")
wrappers = s[s.index("int spu_workload_dispatch_async(", s.index("/* Unowned async services")):]
preamble = r'''/* Generated from production source; not a copied implementation. */
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "spu_async_lifetime.h"
#include "spu_coherency.h"
typedef void (*spu_lifted_entry_fn)(void*);
uint8_t* vm_base;
/* Native atomics are linked from the real coherence TU too. This fixture
 * supplies their external notification boundary; no native atomic executes
 * in the detached-worker ownership cases. */
void (*g_spu_line_commit_hook)(uint32_t line);
void spu_taskset_line_committed(uint32_t line) {(void)line;}
static void fixture_entry(void* p) { (void)p; abort(); }
static struct { uint64_t fp; spu_lifted_entry_fn fn; int image_id; } s_registry[]={{1,fixture_entry,1}};
static unsigned s_registry_count=1;
static uint64_t spu_workload_fingerprint(const uint8_t* p,uint32_t n) { (void)p;(void)n;return 1; }
static size_t spu_elf_image_size(const uint8_t* p,size_t n) { (void)n;return *p==0x7f ? 16 : 0; }
static atomic_ullong g_spu_async_active, g_spu_async_peak, g_spu_async_started;
static atomic_ullong g_spu_async_completed, g_spu_async_coalesced, g_spu_async_rejected;
static atomic_ullong g_spu_async_task_started, g_spu_async_workload_started, g_spu_async_unowned_started;
int spu_workload_dispatch_task_async(const uint8_t*,uint32_t,uint32_t,uint32_t,uint32_t);
'''
mocks = r'''
static spu_async_job* fixture_jobs[256];
static unsigned fixture_launches,fixture_jobs_count,fixture_runs,fixture_stack_cleanups;
static int fixture_fail_create;
static spu_async_job* fixture_running;
static DWORD WINAPI spu_async_thread(LPVOID);
static HANDLE fixture_create_thread(LPSECURITY_ATTRIBUTES security,SIZE_T stack,
 LPTHREAD_START_ROUTINE entry,LPVOID arg,DWORD flags,LPDWORD id) {
 (void)security;(void)id;
 assert(entry==spu_async_thread && stack==(256u<<20));
 assert(flags==STACK_SIZE_PARAM_IS_A_RESERVATION);
 spu_async_job* j=(spu_async_job*)arg;
 /* A worker may start before CreateThread returns. Its owner must exist now. */
 if(j->taskset_ea) assert(async_owner_active(SPU_ASYNC_TASK,j->taskset_ea,0,j->taskid));
 if(j->lifetime.owner) assert(j->lifetime.owner->slots[j->lifetime.slot]==j->lifetime.serial);
 assert(g_spu_async_active>0);
 if(fixture_fail_create) return NULL;
 assert(fixture_jobs_count<256);
 fixture_jobs[fixture_jobs_count++]=j;
 ++fixture_launches;return (HANDLE)(uintptr_t)1;
}
static void fixture_close_handle(HANDLE h) { assert(h==(HANDLE)(uintptr_t)1); }
static void spu_async_run(spu_async_job* j) {
 assert(fixture_running==j);
 if(j->lifetime.owner) assert(j->lifetime.owner->slots[j->lifetime.slot]==j->lifetime.serial);
 ++fixture_runs;
}
static void fixture_forget_range(uintptr_t lo,uintptr_t hi) {
 assert(lo<hi && fixture_running);
 if(fixture_running->lifetime.owner) {
  spu_async_ticket* t=&fixture_running->lifetime;
  assert(t->owner->slots[t->slot]==t->serial);
 }
 ++fixture_stack_cleanups;
 spu_coh_forget_range(lo,hi);
}
#define spu_coh_forget_range fixture_forget_range
#define CreateThread fixture_create_thread
#define CloseHandle fixture_close_handle
'''
ending = r'''
#undef CreateThread
#undef CloseHandle
#undef spu_coh_forget_range
static void fixture_finish(unsigned i) {
 assert(i<fixture_jobs_count && !fixture_running);
 spu_async_job* j=fixture_jobs[i];
 memmove(fixture_jobs+i,fixture_jobs+i+1,(fixture_jobs_count-i-1)*sizeof(*fixture_jobs));
 --fixture_jobs_count;
 fixture_running=j;
 assert(spu_async_thread(j)==0);
 fixture_running=NULL;
}
static void fixture_retire_all(void) {
 while(fixture_jobs_count) fixture_finish(0);
}
static void fixture_be32(uint32_t ea,uint32_t v) {
 vm_base[ea]=(uint8_t)(v>>24);vm_base[ea+1]=(uint8_t)(v>>16);
 vm_base[ea+2]=(uint8_t)(v>>8);vm_base[ea+3]=(uint8_t)v;
}
static void fixture_task(uint32_t ts,uint32_t task,uint32_t image,uint32_t args) {
 uint8_t mask=(uint8_t)(0x80u>>(task&7));
 vm_base[ts+0x10+task/8]|=mask;
 vm_base[ts+0x20+task/8]|=mask;
 vm_base[ts+0x30+task/8]|=mask;
 fixture_be32(ts+0x80+0x30*task+0x14,image);
 fixture_be32(ts+0x80+0x30*task+0x1c,args);
}
static void actual_dispatch_cases(void) {
 const uint32_t ts=0x90000,other=0xA0000,args=0x30000,elf=0x10000;
 vm_base=(uint8_t*)calloc(1,2u<<20);assert(vm_base);vm_base[elf]=0x7f;
 const uint8_t* image=vm_base+elf;
 assert(spu_workload_taskset_begin(ts));
 fixture_be32(args+0x10,0x00401234);fixture_be32(args+0x14,0x23456789);
 fixture_be32(args+0x18,0x3456789a);fixture_be32(args+0x1c,0x456789ab);
 fixture_task(ts,5,elf,args);
 assert(spu_workload_dispatch_task_async(image,16,args,ts,5)==1);
 assert(fixture_jobs[0]->have_r3 && fixture_jobs[0]->r3[0]==0x00401234 && fixture_jobs[0]->r3[3]==0x456789ab);
 memset(vm_base+args+0x10,0,16);
 assert(fixture_jobs[0]->r3[0]==0x00401234); /* caller's descriptor can change */
 for(unsigned i=0;i<10000;i++)
  assert(spu_workload_dispatch_task_async(image,16,0,ts,5)==1);
 assert(fixture_launches==1 && g_spu_async_active==1 && g_spu_async_coalesced==10000);
 /* Distinct task ids, tasksets and workload ids remain independent. */
 fixture_task(ts,6,elf,0);fixture_task(other,5,elf,0);
 assert(spu_workload_dispatch_task_async(image,16,0,ts,6)==1);
 assert(spu_workload_dispatch_task_async(image,16,0,other,5)==1);
 assert(spu_workload_dispatch_workload_async(image,16,0,ts,5)==1);
 assert(spu_workload_dispatch_workload_async(image,16,0,ts,6)==1);
 assert(fixture_launches==5 && g_spu_async_active==5);
 /* Completing one task runs the actual scheduler and cannot duplicate its
  * still-active sibling, even though that sibling remains guest-ready. */
 fixture_finish(0);assert(fixture_launches==5 && fixture_jobs_count==4);
 assert(!(vm_base[ts+0x30]&(0x80u>>5)));
 assert(vm_base[ts+0x30]&(0x80u>>6));
 fixture_retire_all();
 assert(g_spu_async_active==0 && g_spu_async_completed==5);
 /* Actual task-exit scheduling must discover an SPU-created ready sibling. */
 fixture_task(ts,7,elf,0);fixture_task(ts,8,elf,0);
 assert(spu_workload_dispatch_task_async(image,16,0,ts,7)==1);
 fixture_finish(0);
 assert(fixture_jobs_count==1 && fixture_jobs[0]->taskid==8);
 assert(!(vm_base[ts+0x20+8/8]&(0x80u>>(8&7))));
 fixture_retire_all();
 /* SPU-published task descriptors carry low ELF flag bits. A scan must mask
  * them before sizing/fingerprinting the image, as the PM builder does. */
 for(unsigned flags=1;flags<8;flags++) {
  fixture_task(ts,7,elf,0);fixture_task(ts,8,elf|flags,0);
  assert(spu_workload_dispatch_task_async(image,16,0,ts,7)==1);
  fixture_finish(0);
  assert(fixture_jobs_count==1 && fixture_jobs[0]->taskid==8);
  assert(fixture_jobs[0]->image==image);
  fixture_retire_all();
 }
 /* Same guest slot and whole taskset address can be reused after all cleanup. */
 for(unsigned i=0;i<100;i++) {
  assert(spu_workload_taskset_close(ts));assert(spu_workload_taskset_begin(ts));
  fixture_task(ts,5,elf,0);
  assert(spu_workload_dispatch_task_async(image,16,0,ts,5)==1);
  fixture_retire_all();assert(g_spu_async_active==0);
 }
 /* A stale completion must not clear a newly occupied guest enabled bit. */
 spu_async_job stale={0};stale.taskset_ea=ts;stale.taskid=5;
 assert(async_owner_claim(SPU_ASYNC_TASK,ts,0,5,&stale.lifetime)==1);
 spu_async_ticket old=stale.lifetime;assert(async_owner_finish(&stale.lifetime));
 fixture_task(ts,5,elf,0);
 assert(spu_workload_dispatch_task_async(image,16,0,ts,5)==1);
 stale.lifetime=old;spu_taskset_task_exited(&stale);
 assert(vm_base[ts+0x30]&(0x80u>>5));
 assert(async_owner_active(SPU_ASYNC_TASK,ts,0,5));fixture_retire_all();
 /* Failed host launch relinquishes the reserved key and can retry. */
 fixture_fail_create=1;
 assert(spu_workload_dispatch_task_async(image,16,0,ts,5)==0);
 assert(!async_owner_active(SPU_ASYNC_TASK,ts,0,5));
 assert(g_spu_async_active==0 && g_spu_async_rejected==1);
 fixture_fail_create=0;
 assert(spu_workload_dispatch_task_async(image,16,0,ts,5)==1);fixture_retire_all();
 /* Generic/workload services never inherit the last-created task owner. */
 fixture_task(ts,5,elf,0);
 assert(spu_workload_dispatch_task_async(image,16,0,ts,5)==1);
 assert(spu_workload_dispatch_async(image,16,0)==1);
 assert(!fixture_jobs[1]->taskset_ea && !fixture_jobs[1]->lifetime.owner);
 fixture_finish(1);
 assert(vm_base[ts+0x30]&(0x80u>>5));assert(async_owner_active(SPU_ASYNC_TASK,ts,0,5));
 fixture_retire_all();
 assert(g_spu_async_active==0 && g_spu_async_started==g_spu_async_completed);
 assert(fixture_runs==g_spu_async_completed && fixture_stack_cleanups==fixture_runs);
 free(vm_base);vm_base=NULL;
}
'''
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(preamble+s[struct_begin:struct_end]+mocks+taskset+completion+dispatch+wrappers+ending, encoding="utf-8")
