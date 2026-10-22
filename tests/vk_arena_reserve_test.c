/* Exercise the actual reservation code with synthetic resource/queue lifetimes.
 * No Vulkan device or game data is needed. A layout transition must not submit
 * existing work, and a capacity submission must allocate in the new slot. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t VkDeviceSize, VkDescriptorPool;
typedef struct { uint64_t buf; void* ptr; VkDeviceSize size; } vk_buffer;
typedef struct {
    vk_buffer verts, idx, ubo;
    VkDeviceSize vpos, ipos, upos, ubo_align;
    VkDescriptorPool pool, split_pool;
    u32 nsets, nsplit;
} arena_slot;
static arena_slot s_eb, spare;
static int submissions, allocations, allocation_failure, submit_failure;
static struct { int pending; uint64_t fence; } s_spare;
static struct { uint64_t device; } s_vk;
static int fence_ready, fence_queries;
#define VK_SUCCESS 0
static int pvkGetFenceStatus(uint64_t device, uint64_t fence)
{
    (void)device; (void)fence;
    ++fence_queries;
    return fence_ready ? VK_SUCCESS : 1;
}
#define VK_EB_SETS 8192
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 1
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT 2
#define VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT 4
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 8
static int vk_batch_flush_async_at(int line)
{
    (void)line;
    ++submissions;
    if (submit_failure) return -1;
    arena_slot tmp = s_eb; s_eb = spare; spare = tmp;
    s_eb.vpos = s_eb.ipos = s_eb.upos = 0;
    s_eb.nsets = s_eb.nsplit = 0;
    return 0;
}
static int vk_ensure_buffer(vk_buffer* b, VkDeviceSize bytes, VkDeviceSize floor, int usage)
{
    (void)usage;
    b->buf = 1;
    if (b->size < bytes) b->size = bytes > floor ? bytes : floor;
    return 0;
}
static VkDescriptorPool vk_eb_make_pool(void)
{
    ++allocations;
    return allocation_failure ? 0 : (VkDescriptorPool)(100 + allocations);
}
#include "vk-arena-fixture.inc"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void reset(void)
{
    memset(&s_eb, 0, sizeof s_eb);
    s_eb.verts.buf = s_eb.idx.buf = s_eb.ubo.buf = 1;
    s_eb.verts.size = s_eb.idx.size = s_eb.ubo.size = 4096;
    s_eb.ubo_align = 256;
    spare = s_eb;
    submissions = allocations = allocation_failure = submit_failure = 0;
    s_spare.pending = fence_queries = 0; fence_ready = 1;
}
int main(void)
{
    reset();
    /* This stranded pair used to swap on every ordinary/shadow transition. */
    s_eb.pool = 1; spare.split_pool = 2;
    for (int i = 0; i < 24; ++i) {
        CHECK(vk_eb_reserve(64, 12, 256, i & 1) == 0);
        CHECK(submissions == 0);
        CHECK(s_eb.pool && s_eb.split_pool == (i ? 101 : 0));
    }
    CHECK(allocations == 1);
    CHECK(spare.pool == 0 && spare.split_pool == 2);
    /* A real capacity flush changes the active slot; create its missing pool. */
    s_eb.vpos = 4096;
    CHECK(vk_eb_reserve(64, 12, 256, 0) == 0);
    CHECK(submissions == 1 && allocations == 2);
    CHECK(s_eb.pool == 102 && s_eb.split_pool == 2);
    CHECK(spare.pool == 1 && spare.split_pool == 101);
    reset();
    allocation_failure = 1;
    CHECK(vk_eb_reserve(64, 12, 256, 1) == -1);
    CHECK(submissions == 0 && s_eb.split_pool == 0);
    reset();
    s_eb.pool = 1; s_eb.nsets = VK_EB_SETS;
    CHECK(vk_eb_reserve(64, 12, 256, 0) == 0);
    CHECK(submissions == 1 && s_eb.pool != 0);
    reset();
    s_eb.pool = 1; s_eb.upos = 4096; submit_failure = 1;
    CHECK(vk_eb_reserve(64, 12, 256, 0) == -1);
    CHECK(submissions == 1 && allocations == 0);
    reset();
    s_eb.pool = 1; s_eb.nsets = 256; s_spare.pending = 1;
    fence_ready = 0;
    CHECK(vk_eb_reserve(64, 12, 256, 0) == 0);
    CHECK(submissions == 0 && fence_queries == 1);
    /* Busy GPU: keep recording, without querying on every draw. */
    s_eb.nsets = 257;
    CHECK(vk_eb_reserve(64, 12, 256, 0) == 0);
    CHECK(submissions == 0 && fence_queries == 1);
    s_eb.nsets = 320; fence_ready = 1;
    CHECK(vk_eb_reserve(64, 12, 256, 0) == 0);
    CHECK(submissions == 1 && fence_queries == 2 && s_eb.pool != 0);
    puts("Vulkan arena layout transitions and slot ownership passed");
    return 0;
}
