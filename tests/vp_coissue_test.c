#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_vp_decompiler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* NV40 issues an instruction's vector and scalar operations together: both
 * read their operands before either writes. Hand-encoded programs check
 * that the scalar operand is taken before the vector unit writes the temp
 * or address register it depends on. */
#define SWZ(x,y,z,w) (((w)<<8)|((z)<<10)|((y)<<12)|((x)<<14))
#define XYZW SWZ(0,1,2,3)
#define XXXX SWZ(0,0,0,0)
enum { TEMP=1, INPUT=2, CONST=3 };
enum { V_MOV=1, V_ARL=0x0D, S_RCP=2 };

typedef struct { uint32_t d[4]; } insn;
static uint32_t src(unsigned type,unsigned tmp,unsigned swz) { return type|(tmp<<2)|swz; }
static insn op(unsigned vec,unsigned vmask,unsigned dst_tmp,unsigned sca,unsigned smask,unsigned sca_dst) {
    insn i={{0,0,0,0}};
    i.d[0]=dst_tmp<<15;
    i.d[1]=(vec<<22)|(sca<<27);
    i.d[3]=(0x1Fu<<2)|(sca_dst<<7)|(vmask<<13)|(smask<<17);
    return i;
}
static void src0(insn* i,uint32_t s) { i->d[2]|=(s&0x1FF)<<23; i->d[1]|=(s>>9)&0xFF; }
static void src2(insn* i,uint32_t s) { i->d[3]|=(s&0x7FF)<<21; i->d[2]|=(s>>11)&0x3F; }
static insn output(unsigned tmp) {   /* o[0] = r[tmp], end */
    insn i=op(V_MOV,0xF,0x3F,0,0,0x3F);
    src0(&i,src(TEMP,tmp,XYZW));
    i.d[0]|=1u<<30; i.d[3]&=~(0x1Fu<<2); i.d[3]|=1u;
    return i;
}
static char hlsl[64*1024];
static const char* decompile(const insn* p,unsigned n) {
    uint8_t code[16*8];
    for (unsigned k=0;k<n;++k) for (unsigned w=0;w<4;++w) for (unsigned b=0;b<4;++b)
        code[k*16+w*4+b]=(uint8_t)(p[k].d[w]>>(8*b));
    assert(rsx_vp_decompile(code,n*16,hlsl,sizeof hlsl)==(int)n);
    return hlsl;
}
static unsigned count(const char* s,const char* needle) {
    unsigned n=0; for (const char* p=s;(p=strstr(p,needle));++p) ++n; return n;
}
static const char* after(const char* s,const char* needle) { const char* p=strstr(s,needle); assert(p); return p; }

int main(void) {
    /* r1 = v0; then { vec: r1 = c5 ; sca: r2.x = 1/r1.x } -> scalar sees v0. */
    insn temp[3];
    temp[0]=op(V_MOV,0xF,1,0,0,0x3F); src0(&temp[0],src(INPUT,0,XYZW));
    temp[1]=op(V_MOV,0xF,1,S_RCP,0x8,2); temp[1].d[1]|=5u<<12;
    src0(&temp[1],src(CONST,0,XYZW)); src2(&temp[1],src(TEMP,1,XXXX));
    temp[2]=output(2);
    const char* s=decompile(temp,3);
    const char* snapshot=after(s,"float4 _sc = (float4)((r[1]).xxxx);");
    assert(snapshot<after(s,"vp_c[5]"));                 /* read before the vector write */
    assert(strstr(s,"(1.0/(_sc).x)") && !strstr(s,"(1.0/(r[1]).x)"));
    assert(count(s,"_sc =")==1);                         /* only the co-issued instruction */

    /* { vec: a0.x = floor(v1.x) ; sca: r3.x = 1/c[a0.x+2].x } -> old a0. */
    insn arl[2];
    arl[0]=op(V_ARL,0x8,0,S_RCP,0x8,3); arl[0].d[1]|=(1u<<8)|(2u<<12); arl[0].d[3]|=1u<<1;
    src0(&arl[0],src(INPUT,0,XXXX)); src2(&arl[0],src(CONST,0,XXXX));
    arl[1]=output(3);
    s=decompile(arl,2);
    assert(after(s,"float4 _sc = (float4)((vp_c[(2u + a0.x) & 511u]).xxxx);")<after(s,"floor("));
    assert(strstr(s,"(1.0/(_sc).x)"));

    /* A scalar-only LIT of an indexed, abs and negated constant repeats that
     * operand four times; the whole expression must survive. */
    insn lit[2];
    lit[0]=op(0,0,0x3F,0x07,0xF,4); lit[0].d[1]|=7u<<12; lit[0].d[3]|=1u<<1;
    lit[0].d[0]|=(1u<<23)|(1u<<24)|3u;
    src2(&lit[0],src(CONST,0,SWZ(1,1,3,3))|(1u<<16));
    lit[1]=output(4);
    s=decompile(lit,2);
    assert(count(s,"(-abs(vp_c[(7u + a1.w) & 511u]).yyww)")==4);
    assert(strstr(s,"1e-10))) : 0.0, 1.0));"));

    /* A vector-only program is emitted without the co-issue scope. */
    insn single[2];
    single[0]=op(V_MOV,0xF,1,0,0,0x3F); src0(&single[0],src(INPUT,0,XYZW));
    single[1]=output(1);
    assert(!strstr(decompile(single,2),"_sc"));
    puts("Vertex-program co-issue: scalar operands read before vector temp and address writes");
    return 0;
}
