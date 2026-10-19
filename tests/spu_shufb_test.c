/* Synthetic SHUFB semantics regression. No game code or captured game data. */
#include "spu_helpers.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef PS3RECOMP_SPU_SHUFB_SCALAR
#define PS3RECOMP_SPU_SHUFB_SCALAR 0
#endif
#if defined(WKC2_SHUFB_EXPECT_SIMD) && (!defined(__SSE4_1__) || PS3RECOMP_SPU_SHUFB_SCALAR)
#error This target must exercise the SSE4.1 SHUFB path
#endif
#if defined(WKC2_SHUFB_EXPECT_SCALAR) && !PS3RECOMP_SPU_SHUFB_SCALAR
#error This target must explicitly select the scalar SHUFB path
#endif

static uint32_t rng = UINT32_C(0x739a801f);
static uint32_t next_random(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* Interpret each numerical SPU word from its most significant byte. This
 * oracle deliberately uses neither SPU_W nor either SDK shuffle helper. */
static unsigned byte_at(u128 value, unsigned position)
{
    return (value._u32[position / 4] >> (24 - 8 * (position % 4))) & 255u;
}
static void set_byte(u128* value, unsigned position, unsigned byte)
{
    unsigned shift = 24 - 8 * (position % 4);
    uint32_t mask = UINT32_C(255) << shift;
    value->_u32[position / 4] = (value->_u32[position / 4] & ~mask) |
                                ((uint32_t)(byte & 255u) << shift);
}
static u128 expected_shuffle(u128 a, u128 b, u128 control)
{
    u128 result = {{0}};
    for (unsigned lane = 0; lane < 16; ++lane) {
        unsigned selector = byte_at(control, lane);
        unsigned value;
        if (selector >= 224) value = 128;
        else if (selector >= 192) value = 255;
        else if (selector >= 128) value = 0;
        else if (selector & 16) value = byte_at(b, selector & 15);
        else value = byte_at(a, selector & 15);
        set_byte(&result, lane, value);
    }
    return result;
}

static unsigned cases, failures;
static void check(u128 a, u128 b, u128 control)
{
    u128 expected = expected_shuffle(a, b, control);
    u128 actual = spu_shufb(a, b, control);
    u128 reference = spu_shufb_ref(a, b, control);
    ++cases;
    if (memcmp(&actual, &expected, sizeof actual) ||
        memcmp(&reference, &expected, sizeof reference)) {
        if (failures < 8)
            fprintf(stderr, "SHUFB mismatch at synthetic case %u (selector lane 0: %u)\n",
                    cases, byte_at(control, 0));
        ++failures;
    }
}

int main(void)
{
    u128 a = {{0}}, b = {{0}}, control = {{0}};
    for (unsigned lane = 0; lane < 16; ++lane) {
        set_byte(&a, lane, lane + 1);
        set_byte(&b, lane, lane + 0x71);
    }
    /* Every selector value in every output lane, plus mixed selectors in
     * neighbouring lanes to expose cross-byte shift/mask contamination. */
    for (unsigned selector = 0; selector < 256; ++selector) {
        for (unsigned lane = 0; lane < 16; ++lane)
            set_byte(&control, lane, selector);
        check(a, b, control);
        for (unsigned lane = 0; lane < 16; ++lane) {
            for (unsigned other = 0; other < 16; ++other)
                set_byte(&control, other, next_random());
            set_byte(&control, lane, selector);
            check(a, b, control);
        }
    }
    for (unsigned iteration = 0; iteration < 200000; ++iteration) {
        for (unsigned word = 0; word < 4; ++word) {
            a._u32[word] = next_random();
            b._u32[word] = next_random();
            control._u32[word] = next_random();
        }
        check(a, b, control);
    }
    if (cases != 204352 || failures) {
        fprintf(stderr, "SHUFB: %u cases, %u mismatches\n", cases, failures);
        return 1;
    }
#if defined(__SSE4_1__) && !PS3RECOMP_SPU_SHUFB_SCALAR
    puts("SHUFB SSE4.1: 204352 synthetic cases match independent SPU byte oracle");
#else
    puts("SHUFB scalar: 204352 synthetic cases match independent SPU byte oracle");
#endif
    return 0;
}
