/* Execute production retirement helpers with delayed and failing GPU completion.
 * Synthetic handles only; no GPU, window or game data. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef int VkResult;
typedef uint64_t VkDevice;
typedef uint64_t VkFence;
typedef uint32_t VkBool32;
#define VK_TRUE 1
#define VK_SUCCESS 0
#define WT_GPU_WAIT 0
#define VK_DEFER_MAX 2
typedef struct { uint64_t img; } vk_image;
static struct { VkDevice device; } s_vk;
static struct { int open; } s_batch;
static struct { VkFence fence; int pending; vk_image defer[VK_DEFER_MAX]; u32 defer_n; } s_spare;
static vk_image s_defer[VK_DEFER_MAX];
static u32 s_defer_n;
static int s_batch_failed, wait_failure, flush_failure;
static unsigned waits, resets, destroys, swaps, flushes;
static uint64_t destroyed[8];
static uint64_t wt_begin(void) { return 0; }
static void wt_end(int kind, u32 count, int ignored, uint64_t start)
{ (void)kind; (void)count; (void)ignored; (void)start; }
static VkResult pvkWaitForFences(VkDevice device, u32 count, const VkFence* fences,
                                VkBool32 all, uint64_t timeout)
{
    assert(device == 1 && count == 1 && *fences == 2 && all && timeout == UINT64_MAX);
    ++waits;
    return wait_failure ? -1 : VK_SUCCESS;
}
static void vk_eng_batch_reset(void) { ++resets; }
static void vk_destroy_image(vk_image* image)
{
    // Every image used by the delayed spare batch must outlive its fence.
    assert(!s_spare.pending);
    assert(destroys < 8);
    destroyed[destroys++] = image->img;
    image->img = 0;
}
static void vk_slot_swap(void)
{
    vk_image copy[VK_DEFER_MAX];
    memcpy(copy, s_defer, sizeof copy);
    memcpy(s_defer, s_spare.defer, sizeof copy);
    memcpy(s_spare.defer, copy, sizeof copy);
    const u32 n = s_defer_n;
    s_defer_n = s_spare.defer_n;
    s_spare.defer_n = n;
    ++swaps;
}
static int vk_slot_drain(void);
static int vk_batch_flush(void)
{
    ++flushes;
    if (flush_failure) return -1;
    s_batch.open = 0;
    return vk_slot_drain();
}
#include "vk-lifetime-fixture.inc"

static void reset(void)
{
    memset(&s_spare, 0, sizeof s_spare);
    memset(s_defer, 0, sizeof s_defer);
    s_vk.device = 1;
    s_spare.fence = 2;
    s_batch.open = s_batch_failed = wait_failure = flush_failure = 0;
    waits = resets = destroys = swaps = flushes = s_defer_n = 0;
}
int main(void)
{
    vk_image image;
    // Between recording batches, an asynchronously submitted slot is still live.
    reset(); s_spare.pending = 1; image.img = 10;
    s_spare.defer[0].img = 11; s_spare.defer_n = 1;
    assert(!vk_defer_destroy_image(&image));
    assert(waits == 1 && swaps == 2 && resets == 1 && destroys == 2);
    assert(destroyed[0] == 11 && destroyed[1] == 10 && !image.img);

    // Open batches retain ownership; deferred images retire only after completion.
    reset(); s_batch.open = 1; s_spare.pending = 1; image.img = 20;
    assert(!vk_defer_destroy_image(&image));
    assert(s_defer_n == 1 && s_defer[0].img == 20 && !image.img);
    assert(!waits && !destroys && !resets);

    // A failed fence cannot free the image, reset arenas or mark the slot idle.
    reset(); s_spare.pending = wait_failure = 1; image.img = 30;
    s_spare.defer[0].img = 31; s_spare.defer_n = 1;
    assert(vk_defer_destroy_image(&image));
    assert(waits == 1 && s_batch_failed && s_spare.pending && image.img == 30);
    assert(!swaps && !resets && !destroys && s_spare.defer_n == 1);
    vk_batch_retire();
    assert(!resets && !destroys);
    assert(vk_defer_destroy_image(&image) && waits == 1);

    // A full deferred list must not discard the incoming handle on flush failure.
    reset(); s_batch.open = 1; s_defer_n = VK_DEFER_MAX; flush_failure = 1; image.img = 40;
    assert(vk_defer_destroy_image(&image));
    assert(flushes == 1 && image.img == 40 && !destroys && s_defer_n == VK_DEFER_MAX);

    reset(); image.img = 50;
    assert(!vk_defer_destroy_image(&image) && destroys == 1 && !waits);
    puts("Deferred images preserve GPU ownership through closed batches and failed completion.");
    return 0;
}
