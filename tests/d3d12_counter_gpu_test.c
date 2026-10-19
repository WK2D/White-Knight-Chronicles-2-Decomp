/* Production overlay copies on WARP: exact RGBA readback, transparent spaces,
 * Off, clipping, tiny targets and upload reuse after a completed GPU fence. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include "rsx_d3d12_counter.h"
#include <assert.h>
#include <stdio.h>

static void checked(HRESULT hr){assert(SUCCEEDED(hr));}
static void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
    D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=resource;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore=from;b.Transition.StateAfter=to;
    list->lpVtbl->ResourceBarrier(list,1,&b);
}
int main(void)
{
    IDXGIFactory4* factory=NULL;IDXGIAdapter* warp=NULL;ID3D12Device* device=NULL;
    checked(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    checked(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&warp));
    checked(D3D12CreateDevice((IUnknown*)warp,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
    ID3D12CommandQueue* queue=NULL;ID3D12CommandAllocator* allocator=NULL;ID3D12GraphicsCommandList* list=NULL;
    D3D12_COMMAND_QUEUE_DESC q={0};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    checked(device->lpVtbl->CreateCommandQueue(device,&q,&IID_ID3D12CommandQueue,(void**)&queue));
    checked(device->lpVtbl->CreateCommandAllocator(device,q.Type,&IID_ID3D12CommandAllocator,(void**)&allocator));
    checked(device->lpVtbl->CreateCommandList(device,0,q.Type,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));
    checked(list->lpVtbl->Close(list));
    ID3D12Fence* fence=NULL;checked(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));
    HANDLE event=CreateEventA(NULL,FALSE,FALSE,NULL);assert(event);
    ID3D12DescriptorHeap* rtv=NULL;D3D12_DESCRIPTOR_HEAP_DESC hd={0};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    checked(device->lpVtbl->CreateDescriptorHeap(device,&hd,&IID_ID3D12DescriptorHeap,(void**)&rtv));
    D3D12_CPU_DESCRIPTOR_HANDLE handle;rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv,&handle);
    rsx_d3d12_counter counter={0};
    const struct {unsigned width,height;int enabled;double fps;} cases[]={
        {256,64,0,29.9},{7,7,1,29.9},{256,64,1,29.9},{31,19,1,9.9},
        {256,64,0,29.9},{256,64,1,120.0},{9,9,1,0.0}
    };
    ID3D12Resource* original_upload=NULL;
    for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c){
        const unsigned w=cases[c].width,h=cases[c].height;
        D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d={0};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ID3D12Resource* color=NULL;
        checked(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&color));
        device->lpVtbl->CreateRenderTargetView(device,color,NULL,handle);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;UINT64 bytes;
        device->lpVtbl->GetCopyableFootprints(device,&d,0,1,0,&footprint,NULL,NULL,&bytes);
        hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd={0};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* readback=NULL;
        checked(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&readback));
        checked(allocator->lpVtbl->Reset(allocator));checked(list->lpVtbl->Reset(list,allocator,NULL));
        const float clear[4]={8.0f/255.0f,32.0f/255.0f,64.0f/255.0f,1.0f};
        list->lpVtbl->ClearRenderTargetView(list,handle,clear,0,NULL);
        transition(list,color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
        checked(rsx_d3d12_counter_draw(&counter,device,list,color,w,h,cases[c].enabled,cases[c].fps));
        if(c<2) assert(!counter.upload && !counter.mapped);
        if(c==2) original_upload=counter.upload;
        if(c>=2) assert(original_upload && counter.upload==original_upload);
        transition(list,color,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=color;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=readback;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        list->lpVtbl->CopyTextureRegion(list,&dst,0,0,0,&src,NULL);
        checked(list->lpVtbl->Close(list));ID3D12CommandList* lists[]={(ID3D12CommandList*)list};queue->lpVtbl->ExecuteCommandLists(queue,1,lists);
        checked(queue->lpVtbl->Signal(queue,fence,c+1));checked(fence->lpVtbl->SetEventOnCompletion(fence,c+1,event));
        assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
        uint8_t bitmap[RSX_FPS_BITMAP_PITCH*RSX_FPS_BITMAP_HEIGHT];rsx_fps_bitmap(bitmap,cases[c].fps);
        uint8_t* pixels=NULL;checked(readback->lpVtbl->Map(readback,0,NULL,(void**)&pixels));
        unsigned lit=0;
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x){
            const int foreground=cases[c].enabled && x>=8 && y>=8 && x-8<RSX_FPS_BITMAP_WIDTH && y-8<RSX_FPS_BITMAP_HEIGHT && bitmap[(y-8)*RSX_FPS_BITMAP_PITCH+(x-8)*4+3];
            const uint8_t* p=pixels+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
            if(foreground){assert(p[0]==240 && p[1]==240 && p[2]==240 && p[3]==255);++lit;}
            else assert(p[0]==8 && p[1]==32 && p[2]==64 && p[3]==255);
        }
        if(c==2 || c==5) assert(lit>300);
        D3D12_RANGE no_writes={0,0};readback->lpVtbl->Unmap(readback,0,&no_writes);
        readback->lpVtbl->Release(readback);color->lpVtbl->Release(color);
    }
    rsx_d3d12_counter_release(&counter);assert(!counter.upload && !counter.mapped);
    rtv->lpVtbl->Release(rtv);list->lpVtbl->Release(list);allocator->lpVtbl->Release(allocator);
    queue->lpVtbl->Release(queue);fence->lpVtbl->Release(fence);CloseHandle(event);
    device->lpVtbl->Release(device);warp->lpVtbl->Release(warp);factory->lpVtbl->Release(factory);
    puts("D3D12 FPS overlay: seven WARP cases preserve background pixels, clip correctly and reuse retired upload storage.");
    return 0;
}
