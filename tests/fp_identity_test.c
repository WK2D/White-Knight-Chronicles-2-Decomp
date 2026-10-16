#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_fp_decompiler.h"
#include "rsx_test_programs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void constant_mov(u8* p, u32 value)
{
    rsx_test_fp_instr(p,RSX_TEST_FP_OPC(RSX_TEST_FP_OP_MOV)|
        RSX_TEST_FP_MASK_XYZW|RSX_TEST_FP_END,2u|RSX_TEST_FP_SWZ_IDENT);
    for(unsigned i=0;i<4;++i) rsx_test_fp_put_word(p+16+i*4,value+i);
}
int main(void)
{
    u8 a[96]={0}, b[96]={0}, c[96]={0};
    static char ah[RSX_FP_HLSL_BUFFER_SIZE],bh[RSX_FP_HLSL_BUFFER_SIZE];
    u32 ac,bc;
    constant_mov(a,0x3F800000u); constant_mov(b,0x40000000u);
    assert(a!=b && memcmp(a,b,32));
    assert(rsx_fp_program_size(a,sizeof a)==32);
    assert(rsx_fp_code_equal(a,sizeof a,b,sizeof b));
    assert(rsx_fp_code_hash(a,sizeof a)==rsx_fp_code_hash(b,sizeof b));
    assert(rsx_fp_decompile_buffered_ex(a,sizeof a,0x40,0,ah,sizeof ah,&ac)>0);
    assert(rsx_fp_decompile_buffered_ex(b,sizeof b,0x40,0,bh,sizeof bh,&bc)>0);
    assert(ac==1 && bc==1 && !strcmp(ah,bh));
    rsx_fp_constant_block ak,bk;
    assert(rsx_fp_collect_constants(a,sizeof a,&ak)==1);
    assert(rsx_fp_collect_constants(b,sizeof b,&bk)==1);
    assert(memcmp(ak.values,bk.values,16));
    /* Unrelated resident programs after END are not part of the key. */
    memset(a+32,0x19,64); memset(b+32,0xE7,64);
    assert(rsx_fp_code_equal(a,sizeof a,b,sizeof b));
    /* Same opcode, different operand swizzle must remain distinct even if
     * a hash lookup were to collide. Equality inspects instruction bytes. */
    memcpy(c,b,sizeof c);
    rsx_test_fp_put_word(c+4,rsx_fp_read_word(c+4)^(1u<<9));
    assert(!rsx_fp_code_equal(a,sizeof a,c,sizeof c));
    memcpy(c,b,sizeof c);
    rsx_test_fp_put_word(c,rsx_fp_read_word(c)^(1u<<24));
    assert(!rsx_fp_code_equal(a,sizeof a,c,sizeof c));
    assert(!rsx_fp_code_equal(NULL,32,b,32));
    assert(!rsx_fp_code_equal(a,31,b,32));
    assert(!rsx_fp_code_equal(a,15,b,32));
    memset(c,0,sizeof c);
    assert(!rsx_fp_code_equal(c,sizeof c,c,sizeof c));
    /* Position and number of constant slots are structural, not payload. */
    rsx_test_fp_mov_r0_col0(c,RSX_TEST_FP_SWZ_IDENT);
    assert(!rsx_fp_code_equal(a,sizeof a,c,sizeof c));
    puts("Fragment identity preserves instruction bytes, ignores runtime constants and rejects malformed programs");
    return 0;
}
