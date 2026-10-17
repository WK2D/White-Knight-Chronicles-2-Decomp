#include "spu_context.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" void spu_spurs_taskset_syscall(spu_context*);
extern "C" int spu_run_with_halt(void (*)(spu_context*),spu_context*);

static bool returned;
static void task_exit(spu_context* ctx) {
    spu_spurs_taskset_syscall(ctx);
    returned=true;
}
static void check_exit(spu_context* ctx,int image,bool bootstrap) {
    spu_context_init(ctx,0);
    ctx->image_id=image;
    ctx->status=SPU_STATUS_RUNNING;
    returned=false;
    const int halted=spu_run_with_halt(task_exit,ctx);
    assert(returned==bootstrap);
    assert(halted==(bootstrap ? 0 : 1));
    assert(ctx->status==(bootstrap ? SPU_STATUS_RUNNING : SPU_STATUS_STOPPED_BY_STOP));
}

// Runs the actual channel/syscall implementation; no game files or VM required.
int wkc2_test_spu_title(int selected) {
    auto* ctx=static_cast<spu_context*>(malloc(sizeof(spu_context)));
    assert(ctx);
    check_exit(ctx,22,selected==22);
    if(selected!=22 && selected>=0) check_exit(ctx,selected,true);
    // A selected CRI task's next EXIT ends it. Other tasks always end at once.
    check_exit(ctx,selected>=0 ? selected : 22,false);
    check_exit(ctx,7,false);
    // A disabled selection matches no image, including pure interpretation's -1.
    check_exit(ctx,-1,false);
    free(ctx);
    puts("SPU title selection and actual task EXIT behavior passed");
    return 0;
}
