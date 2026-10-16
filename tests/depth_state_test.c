#include "rsx_depth_state.h"
#include <assert.h>
#include <stdio.h>

static int passes(unsigned f,float a,float b) {
    switch(f) {
    case 1:return 0; case 2:return a<b; case 3:return a==b;
    case 4:return a<=b; case 5:return a>b; case 6:return a!=b;
    case 7:return a>=b; case 8:return 1;
    default:assert(0);return 0;
    }
}
static int draw(unsigned key,float z,float* stored) {
    if (rsx_depth_enabled(key) && !passes(rsx_depth_comparison(key),z,*stored)) return 0;
    if (rsx_depth_writes(key)) *stored=z;
    return 1;
}
int main(void) {
    for (unsigned f=0;f<8;++f) {
        unsigned gl=rsx_depth_key(1,1,0x200+f);
        assert(rsx_depth_comparison(gl)==f+1);
        assert(gl==rsx_depth_key(1,1,f));
        assert(rsx_depth_enabled(gl) && rsx_depth_writes(gl));
    }
    assert(!rsx_depth_key(1,1,0x208));
    assert(!rsx_depth_key(1,1,0xffffffff));
    // Deferred submission: a disabled-depth UI quad must not occlude later
    // geometry. Changes in the next draw must not reuse the earlier pipeline.
    unsigned ui=rsx_depth_key(0,1,0x203);
    unsigned model=rsx_depth_key(1,1,0x203);
    unsigned overlay=rsx_depth_key(1,0,0x207);
    assert(ui!=model && model!=overlay && ui!=overlay);
    float depth=1;
    assert(draw(ui,0.5f,&depth) && depth==1);
    assert(draw(model,0.9965f,&depth) && depth==0.9965f);
    assert(draw(overlay,0.2f,&depth) && depth==0.9965f);
    assert(!draw(model,0.999f,&depth));
    assert(draw(rsx_depth_key(1,0,0x204),0.999f,&depth));
    assert(!draw(rsx_depth_key(1,0,0x200),0,&depth));
    assert(passes(3,0.5f,0.5f) && !passes(6,0.5f,0.5f));
    assert(passes(7,0.5f,0.5f) && !passes(2,0.5f,0.5f));
    puts("Guest depth comparisons, deferred state and UI/model occlusion passed");
    return 0;
}
