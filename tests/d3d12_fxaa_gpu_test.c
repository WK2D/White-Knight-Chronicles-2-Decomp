/* Synthetic data only: production FXAA dispatch versus independent CPU math,
 * source immutability, alpha, odd dimensions, resizing and fence-safe reuse. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include "rsx_d3d12_fxaa.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void checked(HRESULT hr){assert(SUCCEEDED(hr));}
static float minimum(float a,float b){return a<b?a:b;}
static float maximum(float a,float b){return a>b?a:b;}
static float clamp(float a,float lo,float hi){return minimum(maximum(a,lo),hi);}
static float luminance(const float c[3]){return c[0]*.299f+c[1]*.587f+c[2]*.114f;}
static void sample(const unsigned char* p,unsigned w,unsigned h,float x,float y,float c[3])
{
    x=clamp(x,0,(float)w-1);y=clamp(y,0,(float)h-1);
    const unsigned x0=(unsigned)floorf(x),y0=(unsigned)floorf(y);
    const unsigned x1=x0+1<w?x0+1:x0,y1=y0+1<h?y0+1:y0;
    const float fx=x-x0,fy=y-y0;
    for(unsigned i=0;i<3;++i){
        const float a=p[(y0*w+x0)*4+i]*(1-fx)+p[(y0*w+x1)*4+i]*fx;
        const float b=p[(y1*w+x0)*4+i]*(1-fx)+p[(y1*w+x1)*4+i]*fx;
        c[i]=(a*(1-fy)+b*fy)/255.0f;
    }
}
static void reference(const unsigned char* p,unsigned w,unsigned h,unsigned x,unsigned y,float out[3])
{
    float center[3],corner[4][3];sample(p,w,h,(float)x,(float)y,center);
    sample(p,w,h,(float)x-1,(float)y-1,corner[0]);sample(p,w,h,(float)x+1,(float)y-1,corner[1]);
    sample(p,w,h,(float)x-1,(float)y+1,corner[2]);sample(p,w,h,(float)x+1,(float)y+1,corner[3]);
    const float m=luminance(center),nw=luminance(corner[0]),ne=luminance(corner[1]),sw=luminance(corner[2]),se=luminance(corner[3]);
    const float low=minimum(m,minimum(minimum(nw,ne),minimum(sw,se)));
    const float high=maximum(m,maximum(maximum(nw,ne),maximum(sw,se)));
    if(high-low<maximum(.0312f,high*.125f)){memcpy(out,center,sizeof center);return;}
    float ex=sw+se-nw-ne,ey=nw+sw-ne-se;
    const float reduction=maximum((nw+ne+sw+se)*(.25f*.125f),1.0f/128.0f);
    const float divisor=minimum(fabsf(ex),fabsf(ey))+reduction;
    ex=clamp(ex/divisor,-8,8);ey=clamp(ey/divisor,-8,8);
    float a[3],b[3],c[3],d[3],inner[3],wide[3];
    sample(p,w,h,x-ex/6,y-ey/6,a);sample(p,w,h,x+ex/6,y+ey/6,b);
    sample(p,w,h,x-ex*.5f,y-ey*.5f,c);sample(p,w,h,x+ex*.5f,y+ey*.5f,d);
    for(unsigned i=0;i<3;++i){inner[i]=(a[i]+b[i])*.5f;wide[i]=inner[i]*.5f+(c[i]+d[i])*.25f;}
    const float check=luminance(wide);memcpy(out,check<low||check>high?inner:wide,sizeof wide);
}
static ID3D12Resource* buffer(ID3D12Device* device,UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=type;D3D12_RESOURCE_DESC d={0};
    d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource* result=NULL;
    checked(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&d,state,NULL,&IID_ID3D12Resource,(void**)&result));return result;
}
static void copy_to_buffer(ID3D12GraphicsCommandList* list,ID3D12Resource* image,
    ID3D12Resource* readback,D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp)
{
    D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=image;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=readback;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->lpVtbl->CopyTextureRegion(list,&dst,0,0,0,&src,NULL);
}
int main(void)
{
    ID3D12Debug* debug=NULL;
    if(SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug))){debug->lpVtbl->EnableDebugLayer(debug);debug->lpVtbl->Release(debug);}
    IDXGIFactory4* factory=NULL;IDXGIAdapter* warp=NULL;ID3D12Device* device=NULL;
    checked(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    checked(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&warp));
    checked(D3D12CreateDevice((IUnknown*)warp,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
    ID3D12InfoQueue* info=NULL;device->lpVtbl->QueryInterface(device,&IID_ID3D12InfoQueue,(void**)&info);
    ID3D12CommandQueue* queue=NULL;ID3D12CommandAllocator* allocator=NULL;ID3D12GraphicsCommandList* list=NULL;
    D3D12_COMMAND_QUEUE_DESC q={0};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    checked(device->lpVtbl->CreateCommandQueue(device,&q,&IID_ID3D12CommandQueue,(void**)&queue));
    checked(device->lpVtbl->CreateCommandAllocator(device,q.Type,&IID_ID3D12CommandAllocator,(void**)&allocator));
    checked(device->lpVtbl->CreateCommandList(device,0,q.Type,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));checked(list->lpVtbl->Close(list));
    ID3D12Fence* fence=NULL;checked(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));
    HANDLE event=CreateEventA(NULL,FALSE,FALSE,NULL);assert(event);
    rsx_d3d12_fxaa aa={0};
    const struct {unsigned w,h,pattern;int enabled;} cases[]={
        {17,19,1,0},{17,19,0,1},{17,19,1,1},{17,19,2,1},
        {31,13,1,1},{1,1,0,1},{33,24,2,1},{33,24,1,0}
    };
    unsigned total_changed=0;
    for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c){
        const unsigned w=cases[c].w,h=cases[c].h;unsigned char pixels[33*24*4];
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x){
            const int bright=cases[c].pattern==1 ? (x*2+y>w) : (x/3+y/3)%2;
            const unsigned pos=(y*w+x)*4;
            pixels[pos]=cases[c].pattern? (bright?220:20):60;
            pixels[pos+1]=cases[c].pattern? (bright?180:40):90;
            pixels[pos+2]=cases[c].pattern? (bright?160:60):120;
            pixels[pos+3]=(unsigned char)((x*31+y*17)&255);
        }
        D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d={0};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
        ID3D12Resource* source=NULL;checked(device->lpVtbl->CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&source));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;UINT64 bytes;
        device->lpVtbl->GetCopyableFootprints(device,&d,0,1,0,&fp,NULL,NULL,&bytes);
        ID3D12Resource* upload=buffer(device,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        ID3D12Resource* out=buffer(device,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Resource* original=buffer(device,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        unsigned char* mapped=NULL;D3D12_RANGE empty={0,0};checked(upload->lpVtbl->Map(upload,0,&empty,(void**)&mapped));
        for(unsigned y=0;y<h;++y) memcpy(mapped+fp.Offset+y*fp.Footprint.RowPitch,pixels+y*w*4,w*4);
        upload->lpVtbl->Unmap(upload,0,NULL);
        checked(allocator->lpVtbl->Reset(allocator));checked(list->lpVtbl->Reset(list,allocator,NULL));
        D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        dst.pResource=source;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->lpVtbl->CopyTextureRegion(list,&dst,0,0,0,&src,NULL);
        rsx_d3d12_fxaa_transition(list,source,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12Resource* result=NULL;
        checked(rsx_d3d12_fxaa_apply(&aa,device,list,source,cases[c].enabled,&result));
        if(c==0) assert(!aa.pipeline && !aa.output);
        assert(cases[c].enabled ? result!=source : result==source);
        copy_to_buffer(list,result,out,fp);copy_to_buffer(list,source,original,fp);
        checked(list->lpVtbl->Close(list));ID3D12CommandList* lists[]={(ID3D12CommandList*)list};queue->lpVtbl->ExecuteCommandLists(queue,1,lists);
        checked(queue->lpVtbl->Signal(queue,fence,c+1));checked(fence->lpVtbl->SetEventOnCompletion(fence,c+1,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
        unsigned char *actual=NULL,*unchanged=NULL;checked(out->lpVtbl->Map(out,0,NULL,(void**)&actual));checked(original->lpVtbl->Map(original,0,NULL,(void**)&unchanged));
        unsigned changed=0;
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x){
            const unsigned char* a=actual+fp.Offset+y*fp.Footprint.RowPitch+x*4;const unsigned char* p=pixels+(y*w+x)*4;
            assert(memcmp(unchanged+fp.Offset+y*fp.Footprint.RowPitch+x*4,p,4)==0);
            assert(a[3]==p[3]);
            if(!cases[c].enabled || cases[c].pattern==0) assert(memcmp(a,p,4)==0);
            else {float expected[3];reference(pixels,w,h,x,y,expected);
                for(unsigned k=0;k<3;++k){int value=(int)(clamp(expected[k],0,1)*255+.5f);if(abs((int)a[k]-value)>2){fprintf(stderr,"case=%u x=%u y=%u c=%u got=%u expected=%d\n",c,x,y,k,a[k],value);assert(0);}}
            }
            if(memcmp(a,p,3)) ++changed;
        }
        if(cases[c].enabled && cases[c].pattern) assert(changed>5);
        total_changed+=changed;
        out->lpVtbl->Unmap(out,0,&empty);original->lpVtbl->Unmap(original,0,&empty);
        upload->lpVtbl->Release(upload);out->lpVtbl->Release(out);original->lpVtbl->Release(original);source->lpVtbl->Release(source);
    }
    assert(total_changed>100);rsx_d3d12_fxaa_release(&aa);assert(!aa.output && !aa.pipeline);
    if(info){
        const UINT64 count=info->lpVtbl->GetNumStoredMessages(info);
        for(UINT64 i=0;i<count;++i){SIZE_T bytes=0;checked(info->lpVtbl->GetMessage(info,i,NULL,&bytes));D3D12_MESSAGE* m=malloc(bytes);assert(m);checked(info->lpVtbl->GetMessage(info,i,m,&bytes));
            if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"D3D validation: %s\n",m->pDescription);assert(0);}free(m);}
        info->lpVtbl->Release(info);
    }
    list->lpVtbl->Release(list);allocator->lpVtbl->Release(allocator);queue->lpVtbl->Release(queue);fence->lpVtbl->Release(fence);CloseHandle(event);
    device->lpVtbl->Release(device);warp->lpVtbl->Release(warp);factory->lpVtbl->Release(factory);
    puts("DirectX FXAA: eight WARP cases match independent CPU math, preserve source/alpha, support Off and resize/reuse.");return 0;
}
