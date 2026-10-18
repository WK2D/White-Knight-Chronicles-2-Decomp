#ifdef NDEBUG
#undef NDEBUG
#endif
#include "spu_helpers.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static uint32_t state=0xb17350a9u;
static uint32_t random32(void) {state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static u128 encode(const uint8_t bytes[16]) {
    u128 r;
    for(unsigned w=0;w<4;w++) r._u32[w]=((uint32_t)bytes[4*w]<<24)|
        ((uint32_t)bytes[4*w+1]<<16)|((uint32_t)bytes[4*w+2]<<8)|bytes[4*w+3];
    return r;
}
static void check(u128 result,const uint8_t bytes[16],unsigned count,unsigned kind) {
    /* Construct the oracle in architectural byte order, independent of SIMD
     * shuffle masks and the helper's host-byte mapping. */
    uint8_t expected[16];
    for(unsigned p=0;p<16;p++) {
        unsigned source=kind==0?p+count:kind==1?(p+count)%16:p-count;
        expected[p]=source<16?bytes[source]:0;
    }
    u128 want=encode(expected);assert(!memcmp(&result,&want,sizeof want));
}
int main(void) {
    const int immediate[]={INT_MIN,-257,-128,-65,-64,-33,-32,-31,-17,-16,-15,-1,
                           0,1,15,16,17,31,32,33,63,64,127,128,255,INT_MAX};
    for(unsigned trial=0;trial<1024;trial++) {
        uint8_t bytes[16];for(unsigned p=0;p<16;p++) bytes[p]=trial==0?(uint8_t)(p+1):(uint8_t)random32();
        u128 a=encode(bytes),b;
        for(unsigned count=0;count<256;count++) {
            for(unsigned w=0;w<4;w++) b._u32[w]=random32();
            b._u32[0]=(b._u32[0]&~255u)|count;
            check(spu_shlqby(a,b),bytes,count&31,0);
            check(spu_rotqby(a,b),bytes,count&15,1);
            check(spu_rotqmby(a,b),bytes,(0u-count)&31,2);
            check(spu_shlqbybi(a,b),bytes,(count>>3)&31,0);
            check(spu_rotqbybi(a,b),bytes,(count>>3)&15,1);
            check(spu_rotqmbybi(a,b),bytes,(0u-(count>>3))&31,2);
        }
        for(unsigned j=0;j<sizeof immediate/sizeof *immediate;j++) {
            const int i=immediate[j];
            check(spu_shlqbyi(a,i),bytes,(unsigned)i&31,0);
            check(spu_rotqbyi(a,i),bytes,(unsigned)i&15,1);
            check(spu_rotqmbyi(a,i),bytes,(0u-(unsigned)i)&31,2);
        }
    }
#ifdef PS3RECOMP_SPU_BYTE_VECTOR
    puts("SIMD SPU byte operations: all counts, BE byte order, wrap/zero fill and high register bits pass.");
#else
    puts("Scalar SPU byte operations: all counts, BE byte order, wrap/zero fill and high register bits pass.");
#endif
}
