// The title's name path uses UCS-2 string conversions, with guest byte order
// and counts by reference. These are distinct from the SDK's host-pointer API.
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <cstdint>

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register_ctx(uint32_t,const char*,void (*)(ppu_context*));
}
namespace {
uint32_t r32(uint32_t a) {
    return uint32_t(vm_base[a])<<24|uint32_t(vm_base[a+1])<<16|
           uint32_t(vm_base[a+2])<<8|vm_base[a+3];
}
void w32(uint32_t a,uint32_t v) {
    for(unsigned i=0;i<4;++i) vm_base[a+i]=uint8_t(v>>(24-8*i));
}
bool range(uint32_t a,uint64_t n) { return a && n<=0x100000000ull-a; }
template<bool to_utf8> void convert(ppu_context* c) {
    uint32_t src=uint32_t(c->gpr[3]),src_count=uint32_t(c->gpr[4]);
    uint32_t dst=uint32_t(c->gpr[5]),dst_count=uint32_t(c->gpr[6]);
    // L10n results: OK=0, illegal source=1, exhausted destination=2.
    if(!range(src_count,4)||!range(dst_count,4)) { c->gpr[3]=1;return; }
    uint32_t total=r32(src_count),capacity=r32(dst_count),consumed=0,written=0,status=0;
    if((total && !range(src,uint64_t(total)*(to_utf8 ? 2 : 1))) ||
       (dst && !range(dst,uint64_t(capacity)*(to_utf8 ? 1 : 2)))) {
        c->gpr[3]=1;return;
    }
    while(consumed<total) {
        uint32_t cp=0,in=1,out=1;
        uint8_t bytes[3];
        if constexpr(to_utf8) {
            uint32_t a=src+2*consumed;
            cp=uint32_t(vm_base[a])<<8|vm_base[a+1];
            if(cp>=0xD800 && cp<=0xDFFF) { status=1;break; }
            out=cp<0x80 ? 1 : cp<0x800 ? 2 : 3;
            if(out==1) bytes[0]=uint8_t(cp);
            else if(out==2) { bytes[0]=uint8_t(0xC0|(cp>>6));bytes[1]=uint8_t(0x80|(cp&63)); }
            else { bytes[0]=uint8_t(0xE0|(cp>>12));bytes[1]=uint8_t(0x80|((cp>>6)&63));bytes[2]=uint8_t(0x80|(cp&63)); }
        } else {
            uint8_t first=vm_base[src+consumed];
            uint32_t minimum=0;
            if(first<0x80) cp=first;
            else if(first>=0xC2 && first<=0xDF) { in=2;cp=first&31;minimum=0x80; }
            else if(first>=0xE0 && first<=0xEF) { in=3;cp=first&15;minimum=0x800; }
            else { status=1;break; }
            if(in>total-consumed) { status=1;break; }
            for(uint32_t i=1;i<in;++i) {
                uint8_t b=vm_base[src+consumed+i];
                if((b&0xC0)!=0x80) { status=1;break; }
                cp=(cp<<6)|(b&63);
            }
            if(status || cp<minimum || (cp>=0xD800 && cp<=0xDFFF)) { status=1;break; }
        }
        if(dst && out>capacity-written) { status=2;break; }
        if(out>UINT32_MAX-written) { status=2;break; }
        if(dst) {
            if constexpr(to_utf8) {
                for(uint32_t i=0;i<out;++i) vm_base[dst+written+i]=bytes[i];
            } else {
                vm_base[dst+2*written]=uint8_t(cp>>8);vm_base[dst+2*written+1]=uint8_t(cp);
            }
        }
        consumed+=in;written+=out;
    }
    // On success the source count is unchanged. On error it reports the
    // remaining input. No implicit terminator: these APIs convert exact counts.
    if(status) w32(src_count,total-consumed);
    w32(dst_count,written);c->gpr[3]=status;
}
}
extern "C" void wkc2_register_l10n(void) {
    ps3_hle_register_ctx(ps3_compute_nid("UCS2stoUTF8s"),"UCS2stoUTF8s",convert<true>);
    ps3_hle_register_ctx(ps3_compute_nid("UTF8stoUCS2s"),"UTF8stoUCS2s",convert<false>);
}
