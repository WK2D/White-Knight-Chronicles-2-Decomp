#include "spu_helpers.h"
#include "spu_fp_vectors.h"
#include <stdio.h>

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
    return bad ? 1 : 0;
}
