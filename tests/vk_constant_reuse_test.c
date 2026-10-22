/* Production constant helpers against delayed synthetic GPU upload arenas. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_constant_reuse.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t VkDeviceSize;
typedef struct { uint64_t buf, size; void* ptr; } vk_buffer;
static struct { int on; VkDeviceSize ps_off; vk_buffer ubo; } s_g;
static struct { u32 vs_bytes, ps_bytes, warned; } s_e2;
static struct {
    vk_buffer ubo;
    VkDeviceSize vpos, ipos, upos, spos, ubo_align;
    uint64_t pool, split_pool;
    u32 nsets, nsplit;
    u8 *vsc, *psc;
} s_eb;
static struct { uint64_t device; } s_vk;
static rsx_constant_reuse s_constant_reuse;
static int s_constant_reuse_on = 1, allocation_failure;
static unsigned pool_resets;
static void* constant_malloc(size_t bytes) { return allocation_failure ? NULL : malloc(bytes); }
static void pvkResetDescriptorPool(uint64_t device, uint64_t pool, unsigned flags)
{ assert(device == 1 && pool && !flags); ++pool_resets; }
#define VK_LOG(...) ((void)0)
#define malloc constant_malloc
/* The production batch reset also drops the descriptor-set cache. */
static struct { int valid; } s_desc_last;
#include "vk-constant-fixture.inc"
#undef malloc

static u8 arenas[2][4096], submitted[2][4096];
static int pending[2];
static unsigned slot;
static unsigned random_state = 0xc0e1903u;
static unsigned next(void)
{ random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }

