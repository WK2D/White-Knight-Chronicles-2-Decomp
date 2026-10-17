#ifdef NDEBUG
#undef NDEBUG
#endif
#include "spu_helpers.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SPU single precision: truncation, denormals read as zero, exponent 255
 * finite, saturation. Exact routines are checked against the FNMS routine
 * validated in native runs and against an exact double reference; the vector
 * helpers are checked in the mode named by argv[1]. */
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
/* Any sign, a biased exponent in [lo, hi], any significand. */
static uint32_t make(int lo, int hi) { return (rnd() & 0x80000000u) | ((uint32_t)(lo + (int)(rnd() % (unsigned)(hi - lo + 1))) << 23) | (rnd() & 0x7fffffu); }
/* Exact double -> float toward zero; the inputs keep it in the normal range. */
static uint32_t truncate(double d) {
    float f = (float)d;
    if (fabs((double)f) > fabs(d)) f = nextafterf(f, 0.0f);
    return d == 0.0 ? 0u : bits(f);
}
static u128 splat(uint32_t v) { return spu_splat_u32(v); }

static void exact_routines(void) {
    /* The generalized routine reproduces FNMS bit for bit, every exponent. */
    for (int i = 0; i < 4000000; i++) {
        uint32_t a = rnd() ^ (rnd() << 16), b = rnd() ^ (rnd() << 16), c = rnd() ^ (rnd() << 16);
        if (i & 1) { a = make(0, 255); b = make(0, 255); c = make(0, 255); }
        assert(spu_sf_fused(a, b, c, 1, 0) == spu_sf_fnms(a, b, c));
    }
    /* Products, sums and near-cancelling FMA are exact in double here. */
    unsigned rounded = 0;
    for (int i = 0; i < 2000000; i++) {
        uint32_t a = make(97, 157), b = make(97, 157);
        uint32_t prod = spu_sf_fused(a, b, 0, 0, 0);
        assert(prod == truncate((double)flt(a) * flt(b)));
        rounded += prod != bits(flt(a) * flt(b));
        int ea = (int)((a >> 23) & 255);
        uint32_t c = make(ea - 20 < 1 ? 1 : ea - 20, ea + 20 > 254 ? 254 : ea + 20);
        assert(spu_sf_fused(a, 0x3f800000u, c, 0, 0) == truncate((double)flt(a) + flt(c)));
        assert(spu_sf_fused(a, 0x3f800000u, c, 0, 1) == truncate((double)flt(a) - flt(c)));
        int ep = ea + (int)((b >> 23) & 255) - 127;
        uint32_t d = make(ep - 2, ep + 2);
        assert(spu_sf_fused(a, b, d, 0, 0) == truncate((double)flt(a) * flt(b) + flt(d)));
        assert(spu_sf_fused(a, b, d, 0, 1) == truncate((double)flt(a) * flt(b) - flt(d)));
    }
    assert(rounded > 100000);   /* truncation differs from host rounding */

    /* Exponent 255 is finite; overflow saturates; denormals read as zero. */
    assert(spu_sf_fused(0x7f800000u, 0x3f000000u, 0, 0, 0) == 0x7f000000u);  /* 2^128 * 0.5 */
    assert(spu_sf_fused(0x7f800000u, 0x40000000u, 0, 0, 0) == 0x7fffffffu);  /* saturates */
    assert(spu_sf_fused(0xffc00000u, 0x40000000u, 0, 0, 0) == 0xffffffffu);
    assert(spu_sf_fused(0x7fc00000u, 0, 0, 0, 0) == 0);                       /* host NaN * 0 */
    assert(spu_sf_fused(0x00400000u, 0x3f800000u, 0x00400000u, 0, 0) == 0);    /* denormal sum */
    assert(spu_sf_fused(0x00800000u, 0x3f000000u, 0, 0, 0) == 0);             /* underflow */
    assert(spu_sf_fused(0x3f800000u, 0x3f800000u, 0x3f800000u, 0, 1) == 0);   /* exact zero */

    /* Conversions truncate and saturate by sign, exponent 255 included. */
    assert(spu_sf_cflt(0x3f800000u, 173, 1) == 1 && spu_sf_cflt(bits(-2.75f), 173, 1) == (uint32_t)-2);
    assert(spu_sf_cflt(bits(42.5f), 172, 1) == 85 && spu_sf_cflt(bits(0.75f), 173, 1) == 0);
    assert(spu_sf_cflt(0x7fc00000u, 173, 1) == 0x7fffffffu && spu_sf_cflt(0xffc00000u, 173, 1) == 0x80000000u);
    assert(spu_sf_cflt(bits(-1.0f), 173, 0) == 0 && spu_sf_cflt(0x7fc00000u, 173, 0) == 0xffffffffu);
    assert(spu_sf_cflt(bits(4294967040.0f), 173, 0) == 4294967040u && spu_sf_cflt(0x00400000u, 173, 1) == 0);
    for (int i = 0; i < 1000000; i++) {
        uint32_t a = make(100, 157); int i8 = 173 - (int)(rnd() % 8);
        double v = (double)flt(a) * ldexp(1.0, 173 - i8);
        int32_t s = v > 2147483647.0 ? 2147483647 : v < -2147483648.0 ? (-2147483647 - 1) : (int32_t)v;
        assert(spu_sf_cflt(a, i8, 1) == (uint32_t)s);
    }
    assert(spu_sf_cvflt(16777219u, 155, 0) == bits(16777218.0f));   /* host rounds to 16777220 */
    assert(spu_sf_cvflt((uint32_t)-16777219, 155, 1) == bits(-16777218.0f));
    assert(spu_sf_cvflt(8, 156, 0) == bits(16.0f) && spu_sf_cvflt(8, 154, 1) == bits(4.0f));
    assert(spu_sf_cvflt(0x80000000u, 155, 1) == bits(-2147483648.0f) && spu_sf_cvflt(0, 155, 1) == 0);
    for (int i = 0; i < 1000000; i++) {
        uint32_t v = rnd() ^ (rnd() << 16); int i8 = 150 + (int)(rnd() % 10);
        assert(spu_sf_cvflt(v, i8, 0) == truncate((double)v * ldexp(1.0, i8 - 155)));
        assert(spu_sf_cvflt(v, i8, 1) == truncate((double)(int32_t)v * ldexp(1.0, i8 - 155)));
    }
    /* Compare order: zero and denormals equal, exponent 255 ordinary. */
    assert(spu_sf_order(0x00000001u) == spu_sf_order(0x80000000u));
    assert(spu_sf_order(0x7fc00000u) > spu_sf_order(0x7f7fffffu));
    assert(spu_sf_order(0xffc00000u) < spu_sf_order(0xff7fffffu));
    assert(spu_sf_magnitude(0xffc00000u) == spu_sf_magnitude(0x7fc00000u));
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* mode = argv[1];
    int expect = !strcmp(mode, "exact") ? SPU_FLOAT_EXACT : !strcmp(mode, "audit") ? SPU_FLOAT_AUDIT : SPU_FLOAT_HOST;
    assert(spu_float_mode_init() == expect && g_spu_float_mode == expect);
    exact_routines();

    /* 2^128 (host +Inf) times 0.5, and a value host and SPU round apart. */
    u128 r = spu_fm(splat(0x7f800000u), splat(0x3f000000u));
    u128 s = spu_csflt(splat(16777219u), 155);
    u128 g = spu_fcgt(splat(0x7fc00000u), splat(0x3f800000u));
    u128 c = spu_cflts(splat(0x7fc00000u), 173);
    uint32_t inf = bits(INFINITY);
    if (expect == SPU_FLOAT_EXACT) {
        assert(r._u32[0] == 0x7f000000u && s._u32[0] == bits(16777218.0f));
        assert(g._u32[0] == 0xffffffffu && c._u32[0] == 0x7fffffffu);
        r = spu_fa(splat(0x00400000u), splat(0x00400000u)); assert(r._u32[3] == 0);
        r = spu_fma(splat(0x3f800000u), splat(0x40000000u), splat(0x40400000u)); assert(r._f32[2] == 5.0f);
    } else {
        /* Host and audit modes keep today's results. */
        assert(r._u32[0] == inf && s._u32[0] == bits(16777220.0f) && g._u32[0] == 0);
        r = spu_fa(splat(0x00400000u), splat(0x00400000u)); assert(r._u32[1] == 0x00800000u);
    }
    uint64_t special = spu_float_audit_count(SPU_FOP_FM, 1), rounding = spu_float_audit_count(SPU_FOP_CSFLT, 0);
    if (expect == SPU_FLOAT_AUDIT)
        assert(special == 4 && rounding == 4 && spu_float_audit_count(SPU_FOP_FA, 1) == 4 &&
               spu_float_audit_count(SPU_FOP_FCGT, 1) == 4 && spu_float_audit_count(SPU_FOP_CFLTS, 1) == 4);
    else
        assert(special == 0 && rounding == 0);
    printf("SPU float mode %s: exact routines, saturation, conversions, compares and lane selection passed\n", mode);
    return 0;
}
