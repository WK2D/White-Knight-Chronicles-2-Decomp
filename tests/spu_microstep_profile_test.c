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
    puts("Actual microstep dispatch, branch return and interpreter handoff accounting passed");
    return 0;
}
