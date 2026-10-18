#ifdef NDEBUG
#undef NDEBUG
#endif
#include "spu_helpers.h"
#include <stdio.h>
#include <string.h>
#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEST_MXCSR 1
#endif

static uint32_t seed=0x4e4d5356u;
static uint32_t rnd(void) {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void) {
    unsigned modes=1;
#ifdef TEST_MXCSR
    const unsigned original=_mm_getcsr();
    modes=16;
#endif
    for(unsigned mode=0;mode<modes;mode++) {
#ifdef TEST_MXCSR
        unsigned control=(original & ~(0x6000u|0x8040u|0x3fu))|((mode&3u)<<13);
        if(mode&4u)control|=0x8000u;
        if(mode&8u)control|=0x40u;
        _mm_setcsr(control);
#endif
        for(unsigned i=0;i<65536;i++) {
            u128 a,b,c,want;
            for(int lane=0;lane<4;lane++) {
                a._u32[lane]=rnd();b._u32[lane]=rnd();c._u32[lane]=rnd();
                switch(i&7u) {
                case 0:
                    a._u32[lane]=(a._u32[lane]&0x807fffffu)|((110+rnd()%35)<<23);
                    b._u32[lane]=(b._u32[lane]&0x807fffffu)|((110+rnd()%35)<<23);
                    c._u32[lane]=(c._u32[lane]&0x807fffffu)|((110+rnd()%35)<<23);break;
                case 1:a._u32[lane]=0x3f800000;c._u32[lane]=b._u32[lane];break;
                case 2:a._u32[lane]=0x3f800001;b._u32[lane]=0x3f7fffff;c._u32[lane]=0x3f800000+(rnd()%3)-1;break;
                case 3:a._u32[lane]&=0x807fffffu;break;
                case 4:c._u32[lane]&=0x807fffffu;break;
                case 5:a._u32[lane]|=0x7f800000u;break;
                default:break;
                }
                want._u32[lane]=spu_sf_fnms(a._u32[lane],b._u32[lane],c._u32[lane]);
            }
            const u128 got=spu_fnms(a,b,c);
            int failed=memcmp(&got,&want,sizeof got)!=0;
#ifdef TEST_MXCSR
            failed|=_mm_getcsr()!=control;
#endif
            if(failed) {
#ifdef TEST_MXCSR
                _mm_setcsr(original);
#endif
                fprintf(stderr,"FNMS lane/MXCSR mismatch: mode=%u vector=%u\n",mode,i);
                return 1;
            }
        }
    }
#ifdef TEST_MXCSR
    _mm_setcsr(original);
#endif
#if defined(__AVX2__)
    puts("AVX2 FNMS: independent lanes, all rounding/FTZ/DAZ modes and unchanged MXCSR pass");
#else
    puts("Scalar FNMS fallback: independent lanes and host-state checks pass");
#endif
    return 0;
}
