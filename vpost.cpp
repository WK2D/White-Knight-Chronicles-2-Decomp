/* Guest cellVpost ABI and progressive 8-bit YUV420 processing. Implements
 * actual crop/resize and RGBA conversion, rather than acknowledging Exec.
 * Field layouts and enum values were checked against public interface records.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register_ctx(uint32_t,const char*,void (*)(ppu_context*));
void rsx_draw_engine_note_host_write(uint32_t ea,uint32_t bytes);
}
namespace {
bool range(uint32_t a,uint64_t n) {return a && n<=(uint64_t(1)<<32)-a;}
uint32_t r32(uint32_t a) {return uint32_t(vm_base[a])<<24|uint32_t(vm_base[a+1])<<16|uint32_t(vm_base[a+2])<<8|vm_base[a+3];}
void w32(uint32_t a,uint32_t v) {for(unsigned i=0;i<4;++i) vm_base[a+i]=uint8_t(v>>(24-i*8));}
uint32_t arg(ppu_context* c,unsigned n) {return uint32_t(c->gpr[3+n]);}
void result(ppu_context* c,uint32_t v) {c->gpr[3]=uint64_t(int64_t(int32_t(v)));}
struct Config {uint32_t iw,ih,ow,oh,format;};
std::mutex mutex;
std::map<uint32_t,Config> configs;
uint32_t next_handle=1;
bool config(uint32_t a,Config& cfg) {
    if(!range(a,40)) return false;
    cfg={r32(a),r32(a+4),r32(a+16),r32(a+20),r32(a+28)};
    return cfg.iw && cfg.ih && cfg.ow && cfg.oh && cfg.iw<=8192 && cfg.ih<=8192 && cfg.ow<=8192 && cfg.oh<=8192 &&
        !r32(a+8) && !r32(a+12) && !r32(a+24) && cfg.format<=1;
}
void query(ppu_context* c) {
    uint32_t a=arg(c,0),out=arg(c,1); Config cfg;
    if(!a) {result(c,0x80610410); return;}
    if(!config(a,cfg)) {result(c,0x80610411); return;}
    if(!range(out,16)) {result(c,0x80610412); return;}
    std::memset(vm_base+out,0,16); w32(out,256); w32(out+8,0x10000); result(c,0);
}
void open(ppu_context* c) {
    uint32_t a=arg(c,0),res=arg(c,1),out=arg(c,2); Config cfg;
    if(!a) {result(c,0x80610440); return;}
    if(!config(a,cfg)) {result(c,0x80610441); return;}
    if(!range(res,24)) {result(c,0x80610442); return;}
    if(!range(r32(res),r32(res+4)) || r32(res+4)<256) {result(c,0x80610443); return;}
    if(!range(out,4)) {result(c,0x80610444); return;}
    std::lock_guard l(mutex); uint32_t handle=next_handle++; configs.emplace(handle,cfg); w32(out,handle);
    std::printf("[wkc2-vpost] open %u: max %ux%u -> %ux%u format=%u\n",handle,cfg.iw,cfg.ih,cfg.ow,cfg.oh,cfg.format); result(c,0);
}
void close(ppu_context* c) {
    uint32_t h=arg(c,0); std::lock_guard l(mutex);
    result(c,!h ? 0x80610470 : configs.erase(h) ? 0 : 0x80610471);
}
struct Window {uint32_t x,y,w,h;};
Window window(uint32_t a) {return {r32(a),r32(a+4),r32(a+8),r32(a+12)};}
bool fits(const Window& win,uint32_t w,uint32_t h) {return win.w && win.h && uint64_t(win.x)+win.w<=w && uint64_t(win.y)+win.h<=h;}
uint8_t byte(double v) {return uint8_t(std::clamp(int(std::floor(v+0.5)),0,255));}
double sample(const uint8_t* p,uint32_t stride,double x,double y,const Window& win) {
    x=std::clamp(x,double(win.x),double(win.x+win.w-1));
    y=std::clamp(y,double(win.y),double(win.y+win.h-1));
    uint32_t x0=uint32_t(x),y0=uint32_t(y),x1=std::min(x0+1,win.x+win.w-1),y1=std::min(y0+1,win.y+win.h-1);
    double dx=x-x0,dy=y-y0;
    return (p[y0*stride+x0]*(1-dx)+p[y0*stride+x1]*dx)*(1-dy)+
        (p[y1*stride+x0]*(1-dx)+p[y1*stride+x1]*dx)*dy;
}
void resize(const uint8_t* src,uint32_t sw,uint8_t* dst,uint32_t dw,const Window& in,const Window& out) {
    for(uint32_t y=0;y<out.h;++y) for(uint32_t x=0;x<out.w;++x)
        dst[(out.y+y)*dw+out.x+x]=byte(sample(src,sw,in.x+(x+0.5)*in.w/out.w-0.5,in.y+(y+0.5)*in.h/out.h-0.5,in));
}
void exec(ppu_context* c) {
    uint32_t h=arg(c,0),input=arg(c,1),ctrl=arg(c,2),output=arg(c,3),info=arg(c,4); Config cfg;
    {std::lock_guard l(mutex); auto i=configs.find(h); if(i==configs.end()) {result(c,h ? 0x806104A1 : 0x806104A0); return;} cfg=i->second;}
    if(!input) {result(c,0x806104A2); return;}
    if(!range(ctrl,96)) {result(c,0x806104A4); return;}
    if(!output) {result(c,0x806104A6); return;}
    if(!range(info,88)) {result(c,0x806104A8); return;}
    uint32_t iw=r32(ctrl+12),ih=r32(ctrl+16),ow=r32(ctrl+48),oh=r32(ctrl+52),quant=r32(ctrl+24),matrix=r32(ctrl+28);
    Window in=window(ctrl+32),out=window(ctrl+56);
    // Other execution modes require field-aware deinterlacing. Do not silently
    // acknowledge those paths or pretend that an unimplemented filter ran.
    if(r32(ctrl)!=0 || r32(ctrl+4)!=0 || !iw || !ih || !ow || !oh || iw>cfg.iw || ih>cfg.ih || ow>cfg.ow || oh>cfg.oh ||
        ((iw|ih|ow|oh|in.x|in.y|in.w|in.h)&1) || !fits(in,iw,ih) || !fits(out,ow,oh) || quant>1 || matrix>1 ||
        (cfg.format==1 && ((out.x|out.y|out.w|out.h)&1))) {
        std::fprintf(stderr,"[wkc2-vpost] unsupported/invalid control: exec=%u scaler=%u in=%ux%u out=%ux%u\n",r32(ctrl),r32(ctrl+4),iw,ih,ow,oh);
        result(c,0x806104A5); return;
    }
    uint64_t input_bytes=uint64_t(iw)*ih*3/2,output_bytes=uint64_t(ow)*oh*(cfg.format==0 ? 4 : 3)/ (cfg.format==0 ? 1 : 2);
    if(!range(input,input_bytes)) {result(c,0x806104A3); return;}
    if(!range(output,output_bytes) || (uint64_t(output)<uint64_t(input)+input_bytes && uint64_t(input)<uint64_t(output)+output_bytes)) {result(c,0x806104A7); return;}
    const uint8_t* py=vm_base+input; const uint8_t* pu=py+iw*ih; const uint8_t* pv=pu+iw*ih/4;
    uint8_t* dest=vm_base+output;
    Window ci={in.x/2,in.y/2,in.w/2,in.h/2};
    if(cfg.format==1) {
        resize(py,iw,dest,ow,in,out);
        Window co={out.x/2,out.y/2,out.w/2,out.h/2};
        resize(pu,iw/2,dest+ow*oh,ow/2,ci,co); resize(pv,iw/2,dest+ow*oh*5/4,ow/2,ci,co);
    } else {
        double ys=quant ? 255.0/219 : 1,cs=quant ? 255.0/224 : 1;
        bool direct=in.w==out.w && in.h==out.h;
        for(uint32_t y=0;y<out.h;++y) for(uint32_t x=0;x<out.w;++x) {
            double yy,u,v;
            if(direct) {
                uint32_t sx=in.x+x,sy=in.y+y;
                yy=py[sy*iw+sx]; u=pu[(sy/2)*(iw/2)+sx/2]; v=pv[(sy/2)*(iw/2)+sx/2];
            } else {
                double sx=in.x+(x+0.5)*in.w/out.w-0.5,sy=in.y+(y+0.5)*in.h/out.h-0.5;
                yy=sample(py,iw,sx,sy,in); u=sample(pu,iw/2,(sx-0.5)/2,(sy-0.5)/2,ci); v=sample(pv,iw/2,(sx-0.5)/2,(sy-0.5)/2,ci);
            }
            yy=(yy-(quant ? 16 : 0))*ys; u=(u-128)*cs; v=(v-128)*cs;
            auto* p=dest+((out.y+y)*ow+out.x+x)*4;
            p[0]=byte(yy+(matrix ? 1.5748 : 1.402)*v);
            p[1]=byte(yy-(matrix ? 0.187324 : 0.344136)*u-(matrix ? 0.468124 : 0.714136)*v);
            p[2]=byte(yy+(matrix ? 1.8556 : 1.772)*u); p[3]=vm_base[ctrl+72];
        }
    }
    // The frame may land in a buffer the renderer also holds as a GPU surface
    // (WKC reuses a render target for its cutscene movie); have it reload.
    rsx_draw_engine_note_host_write(output,uint32_t(output_bytes));
    std::memset(vm_base+info,0,88);
    w32(info,iw); w32(info+4,ih); w32(info+20,r32(ctrl+20)); w32(info+28,quant); w32(info+32,matrix);
    w32(info+36,ow); w32(info+40,oh); w32(info+52,cfg.format); w32(info+64,cfg.format ? quant : 0); w32(info+68,matrix);
    w32(info+72,r32(ctrl+80)); w32(info+76,r32(ctrl+84));
    static std::atomic<unsigned> count=0; if(count.fetch_add(1,std::memory_order_relaxed)<4) std::printf("[wkc2-vpost] converted %ux%u -> %ux%u format=%u\n",in.w,in.h,out.w,out.h,cfg.format);
    result(c,0);
}
}
extern "C" void wkc2_register_vpost(void) {
    struct Binding {const char* name; void (*fn)(ppu_context*);};
    const Binding bindings[]={{"cellVpostQueryAttr",query},{"cellVpostOpen",open},{"cellVpostClose",close},{"cellVpostExec",exec}};
    for(auto& b:bindings) ps3_hle_register_ctx(ps3_compute_nid(b.name),b.name,b.fn);
}
