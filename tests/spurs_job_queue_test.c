/* Queue/port ABI regression: descriptors must execute, own their storage and
 * preserve dispatch/staging failure. No game code or data is used by this test. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cellSpursJq.h"
#include "ppu_memory.h"
uint8_t* vm_base;
static int dispatch_result=1;
static u32 calls, addresses[16], sizes[16], values[16];
static const u32 source=0x7000,binary=0x8000,output=0x9000,owned=0x4000;
int spu_workload_dispatch_job(const uint8_t* image,u32 size,u32 job,u32 descriptor_size)
{
    assert(image==vm_base+binary && size==0x100);
    assert(job!=source && job>=owned && job<owned+512);
    assert(descriptor_size==256);
    assert(vm_read64(job)==(binary|1u)); /* cache-inhibit flag preserved */
    assert(vm_read32(job+0x30)==output);
    addresses[calls]=job; sizes[calls]=descriptor_size;
    values[calls]=vm_read32(job+0x34); ++calls;
    /* An owned descriptor survives its caller reusing the source buffer. */
    memset(vm_base+source,0xA5,256);
    assert(vm_read32(job+0x30)==output);
    if (dispatch_result>0) vm_write32(output,values[calls-1]);
    return dispatch_result;
}
static void descriptor(u32 value)
{
    memset(vm_base+source,0,256);
    vm_write64(source,binary|1u); vm_write16(source+8,0x10);
    vm_write32(source+0x30,output); vm_write32(source+0x34,value);
}
static void create(u32 queue,u32 port,u32 attr,u32 max_descriptor)
{
    assert(cellSpursJobQueueAttributeInitialize((void*)(uintptr_t)attr)==CELL_OK);
    assert(cellSpursJobQueueAttributeSetMaxSizeJobDescriptor(attr,max_descriptor)==CELL_OK);
    assert(cellSpursJobQueueAttributeSetGrabParameters(attr,255,8)==CELL_OK);
    assert(cellSpursJobQueueAttributeSetDoBusyWaiting(attr,0)==CELL_OK);
    assert(cellSpursJobQueueAttributeSetSubmitWithEntryLock(attr,0)==CELL_OK);
    assert(cellSpursJobQueueAttributeSetIsHaltOnError(attr,1)==CELL_OK);
    assert(cellSpursJobQueueAttributeSetIsJobTypeMemoryCheck(attr,0)==CELL_OK);
    assert(_cellSpursCreateJobQueue(2,0x310000,0x1000,queue,attr,0,0xA000,32)==CELL_OK);
    assert(cellSpursJobQueuePortInitializeWithDescriptorBuffer(port,queue,owned,max_descriptor,2,1)==CELL_OK);
}
static void retire(u32 queue,u32 port,s32 expected)
{
    assert(cellSpursJobQueuePortSync(port)==expected);
    vm_write32(0xB008,0xC0FFEE);
    assert(cellSpursJobQueueGetError(queue,0xB000,0xB004)==CELL_OK);
    assert(vm_read32(0xB008)==0xC0FFEE); /* cause is a guest pointer, four bytes */
    assert((s32)vm_read32(0xB000)==expected);
    if (expected) assert(vm_read32(0xB004)>=owned && vm_read32(0xB004)<owned+512);
    assert(cellSpursJobQueuePortFinalize(port)==expected);
    assert(cellSpursShutdownJobQueue(queue)==CELL_OK);
    assert(cellSpursJoinJobQueue(queue,0xB008)==CELL_OK);
    assert((s32)vm_read32(0xB008)==expected);
}
int main(void)
{
    vm_base=calloc(1,0x10000); assert(vm_base);
    const u32 queue=0x2000,port=0x3000,attr=0xC008;
    /* Zero is the registry's vacant-slot marker, never an initialized object. */
    assert(cellSpursJobQueueAttributeSetMaxSizeJobDescriptor(0,256)!=CELL_OK);
    assert(cellSpursJobQueueAttributeSetGrabParameters(0,255,8)!=CELL_OK);
    assert(cellSpursJobQueueAttributeSetIsHaltOnError(0,1)!=CELL_OK);
    assert(_cellSpursCreateJobQueue(2,0x310000,0x1000,queue,0,0,0xA000,32)!=CELL_OK);
    assert(_cellSpursCreateJobQueue(2,0x310000,0x1000,queue,attr,0,0xA000,32)!=CELL_OK);
    create(queue,port,attr,256);
    assert(_cellSpursCreateJobQueue(2,0x310000,0x1000,queue,attr,0,0xA000,32)!=CELL_OK);
    assert(cellSpursJobQueuePortInitializeWithDescriptorBuffer(port,queue,owned,256,2,1)!=CELL_OK);
    memset(vm_base+1,0xCD,8);
    for(u32 i=0;i<3;++i) {
        descriptor(0x12340000+i);
        assert(_cellSpursJobQueuePortCopyPushBody(port,source,256,1,1)==CELL_OK);
        assert(calls==i+1 && vm_read32(output)==0x12340000+i);
        assert(addresses[i]==owned+(i%2)*256 && sizes[i]==256);
        assert(cellSpursJobQueuePortSync(port)==CELL_OK);
    }
    for(u32 i=1;i<9;++i) assert(vm_base[i]==0xCD); /* r7 is not an output pointer */
    assert(_cellSpursJobQueuePortCopyPushBody(port,0xFFFFFF80u,256,1,1)!=CELL_OK);
    assert(_cellSpursJobQueuePortCopyPushBody(port,source,256,2,1)!=CELL_OK);
    assert(_cellSpursJobQueuePortCopyPushBody(port,source+1,256,1,1)!=CELL_OK);
    assert(calls==3);
    retire(queue,port,CELL_OK);
    /* A registered-image miss and a staging error both stay errors. */
    for(int rc=0;rc>=-1;--rc) {
        create(queue,port,attr,256); descriptor(0xDEADBEEF); dispatch_result=rc;
        u32 before=calls,previous_output=vm_read32(output);
        assert(_cellSpursJobQueuePortCopyPushBody(port,source,256,1,1)!=CELL_OK);
        assert(calls==before+1 && vm_read32(output)==previous_output);
        descriptor(0xBAD);
        assert(_cellSpursJobQueuePortCopyPushBody(port,source,256,1,1)!=CELL_OK);
        assert(calls==before+1); /* halt-on-error never submits the next job */
        retire(queue,port,(s32)CELL_SPURS_JOB_ERROR_ABORT);
    }
    /* Validate the complete binary range before dispatch, without truncating EA. */
    const u64 bad_binary[]={UINT64_C(0xFFFFFF80),UINT64_C(0x100008000),UINT64_C(0x8002)};
    for(u32 i=0;i<3;++i) {
        create(queue,port,attr,256); descriptor(1); vm_write64(source,bad_binary[i]);
        u32 before=calls;
        assert(_cellSpursJobQueuePortCopyPushBody(port,source,256,1,1)!=CELL_OK);
        assert(calls==before);
        retire(queue,port,(s32)CELL_SPURS_JOB_ERROR_ABORT);
    }
    create(queue,port,attr,128); descriptor(1);
    assert(_cellSpursJobQueuePortCopyPushBody(port,source,256,1,1)!=CELL_OK);
    retire(queue,port,CELL_OK);
    assert(cellSpursJobQueueAttributeSetMaxSizeJobDescriptor(attr,96)!=CELL_OK);
    assert(cellSpursJobQueueAttributeSetIsHaltOnError(attr,2)!=CELL_OK);
    assert(cellSpursJobQueueAttributeInitialize(NULL)!=CELL_OK);
    assert(_cellSpursCreateJobQueue(2,0x310000,0x1000,queue+1,attr,0,0xA000,32)!=CELL_OK);
    assert(_cellSpursCreateJobQueue(2,0x310000,0x1000,queue,attr,0,UINT64_C(0xFFFFFF80),32)!=CELL_OK);
    assert(cellSpursJobQueuePortSync(port)!=CELL_OK);
    assert(_cellSpursJobQueuePortCopyPushBody(port,source,256,1,1)==(s32)CELL_SPURS_JOB_ERROR_STAT);
    free(vm_base); puts("SPURS queue owned dispatch, ordering and failure tests passed"); return 0;
}
uint32_t g_ww_lo,g_ww_hi;
int g_resv_store_active;
void ps3_ww_report_inline(uint32_t a,uint64_t v,int w) { (void)a;(void)v;(void)w; abort(); }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_lockline_lock(void) { abort(); }
void spu_lockline_unlock(void) { abort(); }
void spu_coh_notify_write(uint32_t a) { (void)a; abort(); }
void ppu_resv_break_store(uint64_t a) { (void)a; abort(); }
