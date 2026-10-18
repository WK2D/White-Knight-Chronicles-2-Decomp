/* Drive the production sampler builder with a recording Vulkan entry point.
 * No window, GPU or game inputs. Changes in bias and fine LOD bounds must
 * create distinct samplers; returning to an earlier state must reuse it. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_draw_engine.h"
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VK_ENG_SAMP_CACHE 128
static struct { VkDevice device; VkSampler sampler; float max_sampler_lod_bias, max_anisotropy; int anisotropy_ok; } s_vk;
static struct { u32 anisotropy; } s_graphics;
static struct { rsx_be_sampler_desc desc; VkSampler s; } s_esamp[VK_ENG_SAMP_CACHE];
static u32 s_esamp_count;
static VkSamplerCreateInfo recorded;
static u32 creations;
static int fail_create;
static VkSamplerAddressMode vk_gcm_wrap(u32 wrap)
{ (void)wrap; return VK_SAMPLER_ADDRESS_MODE_REPEAT; }
static VkCompareOp vk_gcm_compare(u32 op)
{ (void)op; return VK_COMPARE_OP_ALWAYS; }
static u32 rsx_texture_compare_host_func(u32 op) { return op; }
static VkResult pvkCreateSampler(VkDevice device, const VkSamplerCreateInfo* info,
                                 const VkAllocationCallbacks* alloc, VkSampler* out)
{
    (void)device; (void)alloc;
    if (fail_create) return VK_ERROR_OUT_OF_HOST_MEMORY;
    recorded = *info;
    *out = (VkSampler)(uintptr_t)(++creations);
    return VK_SUCCESS;
}
#include "sampler-fixture.inc"

int main(void)
{
    s_vk.max_sampler_lod_bias = 2.0f;
    rsx_be_sampler_desc d = {0};
    d.min_linear = d.mag_linear = d.mip_linear = d.mip_present = 1;
    d.wrap_s = d.wrap_t = d.wrap_r = 1;
    d.max_lod = 8.0f;
    const VkSampler zero = vk_eng_sampler(&d);
    assert(zero && recorded.mipLodBias == 0.0f);
    d.lod_bias = -1.5f;
    const VkSampler sharper = vk_eng_sampler(&d);
    assert(sharper != zero && recorded.mipLodBias == -1.5f);
    assert(vk_eng_sampler(&d) == sharper && creations == 2);
    d.lod_bias = 1.5f;
    assert(vk_eng_sampler(&d) != sharper && recorded.mipLodBias == 1.5f);
    d.lod_bias = 0;
    assert(vk_eng_sampler(&d) == zero && creations == 3);
    /* These 4.8 values alias if a cache keeps only the upper float bits. */
    d.max_lod = 8.0f + 1.0f/256.0f;
    assert(vk_eng_sampler(&d) != zero && recorded.maxLod == d.max_lod);
    d.lod_bias = -16.0f;
    assert(vk_eng_sampler(&d) && recorded.mipLodBias == -2.0f);
    d.lod_bias = 16.0f - 1.0f/256.0f;
    assert(vk_eng_sampler(&d) && recorded.mipLodBias == 2.0f);
    d.mip_present = 0; d.min_lod = 1.0f;
    assert(vk_eng_sampler(&d) && recorded.minLod == 1 && recorded.maxLod == 1);
    d.compare_func = 1;
    assert(vk_eng_sampler(&d) && recorded.compareEnable == VK_TRUE);
    /* Preferences retire the cache before changing the global override. */
    s_esamp_count=0;s_graphics.anisotropy=16;s_vk.anisotropy_ok=1;s_vk.max_anisotropy=8;
    d.compare_func=0;d.mip_present=d.min_linear=1;
    assert(vk_eng_sampler(&d) && recorded.anisotropyEnable && recorded.maxAnisotropy==8);
    s_esamp_count=0;s_vk.anisotropy_ok=0;
    assert(vk_eng_sampler(&d) && !recorded.anisotropyEnable);
    s_esamp_count=0;s_vk.anisotropy_ok=1;d.mip_present=0;
    assert(vk_eng_sampler(&d) && !recorded.anisotropyEnable);
    s_esamp_count=0;d.mip_present=1;d.min_linear=0;
    assert(vk_eng_sampler(&d) && !recorded.anisotropyEnable);
    s_esamp_count=0;s_graphics.anisotropy=0;d.min_linear=1;
    assert(vk_eng_sampler(&d) && !recorded.anisotropyEnable && recorded.maxAnisotropy==1);
    d.lod_bias = -0.5f; fail_create = 1;
    const u32 before = s_esamp_count;
    assert(!vk_eng_sampler(&d) && s_esamp_count == before);
    puts("Signed LOD bias, sampler cache identity, device limits and comparison state pass.");
    return 0;
}
