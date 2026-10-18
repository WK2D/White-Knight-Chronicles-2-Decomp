/* Production settings/HUD against recording calls: no game, GPU or desktop. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_graphics_settings.h"
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint8_t u8;
typedef struct { VkImage img; } vk_image;
static struct { VkDevice device; int windowed; void* window; VkExtent2D sc_extent; VkImage sc_images[2]; } s_vk;
static rsx_vulkan_graphics_config s_graphics,s_graphics_applied;
static int s_graphics_enabled,s_graphics_dirty;
static struct { VkSampler s; } s_esamp[128];
static u32 s_esamp_count;
static double time_ms;
static u32 allocations,uploads,blits,flushes,drains,idles,rebuilds,destroyed,fullscreen,width,height;
static unsigned char uploaded[128*24*4];
static u32 foreground_regions;
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x1001
#define SDL_WINDOW_FULLSCREEN 1
#define VK_LOG(...) ((void)0)
typedef struct { int w,h; } SDL_DisplayMode;
static const char* SDL_GetError(void) { return "test"; }
static int SDL_SetWindowDisplayMode(void* w,const SDL_DisplayMode* mode) { (void)w;width=mode->w;height=mode->h;return 0; }
static double vk_now_ms(void) { return time_ms; }
static int vk_create_image(vk_image* im,VkFormat fmt,u32 w,u32 h,VkImageUsageFlags usage,VkImageAspectFlags aspect) {
    assert(fmt==VK_FORMAT_R8G8B8A8_UNORM && w==128 && h==24);
    assert((usage&(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT))==usage);
    assert(usage&VK_IMAGE_USAGE_SAMPLED_BIT); // image helper also creates a view
    (void)aspect;im->img=(VkImage)(uintptr_t)11;allocations++;return 0;
}
static void vk_barrier_image(VkImage im,VkImageAspectFlags a,VkImageLayout l) { (void)im;(void)a;(void)l; }
static void vk_barrier_image_in(VkCommandBuffer c,VkImage im,VkImageAspectFlags a,VkImageLayout l) { (void)c;vk_barrier_image(im,a,l); }
static int vk_eng_upload_region(VkImage im,u32 layer,u32 mip,u32 x,u32 y,u32 w,u32 h,size_t tight,u32 rows,const void* p,u32 stride,VkImageAspectFlags aspect) {
    (void)im;(void)layer;(void)mip;(void)x;(void)y;(void)aspect;
    assert(w==128 && h==24 && tight==512 && rows==24 && stride==512);
    memcpy(uploaded,p,sizeof uploaded);uploads++;return 0;
}
static void pvkCmdBlitImage(VkCommandBuffer c,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,u32 n,const VkImageBlit* b,VkFilter f) {
    (void)c;(void)src;(void)dst;assert(sl==VK_IMAGE_LAYOUT_GENERAL && dl==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && n>1 && n<=512 && f==VK_FILTER_NEAREST);
    foreground_regions=n;
    for(u32 i=0;i<n;i++) {
        assert(b[i].dstOffsets[0].x>=0 && b[i].dstOffsets[1].x<=128);
        assert(b[i].dstOffsets[0].y>=0 && b[i].dstOffsets[1].y<=24);
        for(int y=b[i].srcOffsets[0].y;y<b[i].srcOffsets[1].y;y++)
            for(int x=b[i].srcOffsets[0].x;x<b[i].srcOffsets[1].x;x++)
                assert(uploaded[(y*128+x)*4]==240); // No background pixels reach the window.
    }
    blits++;
}
static int vk_batch_flush(void) { flushes++;return 0; }
static void vk_slot_drain(void) { drains++; }
static VkResult pvkDeviceWaitIdle(VkDevice d) { (void)d;idles++;return VK_SUCCESS; }
static void pvkDestroySampler(VkDevice d,VkSampler s,const VkAllocationCallbacks* a) { (void)d;(void)s;(void)a;destroyed++; }
static int SDL_SetWindowFullscreen(void* w,u32 f) { (void)w;fullscreen=f;return 0; }
static void SDL_SetWindowSize(void* w,int x,int y) { (void)w;width=x;height=y; }
static void vk_swapchain_lost(void) { rebuilds++; }
#include "graphics-fixture.inc"
int main(void) {
    s_vk.device=(VkDevice)(uintptr_t)1;s_vk.windowed=1;s_vk.sc_extent=(VkExtent2D){1280,720};
    rsx_vulkan_graphics_config config={0,1920,1080,1,16,144,0};
    rsx_vulkan_backend_configure(&config);assert(s_graphics_dirty);
    s_esamp_count=2;vk_apply_graphics();assert(!s_graphics_dirty && destroyed==2 && !s_esamp_count);
    assert(width==1920 && height==1080 && rebuilds==1 && flushes==1 && drains==1 && idles==1);
    rsx_vulkan_backend_configure(&config);assert(!s_graphics_dirty);
    rsx_vulkan_graphics_config bad=config;bad.width=0;rsx_vulkan_backend_configure(&bad);assert(!s_graphics_dirty);
    bad=config;bad.frame_limit=143;rsx_vulkan_backend_configure(&bad);assert(!s_graphics_dirty && s_graphics.frame_limit==144);
    time_ms=100;vk_counter_update();assert(!allocations && !uploads && !s_counter_ready);
    config.fps_counter=1;rsx_vulkan_backend_configure(&config);vk_apply_graphics();assert(flushes==1);
    for(unsigned i=1;i<30;i++) {time_ms=100+i*(1000.0/30);vk_counter_update();}
    time_ms=1100;vk_counter_update();assert(s_counter_fps>29.99 && s_counter_fps<30.01 && allocations==1 && uploads==30);
    unsigned text_pixels=0;for(unsigned i=0;i<128*24;i++) {assert(uploaded[i*4+3]==255);text_pixels+=uploaded[i*4]==240;}
    assert(text_pixels>100);vk_counter_blit((VkCommandBuffer)0,0);
    assert(blits==1 && foreground_regions>20);
    config.fps_counter=0;rsx_vulkan_backend_configure(&config);vk_counter_update();vk_counter_blit((VkCommandBuffer)0,0);
    assert(!s_counter_ready && uploads==30 && blits==1);
    config.window_mode=1;rsx_vulkan_backend_configure(&config);vk_apply_graphics();assert(fullscreen==SDL_WINDOW_FULLSCREEN_DESKTOP);
    config.window_mode=2;rsx_vulkan_backend_configure(&config);vk_apply_graphics();assert(fullscreen==SDL_WINDOW_FULLSCREEN && width==1920 && height==1080);
    puts("Settings changes, sampler retirement, completed-frame HUD, upload and Off pass.");
}
