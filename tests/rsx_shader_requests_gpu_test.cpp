/* GPU execution of decompiled synthetic RSX fragment programs. No window
 * or game data; the emitted function runs inside a compute test wrapper. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "rsx_fp_decompiler.h"
#include "rsx_test_programs.h"
#include <string>
#include <cstring>
#include "rsx_shader_spirv.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#define FNS(X) \
 X(vkCreateInstance) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) \
 X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
 X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties) \
 X(vkCreateDevice) X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) \
 X(vkCreateImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkBindImageMemory) \
 X(vkCreateImageView) X(vkDestroyImageView) X(vkDestroyImage) X(vkFreeMemory) \
 X(vkCreateBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkDestroyBuffer) \
 X(vkMapMemory) X(vkUnmapMemory) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
 X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
 X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
 X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) \
 X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle) \
 X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdSetDepthBias) \
 X(vkCmdDraw) X(vkCmdCopyImageToBuffer) \
 X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
 X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) \
 X(vkUpdateDescriptorSets) X(vkCreateComputePipelines) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)
#define DECL(name) static PFN_##name name;
FNS(DECL)
static VkDevice device;
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
static void instruction(u8* p,u32 d,u32 s0,u32 s1=0,u32 s2=0)
{
    rsx_test_fp_put_word(p,d);rsx_test_fp_put_word(p+4,s0);
    rsx_test_fp_put_word(p+8,s1);rsx_test_fp_put_word(p+12,s2);
}
static const u32 identity=(1u<<11)|(2u<<13)|(3u<<15);
static const u32 cc_identity=(1u<<23)|(2u<<25)|(3u<<27);
static const u32 always=7u<<18;
static const u32 initial[4]={0x3E800000,0x80000000,0x7F800000,0x7FC12345};
static const u32 replacement[4]={0x7FC23456,0x3F000000,0x40400000,0x3F400000};
static std::string decompile(u8* program,u32 length,u32 ctrl,u32 tc2d,u32& count)
{
    static char hlsl[RSX_FP_HLSL_BUFFER_SIZE];
    assert(rsx_fp_decompile_buffered_requests_ex(program,length,ctrl,0,0,tc2d,hlsl,sizeof hlsl,&count)>0);
    u32 fragment_words[32768],fragment_count=0;char fragment_log[4096]={};
    int fragment_result=rsx_hlsl_to_spirv(hlsl,RSX_SHADER_STAGE_FRAGMENT,
        fragment_words,32768,&fragment_count,fragment_log,sizeof fragment_log);
    if(fragment_result)std::fprintf(stderr,"Fragment compilation: %s\n",fragment_log);
    assert(!fragment_result && fragment_count);
    std::string source=hlsl;size_t at=source.find("main(PSInput input)");assert(at!=std::string::npos);
    source.replace(at,4,"evaluate_fragment");
    return source;
}
struct Buffer {VkBuffer buffer;VkDeviceMemory mem;void* mapped;};
static Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage)
{
    Buffer b{};VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=usage;
    checked(vkCreateBuffer(device,&info,nullptr,&b.buffer));VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device,b.buffer,&req);
    b.mem=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checked(vkBindBufferMemory(device,b.buffer,b.mem,0));checked(vkMapMemory(device,b.mem,0,VK_WHOLE_SIZE,0,&b.mapped));
    return b;
}
static void run(VkQueue queue,uint32_t family)
{
    Buffer output=buffer(16*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer constants=buffer(4096,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    VkDescriptorSetLayoutBinding binding[2]={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
                                           {1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=2;sl.pBindings=binding;
    VkDescriptorSetLayout set_layout;checked(vkCreateDescriptorSetLayout(device,&sl,nullptr,&set_layout));
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&set_layout;
    VkPipelineLayout layout;checked(vkCreatePipelineLayout(device,&pl,nullptr,&layout));
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=2;dp.pPoolSizes=sizes;
    VkDescriptorPool pool;checked(vkCreateDescriptorPool(device,&dp,nullptr,&pool));
    VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=pool;sa.descriptorSetCount=1;sa.pSetLayouts=&set_layout;
    VkDescriptorSet set;checked(vkAllocateDescriptorSets(device,&sa,&set));
    VkDescriptorBufferInfo infos[2]={{output.buffer,0,16*16},{constants.buffer,0,4096}};
    VkWriteDescriptorSet writes[2]{};
    for(unsigned i=0;i<2;i++){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;
        writes[i].descriptorCount=1;writes[i].descriptorType=binding[i].descriptorType;writes[i].pBufferInfo=&infos[i];}
    vkUpdateDescriptorSets(device,2,writes,0,nullptr);
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=family;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool command_pool;checked(vkCreateCommandPool(device,&cp,nullptr,&command_pool));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=command_pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer cmd;checked(vkAllocateCommandBuffers(device,&ca,&cmd));
    unsigned cases=0;
    for(unsigned kind=0;kind<8;kind++) {
        u8 program[96]={};u32 count=0;std::string source;
        if(kind<4) {
            unsigned half=kind&1,cc=(kind>>1)&1;
            const u32 dst=half?128u:0u;
            instruction(program,0x01001E00u|dst,2|identity|always);
            for(unsigned lane=0;lane<4;lane++)rsx_test_fp_put_word(program+16+lane*4,initial[lane]);
            /* CC is written from input TC1. Each compute lane supplies one of
             * all 16 sign masks; register 0/1 and r/h destinations are covered. */
            instruction(program+32,0x01001F00u|0x40000000u|(5u<<13),1|identity|always|(cc<<30));
            instruction(program+48,0x01001E01u|dst,2|identity|(1u<<20)|cc_identity|(cc<<31));
            for(unsigned lane=0;lane<4;lane++)rsx_test_fp_put_word(program+64+lane*4,replacement[lane]);
            source=decompile(program,80,half?0:0x40,0,count);assert(count==2);
            assert(source.find("if (_p.x)")!=std::string::npos && source.find("= lerp(")==std::string::npos);
            std::memset(constants.mapped,0,4096);
            std::memcpy(constants.mapped,initial,16);std::memcpy(static_cast<char*>(constants.mapped)+16,replacement,16);
        } else {
            unsigned mode=kind-4;
            instruction(program,0x01001E01u|(4u<<13),1|identity|always,0,(mode&2)?0x80000000u:0);
            source=decompile(program,16,0x40,mode&1,count);assert(!count);
            std::memset(constants.mapped,0,4096);
        }
        source+="\n[[vk::binding(0,0)]] RWStructuredBuffer<uint4> results;\n"
                "[numthreads(16,1,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
                " PSInput p=(PSInput)0; p.position=float4(0.5,0.5,0.5,0.25); p.tc0=float4(2,3,7,8);\n"
                " p.tc1=float4((id.x&1)?1:-1,(id.x&2)?1:-1,(id.x&4)?1:-1,(id.x&8)?1:-1);\n"
                " results[id.x]=asuint(evaluate_fragment(p)); }\n";
        uint32_t words[32768],n=0;char log[4096]={};
        if(rsx_hlsl_to_spirv(source.c_str(),RSX_SHADER_STAGE_COMPUTE,words,32768,&n,log,sizeof log)){
            std::fprintf(stderr,"Compute compilation: %s\n",log);assert(false);}
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=n*4;mi.pCode=words;
        VkShaderModule module;checked(vkCreateShaderModule(device,&mi,nullptr,&module));
        VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=layout;
        pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        pi.stage.module=module;pi.stage.pName="main";VkPipeline pipeline;
        checked(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
        checked(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};checked(vkBeginCommandBuffer(cmd,&cb));
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        vkCmdDispatch(cmd,1,1,1);checked(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        checked(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));checked(vkQueueWaitIdle(queue));
        const uint32_t* actual=static_cast<uint32_t*>(output.mapped);
        for(unsigned mask=0;mask<16;mask++)for(unsigned lane=0;lane<4;lane++) {
            uint32_t expected;
            if(kind<4) {
                expected=(mask&(1u<<lane))?replacement[lane]:initial[lane];
                if((expected&0x7FFFFFFF)>0x7F800000)expected=0; /* Existing output NaN guard. */
            } else {
                const float coords[4][4]={{2,3,7,8},{2,3,0,4},{0.5f,0.75f,1.75f,2},{0.5f,0.75f,0,1}};
                std::memcpy(&expected,&coords[kind-4][lane],4);
            }
            if(actual[mask*4+lane]!=expected){std::fprintf(stderr,"case=%u mask=%u lane=%u got=%08x expected=%08x\n",kind,mask,lane,actual[mask*4+lane],expected);assert(false);}
        }
        cases++;vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,module,nullptr);
    }
    assert(cases==8);
    vkDestroyCommandPool(device,command_pool,nullptr);vkDestroyDescriptorPool(device,pool,nullptr);
    vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
    for(Buffer b:{output,constants}){vkUnmapMemory(device,b.mem);vkDestroyBuffer(device,b.buffer,nullptr);vkFreeMemory(device,b.mem,nullptr);}
    std::puts("512 GPU lane comparisons: masked writes, r/h, CC0/1, nonfinite values, signed zero, 2D inputs and reciprocal-W pass.");
}
int main()
{
    HMODULE library=LoadLibraryA("vulkan-1.dll");assert(library);
#define LOAD(name) name=reinterpret_cast<PFN_##name>(GetProcAddress(library,#name));assert(name);
    FNS(LOAD)
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkInstance instance;
    checked(vkCreateInstance(&ic,nullptr,&instance));uint32_t count=0;
    checked(vkEnumeratePhysicalDevices(instance,&count,nullptr));assert(count);
    std::vector<VkPhysicalDevice> devices(count);checked(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice physical=devices[0];
    for(auto candidate:devices){VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(candidate,&prop);if(prop.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){physical=candidate;break;}}
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());uint32_t family=0;
    while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;assert(family<count);
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
    checked(vkCreateDevice(physical,&di,nullptr,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    run(queue,family);
    checked(vkDeviceWaitIdle(device));vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(library);return 0;
}
