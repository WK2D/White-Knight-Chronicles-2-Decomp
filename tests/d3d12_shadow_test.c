/* Production sampler decoding on real WARP comparisons: direction, equality,
 * point/PCF filtering and border addressing. Synthetic data only. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include "rsx_d3d12_sampler.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void checked(HRESULT hr) { assert(SUCCEEDED(hr)); }
static ID3D12Resource* buffer(ID3D12Device* d, UINT64 bytes, D3D12_HEAP_TYPE type,
                             D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp={0}; hp.Type=type;
    D3D12_RESOURCE_DESC r={0}; r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    r.Width=bytes; r.Height=1; r.DepthOrArraySize=1; r.MipLevels=1;
    r.SampleDesc.Count=1; r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; r.Flags=flags;
    ID3D12Resource* out=NULL;
    checked(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&r,state,NULL,&IID_ID3D12Resource,(void**)&out));
    return out;
}
static int guest_compare(unsigned op,float texel,float ref)
{
    switch(op){case 1:return texel<ref;case 2:return texel==ref;case 3:return texel<=ref;
    case 4:return texel>ref;case 5:return texel!=ref;case 6:return texel>=ref;default:assert(0);return 0;}
}
static void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore=from;b.Transition.StateAfter=to;l->lpVtbl->ResourceBarrier(l,1,&b);
}
int main(void)
{
    ID3D12Debug* debug=NULL;
    if(SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug))){debug->lpVtbl->EnableDebugLayer(debug);debug->lpVtbl->Release(debug);}
    IDXGIFactory4* factory=NULL;IDXGIAdapter* warp=NULL;ID3D12Device* d=NULL;
    checked(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    checked(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&warp));
    checked(D3D12CreateDevice((IUnknown*)warp,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&d));
    ID3D12InfoQueue* info=NULL;d->lpVtbl->QueryInterface(d,&IID_ID3D12InfoQueue,(void**)&info);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support={0};support.Format=DXGI_FORMAT_R32_FLOAT;
    checked(d->lpVtbl->CheckFeatureSupport(d,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof support));
    assert(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON);
    ID3D12CommandQueue* q=NULL;ID3D12CommandAllocator* a=NULL;ID3D12GraphicsCommandList* l=NULL;
    D3D12_COMMAND_QUEUE_DESC qd={0};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    checked(d->lpVtbl->CreateCommandQueue(d,&qd,&IID_ID3D12CommandQueue,(void**)&q));
    checked(d->lpVtbl->CreateCommandAllocator(d,qd.Type,&IID_ID3D12CommandAllocator,(void**)&a));
    checked(d->lpVtbl->CreateCommandList(d,0,qd.Type,a,NULL,&IID_ID3D12GraphicsCommandList,(void**)&l));
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td={0};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width=2;td.Height=2;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.SampleDesc.Count=1;
    ID3D12Resource* texture=NULL;
    checked(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&texture));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;UINT64 bytes;
    d->lpVtbl->GetCopyableFootprints(d,&td,0,1,0,&fp,NULL,NULL,&bytes);
    ID3D12Resource* upload=buffer(d,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
    unsigned char* mapped=NULL;D3D12_RANGE empty={0,0};checked(upload->lpVtbl->Map(upload,0,&empty,(void**)&mapped));
    const float row[2]={.25f,.75f};for(unsigned y=0;y<2;++y)memcpy(mapped+fp.Offset+y*fp.Footprint.RowPitch,row,sizeof row);
    upload->lpVtbl->Unmap(upload,0,NULL);
    D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
    dst.pResource=texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->lpVtbl->CopyTextureRegion(l,&dst,0,0,0,&src,NULL);
    barrier(l,texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12Resource* output=buffer(d,144*sizeof(float),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Resource* readback=buffer(d,144*sizeof(float),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12DescriptorHeap* images=NULL;ID3D12DescriptorHeap* samplers=NULL;
    D3D12_DESCRIPTOR_HEAP_DESC hd={0};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    checked(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&images));
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;hd.NumDescriptors=12;
    checked(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&samplers));
    D3D12_CPU_DESCRIPTOR_HANDLE ic,sc;D3D12_GPU_DESCRIPTOR_HANDLE ig,sg;
    images->lpVtbl->GetCPUDescriptorHandleForHeapStart(images,&ic);images->lpVtbl->GetGPUDescriptorHandleForHeapStart(images,&ig);
    samplers->lpVtbl->GetCPUDescriptorHandleForHeapStart(samplers,&sc);samplers->lpVtbl->GetGPUDescriptorHandleForHeapStart(samplers,&sg);
    const UINT is=d->lpVtbl->GetDescriptorHandleIncrementSize(d,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT ss=d->lpVtbl->GetDescriptorHandleIncrementSize(d,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    d->lpVtbl->CreateShaderResourceView(d,texture,NULL,ic);ic.ptr+=is;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv={0};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=144;uv.Buffer.StructureByteStride=sizeof(float);
    d->lpVtbl->CreateUnorderedAccessView(d,output,NULL,&uv,ic);
    D3D12_DESCRIPTOR_RANGE ranges[3]={{0}};D3D12_ROOT_PARAMETER params[4]={{0}};
    for(unsigned i=0;i<3;++i){ranges[i].RangeType=i==0?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:i==1?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ranges[i].NumDescriptors=1;params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable.NumDescriptorRanges=1;params[i].DescriptorTable.pDescriptorRanges=&ranges[i];}
    params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[3].Constants.Num32BitValues=1;
    D3D12_ROOT_SIGNATURE_DESC rd={0};rd.NumParameters=4;rd.pParameters=params;ID3DBlob *blob=NULL,*err=NULL;
    checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&err));ID3D12RootSignature* root=NULL;
    checked(d->lpVtbl->CreateRootSignature(d,0,blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob),&IID_ID3D12RootSignature,(void**)&root));blob->lpVtbl->Release(blob);
    const char* shader="Texture2D<float> depth:register(t0); SamplerComparisonState samp:register(s0); RWStructuredBuffer<float> result:register(u0); cbuffer C:register(b0){uint base;} [numthreads(12,1,1)] void main(uint3 id:SV_DispatchThreadID){float2 uv[4]={float2(.25,.25),float2(.75,.25),float2(.5,.5),float2(-1,.25)};result[base+id.x]=depth.SampleCmpLevelZero(samp,uv[id.x%4],.25*(1+id.x/4));}";
    HRESULT hr=D3DCompile(shader,strlen(shader),NULL,NULL,NULL,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&err);
    if(FAILED(hr)&&err)fprintf(stderr,"%s\n",(char*)err->lpVtbl->GetBufferPointer(err));checked(hr);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={0};pd.pRootSignature=root;pd.CS.pShaderBytecode=blob->lpVtbl->GetBufferPointer(blob);pd.CS.BytecodeLength=blob->lpVtbl->GetBufferSize(blob);
    ID3D12PipelineState* pipeline=NULL;checked(d->lpVtbl->CreateComputePipelineState(d,&pd,&IID_ID3D12PipelineState,(void**)&pipeline));blob->lpVtbl->Release(blob);
    ID3D12DescriptorHeap* heaps[]={images,samplers};l->lpVtbl->SetDescriptorHeaps(l,2,heaps);l->lpVtbl->SetComputeRootSignature(l,root);l->lpVtbl->SetPipelineState(l,pipeline);
    l->lpVtbl->SetComputeRootDescriptorTable(l,0,ig);ig.ptr+=is;l->lpVtbl->SetComputeRootDescriptorTable(l,1,ig);
    rsx_dsp_texture t={0};t.enabled=1;t.dimension=2;t.mipmaps=1;t.format=RSX_TEX_FMT_DEPTH24_D8;t.width=2;t.height=2;
    for(unsigned filter=0;filter<2;++filter)for(unsigned op=1;op<=6;++op){
        const unsigned index=filter*6+op-1;t.wrap=4u|(4u<<8)|(op<<28);t.filter=(filter?2u:1u)*0x01010000u;
        const D3D12_SAMPLER_DESC sd=rsx_d3d12_sampler_decode(&t,16);assert(rsx_d3d12_texture_is_shadow(&t));
        D3D12_CPU_DESCRIPTOR_HANDLE c=sc;c.ptr+=index*ss;d->lpVtbl->CreateSampler(d,&sd,c);
        D3D12_GPU_DESCRIPTOR_HANDLE g=sg;g.ptr+=index*ss;l->lpVtbl->SetComputeRootDescriptorTable(l,2,g);
        l->lpVtbl->SetComputeRoot32BitConstant(l,3,index*12,0);l->lpVtbl->Dispatch(l,1,1,1);
    }
    barrier(l,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->lpVtbl->CopyBufferRegion(l,readback,0,output,0,144*sizeof(float));checked(l->lpVtbl->Close(l));
    ID3D12CommandList* lists[]={(ID3D12CommandList*)l};q->lpVtbl->ExecuteCommandLists(q,1,lists);
    ID3D12Fence* fence=NULL;checked(d->lpVtbl->CreateFence(d,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));
    HANDLE event=CreateEventA(NULL,FALSE,FALSE,NULL);checked(q->lpVtbl->Signal(q,fence,1));checked(fence->lpVtbl->SetEventOnCompletion(fence,1,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    float* results=NULL;checked(readback->lpVtbl->Map(readback,0,NULL,(void**)&results));
    for(unsigned filter=0;filter<2;++filter)for(unsigned op=1;op<=6;++op)for(unsigned r=0;r<3;++r)for(unsigned coord=0;coord<4;++coord){
        const float ref=.25f*(r+1);float expected;
        if(coord==2&&filter)expected=.5f*(guest_compare(op,.25f,ref)+guest_compare(op,.75f,ref));
        else expected=(float)guest_compare(op,coord==0?.25f:coord==3?0:.75f,ref);
        const float actual=results[(filter*6+op-1)*12+r*4+coord];
        if(fabsf(actual-expected)>.001f){fprintf(stderr,"filter=%u zfunc=%u ref=%f coord=%u actual=%f expected=%f\n",filter,op,ref,coord,actual,expected);assert(0);}
    }
    readback->lpVtbl->Unmap(readback,0,&empty);
    /* Raw modes, alpha kill, cube and mipmapped maps must not become comparison samplers. */
    t.wrap=0;assert(!rsx_d3d12_texture_is_shadow(&t));t.wrap=7u<<28;assert(!rsx_d3d12_texture_is_shadow(&t));
    t.wrap=1u<<28;t.control0=4;assert(!rsx_d3d12_texture_is_shadow(&t));t.control0=0;t.cubemap=1;assert(!rsx_d3d12_texture_is_shadow(&t));
    t.cubemap=0;t.mipmaps=2;assert(!rsx_d3d12_texture_is_shadow(&t));t.mipmaps=1;
    D3D12_SAMPLER_DESC first=rsx_d3d12_sampler_decode(&t,1);t.wrap=4u<<28;D3D12_SAMPLER_DESC other=rsx_d3d12_sampler_decode(&t,1);assert(!rsx_d3d12_sampler_equal(&first,&other));
    if(info){for(UINT64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);++i){SIZE_T size=0;checked(info->lpVtbl->GetMessage(info,i,NULL,&size));D3D12_MESSAGE* m=malloc(size);assert(m);checked(info->lpVtbl->GetMessage(info,i,m,&size));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"D3D validation: %s\n",m->pDescription);assert(0);}free(m);}info->lpVtbl->Release(info);}
    fence->lpVtbl->Release(fence);CloseHandle(event);pipeline->lpVtbl->Release(pipeline);root->lpVtbl->Release(root);
    samplers->lpVtbl->Release(samplers);images->lpVtbl->Release(images);readback->lpVtbl->Release(readback);output->lpVtbl->Release(output);upload->lpVtbl->Release(upload);texture->lpVtbl->Release(texture);
    l->lpVtbl->Release(l);a->lpVtbl->Release(a);q->lpVtbl->Release(q);d->lpVtbl->Release(d);warp->lpVtbl->Release(warp);factory->lpVtbl->Release(factory);
    puts("DirectX shadow comparisons: 144 WARP results match guest ordering, equality, PCF and borders.");return 0;
}
