#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cellGcmSys.h"
#include "rsx_fp_decompiler.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned cases=0;
    /* Integer color formats include ABGR8 and both XBGR8 variants. The
     * unrelated upper bits of an integer reference must be ignored. */
    const unsigned integer_formats[]={1,2,3,4,5,8,9,10,
        CELL_GCM_SURFACE_X8B8G8R8_Z8B8G8R8,
        CELL_GCM_SURFACE_X8B8G8R8_O8B8G8R8,CELL_GCM_SURFACE_A8B8G8R8};
    for(unsigned f=0;f<sizeof integer_formats/sizeof *integer_formats;++f)
        for(unsigned raw=0;raw<256;++raw) {
            const float expected=(float)raw/255.f;
            assert(rsx_fp_alpha_ref(0xa55a0000u|raw,integer_formats[f])==expected);
            ++cases;
        }
    /* Independent arithmetic half-float oracle, including subnormals, both
     * signed zeroes, infinities and NaNs. */
    for(unsigned raw=0;raw<65536;++raw) {
        const unsigned e=(raw>>10)&31,m=raw&1023;
        float expected=e==0?ldexpf((float)m,-24):e==31?(m?NAN:INFINITY):ldexpf(1.f+(float)m/1024.f,(int)e-15);
        if(raw&0x8000)expected=-expected;
        const float actual=rsx_fp_alpha_ref(0xa55a0000u|raw,CELL_GCM_SURFACE_F_W16Z16Y16X16);
        if(isnan(expected))assert(isnan(actual));
        else assert(!memcmp(&actual,&expected,sizeof actual));
        ++cases;
    }
    const unsigned float_bits[]={0,0x80000000u,0x3e800000u,0x3f000000u,0x3f800000u,
        1,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u};
    const unsigned float_formats[]={CELL_GCM_SURFACE_F_W32Z32Y32X32,CELL_GCM_SURFACE_F_X32};
    for(unsigned f=0;f<2;++f)for(unsigned b=0;b<sizeof float_bits/sizeof *float_bits;++b) {
        const float actual=rsx_fp_alpha_ref(float_bits[b],float_formats[f]);
        assert(!memcmp(&actual,&float_bits[b],sizeof actual));++cases;
    }
    /* The captured depth-only foliage pass uses ABGR8 with GEQUAL 0x59.
     * Treating it as float bits produces a denormal that GPUs can flush to 0,
     * admitting even fully transparent samples. */
    const float foliage=rsx_fp_alpha_ref(0x59,CELL_GCM_SURFACE_A8B8G8R8);
    assert(foliage==89.f/255.f && !(0.f>=foliage) && 1.f>=foliage);
    printf("Alpha references: %u integer/half/float format cases and foliage threshold pass.\n",cases);
    return 0;
}
