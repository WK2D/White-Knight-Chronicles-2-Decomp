#ifdef NDEBUG
#undef NDEBUG
#endif
#include "spu_interp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Isolated ISA snippets need no channels or lifted functions. */
u128 spu_rdch(spu_context* c,uint32_t n) {(void)c;(void)n;u128 r={0};return r;}
void spu_wrch(spu_context* c,uint32_t n,u128 v) {(void)c;(void)n;(void)v;}
uint32_t spu_rchcnt(spu_context* c,uint32_t n) {(void)c;(void)n;return 1;}
int g_spu_ls_watch_n,g_spu_ls_probe,g_wws_read_probe,g_wws_code_probe,g_spu_smc_watch;
spu_lifted_fn spu_lifted_lookup(const spu_context* c,uint32_t a) {(void)c;(void)a;return 0;}
void spu_spurs_taskset_syscall(spu_context* c) {(void)c;}
void spu_ls_watch_slow(uint32_t a,int w,const uint8_t* p,uint32_t pc,uint32_t lr)
{(void)a;(void)w;(void)p;(void)pc;(void)lr;}
static spu_context ctx;
static void word(unsigned address,uint32_t value) {
    for(unsigned i=0;i<4;i++) ctx.ls[address+i]=(uint8_t)(value>>(24-8*i));
}
static void accumulate(unsigned opcode,int is_unsigned,unsigned rt,unsigned ra,unsigned rb) {
    spu_context_init(&ctx,0);
    for(unsigned r=0;r<128;r++) for(unsigned lane=0;lane<4;lane++)
        ctx.gpr[r]._u32[lane]=0x7ffff000u+r*73+lane*997;
    for(unsigned lane=0;lane<4;lane++) {
        ctx.gpr[ra]._u16[2*lane+1]=(uint16_t)(lane&1 ? 0xffff : 0x8000);
        ctx.gpr[rb]._u16[2*lane+1]=(uint16_t)(lane&2 ? 0x8000 : 0x7fff);
    }
    u128 before[128];memcpy(before,ctx.gpr,sizeof before);
    uint32_t expected[4];
    for(unsigned lane=0;lane<4;lane++) {
        int64_t a=is_unsigned?before[ra]._u16[2*lane+1]:before[ra]._s16[2*lane+1];
        int64_t b=is_unsigned?before[rb]._u16[2*lane+1]:before[rb]._s16[2*lane+1];
        expected[lane]=before[rt]._u32[lane]+(uint32_t)(a*b);
    }
    word(0x100,(opcode<<21)|(rb<<14)|(ra<<7)|rt);
    word(0x104,0x00002000); /* stop */
    spu_interp_run(&ctx,0x100);assert(ctx.status&SPU_STATUS_STOPPED_BY_STOP);
    for(unsigned lane=0;lane<4;lane++) assert(ctx.gpr[rt]._u32[lane]==expected[lane]);
    for(unsigned r=0;r<128;r++) if(r!=rt) assert(!memcmp(&ctx.gpr[r],&before[r],sizeof(u128)));
}
static void execute(uint32_t instruction) {
    word(0x100,instruction);word(0x104,0x00002000);
    spu_interp_run(&ctx,0x100);assert(ctx.status&SPU_STATUS_STOPPED_BY_STOP);
}
static void borrow(void) {
    const uint32_t pairs[][2]={{0,0},{0,1},{1,0},{0xffffffff,0},{0,0xffffffff},{0xffffffff,0xffffffff}};
    for(unsigned p=0;p<sizeof pairs/sizeof *pairs;p++) for(unsigned bit=0;bit<2;bit++) {
        spu_context_init(&ctx,0);
        for(unsigned i=0;i<4;i++) {
            ctx.gpr[2]._u32[i]=pairs[p][0];ctx.gpr[3]._u32[i]=pairs[p][1];
            ctx.gpr[5]._u32[i]=0xffff0000u+bit;
        }
        execute((0x343u<<21)|(3<<14)|(2<<7)|5); /* bgx */
        uint32_t expected=pairs[p][1]>pairs[p][0] || (bit && pairs[p][1]==pairs[p][0]);
        for(unsigned i=0;i<4;i++) assert(ctx.gpr[5]._u32[i]==expected);
    }
}
static void unsigned_immediate(void) {
    const int immediate[]={-512,-1,0,1,511};
    for(unsigned j=0;j<sizeof immediate/sizeof *immediate;j++) {
        spu_context_init(&ctx,0);
        for(unsigned i=0;i<4;i++) ctx.gpr[2]._u32[i]=0xabcd0000u+0x5555*i;
        execute((0x75u<<24)|(((uint32_t)immediate[j]&1023)<<14)|(2<<7)|5);
        for(unsigned i=0;i<4;i++) assert(ctx.gpr[5]._u32[i]==(0x5555u*i)*(uint16_t)immediate[j]);
    }
}
static void halfword_shifts(void) {
    for(int imm=-64;imm<64;imm++) for(unsigned arithmetic=0;arithmetic<2;arithmetic++) {
        spu_context_init(&ctx,0);
        for(unsigned i=0;i<8;i++) ctx.gpr[2]._u16[i]=(uint16_t)(i*9751+0x8001);
        execute(((arithmetic?0x7eu:0x7du)<<21)|(((uint32_t)imm&127)<<14)|(2<<7)|5);
        unsigned shift=(unsigned)(-imm)&31;
        for(unsigned i=0;i<8;i++) {
            int32_t value=arithmetic?ctx.gpr[2]._s16[i]:ctx.gpr[2]._u16[i];
            /* Division with explicit negative rounding avoids relying on
             * the host's signed right-shift when constructing the oracle. */
            int32_t expected=shift>=16?(value<0?-1:0):
                (value>=0?value/(1<<shift):-((-value+(1<<shift)-1)/(1<<shift)));
            assert(ctx.gpr[5]._u16[i]==(uint16_t)expected);
        }
    }
}
int main(int argc,char** argv) {
    if(argc==2) {
        if(!strcmp(argv[1],"bgx")) borrow();
        else if(!strcmp(argv[1],"mpyui")) unsigned_immediate();
        else if(!strcmp(argv[1],"halfshift")) halfword_shifts();
        else assert(0);
        return 0;
    }
    for(unsigned u=0;u<2;u++) {
        unsigned opcode=u?0x34e:0x346;
        accumulate(opcode,u,5,2,3);
        accumulate(opcode,u,2,2,3);
        accumulate(opcode,u,3,2,3);
        accumulate(opcode,u,5,5,5);
    }
    borrow();unsigned_immediate();halfword_shifts();
    puts("MPYHHA/MPYHHAU: RR destination, preserved registers, aliasing and modular overflow pass.");
}