static void activate(unsigned target)
{
    assert(!pending[target]);
    slot = target;
    s_eb.ubo.buf = target + 1;
    s_eb.ubo.ptr = arenas[target];
    s_eb.ubo.size = sizeof arenas[target];
    vk_eng_batch_reset();
}
static void submit_and_switch(void)
{
    memcpy(submitted[slot], arenas[slot], sizeof arenas[slot]);
    pending[slot] = 1;
    unsigned target = slot ^ 1;
    if (pending[target]) {
        assert(!memcmp(submitted[target], arenas[target], sizeof arenas[target]));
        pending[target] = 0; /* The mock GPU completes before retirement. */
    }
    activate(target);
}
static uint64_t draw_stage(unsigned stage, const void* expected, unsigned bytes)
{
    uint64_t result = vk_eng_constant_take(stage, expected, bytes);
    assert(result % s_eb.ubo_align == 0);
    assert(result + bytes <= s_eb.upos && s_eb.upos <= s_eb.ubo.size);
    if (expected) assert(!memcmp(arenas[slot] + result, expected, bytes));
    unsigned other = slot ^ 1;
    if (pending[other]) assert(!memcmp(arenas[other], submitted[other], sizeof arenas[other]));
    return result;
}
int main(void)
{
    u8 vs[512], ps[256];
    memset(vs, 0x12, sizeof vs); memset(ps, 0x73, sizeof ps);
    s_g.on = 1; s_g.ps_off = 512; s_g.ubo.size = 1024;
    s_vk.device = 1; s_eb.ubo_align = 256;
    s_eb.pool = 5; s_eb.split_pool = 6;
    activate(0);
    vk_eng_bind_vs_constants(NULL, vs, 256);
    vk_eng_bind_ps_constants(NULL, ps, 256);
    assert(draw_stage(0, s_eb.vsc, 256) == 0);
    assert(draw_stage(1, s_eb.psc, 256) == 256);
    uint64_t cursor = s_eb.upos;
    vk_eng_bind_vs_constants(NULL, vs, 256);
    vk_eng_bind_ps_constants(NULL, ps, 256);
    assert(draw_stage(0, s_eb.vsc, 256) == 0);
    assert(draw_stage(1, s_eb.psc, 256) == 256 && s_eb.upos == cursor);

    /* Same handle after retirement: a larger VS overwrites the former PS range.
     * Forgetting only the VS stamp would reuse those wrong bytes for the PS. */
    vk_eng_batch_reset();
    vk_eng_bind_vs_constants(NULL, vs, 512);
    vk_eng_bind_ps_constants(NULL, ps, 256);
    assert(draw_stage(0, s_eb.vsc, 512) == 0);
    assert(draw_stage(1, s_eb.psc, 256) == 512);

    /* A later binding never overwrites constants referenced by earlier draws. */
    u8 old[512]; memcpy(old, arenas[slot], sizeof old);
    vs[511] ^= 1;
    vk_eng_bind_vs_constants(NULL, vs, 512);
    assert(draw_stage(0, s_eb.vsc, 512) == 768);
    assert(!memcmp(old, arenas[slot], sizeof old));

    /* Absent, disabled and failed bindings cannot retain reusable identities. */
    vk_eng_bind_vs_constants(NULL, NULL, 512);
    assert(!s_e2.vs_bytes && !s_constant_reuse.stage[0].valid);
    assert(draw_stage(0, NULL, 16) == 1280);
    s_g.on = 0; vk_eng_bind_ps_constants(NULL, ps, 256);
    assert(!s_e2.ps_bytes && !s_constant_reuse.stage[1].valid); s_g.on = 1;
    free(s_eb.vsc); s_eb.vsc = NULL;
    allocation_failure = 1; vk_eng_bind_vs_constants(NULL, vs, 512);
    assert(!s_e2.vs_bytes && !s_constant_reuse.stage[0].valid); allocation_failure = 0;

    unsigned reuse_hits = 0;
    for (unsigned n = 0; n < 10000; n++) {
        unsigned vb = 16u << (next() % 6), pb = 16u << (next() % 5);
        if (!(next() & 3)) vs[next() % vb] ^= (u8)next();
        if (!(next() & 3)) ps[next() % pb] ^= (u8)next();
        s_constant_reuse_on = n % 31 != 0; /* Also exercise off/on transitions. */
        vk_eng_bind_vs_constants(NULL, vs, vb);
        vk_eng_bind_ps_constants(NULL, ps, pb);
        if (s_eb.upos + 1024 > sizeof arenas[slot]) submit_and_switch();
        cursor = s_eb.upos;
        draw_stage(0, s_eb.vsc, vb); draw_stage(1, s_eb.psc, pb);
        reuse_hits += s_eb.upos == cursor;
        /* Repeat with no change: enabled reuse must retain both exact offsets. */
        int flushed = s_eb.upos + 1024 > sizeof arenas[slot];
        if (flushed) submit_and_switch();
        cursor = s_eb.upos;
        draw_stage(0, s_eb.vsc, vb); draw_stage(1, s_eb.psc, pb);
        assert(!s_constant_reuse_on || flushed || s_eb.upos == cursor);
    }
    assert(reuse_hits > 0);
    uint64_t offset;
    assert(!rsx_constant_lookup(&s_constant_reuse, 3, 1, 4096, 4096, 256, 256, &offset));
    assert(!rsx_constant_lookup(&s_constant_reuse, 0, 99, 4096, 4096, 256, 256, &offset));
    assert(!rsx_constant_range(UINT64_MAX - 15, 32, UINT64_MAX, UINT64_MAX, 16));
    assert(!rsx_constant_range(1, 32, 64, 4096, 16));
    assert(!rsx_constant_range(0, 32, 16, 4096, 16));
    assert(!rsx_constant_range(0, 32, 64, 16, 16));
    assert(rsx_constant_update(&s_constant_reuse, 0, NULL, 32, vs, 32) == -1);
    assert(!s_constant_reuse.stage[0].valid);
    s_eb.nsets = s_eb.nsplit = 1; vk_eng_batch_reset();
    assert(pool_resets == 2 && !s_eb.nsets && !s_eb.nsplit);
    free(s_eb.vsc); free(s_eb.psc);
    puts("Production constants: exact bytes, immutable spans, two arenas, reset ABA, failures and 10000 changing draws pass");
    return 0;
}
