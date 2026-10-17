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
static uint32_t expected_descriptor_size;
extern void (*g_spu_job_trace_hook)(const spu_context*,uint32_t,uint32_t,int);
static void observe_job(const spu_context* ctx,uint32_t job,uint32_t size,int after) {
 assert(job==0x1000 && size==expected_descriptor_size && ctx->image_id==9);
 assert(ctx->spurs_job_mode==1);
 if(after) { assert(queries==observed_before && ctx->gpr[3]._u32[0]==0);++observed_after; }
 else { assert(queries==observed_before && ctx->gpr[3]._u32[0]!=0);++observed_before; }
}

static void put32(uint8_t* p,uint32_t value) {
 p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);
 p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;
}
static void job_entry(spu_context* ctx) {
 assert(ctx->spurs_job_mode==1);
 assert(ctx->spurs_job_spu_num==0);
 // The queue CRT requires a manager pointer in the descriptor-size word.
 const uint32_t context=ctx->gpr[3]._u32[0];
 const uint32_t descriptor=ctx->gpr[4]._u32[0];
 const uint32_t packed=spu_ls_read32(ctx,context+0x14);
 const uint32_t manager=packed&0x0FFFFFFFu;
 assert((packed>>28)==(expected_descriptor_size==64 ? 0u : expected_descriptor_size/128));
 assert(manager!=0 && !(manager&15u));
 assert(manager>=context+0x30 && manager+16<=descriptor && manager+16<=SPU_LS_SIZE);
 // This remains defined after an earlier job dirties its reused control block.
 for(uint32_t i=0;i<16;++i) assert(ctx->ls[manager+i]==0);
 assert(spu_ls_read32(ctx,manager+4)==0);
 uint8_t saved[512];memcpy(saved,ctx->ls,sizeof saved);
 abi_fixture_spu_func_00000000(ctx);
 assert(ctx->gpr[3]._u32[0]==0);
 assert(memcmp(saved,ctx->ls,sizeof saved)==0);
 memset(ctx->ls+manager,0xA5,16);
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
 const uint32_t descriptor_sizes[]={256,64,128,1024};
 for(uint32_t i=0;i<sizeof descriptor_sizes/sizeof *descriptor_sizes;++i) {
  expected_descriptor_size=descriptor_sizes[i];
  assert(spu_run_spurs_job(job_entry,9,0x1000,expected_descriptor_size)==0);
  assert(queries==i+1 && observed_before==i+1 && observed_after==i+1);
 }
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
 free(ctx);free(vm_base);puts("SPURS job manager context, kernel-ID ABI and code preservation passed");
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
