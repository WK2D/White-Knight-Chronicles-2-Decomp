/* Production polygon bias decoding checked against GPU D32 depths on WARP. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include "rsx_d3d12_depth_bias.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
static struct {
    ID3D12Device* device;
    ID3D12CommandQueue* cmd_queue;
    ID3D12CommandAllocator* cmd_allocators[1];
    ID3D12GraphicsCommandList* cmd_list;
    ID3D12Fence* fence;
    HANDLE fence_event;
    ID3D12Resource* depth_buffer;
    ID3D12DescriptorHeap* rtv_heap;
    ID3D12DescriptorHeap* dsv_heap;
} s_d3d;
#include <assert.h>

static void checked(HRESULT hr) { assert(SUCCEEDED(hr)); }
static ID3D12Resource* texture(DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                              D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d = {0};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = 8; d.Height = 8; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = format; d.SampleDesc.Count = 1; d.Flags = flags;
    ID3D12Resource* r = NULL;
    checked(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device,
        &hp, D3D12_HEAP_FLAG_NONE, &d, state, NULL, &IID_ID3D12Resource, (void**)&r));
    return r;
}
static D3D12_CPU_DESCRIPTOR_HANDLE heap(D3D12_DESCRIPTOR_HEAP_TYPE type,
                                      ID3D12DescriptorHeap** result)
{
    D3D12_DESCRIPTOR_HEAP_DESC d = {0}; d.Type = type; d.NumDescriptors = 1;
    checked(s_d3d.device->lpVtbl->CreateDescriptorHeap(s_d3d.device, &d,
        &IID_ID3D12DescriptorHeap, (void**)result));
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    (*result)->lpVtbl->GetCPUDescriptorHandleForHeapStart(*result, &h);
    return h;
}
static void transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from,
                       D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {0}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
}
static ID3D12Resource* readback(ID3D12Resource* r, UINT plane,
                               D3D12_PLACED_SUBRESOURCE_FOOTPRINT* footprint)
{
    D3D12_RESOURCE_DESC desc; r->lpVtbl->GetDesc(r, &desc);
    UINT64 bytes;
    s_d3d.device->lpVtbl->GetCopyableFootprints(s_d3d.device, &desc, plane, 1,
        0, footprint, NULL, NULL, &bytes);
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC d = {0}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* out = NULL;
    checked(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
        D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
        &IID_ID3D12Resource, (void**)&out));
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = out; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = *footprint;
    src.pResource = r; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = plane;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    return out;
}

int main(void)
{
    IDXGIFactory4* factory = NULL; IDXGIAdapter* warp = NULL;
    checked(CreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&factory));
    checked(factory->lpVtbl->EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void**)&warp));
    checked(D3D12CreateDevice((IUnknown*)warp, D3D_FEATURE_LEVEL_11_0,
        &IID_ID3D12Device, (void**)&s_d3d.device));
    D3D12_COMMAND_QUEUE_DESC q = {0}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    checked(s_d3d.device->lpVtbl->CreateCommandQueue(s_d3d.device, &q,
        &IID_ID3D12CommandQueue, (void**)&s_d3d.cmd_queue));
    checked(s_d3d.device->lpVtbl->CreateCommandAllocator(s_d3d.device,
        q.Type, &IID_ID3D12CommandAllocator, (void**)&s_d3d.cmd_allocators[0]));
    checked(s_d3d.device->lpVtbl->CreateCommandList(s_d3d.device, 0, q.Type,
        s_d3d.cmd_allocators[0], NULL, &IID_ID3D12GraphicsCommandList, (void**)&s_d3d.cmd_list));
    checked(s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list));
    checked(s_d3d.device->lpVtbl->CreateFence(s_d3d.device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void**)&s_d3d.fence));
    s_d3d.fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    ID3D12Resource* color = texture(DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    s_d3d.depth_buffer = texture(DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &s_d3d.rtv_heap);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &s_d3d.dsv_heap);
    s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, color, NULL, rtv);
    s_d3d.device->lpVtbl->CreateDepthStencilView(s_d3d.device, s_d3d.depth_buffer, NULL, dsv);


    D3D12_ROOT_SIGNATURE_DESC rd={0};ID3DBlob *serialized=NULL,*errors=NULL;
    checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
    ID3D12RootSignature* root=NULL;checked(s_d3d.device->lpVtbl->CreateRootSignature(s_d3d.device,0,
        serialized->lpVtbl->GetBufferPointer(serialized),serialized->lpVtbl->GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)&root));serialized->lpVtbl->Release(serialized);
    const char* vs="float4 main(uint id:SV_VertexID):SV_Position{float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],.5,1);}";
    const char* ps="float4 main():SV_Target{return 1;}";ID3DBlob *vb=NULL,*pb=NULL;
    checked(D3DCompile(vs,strlen(vs),NULL,NULL,NULL,"main","vs_5_0",0,0,&vb,&errors));
    checked(D3DCompile(ps,strlen(ps),NULL,NULL,NULL,"main","ps_5_0",0,0,&pb,&errors));
    const struct {unsigned primitive,enabled;float slope,bias;} cases[]={
        {5,1,0,0},{5,1,0,-100},{5,1,0,100},{5,0,0,-100},
        {1,1,0,-100},{2,1,0,-100},{3,1,0,-100},{4,1,0,-100},
        {6,1,0,-50},{7,1,0,-150},{8,1,0,-200},{9,1,0,50},{10,1,0,200},
        {5,1,0,1.5f},{5,1,0,-1.5f},{5,1,2,0},
    };
    for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c){
        u32 scale,bias;memcpy(&scale,&cases[c].slope,4);memcpy(&bias,&cases[c].bias,4);
        const rsx_d3d12_depth_bias state=rsx_d3d12_depth_bias_decode(cases[c].primitive,cases[c].enabled,scale,bias);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={0};pd.pRootSignature=root;
        pd.VS.pShaderBytecode=vb->lpVtbl->GetBufferPointer(vb);pd.VS.BytecodeLength=vb->lpVtbl->GetBufferSize(vb);
        pd.PS.pShaderBytecode=pb->lpVtbl->GetBufferPointer(pb);pd.PS.BytecodeLength=pb->lpVtbl->GetBufferSize(pb);
        pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
        pd.RasterizerState.DepthBias=state.constant;pd.RasterizerState.SlopeScaledDepthBias=state.slope;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask=15;pd.SampleMask=UINT_MAX;
        pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        pd.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;pd.SampleDesc.Count=1;ID3D12PipelineState* pipeline=NULL;
        checked(s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device,&pd,&IID_ID3D12PipelineState,(void**)&pipeline));
        checked(s_d3d.cmd_allocators[0]->lpVtbl->Reset(s_d3d.cmd_allocators[0]));checked(s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list,s_d3d.cmd_allocators[0],pipeline));
        s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(s_d3d.cmd_list,dsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0,0,NULL);
        s_d3d.cmd_list->lpVtbl->SetGraphicsRootSignature(s_d3d.cmd_list,root);s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list,1,&rtv,FALSE,&dsv);
        D3D12_VIEWPORT vp={0,0,8,8,0,1};D3D12_RECT sc={0,0,8,8};s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list,1,&vp);s_d3d.cmd_list->lpVtbl->RSSetScissorRects(s_d3d.cmd_list,1,&sc);
        s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,3,1,0,0);
        transition(s_d3d.depth_buffer,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
        ID3D12Resource* rb=readback(s_d3d.depth_buffer,0,&fp);transition(s_d3d.depth_buffer,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        checked(s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list));ID3D12CommandList* lists[]={(ID3D12CommandList*)s_d3d.cmd_list};s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue,1,lists);
        checked(s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue,s_d3d.fence,c+1));checked(s_d3d.fence->lpVtbl->SetEventOnCompletion(s_d3d.fence,c+1,s_d3d.fence_event));assert(WaitForSingleObject(s_d3d.fence_event,10000)==WAIT_OBJECT_0);
        u8* pixels;checked(rb->lpVtbl->Map(rb,0,NULL,(void**)&pixels));
        /* Independent oracle: D32 at .5 has 23 fraction bits and exponent -1.
         * Bias is applied before depth conversion, so the multiplier is 2^-24. */
        int expected_bias=0;if(cases[c].enabled&&cases[c].primitive>=5&&cases[c].primitive<=10)expected_bias=(int)roundf(cases[c].bias);
        const float expected=.5f+expected_bias/16777216.0f;
        assert(fp.Footprint.Format==DXGI_FORMAT_R32_FLOAT || fp.Footprint.Format==DXGI_FORMAT_R32_TYPELESS); const unsigned pixel_stride=4;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){float actual;memcpy(&actual,pixels+fp.Offset+y*fp.Footprint.RowPitch+x*pixel_stride,4);
            if(fabsf(actual-expected)>0.00000012f){fprintf(stderr,"case=%u x=%u y=%u got=%.9g expected=%.9g footprint=%u\n",c,x,y,actual,expected,fp.Footprint.Format);assert(0);}}
        D3D12_RANGE empty={0,0};rb->lpVtbl->Unmap(rb,0,&empty);rb->lpVtbl->Release(rb);pipeline->lpVtbl->Release(pipeline);
    }
    u32 nan=0x7fc00000,infinity=0x7f800000;assert(rsx_d3d12_depth_bias_decode(5,1,nan,0).constant==0);assert(rsx_d3d12_depth_bias_decode(5,1,0,infinity).constant==0);
    assert(rsx_d3d12_depth_bias_decode(5,1,0,0x7f7fffff).constant==INT_MAX);assert(rsx_d3d12_depth_bias_decode(5,1,0,0xff7fffff).constant==INT_MIN);
    vb->lpVtbl->Release(vb);pb->lpVtbl->Release(pb);root->lpVtbl->Release(root);
    color->lpVtbl->Release(color);s_d3d.depth_buffer->lpVtbl->Release(s_d3d.depth_buffer);s_d3d.rtv_heap->lpVtbl->Release(s_d3d.rtv_heap);s_d3d.dsv_heap->lpVtbl->Release(s_d3d.dsv_heap);
    s_d3d.cmd_list->lpVtbl->Release(s_d3d.cmd_list);s_d3d.cmd_allocators[0]->lpVtbl->Release(s_d3d.cmd_allocators[0]);s_d3d.fence->lpVtbl->Release(s_d3d.fence);CloseHandle(s_d3d.fence_event);
    s_d3d.cmd_queue->lpVtbl->Release(s_d3d.cmd_queue);s_d3d.device->lpVtbl->Release(s_d3d.device);warp->lpVtbl->Release(warp);factory->lpVtbl->Release(factory);
    puts("DirectX D32 polygon bias: sixteen WARP cases cover sign, units, disable, primitive isolation and rounding.");return 0;
}
