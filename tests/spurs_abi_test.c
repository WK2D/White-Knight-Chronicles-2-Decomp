#include "spu_context.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint8_t* vm_base;
void abi_fixture_spu_func_00000000(spu_context*);
void abi_fixture_spu_func_00000040(spu_context*);
int spu_run_spurs_job(void (*)(spu_context*),int,uint32_t,uint32_t);
static uint32_t queries;
static unsigned observed_before,observed_after;
extern void (*g_spu_job_trace_hook)(const spu_context*,uint32_t,uint32_t,int);
static void observe_job(const spu_context* ctx,uint32_t job,uint32_t size,int after) {
 assert(job==0x1000 && size==256 && ctx->image_id==9);
 assert(ctx->spurs_job_mode==1);
 if(after) { assert(queries==1 && ctx->gpr[3]._u32[0]==0);++observed_after; }
 else { assert(queries==0 && ctx->gpr[3]._u32[0]!=0);++observed_before; }
}

static void put32(uint8_t* p,uint32_t value) {
 p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);
 p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;
}
static void job_entry(spu_context* ctx) {
 assert(ctx->spurs_job_mode==1);
 assert(ctx->spurs_job_spu_num==0);
 uint8_t saved[512];memcpy(saved,ctx->ls,sizeof saved);
 abi_fixture_spu_func_00000000(ctx);
 assert(ctx->gpr[3]._u32[0]==0);
 assert(memcmp(saved,ctx->ls,sizeof saved)==0);
 ++queries;
}
int main(void) {
 vm_base=calloc(1,0x10000);assert(vm_base);
 put32(vm_base+0x2000,0x30803803);
 put32(vm_base+0x2004,0x3F820183);
 put32(vm_base+0x2008,0x35000000);
 // Job instructions/data deliberately occupy the real kernel's ID address.
 put32(vm_base+0x2000+0x1C8,0xDEADBEEF);
 put32(vm_base+0x1004,0x2000);  // BE64 binary EA, high word zero.
 put32(vm_base+0x1008,0x00200000); // Binary length = 32 quadwords.
 g_spu_job_trace_hook=observe_job;
 assert(spu_run_spurs_job(job_entry,9,0x1000,256)==0);assert(queries==1);
 assert(observed_before==1 && observed_after==1);
 g_spu_job_trace_hook=NULL;
 assert(*(vm_base+0x21C8)==0xDE);

 spu_context* ctx=malloc(sizeof *ctx);assert(ctx);
 spu_context_init(ctx,0);ctx->host_depth=1;
 put32(ctx->ls+0x1C0,0x11111111);put32(ctx->ls+0x1C4,0x22222222);
 put32(ctx->ls+0x1C8,2);put32(ctx->ls+0x1CC,0x33333333);
 // Task/raw contexts retain the original LS query semantics.
 abi_fixture_spu_func_00000000(ctx);assert(ctx->gpr[3]._u32[0]==2);
 // A native job's binding reads its actual handoff, independent of LS bytes.
 ctx->spurs_job_mode=1;ctx->spurs_job_spu_num=1;
 abi_fixture_spu_func_00000000(ctx);assert(ctx->gpr[3]._u32[0]==1);
 assert(spu_ls_read32(ctx,0x1C8)==2);
 // A similar routine rotating by four bytes is not misidentified as GetSpuId.
 abi_fixture_spu_func_00000040(ctx);assert(ctx->gpr[3]._u32[0]==0x22222222);
 free(ctx);free(vm_base);puts("SPURS kernel-ID ABI and code preservation passed");
 return 0;
}

// Harness boundaries: run the generated instructions inside the real job
// staging code; unrelated scheduling/coherency mechanisms are not under test.
int spu_run_with_halt(void (*entry)(spu_context*),spu_context* ctx) {
 ctx->host_depth=1;entry(ctx);return 0;
}
void spu_coh_unregister(const void* ctx) { (void)ctx; }
int g_spu_ls_watch_n;
void spu_ls_watch_slow(uint32_t a,int w,const uint8_t* p,uint32_t pc,uint32_t lr) { abort(); }
void spu_indirect_branch(spu_context* c) { abort(); }
void spu_register_function(uint32_t a,void (*fn)(spu_context*)) { (void)a;(void)fn; }
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
