#include "spu_helpers.h"
#include "spu_fp_vectors.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

int main(void) {
    unsigned count=(unsigned)(sizeof fp_vectors/sizeof fp_vectors[0]),bad=0;
    for(unsigned i=0;i<count;i++) {
        u128 a,b,c;
        for(unsigned lane=0;lane<4;lane++) {
            const uint32_t* v=fp_vectors[(i+lane)%count];
            a._u32[lane]=v[0];b._u32[lane]=v[1];c._u32[lane]=v[2];
        }
        u128 result=spu_fnms(a,b,c);
        for(unsigned lane=0;lane<4;lane++) {
            uint32_t expected=fp_vectors[(i+lane)%count][3];
            if(result._u32[lane]!=expected) {
                if(bad++<8) fprintf(stderr,"case%u lane%u: got%08X expected%08X\n",i,lane,result._u32[lane],expected);
            }
        }
    }
    printf("SPU FNMS: %u vector cases / %u lane results; %u failures\n",count,count*4,bad);

    /* FI builds its power-of-two scale from the exponent bits instead of
     * calling ldexpf; the result must be bit-identical to the ldexpf form for
     * every exponent field and a broad sample of mantissa/step inputs. */
    unsigned fi_bad=0, fi_n=0;
    uint32_t seed=0x2468ACE1u;
    for(unsigned it=0; it<2000000u; it++) {
        seed=seed*1664525u+1013904223u; uint32_t rb=seed;
        seed=seed*1664525u+1013904223u; uint32_t ra=seed;
        if(it<256u*16u) rb=(rb&0x807FFFFFu)|((it&255u)<<23);   /* every exponent */
        u128 a,b; for(int l=0;l<4;l++){a._u32[l]=ra^(uint32_t)l;b._u32[l]=rb^((uint32_t)l<<3);}
        u128 r=spu_fi(a,b);
        for(int l=0;l<4;l++){
            uint32_t xb=b._u32[l], xa=a._u32[l];
            uint32_t base_bits=xb&0xFFFFFC00u; float base; memcpy(&base,&base_bits,4);
            float step=ldexpf((float)(xb&0x3FFu),(int)((xb>>23)&0xFFu)-127-13);
            if(base<0.0f) step=-step;
            float y=ldexpf((float)(xa&0x7FFFFu),-19);
            float want=base-step*y; uint32_t wb; memcpy(&wb,&want,4);
            fi_n++;
            if(r._u32[l]!=wb && !(want!=want && r._f32[l]!=r._f32[l]) && fi_bad++<8)
                fprintf(stderr,"FI ra=%08X rb=%08X got %08X want %08X\n",xa,xb,r._u32[l],wb);
        }
    }
    printf("SPU FI: %u lane results; %u mismatches\n",fi_n,fi_bad);
    return (bad || fi_bad) ? 1 : 0;
}
