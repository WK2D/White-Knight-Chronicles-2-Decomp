/* Synthetic image -> production D3D12 readback -> exact guest ARGB/RGBA bytes.
 * Includes WKC portrait dimensions, padding, partial pixels and invalid spans. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include "rsx_d3d12_surface_readback.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void checked(HRESULT hr){assert(SUCCEEDED(hr));}
static ID3D12Resource* upload_buffer(ID3D12Device* device,UINT64 bytes)
{
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC d={0};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
    d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* result=NULL;checked(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&d,
        D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void**)&result));return result;
}
int main(void)
{
    assert(rsx_d3d12_surface_readback_layout(8,1,1024,256,144,0x24000));
    assert(!rsx_d3d12_surface_readback_layout(5,1,1024,256,144,0x24000));
    assert(!rsx_d3d12_surface_readback_layout(8,2,1024,256,144,0x24000));
    assert(!rsx_d3d12_surface_readback_layout(8,1,512,256,144,0x24000));
    assert(!rsx_d3d12_surface_readback_layout(8,1,1024,256,144,0x24001));
    IDXGIFactory4* factory=NULL;IDXGIAdapter* warp=NULL;ID3D12Device* device=NULL;
    checked(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    checked(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&warp));
    checked(D3D12CreateDevice((IUnknown*)warp,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
    ID3D12CommandQueue* queue=NULL;ID3D12CommandAllocator* allocator=NULL;ID3D12GraphicsCommandList* list=NULL;
    D3D12_COMMAND_QUEUE_DESC q={0};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    checked(device->lpVtbl->CreateCommandQueue(device,&q,&IID_ID3D12CommandQueue,(void**)&queue));
    checked(device->lpVtbl->CreateCommandAllocator(device,q.Type,&IID_ID3D12CommandAllocator,(void**)&allocator));
    checked(device->lpVtbl->CreateCommandList(device,0,q.Type,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));checked(list->lpVtbl->Close(list));
    ID3D12Fence* fence=NULL;checked(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));
    HANDLE event=CreateEventA(NULL,FALSE,FALSE,NULL);assert(event);
    const struct {unsigned w,h,pitch,bytes;int rgba;} cases[]={
        {17,13,80,1040,0},{17,13,80,1040,1},{17,13,80,1,0},{17,13,80,3,0},
        {17,13,80,7,1},{17,13,80,85,0},{256,144,1024,0x24000,0},{1,1,8,8,0}
    };
    for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c){
        const unsigned w=cases[c].w,h=cases[c].h,pitch=cases[c].pitch,span=pitch*h;
        D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d={0};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ID3D12Resource* source=NULL;checked(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&source));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;UINT64 size;device->lpVtbl->GetCopyableFootprints(device,&d,0,1,0,&fp,NULL,NULL,&size);
        ID3D12Resource* upload=upload_buffer(device,size);uint8_t* pixels=NULL;D3D12_RANGE empty={0,0};
        checked(upload->lpVtbl->Map(upload,0,&empty,(void**)&pixels));
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) for(unsigned k=0;k<4;++k)
            pixels[fp.Offset+y*fp.Footprint.RowPitch+x*4+k]=(uint8_t)(x*17+y*31+k*53);
        upload->lpVtbl->Unmap(upload,0,NULL);
        checked(allocator->lpVtbl->Reset(allocator));checked(list->lpVtbl->Reset(list,allocator,NULL));
        D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        dst.pResource=source;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->lpVtbl->CopyTextureRegion(list,&dst,0,0,0,&src,NULL);
        D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource=source;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;list->lpVtbl->ResourceBarrier(list,1,&b);
        rsx_d3d12_surface_readback rb={0};checked(rsx_d3d12_surface_readback_record(&rb,device,list,source));
        assert(rsx_d3d12_surface_readback_record(&rb,device,list,source)==E_INVALIDARG);
        checked(list->lpVtbl->Close(list));ID3D12CommandList* lists[]={(ID3D12CommandList*)list};queue->lpVtbl->ExecuteCommandLists(queue,1,lists);
        checked(queue->lpVtbl->Signal(queue,fence,c+1));checked(fence->lpVtbl->SetEventOnCompletion(fence,c+1,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
        uint8_t* guarded=malloc(span+32);assert(guarded);memset(guarded,0xA5,span+32);
        checked(rsx_d3d12_surface_readback_store(&rb,guarded+16,cases[c].bytes,pitch,cases[c].rgba));
        const unsigned argb[]={3,0,1,2};
        for(unsigned i=0;i<span;++i){
            unsigned expected=0xA5;
            const unsigned y=i/pitch,col=i%pitch;
            if(i<cases[c].bytes && col<w*4){const unsigned k=cases[c].rgba?col%4:argb[col%4];expected=(uint8_t)((col/4)*17+y*31+k*53);}
            assert(guarded[16+i]==expected);
        }
        for(unsigned i=0;i<16;++i) assert(guarded[i]==0xA5 && guarded[span+16+i]==0xA5);
        memset(guarded,0xA5,span+32);
        assert(rsx_d3d12_surface_readback_store(&rb,guarded+16,span+1,pitch,0)==E_INVALIDARG);
        assert(rsx_d3d12_surface_readback_store(&rb,guarded+16,1,w*4-1,0)==E_INVALIDARG);
        assert(rsx_d3d12_surface_readback_store(&rb,guarded+16,0,pitch,0)==E_INVALIDARG);
        for(unsigned i=0;i<span+32;++i) assert(guarded[i]==0xA5);
        free(guarded);rsx_d3d12_surface_readback_release(&rb);assert(!rb.buffer);
        upload->lpVtbl->Release(upload);source->lpVtbl->Release(source);
    }
    uint8_t a[16],rgba[16];memset(a,0xA5,sizeof a);memset(rgba,0x42,sizeof rgba);
    assert(!rsx_rgba8_guest_range(a,4,rgba,15,8,2,2,8,0));
    assert(!rsx_rgba8_guest_range(a,4,rgba,16,8,UINT32_MAX,2,8,0));
    for(unsigned i=0;i<16;++i) assert(a[i]==0xA5);
    list->lpVtbl->Release(list);allocator->lpVtbl->Release(allocator);queue->lpVtbl->Release(queue);fence->lpVtbl->Release(fence);CloseHandle(event);
    device->lpVtbl->Release(device);warp->lpVtbl->Release(warp);factory->lpVtbl->Release(factory);
    puts("Eight WARP guest readback cases: exact ARGB/RGBA, portrait size, padding, partial pixels, bounds and guards pass.");return 0;
}
