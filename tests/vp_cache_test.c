#include "rsx_vp_cache.h"
#include "rsx_vp_decompiler.h"
#include <assert.h>
#include <stdio.h>

static unsigned released[128];
static int blobs[128];
static void release_blob(void* p) {
    ptrdiff_t i=(int*)p-blobs;assert(i>=0 && i<128);
    assert(released[i]++==0);
}
int main(void) {
    rsx_vp_cache cache={0};
    uint8_t code[32]={0};code[12]=1; // END on the first instruction.
    uint32_t bytes=rsx_vp_program_size_instrs(code,sizeof code)*16;
    assert(bytes==16);
    int saved[128];
    for (int i=0;i<128;++i) {
        code[0]=(uint8_t)i;blobs[i]=i;
        // Deliberately collide every hash: bytecode must still distinguish them.
        assert(rsx_vp_cache_find(&cache,7,code,bytes)==-1);
        saved[i]=rsx_vp_cache_add(&cache,7,code,bytes,&blobs[i],i&1);
        assert(saved[i]==i);
    }
    // Queued draws retain all old IDs after many reallocations, including 16+.
    for (int i=0;i<128;++i) {
        code[0]=(uint8_t)i;code[16]=(uint8_t)(i+1);
        bytes=rsx_vp_program_size_instrs(code,sizeof code)*16;
        assert(bytes==16); // A later resident program is outside this identity.
        assert(rsx_vp_cache_find(&cache,7,code,bytes)==saved[i]);
        const rsx_vp_cache_entry* e=&cache.entries[saved[i]];
        assert(e->vs==&blobs[i] && e->uses_c03==(i&1));
        assert(e->ucode[0]==i && e->bytes==16);
    }
    code[12]=0;assert(rsx_vp_program_size_instrs(code,16)==0);
    int count=cache.count;
    assert(rsx_vp_cache_add(&cache,7,code,0,&blobs[0],0)==-1);
    assert(cache.count==count);
    rsx_vp_cache_clear(&cache,release_blob);
    assert(!cache.entries && !cache.count && !cache.capacity);
    rsx_vp_cache_clear(&cache,release_blob);
    for (int i=0;i<128;++i) assert(released[i]==1);
    puts("Vertex-program identities, queued IDs and release lifetime passed");
    return 0;
}
