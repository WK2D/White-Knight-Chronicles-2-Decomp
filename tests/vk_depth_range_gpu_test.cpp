/* Synthetic GPU depth-interval regression. No window or game data.
 * Uses production range/vertex coefficients with independent depth expectations. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "rsx_depth_range.h"
#include "rsx_shader_spirv.h"
#include <cassert>
#include <stdlib.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#define FNS(X) \
 X(vkCreateInstance) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) \
 X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
 X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties) \
 X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice) X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) \
 X(vkCreateImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkBindImageMemory) \
 X(vkCreateImageView) X(vkDestroyImageView) X(vkDestroyImage) X(vkFreeMemory) \
 X(vkCreateBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkDestroyBuffer) \
 X(vkMapMemory) X(vkUnmapMemory) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
 X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
 X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
 X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) \
 X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle) \
 X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdSetDepthBias) \
 X(vkCmdDraw) X(vkCmdCopyImageToBuffer)
#define DECL(name) static PFN_##name name;
FNS(DECL)
static VkDevice device;
static int unrestricted;
static VkPhysicalDeviceMemoryProperties memory;
static void checked(VkResult result) { assert(result == VK_SUCCESS); }
static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
    for(uint32_t i=0;i<memory.memoryTypeCount;i++)
        if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
    assert(!"required memory type unavailable"); return 0;
}
static VkDeviceMemory allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags)
{
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize=req.size; a.memoryTypeIndex=memory_type(req.memoryTypeBits,flags);
    VkDeviceMemory result; checked(vkAllocateMemory(device,&a,nullptr,&result)); return result;
}
static VkShaderModule shader(float z,float w,float scale,float offset)
{
    char source[1024];std::snprintf(source,sizeof source,
        "float4 main(uint id:SV_VertexID):SV_Position { float2 p=id==0?float2(-1,-1):id==1?float2(3,-1):float2(-1,3);"
        "float w=%.9g;float z=%.9g*w;return float4(p*w,z*%.9g+w*%.9g,w);}",
        double(w),double(2*z-1),double(scale),double(offset));
    uint32_t words[4096],count=0;char log[4096]={};
    int result=rsx_hlsl_to_spirv(source,RSX_SHADER_STAGE_VERTEX,words,4096,&count,log,sizeof log);
    if(result)std::fprintf(stderr,"Shader failure: %s\n",log);assert(!result);
    VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};si.codeSize=count*4;si.pCode=words;
    VkShaderModule module;checked(vkCreateShaderModule(device,&si,nullptr,&module));return module;
}
static void run_format(VkPhysicalDevice physical, VkQueue queue, uint32_t family,
                       VkFormat format, uint32_t vendor)
{
    VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; im.imageType=VK_IMAGE_TYPE_2D;
    im.format=format; im.extent={32,32,1}; im.mipLevels=im.arrayLayers=1;
    im.samples=VK_SAMPLE_COUNT_1_BIT; im.tiling=VK_IMAGE_TILING_OPTIMAL;
    im.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image; checked(vkCreateImage(device,&im,nullptr,&image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device,image,&req);
    VkDeviceMemory image_mem=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checked(vkBindImageMemory(device,image,image_mem,0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;
    vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=format;
    vi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
    VkImageView view;checked(vkCreateImageView(device,&vi,nullptr,&view));
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=32*32*4;
    bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer;checked(vkCreateBuffer(device,&bi,nullptr,&buffer));
    vkGetBufferMemoryRequirements(device,buffer,&req);
    VkDeviceMemory buffer_mem=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checked(vkBindBufferMemory(device,buffer,buffer_mem,0));
    VkAttachmentDescription att{};att.format=format;att.samples=VK_SAMPLE_COUNT_1_BIT;
    att.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;att.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;att.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;att.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference depth{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.pDepthStencilAttachment=&depth;
    VkSubpassDependency dep{};dep.srcSubpass=0;dep.dstSubpass=VK_SUBPASS_EXTERNAL;
    dep.srcStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.attachmentCount=1;ri.pAttachments=&att;
    ri.subpassCount=1;ri.pSubpasses=&sub;ri.dependencyCount=1;ri.pDependencies=&dep;
    VkRenderPass rp;checked(vkCreateRenderPass(device,&ri,nullptr,&rp));
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=rp;
    fi.attachmentCount=1;fi.pAttachments=&view;fi.width=fi.height=32;fi.layers=1;
    VkFramebuffer fb;checked(vkCreateFramebuffer(device,&fi,nullptr,&fb));
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout;
    checked(vkCreatePipelineLayout(device,&li,nullptr,&layout));

    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0,0,32,32,0,1};VkRect2D scissor{{0,0},{32,32}};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable=ds.depthWriteEnable=VK_TRUE;ds.depthCompareOp=VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=family;
    cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool pool;
    checked(vkCreateCommandPool(device,&cp,nullptr,&pool));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;
    ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;VkCommandBuffer cmd;
    checked(vkAllocateCommandBuffers(device,&ca,&cmd));
    const struct {float n,f,z,w;bool clipped;} cases[]={
        {0,1,0.5f,1,false},{0.25f,0.75f,0.375f,1,false},
        {0.25f,0.75f,0.1f,1,true},{0.25f,0.75f,0.9f,1,true},
        {0,1,0.1f,1,false},{0.25f,0.75f,0.625f,2,false},
        {0.25f,0.75f,0.375f,0.5f,false},
        {0.9975071f,0.99878764f,0.998f,1,false},
        {0.9975071f,0.99878764f,0.9974f,1,true},
        {0.9975071f,0.99878764f,0.9989f,1,true},
        {0.99800718f,1.00000715f,0.99999f,1,false},
        {0.99800718f,1.00000715f,1.000003f,1,false}};
    unsigned tested=0;
    for(const auto& c:cases) {
        float range[2],coeff[2];
        if(!rsx_depth_range_select(c.n,c.f,unrestricted,range))continue;
        assert(rsx_depth_range_coefficients(0.5f,0.5f,c.n,c.f,coeff));
        viewport.minDepth=range[0];viewport.maxDepth=range[1];
        VkShaderModule module=shader(c.z,c.w,coeff[0],coeff[1]);
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage=VK_SHADER_STAGE_VERTEX_BIT;stage.module=module;stage.pName="main";
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=1;pi.pStages=&stage;
        pi.pVertexInputState=&vertex;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;
        pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pDepthStencilState=&ds;
        pi.pColorBlendState=&blend;pi.layout=layout;pi.renderPass=rp;
        VkPipeline pipeline;checked(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
        checked(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        checked(vkBeginCommandBuffer(cmd,&cb));VkClearValue clear{};clear.depthStencil.depth=0.875f;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=rp;begin.framebuffer=fb;
        begin.renderArea={{0,0},{32,32}};begin.clearValueCount=1;begin.pClearValues=&clear;
        vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};copy.imageExtent={32,32,1};
        vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        checked(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        checked(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));checked(vkQueueWaitIdle(queue));
        void* data;checked(vkMapMemory(device,buffer_mem,0,VK_WHOLE_SIZE,0,&data));
        float z;
        if(format==VK_FORMAT_D24_UNORM_S8_UINT || format==VK_FORMAT_X8_D24_UNORM_PACK32)
            z=float(static_cast<uint32_t*>(data)[16*32+16]&0xFFFFFFu)/16777215.f;
        else z=static_cast<float*>(data)[16*32+16];
        vkUnmapMemory(device,buffer_mem);
        const bool fixed=format==VK_FORMAT_D24_UNORM_S8_UINT || format==VK_FORMAT_X8_D24_UNORM_PACK32;
        const float expected=c.clipped?0.875f:(fixed?std::fmin(1.f,std::fmax(0.f,c.z)):c.z);
        if(std::fabs(z-expected)>0.0000005f) {
            std::fprintf(stderr,"format=%u case=%u actual=%.9g expected=%.9g\n",unsigned(format),tested,z,expected);assert(false);
        }
        tested++;vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,module,nullptr);
    }
    assert(tested>=10);std::printf("format=%u: %u depth preservation/near-far clipping/W/reset cases pass\n",unsigned(format),tested);
    vkDestroyCommandPool(device,pool,nullptr);
    vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyFramebuffer(device,fb,nullptr);vkDestroyRenderPass(device,rp,nullptr);
    vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,buffer_mem,nullptr);
    vkDestroyImageView(device,view,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,image_mem,nullptr);
}
static void conversion_checks() {
    float range[2],coeff[2];
    assert(rsx_depth_range_select(0.25f,0.75f,0,range));
    assert(!rsx_depth_range_select(0,1.1f,0,range) && range[0]==0 && range[1]==1);
    assert(!rsx_depth_range_select(1,0,1,range));
    assert(!rsx_depth_range_select(0.5f,0.5f,1,range));
    assert(!rsx_depth_range_select(NAN,1,1,range));
    assert(!rsx_depth_range_select(0,INFINITY,1,range));
    assert(rsx_depth_range_coefficients(0.5f,0.5f,0.25f,0.75f,coeff));
    assert(coeff[0]==1 && coeff[1]==0.5f);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    conversion_checks();
    HMODULE library=LoadLibraryA("vulkan-1.dll");assert(library);
#define LOAD(name) name=reinterpret_cast<PFN_##name>(GetProcAddress(library,#name));assert(name);
    FNS(LOAD)
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkInstance instance;
    checked(vkCreateInstance(&ic,nullptr,&instance));uint32_t count=0;
    checked(vkEnumeratePhysicalDevices(instance,&count,nullptr));assert(count);
    std::vector<VkPhysicalDevice> devices(count);checked(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice physical=devices[0];
    for(auto candidate:devices){VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(candidate,&prop);if(prop.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){physical=candidate;break;}}
    VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(physical,&prop);
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());uint32_t family=0;
    while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<count);
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
    vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr);
    std::vector<VkExtensionProperties> exts(count);checked(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,exts.data()));
    for(const auto& ext:exts)if(std::string(ext.extensionName)==VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME)unrestricted=1;
    const char* extension=VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME;
    if(unrestricted){di.enabledExtensionCount=1;di.ppEnabledExtensionNames=&extension;}
    checked(vkCreateDevice(physical,&di,nullptr,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    unsigned tested=0;
    for(auto format:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D32_SFLOAT_S8_UINT,
                     VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_X8_D24_UNORM_PACK32}){
        VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(physical,format,&fp);
        if(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT){run_format(physical,queue,family,format,prop.vendorID);tested++;}
    }
    assert(tested);checked(vkDeviceWaitIdle(device));vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(library);
    std::puts("GPU window-depth preservation, clipping, W and range reset pass.");return 0;
}
