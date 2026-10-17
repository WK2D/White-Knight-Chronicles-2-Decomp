#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <array>
#include <cassert>
#include <cstring>
#include <map>
static std::array<uint8_t,65536> memory;
extern "C" {uint8_t* vm_base=memory.data(); void wkc2_register_vpost(void);}
static std::map<uint32_t,void(*)(ppu_context*)> handlers;
extern "C" void ps3_hle_register_ctx(uint32_t n,const char*,void(*f)(ppu_context*)) {handlers[n]=f;}
// Each converted frame is reported to the renderer as a host write.
static uint32_t written_ea=0,written_bytes=0,written_calls=0;
extern "C" void rsx_draw_engine_note_host_write(uint32_t ea,uint32_t bytes) {written_ea=ea;written_bytes=bytes;++written_calls;}
static void w32(uint32_t a,uint32_t v) {for(unsigned i=0;i<4;++i) memory[a+i]=uint8_t(v>>(24-i*8));}
static uint32_t r32(uint32_t a) {return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3];}
static uint32_t call(const char* name,std::initializer_list<uint32_t> args) {ppu_context c{}; unsigned i=3; for(auto a:args) c.gpr[i++]=a; handlers.at(ps3_compute_nid(name))(&c); return uint32_t(c.gpr[3]);}
int main() {
    wkc2_register_vpost(); assert(handlers.size()==4 && handlers.count(0xCD33F3E2) && handlers.count(0x95E788C3));
    constexpr uint32_t cfg=0x100,res=0x200,ctrl=0x300,info=0x400,input=0x1000,output=0x2000;
    w32(cfg,4); w32(cfg+4,4); w32(cfg+16,4); w32(cfg+20,4);
    w32(res,0x8000); w32(res+4,256);
    memory[0x510]=0xA5;
    assert(call("cellVpostQueryAttr",{cfg,0x500})==0 && r32(0x500)==256 && memory[0x510]==0xA5);
    assert(call("cellVpostOpen",{cfg,res,0x500})==0); uint32_t handle=r32(0x500); assert(handle);
    w32(ctrl+12,4); w32(ctrl+16,4); w32(ctrl+24,1); w32(ctrl+40,4); w32(ctrl+44,4);
    w32(ctrl+48,4); w32(ctrl+52,4); w32(ctrl+64,4); w32(ctrl+68,4); memory[ctrl+72]=0xA7;
    w32(ctrl+80,0x11223344); w32(ctrl+84,0x55667788);
    // Neutral chroma, legal-range black and white. Expected RGB endpoints are
    // exact, independent of a particular conversion implementation's rounding.
    std::memset(memory.data()+input,16,24); std::memset(memory.data()+input+16,128,8);
    for(unsigned y=0;y<4;++y) for(unsigned x=2;x<4;++x) memory[input+y*4+x]=235;
    std::memset(memory.data()+output-1,0x5A,66); memory[info+88]=0xA5;
    assert(call("cellVpostExec",{handle,input,ctrl,output,info})==0);
    assert(written_calls==1 && written_ea==output && written_bytes==4u*4u*4u);
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        auto a=output+(y*4+x)*4; assert(memory[a]==(x<2 ? 0 : 255) && memory[a+1]==memory[a] && memory[a+2]==memory[a] && memory[a+3]==0xA7);
    }
    assert(memory[output-1]==0x5A && memory[output+64]==0x5A && memory[info+88]==0xA5);
    assert(r32(info)==4 && r32(info+36)==4 && r32(info+72)==0x11223344 && r32(info+76)==0x55667788);
    // Crop the white half, resize to a two-row window, and preserve other rows.
    w32(ctrl+32,2); w32(ctrl+40,2); w32(ctrl+60,1); w32(ctrl+68,2);
    std::memset(memory.data()+output,0x5A,64);
    assert(call("cellVpostExec",{handle,input,ctrl,output,info})==0);
    for(unsigned x=0;x<16;++x) assert(memory[output+x]==0x5A && memory[output+48+x]==0x5A);
    assert(memory[output+16]==255 && memory[output+47]==0xA7);
    // Legal-range BT.601 red checks channel order; the same YUV triplet has
    // a different green component under BT.709, so a matrix ignored is caught.
    w32(ctrl+32,0); w32(ctrl+40,4); w32(ctrl+60,0); w32(ctrl+68,4);
    std::memset(memory.data()+input,81,16);
    std::memset(memory.data()+input+16,90,4); std::memset(memory.data()+input+20,240,4);
    assert(call("cellVpostExec",{handle,input,ctrl,output,info})==0);
    assert(memory[output]>=253 && memory[output+1]<=1 && memory[output+2]<=1 && memory[output+3]==0xA7);
    w32(ctrl+28,1);
    assert(call("cellVpostExec",{handle,input,ctrl,output,info})==0);
    assert(memory[output]==255 && memory[output+1]>=23 && memory[output+1]<=25 && memory[output+2]==0);
    assert(call("cellVpostClose",{handle})==0);
    assert(call("cellVpostExec",{handle,input,ctrl,output,info})==0x806104A1);
    assert(call("cellVpostOpen",{0,res,0x500})==0x80610440);
    // Planar output retains byte values and plane ordering.
    w32(cfg+28,1); assert(call("cellVpostOpen",{cfg,res,0x500})==0); handle=r32(0x500);
    w32(ctrl+32,0); w32(ctrl+40,4); w32(ctrl+60,0); w32(ctrl+68,4);
    std::memset(memory.data()+output,0x5A,25);
    assert(call("cellVpostExec",{handle,input,ctrl,output,info})==0);
    assert(std::memcmp(memory.data()+input,memory.data()+output,24)==0 && memory[output+24]==0x5A);
    assert(r32(info+52)==1 && call("cellVpostClose",{handle})==0);
}
