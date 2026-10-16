#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>

static std::array<uint8_t,65536> memory;
extern "C" { uint8_t* vm_base=memory.data();void wkc2_register_l10n(void); }
using Handler=void(*)(ppu_context*);
static std::map<uint32_t,Handler> handlers;
extern "C" void ps3_hle_register_ctx(uint32_t nid,const char*,Handler f) { handlers[nid]=f; }
static void w32(uint32_t a,uint32_t v) { for(unsigned i=0;i<4;++i) memory[a+i]=uint8_t(v>>(24-8*i)); }
static uint32_t r32(uint32_t a) { return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3]; }
static uint32_t call(const char* name,uint32_t count,uint32_t capacity,uint32_t dst=0x400) {
    w32(0x100,count);w32(0x104,capacity);
    ppu_context c{};c.gpr[3]=0x200;c.gpr[4]=0x100;c.gpr[5]=dst;c.gpr[6]=0x104;
    handlers.at(ps3_compute_nid(name))(&c);return uint32_t(c.gpr[3]);
}
int main() {
    wkc2_register_l10n();assert(handlers.size()==2 && handlers.count(0x2AD091C6));
    const uint8_t ucs2[]={0,'T',0,'e',0,'s',0,'t',0,0,0x00,0xE9,0x65,0xE5};
    const uint8_t utf8[]={'T','e','s','t',0,0xC3,0xA9,0xE6,0x97,0xA5};
    std::memcpy(memory.data()+0x200,ucs2,sizeof(ucs2));
    std::memset(memory.data()+0x400,0xA5,32);
    assert(call("UCS2stoUTF8s",7,10)==0 && r32(0x100)==7 && r32(0x104)==10);
    assert(!std::memcmp(memory.data()+0x400,utf8,10) && memory[0x40A]==0xA5);
    assert(call("UCS2stoUTF8s",7,0,0)==0 && r32(0x104)==10); // size query
    assert(call("UCS2stoUTF8s",7,6)==2 && r32(0x100)==2 && r32(0x104)==5);
    assert(call("UCS2stoUTF8s",0,0)==0 && r32(0x104)==0);
    memory[0x200]=0xD8;memory[0x201]=0;
    assert(call("UCS2stoUTF8s",1,10)==1 && r32(0x100)==1 && r32(0x104)==0);
    std::memcpy(memory.data()+0x200,utf8,sizeof(utf8));
    std::memset(memory.data()+0x400,0xA5,32);
    assert(call("UTF8stoUCS2s",10,7)==0 && r32(0x100)==10 && r32(0x104)==7);
    assert(!std::memcmp(memory.data()+0x400,ucs2,14) && memory[0x40E]==0xA5);
    assert(call("UTF8stoUCS2s",10,0,0)==0 && r32(0x104)==7);
    assert(call("UTF8stoUCS2s",10,5)==2 && r32(0x100)==5 && r32(0x104)==5);
    for(const auto& bad : {std::array<uint8_t,4>{0xC0,0xAF,0,0}, {0xE0,0x80,0xAF,0},
                          {0xED,0xA0,0x80,0}, {0xF0,0x9F,0x98,0x80}, {0xE6,0,0,0}}) {
        std::memcpy(memory.data()+0x200,bad.data(),4);
        assert(call("UTF8stoUCS2s",4,10)==1 && r32(0x104)==0);
    }
    memory[0x200]=0xC3;
    assert(call("UTF8stoUCS2s",1,10)==1 && r32(0x100)==1);
    std::puts("Guest UCS-2/UTF-8 counts, byte order, exact buffers, queries and errors passed");
}
