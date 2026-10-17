#include "spu_helpers.h"
#include "spu_fp_vectors.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* Independent pre-0060 conversion expressions. Keep the scale computation
 * dynamic so an optimized test cannot fold away the original CRT path. */
static u128 conversion_reference(u128 a, int op, volatile int i8) {
    u128 r;
    double scale=exp2((double)(op<2 ? 173-i8 : i8-155));
    for(int lane=0;lane<4;lane++) {
        if(op==0) {
            double v=(double)a._f32[lane]*scale;
            if(v!=v) v=-2147483648.0;
            if(v>2147483647.0) v=2147483647.0;
            if(v<-2147483648.0) v=-2147483648.0;
            r._s32[lane]=(int32_t)v;
        } else if(op==1) {
            double v=(double)a._f32[lane]*scale;
            if(v!=v) v=0;
            if(v<0) v=0;
            if(v>4294967295.0) v=4294967295.0;
            r._u32[lane]=(uint32_t)v;
        } else if(op==2) {
            r._f32[lane]=(float)((double)a._s32[lane]*scale);
        } else {
            r._f32[lane]=(float)((double)a._u32[lane]*scale);
        }
    }
    return r;
}

static unsigned conversion_checks(void) {
    /* Zeros, denormals, finite extremes, both integer saturation boundaries,
     * infinities and quiet/signalling NaNs; also signed/unsigned int limits. */
    static const uint32_t edges[]={
        0,1,0x007FFFFFu,0x00800000u,0x3F000000u,0x3F800000u,
        0x4EFFFFFFu,0x4F000000u,0x4F7FFFFFu,0x4F800000u,
        0x7F7FFFFFu,0x7F800000u,0x7FC00000u,0x7F800001u,
        0x80000000u,0x80000001u,0x80800000u,0xBF000000u,
        0xBF800000u,0xCEFFFFFFu,0xCF000000u,0xCF000001u,
        0xFF7FFFFFu,0xFF800000u,0xFFC00000u,0xFFFFFFFFu,
        0x7FFFFFFFu,0x80000000u
    };
    const unsigned edge_n=(unsigned)(sizeof edges/sizeof edges[0]);
    uint32_t seed=0xC5839A12u;
    unsigned bad=0,lanes=0;
    for(int imm=0;imm<256;imm++) {
        volatile int i8=imm;
        for(unsigned it=0;it<edge_n+1024u;it++) {
            u128 a;
            for(unsigned lane=0;lane<4;lane++) {
                seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
                a._u32[lane]=it<edge_n ? edges[(it+lane)%edge_n] : seed;
            }
            u128 actual[4]={spu_cflts_host(a,i8),spu_cfltu_host(a,i8),
                            spu_csflt_host(a,i8),spu_cuflt_host(a,i8)};
            for(int op=0;op<4;op++) {
                u128 expected=conversion_reference(a,op,i8);
                for(unsigned lane=0;lane<4;lane++) {
                    lanes++;
                    if(actual[op]._u32[lane]!=expected._u32[lane] && bad++<8)
                        fprintf(stderr,"conversion op%d i8%d input%08X: got%08X expected%08X\n",
                                op,imm,a._u32[lane],actual[op]._u32[lane],expected._u32[lane]);
                }
            }
        }
    }
    /* The helper also keeps the old exp2 fallback outside normal doubles. */
    static const int exponents[]={-1075,-1074,-1023,-1022,-155,-82,0,100,173,1023,1024};
    for(unsigned it=0;it<sizeof exponents/sizeof exponents[0];it++) {
        volatile int e=exponents[it];
        double actual=spu_pow2d(e),expected=exp2((double)e);
        uint64_t ab,eb;memcpy(&ab,&actual,sizeof ab);memcpy(&eb,&expected,sizeof eb);
        if(ab!=eb && bad++<8)
            fprintf(stderr,"power-of-two exponent%d differs\n",(int)e);
    }
    printf("SPU conversions: all 256 immediates / %u lane results; %u mismatches\n",lanes,bad);
    return bad;
}

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
    unsigned conversion_bad=conversion_checks();
    return (bad || fi_bad || conversion_bad) ? 1 : 0;
}
