#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include "ppu-vsub-fixture.h"
static void put(void* pointer,unsigned lane,uint32_t value) {
    auto p=static_cast<unsigned char*>(pointer)+lane*4;
    for(unsigned i=0;i<4;i++) p[i]=static_cast<unsigned char>(value>>(24-i*8));
}
static uint32_t get(const void* pointer,unsigned lane) {
    auto p=static_cast<const unsigned char*>(pointer)+lane*4;
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
int main() {
    using Op=void(*)(ppu_context*);
    const Op ops[]={separate,alias_a,alias_b,all_same};
    const unsigned dest[]={1,2,3,2};
    const uint32_t values[]={0,1,0xff,0x100,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
    for(unsigned op=0;op<4;op++) for(auto a:values) for(auto b:values) for(unsigned sticky=0;sticky<2;sticky++) {
        ppu_context ctx{};ctx.vscr=0x10000|sticky;
        for(unsigned lane=0;lane<4;lane++) {
            put(&ctx.vr[2],lane,a^uint32_t(lane*0x01020304));
            put(&ctx.vr[3],lane,b^uint32_t(lane*0x10203040));
        }
        uint32_t expected[4];bool saturated=false;
        for(unsigned lane=0;lane<4;lane++) {
            uint32_t x=get(&ctx.vr[2],lane),y=get(&ctx.vr[op==3?2:3],lane);
            expected[lane]=x>=y?x-y:0;saturated|=x<y;
        }
        auto before=ctx;
        ops[op](&ctx);
        for(unsigned lane=0;lane<4;lane++) assert(get(&ctx.vr[dest[op]],lane)==expected[lane]);
        assert(ctx.vscr==(before.vscr|unsigned(saturated)));
        for(unsigned r=0;r<32;r++) if(r!=dest[op]) assert(!std::memcmp(&ctx.vr[r],&before.vr[r],16));
    }
    std::puts("Decoded/lifted VSUBUWS: big-endian lanes, unsigned saturation, aliases and sticky VSCR pass.");
}
