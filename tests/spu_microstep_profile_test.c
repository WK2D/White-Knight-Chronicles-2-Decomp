#include "spu_context.h"
#include "spu_interp_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void spu_indirect_branch(spu_context*);
static spu_context ctx;
static void word(uint32_t address, uint32_t value) {
    for (unsigned i=0;i<4;i++) ctx.ls[address+i]=(uint8_t)(value>>(24-i*8));
}
static void check_row(const char* path,const char* kind,uint32_t entry,
                      unsigned long long sessions,unsigned long long steps) {
    FILE* f=fopen(path,"rb");assert(f);
    char line[256];int found=0;
    while(fgets(line,sizeof line,f)) {
        char k[16];int image;unsigned e;unsigned long long hash,s,t,m;
        if(sscanf(line,"%15s %d 0x%x %llx %llu %llu %llu",k,&image,&e,&hash,&s,&t,&m)==7 &&
           !strcmp(k,kind) && image==991 && e==entry) {
            assert(s==sessions && t==steps);found++;
        }
    }
    fclose(f);assert(found==1);
}
static void absolute_memory(void) {
    /* I16 denotes words, not quadwords. Guard both the intended destination
     * and the old, erroneous four-times-larger address; do not let a matching
     * bad store/load pair conceal the addressing error. */
    static const uint16_t immediates[]={0x0800,0x0803,0x8123,0xffff};
    for(unsigned j=0;j<sizeof immediates/sizeof *immediates;j++) {
        const uint32_t imm=immediates[j], address=(imm*4)&0x3fff0;
        const uint32_t wrong=(imm*16)&0x3fff0;
        spu_context_init(&ctx,0);ctx.image_id=992;ctx.policy_mode=1;
        memset(ctx.ls,0xa5,0x40000);
        for(unsigned i=0;i<16;i++) ctx.gpr[2]._u8[i]=(uint8_t)(0x30+i+j);
        const u128 expected=ctx.gpr[2];
        ctx.pc=0x100;ctx.status=SPU_STATUS_RUNNING;ctx.gpr[0]._u32[0]=0x200;
        word(0x100,(0x041u<<23)|(imm<<7)|2); /* stqa $2,I16 */
        word(0x104,0x35000000); /* bi $0 */
        spu_indirect_branch(&ctx);assert(ctx.pc==0x200);
        /* SPU LS is big endian within each host word. */
        for(unsigned i=0;i<16;i++) assert(ctx.ls[address+i]==expected._u8[i^3]);
        if(wrong!=address) for(unsigned i=0;i<16;i++) assert(ctx.ls[wrong+i]==0xa5);
        /* Load an independently initialized pattern, with a different guard. */
        for(unsigned i=0;i<16;i++) ctx.ls[address+i]=(uint8_t)(0x70+i+j);
        ctx.pc=0x100;ctx.status=SPU_STATUS_RUNNING;
        word(0x100,(0x061u<<23)|(imm<<7)|3); /* lqa $3,I16 */
        spu_indirect_branch(&ctx);
        for(unsigned i=0;i<16;i++) assert(ctx.gpr[3]._u8[i]==(uint8_t)(0x70+(i^3)+j));
    }
}
int wkc2_test_spu_microstep_profile(const char* path) {
    remove(path);
    for(unsigned i=0;i<2;i++) {
        spu_context_init(&ctx,0);ctx.image_id=991;ctx.policy_mode=1;
        ctx.pc=0x100;ctx.status=SPU_STATUS_RUNNING;ctx.gpr[0]._u32[0]=0x200;
        word(0x100,0x40800282); /* il $2,5 */
        word(0x104,0x35000000); /* bi $0 */
        spu_indirect_branch(&ctx);
        assert(ctx.pc==0x200 && ctx.gpr[2]._u32[0]==5);
    }
    spu_context_init(&ctx,0);ctx.image_id=991;ctx.policy_mode=1;
    ctx.pc=0x300;ctx.status=SPU_STATUS_RUNNING;
    word(0x300,0x40800282); /* microstep: il $2,5 */
    word(0x304,0x41800383); /* interpreter: ilh $3,7 */
    word(0x308,0x00002000); /* interpreter: stop 0x2000 */
    spu_indirect_branch(&ctx);
    assert(ctx.status&SPU_STATUS_STOPPED_BY_STOP);
    spu_interp_profile_flush();
    check_row(path,"microstep",0x100,2,4);
    check_row(path,"microstep",0x300,1,1);
    check_row(path,"interp",0x304,1,2);
    absolute_memory();
    puts("Actual microstep dispatch, branch return and interpreter handoff accounting passed");
    return 0;
}
