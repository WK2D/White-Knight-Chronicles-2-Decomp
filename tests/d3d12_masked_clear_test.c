/* Production masked color clears on WARP: every RGBA mask on RGBA8/FP16,
 * clipping/empty scissors and independent depth/stencil preservation. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include "rsx_d3d12_masked_clear.h"
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
    s_d3d.depth_buffer = texture(DXGI_FORMAT_D24_UNORM_S8_UINT,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &s_d3d.rtv_heap);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &s_d3d.dsv_heap);
    s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, color, NULL, rtv);
    s_d3d.device->lpVtbl->CreateDepthStencilView(s_d3d.device, s_d3d.depth_buffer, NULL, dsv);

    rsx_d3d12_masked_clear cache={0};
    const float initial[4]={.125f,.25f,.5f,.75f},replacement[4]={.875f,.75f,.25f,.5f};
    const unsigned char initial8[4]={32,64,128,191},replace8[4]={223,191,64,128};
    const unsigned short initial16[4]={0x3000,0x3400,0x3800,0x3A00},replace16[4]={0x3B00,0x3A00,0x3400,0x3800};
    const unsigned rects[4][4]={{0,0,8,8},{2,3,4,3},{7,6,100,100},{9,0,3,2}};
    UINT64 serial=0;
    ID3D12PipelineState* remembered[2][16]={{0}};
    for(unsigned f=0;f<2;++f){
        if(f){color->lpVtbl->Release(color);color=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device,color,NULL,rtv);}
        for(unsigned mask=0;mask<16;++mask)for(unsigned rect=0;rect<4;++rect){
            checked(s_d3d.cmd_allocators[0]->lpVtbl->Reset(s_d3d.cmd_allocators[0]));
            checked(s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list,s_d3d.cmd_allocators[0],NULL));
            s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list,rtv,initial,0,NULL);
            s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(s_d3d.cmd_list,dsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.2f,0x5A,0,NULL);
            /* Bind depth first: a partial color clear must detach it, not
             * inherit a stale DSV/depth comparison from the guest pass. */
            s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list,1,&rtv,FALSE,&dsv);
            const rsx_surface_clear clear=rsx_surface_clear_make(mask<<4,1,0xA5,rects[rect][0],rects[rect][1],rects[rect][2],rects[rect][3],8,8);
            checked(rsx_d3d12_clear_masked(&cache,s_d3d.device,s_d3d.cmd_list,&clear,replacement,rtv,8,8,
                f?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM));
            if(mask && mask!=15 && rect!=3){
                if(!remembered[f][mask])remembered[f][mask]=cache.pipelines[f][mask];
                assert(remembered[f][mask] && cache.pipelines[f][mask]==remembered[f][mask]);
            }
            if(f==0 && mask==0)assert(!cache.root);
            transition(color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(s_d3d.depth_buffer,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[3];
            ID3D12Resource* rb[3]={readback(color,0,&fp[0]),readback(s_d3d.depth_buffer,0,&fp[1]),readback(s_d3d.depth_buffer,1,&fp[2])};
            transition(color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
            transition(s_d3d.depth_buffer,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
            checked(s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list));ID3D12CommandList* lists[]={(ID3D12CommandList*)s_d3d.cmd_list};
            s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue,1,lists);
            checked(s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue,s_d3d.fence,++serial));
            checked(s_d3d.fence->lpVtbl->SetEventOnCompletion(s_d3d.fence,serial,s_d3d.fence_event));assert(WaitForSingleObject(s_d3d.fence_event,10000)==WAIT_OBJECT_0);
            u8* pixels[3];for(unsigned i=0;i<3;++i)checked(rb[i]->lpVtbl->Map(rb[i],0,NULL,(void**)&pixels[i]));
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
                const int inside=x>=clear.x && y>=clear.y && x<(uint64_t)clear.x+clear.w && y<(uint64_t)clear.y+clear.h;
                const u8* p=pixels[0]+fp[0].Offset+y*fp[0].Footprint.RowPitch+x*(f?8:4);
                for(unsigned component=0;component<4;++component){
                    const int changed=inside && (mask&(1u<<component));
                    if(f){unsigned short value;memcpy(&value,p+component*2,2);assert(value==(changed?replace16[component]:initial16[component]));}
                    else if(p[component]!=(changed?replace8[component]:initial8[component])){
                        fprintf(stderr,"f=%u mask=%u rect=%u pixel=%u,%u c=%u got=%u\n",f,mask,rect,x,y,component,p[component]);assert(0);
                    }
                }
                u32 z;memcpy(&z,pixels[1]+fp[1].Offset+y*fp[1].Footprint.RowPitch+x*4,4);
                assert(fabsf((z&0xFFFFFFu)/(float)0xFFFFFFu-.2f)<0.000001f);
                assert(*(pixels[2]+fp[2].Offset+y*fp[2].Footprint.RowPitch+x)==0x5A);
            }
            D3D12_RANGE no_writes={0,0};for(unsigned i=0;i<3;++i){rb[i]->lpVtbl->Unmap(rb[i],0,&no_writes);rb[i]->lpVtbl->Release(rb[i]);}
        }
    }
    assert(serial==128);rsx_d3d12_masked_clear_release(&cache);
    color->lpVtbl->Release(color);
    s_d3d.depth_buffer->lpVtbl->Release(s_d3d.depth_buffer);
    s_d3d.rtv_heap->lpVtbl->Release(s_d3d.rtv_heap);
    s_d3d.dsv_heap->lpVtbl->Release(s_d3d.dsv_heap);
    s_d3d.cmd_list->lpVtbl->Release(s_d3d.cmd_list);
    s_d3d.cmd_allocators[0]->lpVtbl->Release(s_d3d.cmd_allocators[0]);
    s_d3d.fence->lpVtbl->Release(s_d3d.fence);
    CloseHandle(s_d3d.fence_event);
    s_d3d.cmd_queue->lpVtbl->Release(s_d3d.cmd_queue);
    s_d3d.device->lpVtbl->Release(s_d3d.device);
    warp->lpVtbl->Release(warp); factory->lpVtbl->Release(factory);
    puts("Masked clears: 128 WARP cases, every color mask, RGBA8/FP16, clipped/empty rectangles and preserved depth/stencil pass");
    return 0;
}
