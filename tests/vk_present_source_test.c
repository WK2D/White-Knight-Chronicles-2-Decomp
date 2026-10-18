/* Execute the production presentation path against recording Vulkan calls.
 * Synthetic image handles only; no window, GPU, game data or desktop input. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define HANDLE(type, value) ((type)(uintptr_t)(value))
#define VK_LOG(...) ((void)0)
static struct { u32 frame_limit; } s_graphics;
static int s_graphics_enabled;
static double vk_now_ms(void) { return 1000.0; }
static void SDL_Delay(u32 delay) { (void)delay; }
static void vk_counter_update(void) {}
static void vk_counter_blit(VkCommandBuffer cmd,u32 index) { (void)cmd; (void)index; }
#define VK_ENG_COLOR 1
typedef struct { VkImage img; VkImageView view; } vk_image;
typedef struct { VkBuffer buf; void* ptr; } vk_buffer;
typedef struct { vk_image im; u32 w, h; } vk_eng_obj;
static unsigned char pixels[1280 * 720 * 4];
static vk_eng_obj guest;
static struct {
    VkDevice device; VkQueue queue; VkCommandPool pool; VkCommandBuffer cmd;
    int windowed; VkSwapchainKHR swapchain; VkImage sc_images[2]; VkExtent2D sc_extent;
    vk_image color; vk_buffer readback; u32 width, height;
    const char* dump_path; u32 dump_every, presented, last_center;
    double hold_seconds;
} s_vk;
static struct { int open; } s_batch;
static struct { u32 draws, last_draws; } s_g;
static VkCommandBuffer s_cmd_win;
static struct { VkImage src, dst; VkImageBlit region; VkFilter filter; } blits[4];
static VkBufferImageCopy copy;
static VkImage vk_fxaa(VkImage source, VkImageView view,u32 w,u32 h) { (void)view;(void)w;(void)h;return source; }
static VkImage copy_source;
static u32 nblits, nsubmits, presents, retired, dumped, source_barriers;
static int allocation_failure, acquire_result;
static void vk_barrier_image_in(VkCommandBuffer cmd, VkImage image,
                                 VkImageAspectFlags aspect, VkImageLayout layout)
{ (void)cmd; (void)aspect; (void)layout; if (image == guest.im.img) source_barriers++; }
static void vk_barrier_image(VkImage image, VkImageAspectFlags aspect, VkImageLayout layout)
{ vk_barrier_image_in(s_vk.cmd, image, aspect, layout); }
static void pvkCmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags src,
    VkPipelineStageFlags dst, VkDependencyFlags flags, u32 nm, const VkMemoryBarrier* m,
    u32 nb, const VkBufferMemoryBarrier* b, u32 ni, const VkImageMemoryBarrier* images)
{
    (void)cmd; (void)src; (void)dst; (void)flags; (void)nm; (void)m; (void)nb; (void)b;
    assert(ni == 1 && images->image == s_vk.sc_images[0]);
    assert(images->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
           images->newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
static void pvkCmdBlitImage(VkCommandBuffer cmd, VkImage src, VkImageLayout sl,
    VkImage dst, VkImageLayout dl, u32 count, const VkImageBlit* region, VkFilter filter)
{
    (void)cmd; (void)sl; (void)dl; assert(count == 1 && nblits < 4);
    blits[nblits].src = src; blits[nblits].dst = dst;
    blits[nblits].region = *region; blits[nblits++].filter = filter;
}
static VkResult pvkAllocateCommandBuffers(VkDevice device,
    const VkCommandBufferAllocateInfo* info, VkCommandBuffer* cmd)
{
    (void)device; (void)info;
    if (allocation_failure) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *cmd = HANDLE(VkCommandBuffer, 20); return VK_SUCCESS;
}
static VkResult pvkEndCommandBuffer(VkCommandBuffer cmd) { (void)cmd; return VK_SUCCESS; }
static VkResult pvkResetCommandBuffer(VkCommandBuffer cmd, VkCommandBufferResetFlags flags)
{ (void)cmd; (void)flags; return VK_SUCCESS; }
static VkResult pvkBeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* info)
{ (void)cmd; (void)info; return VK_SUCCESS; }
static VkResult pvkQueueSubmit(VkQueue queue, u32 count, const VkSubmitInfo* info, VkFence fence)
{ (void)queue; (void)info; (void)fence; assert(count == 1); nsubmits++; return VK_SUCCESS; }
static VkResult pvkDeviceWaitIdle(VkDevice device) { (void)device; return VK_SUCCESS; }
static int vk_window_acquire(u32* idx) { *idx = 0; return acquire_result; }
static int vk_begin(void) { return 0; }
static int vk_window_submit_present(VkCommandBuffer cmd, u32 idx)
{ (void)cmd; assert(idx == 0); presents++; return 0; }
static void vk_batch_end_pass(void) {}
static void vk_batch_flush(void) { s_batch.open = 0; }
static void vk_batch_retire(void) { retired++; }
static void vk_flush_report(void) {}
static int vk_rec_begin(void) { s_batch.open = 1; return 0; }
static vk_eng_obj* vk_eng_get(u32 surface, int kind)
{ assert(kind == VK_ENG_COLOR); return surface == 7 ? &guest : NULL; }
static void pvkCmdCopyImageToBuffer(VkCommandBuffer cmd, VkImage src, VkImageLayout layout,
    VkBuffer dst, u32 count, const VkBufferImageCopy* region)
{ (void)cmd; (void)layout; (void)dst; assert(count == 1); copy_source = src; copy = *region; }
static void vk_cmd_buffer_to_host(VkBuffer buffer) { (void)buffer; }
static void vk_invalidate(vk_buffer* buffer) { (void)buffer; }
static void vk_dump_ppm(const unsigned char* rgba) { assert(rgba == pixels); dumped++; }
#include "present-fixture.inc"

static void reset(void)
{
    memset(&s_vk, 0, sizeof s_vk);
    memset(blits, 0, sizeof blits); memset(&copy, 0, sizeof copy);
    s_vk.device = HANDLE(VkDevice, 1); s_vk.cmd = HANDLE(VkCommandBuffer, 2);
    s_vk.color.img = HANDLE(VkImage, 3); s_vk.sc_images[0] = HANDLE(VkImage, 4);
    s_vk.swapchain = HANDLE(VkSwapchainKHR, 5); s_vk.windowed = 1;
    s_vk.width = 1280; s_vk.height = 720; s_vk.sc_extent = (VkExtent2D){1920,1080};
    s_vk.readback.ptr = pixels; s_vk.dump_every = 1;
    guest.im.img = HANDLE(VkImage, 6); guest.w = 1920; guest.h = 1080;
    nblits = nsubmits = presents = retired = dumped = source_barriers = 0;
    allocation_failure = acquire_result = 0; s_cmd_win = VK_NULL_HANDLE;
    memset(pixels, 0x5a, sizeof pixels); s_g.draws = 7;
}
static void check_window(u32 at)
{
    assert(blits[at].src == guest.im.img && blits[at].dst == s_vk.sc_images[0]);
    assert(blits[at].region.srcOffsets[1].x == 1920 && blits[at].region.srcOffsets[1].y == 1080);
    assert(blits[at].region.dstOffsets[1].x == (int)s_vk.sc_extent.width);
    assert(blits[at].region.dstOffsets[1].y == (int)s_vk.sc_extent.height);
}
int main(void)
{
    reset(); vk_eng_present(NULL, 7);
    assert(nblits == 1); check_window(0);
    assert(blits[0].filter == VK_FILTER_NEAREST && presents == 1 && nsubmits == 1 && retired == 1);
    assert(copy_source == guest.im.img && copy.imageOffset.x == 960 && copy.imageOffset.y == 540);
    assert(copy.imageExtent.width == 1 && copy.imageExtent.height == 1 && source_barriers >= 2);
    assert(s_vk.last_center == 0x5a5a5a && s_vk.presented == 1 && s_g.last_draws == 7 && s_g.draws == 0);
    reset(); s_vk.sc_extent = (VkExtent2D){2560,1440}; vk_eng_present(NULL, 7);
    assert(nblits == 1); check_window(0); assert(blits[0].filter == VK_FILTER_LINEAR);
    reset(); s_vk.sc_extent = (VkExtent2D){960,540}; vk_eng_present(NULL, 7);
    assert(nblits == 1); check_window(0); assert(blits[0].filter == VK_FILTER_LINEAR);
    reset(); allocation_failure = 1; vk_eng_present(NULL, 7);
    assert(nblits == 1 && presents == 1); check_window(0);
    reset(); acquire_result = 1; vk_eng_present(NULL, 7);
    assert(nblits == 0 && presents == 0 && retired == 1);
    reset(); s_vk.windowed = 0; vk_eng_present(NULL, 7);
    assert(nblits == 0 && presents == 0 && copy_source == guest.im.img);
    reset(); s_vk.dump_path = "synthetic"; vk_eng_present(NULL, 7);
    assert(nblits == 2 && blits[0].dst == s_vk.color.img); check_window(1);
    assert(copy_source == s_vk.color.img && copy.imageOffset.x == 0 && copy.imageOffset.y == 0);
    assert(copy.imageExtent.width == 1280 && copy.imageExtent.height == 720 && dumped == 1);
    reset(); s_vk.hold_seconds = 1; vk_eng_present(NULL, 7);
    assert(nblits == 2 && blits[0].dst == s_vk.color.img); check_window(1);
    reset(); vk_eng_present(NULL, 0);
    assert(nblits == 1 && blits[0].src == s_vk.color.img && copy_source == s_vk.color.img);
    puts("Direct guest-frame presentation, scaling, fallback, diagnostics and headless paths pass.");
    return 0;
}
