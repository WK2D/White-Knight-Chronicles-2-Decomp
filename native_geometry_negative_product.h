// Project-written extended negative-product arithmetic for owned geometry jobs.
// Evaluate c-a*b on encoded extended binary32 values. No SDK or guest state.
#ifndef WKC_NATIVE_GEOMETRY_NEGATIVE_PRODUCT_H
#define WKC_NATIVE_GEOMETRY_NEGATIVE_PRODUCT_H
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace wkc::geometry::negative_product::portable_detail {
namespace detail {
struct AlignedMagnitude {
    std::uint64_t whole;
    bool fractional;
};

// floor(significand * 2^-shift), with an exact indication of a discarded tail.
// Callers normalize the largest term at bit 62. Therefore a left shift is at
// most 39 for a 24-bit addend, or 16 for a 47/48-bit product, and fits uint64_t.
constexpr AlignedMagnitude align_floor(std::uint64_t significand,int shift) noexcept {
    if(significand==0)return {0,false};
    if(shift<=0)return {significand<<unsigned(-shift),false};
    if(shift>=64)return {0,significand!=0};
    const auto mask=(std::uint64_t{1}<<unsigned(shift))-1;
    return {significand>>unsigned(shift),(significand&mask)!=0};
}
constexpr int highest_bit(std::uint64_t nonzero) noexcept {
    return 63-int(std::countl_zero(nonzero));
}
}

// Encodings represent signed extended binary32 values. Exponents 1..255 are
// finite, with value sign*(0x800000+fraction)*2^(exponent-150). Exponent zero
// represents zero regardless of sign/fraction. There are no NaN/Inf operands.
// Return c-a*b, truncated to 24 significant bits; underflow/cancellation gives
// positive zero, overflow saturates to sign|0x7fffffff. This reproduces the
// current compatibility contract even when the rest of a job uses HOST math.
constexpr std::uint32_t bits(std::uint32_t a,std::uint32_t b,std::uint32_t c) noexcept {
    const int a_exp=int((a>>23)&255u),b_exp=int((b>>23)&255u),c_exp=int((c>>23)&255u);
    if(a_exp==0||b_exp==0)return c_exp!=0?c:0u;

    const auto product=std::uint64_t((a&0x7fffffu)|0x800000u)*
        std::uint64_t((b&0x7fffffu)|0x800000u);
    const auto addend=c_exp!=0?std::uint64_t((c&0x7fffffu)|0x800000u):0u;
    const int product_scale=a_exp+b_exp-300,addend_scale=c_exp-150;
    const int product_top=product_scale+detail::highest_bit(product);
    const int addend_top=addend!=0?addend_scale+23:product_top;
    const int scale=(product_top>addend_top?product_top:addend_top)-62;
    const auto p=detail::align_floor(product,scale-product_scale);
    const auto q=detail::align_floor(addend,scale-addend_scale);

    const auto product_sign=(a^b^0x80000000u)&0x80000000u;
    const auto addend_sign=c&0x80000000u;
    std::uint64_t magnitude;
    std::uint32_t sign;
    if(product_sign==addend_sign) {
        // The largest term is exact at this scale. Thus at most one tail is
        // fractional, and adding it cannot carry into the whole integer.
        magnitude=p.whole+q.whole;
        sign=product_sign;
    } else if(p.whole>=q.whole) {
        // Subtracting a nonzero fractional tail borrows one whole unit.
        // Cancellation near equal magnitudes has no tail: the complete
        // 48-bit product and 24-bit addend fit in the 63-bit working window.
        magnitude=p.whole-q.whole-std::uint64_t(q.fractional);
        sign=product_sign;
    } else {
        magnitude=q.whole-p.whole-std::uint64_t(p.fractional);
        sign=addend_sign;
    }
    if(magnitude==0)return 0;
    const int top=detail::highest_bit(magnitude);
    const int exponent=scale+top+127;
    if(exponent<=0)return 0;
    if(exponent>255)return sign|0x7fffffffu;
    const auto significand=top>23?magnitude>>unsigned(top-23):magnitude<<unsigned(23-top);
    return sign|(std::uint32_t(exponent)<<23)|(std::uint32_t(significand)&0x7fffffu);
}

inline float scalar(float a,float b,float c) noexcept {
    return std::bit_cast<float>(bits(std::bit_cast<std::uint32_t>(a),
        std::bit_cast<std::uint32_t>(b),std::bit_cast<std::uint32_t>(c)));
}
inline std::array<float,4> vector(const std::array<float,4>& a,
                                const std::array<float,4>& b,
                                const std::array<float,4>& c) noexcept {
    std::array<float,4> result;
    for(unsigned lane=0;lane<4;++lane)result[lane]=scalar(a[lane],b[lane],c[lane]);
    return result;
}
}

namespace wkc::geometry::negative_product {
using Float4=std::array<float,4>;
constexpr std::uint32_t bits(std::uint32_t a,std::uint32_t b,std::uint32_t c) noexcept {
    return portable_detail::bits(a,b,c);
}
inline float scalar(float a,float b,float c) noexcept {
    return portable_detail::scalar(a,b,c);
}
inline Float4 portable(const Float4& a,const Float4& b,const Float4& c) noexcept {
    return portable_detail::vector(a,b,c);
}

#if defined(__AVX2__)
namespace detail {
inline __m256i choose(__m256i mask,__m256i when_true,__m256i when_false) noexcept {
    return _mm256_blendv_epi8(when_false,when_true,mask);
}
inline __m256i words(const Float4& input) noexcept {
    __m128i raw;std::memcpy(&raw,input.data(),16);
    return _mm256_cvtepu32_epi64(raw);
}
struct Floor {
    __m256i whole;
    __m256i fractional; // All bits one iff the exact nonnegative tail is nonzero.
};
inline Floor align_floor(__m256i magnitude,__m256i shift) noexcept {
    const auto zero=_mm256_setzero_si256();
    // AVX2 variable shifts with counts >=64 return zero. A negative signed
    // count therefore disables that direction, without C++ out-of-range shifts.
    const auto right=_mm256_srlv_epi64(magnitude,shift);
    const auto left=_mm256_sllv_epi64(magnitude,_mm256_sub_epi64(zero,shift));
    const auto reconstructed=_mm256_sllv_epi64(right,shift);
    const auto positive=_mm256_cmpgt_epi64(shift,zero);
    const auto equal=_mm256_cmpeq_epi64(magnitude,reconstructed);
    return {_mm256_or_si256(right,left),_mm256_andnot_si256(equal,positive)};
}

// Four independent uint64_t leading-bit indices. Select a nonzero high half,
// or otherwise the low half, and convert that 32-bit integer to double exactly.
// This avoids rounding a whole 64-bit integer or using float.
// Positive int32 values have an exact double exponent. Negative halves denote
// an unsigned high bit and are assigned bit 31 directly. This conversion is
// exact under every host rounding mode and does not alter exception flags.
// A zero input returns -1023; the caller's underflow test flushes that lane.
inline __m256i highest_bits(__m256i values) noexcept {
    const auto zero=_mm256_setzero_si256();
    const auto high=_mm256_srli_epi64(values,32);
    const auto high_zero=_mm256_cmpeq_epi64(high,zero);
    const auto selected=choose(high_zero,values,high);
    const auto packed=_mm256_permutevar8x32_epi32(selected,_mm256_setr_epi32(0,2,4,6,0,2,4,6));
    const auto converted=_mm256_castpd_si256(_mm256_cvtepi32_pd(_mm256_castsi256_si128(packed)));
    auto index=_mm256_sub_epi64(_mm256_srli_epi64(converted,52),_mm256_set1_epi64x(1023));
    // A negative signed conversion means the selected uint32 has bit 31 set.
    // Its sign contributes 2048 to index; clamping at 31 handles that case.
    // Each index is in [-1023,2079], so signed 32-bit min preserves the high
    // half's sign extension while clamping the low half of every 64-bit lane.
    index=_mm256_min_epi32(index,_mm256_set1_epi32(31));
    return _mm256_add_epi64(index,_mm256_andnot_si256(high_zero,_mm256_set1_epi64x(32)));
}
}

inline Float4 avx2(const Float4& a,const Float4& b,const Float4& c) noexcept {
    const auto zero=_mm256_setzero_si256();
    const auto one=_mm256_set1_epi64x(1);
    const auto fraction_mask=_mm256_set1_epi64x(0x7fffff);
    const auto implicit_one=_mm256_set1_epi64x(0x800000);
    const auto sign_bit=_mm256_set1_epi64x(0x80000000ll);
    const auto exponent_mask=_mm256_set1_epi64x(255);
    const auto a_words=detail::words(a),b_words=detail::words(b),c_words=detail::words(c);
    const auto a_exponent=_mm256_and_si256(_mm256_srli_epi64(a_words,23),exponent_mask);
    const auto b_exponent=_mm256_and_si256(_mm256_srli_epi64(b_words,23),exponent_mask);
    const auto c_exponent=_mm256_and_si256(_mm256_srli_epi64(c_words,23),exponent_mask);
    const auto c_zero=_mm256_cmpeq_epi64(c_exponent,zero);
    const auto product=_mm256_mul_epu32(
        _mm256_or_si256(_mm256_and_si256(a_words,fraction_mask),implicit_one),
        _mm256_or_si256(_mm256_and_si256(b_words,fraction_mask),implicit_one));
    const auto addend=_mm256_andnot_si256(c_zero,
        _mm256_or_si256(_mm256_and_si256(c_words,fraction_mask),implicit_one));
    const auto product_scale=_mm256_sub_epi64(_mm256_add_epi64(a_exponent,b_exponent),_mm256_set1_epi64x(300));
    const auto addend_scale=_mm256_sub_epi64(c_exponent,_mm256_set1_epi64x(150));
    const auto product_top=_mm256_add_epi64(product_scale,
        _mm256_add_epi64(_mm256_set1_epi64x(46),_mm256_srli_epi64(product,47)));
    const auto addend_top=detail::choose(c_zero,product_top,
        _mm256_add_epi64(addend_scale,_mm256_set1_epi64x(23)));
    const auto scale=_mm256_sub_epi64(detail::choose(_mm256_cmpgt_epi64(product_top,addend_top),
        product_top,addend_top),_mm256_set1_epi64x(62));
    const auto p=detail::align_floor(product,_mm256_sub_epi64(scale,product_scale));
    const auto q=detail::align_floor(addend,_mm256_sub_epi64(scale,addend_scale));

    const auto product_sign=_mm256_and_si256(_mm256_xor_si256(
        _mm256_xor_si256(a_words,b_words),sign_bit),sign_bit);
    const auto addend_sign=_mm256_and_si256(c_words,sign_bit);
    const auto equal_signs=_mm256_cmpeq_epi64(product_sign,addend_sign);
    const auto q_larger=_mm256_cmpgt_epi64(q.whole,p.whole);
    const auto signed_difference=_mm256_sub_epi64(p.whole,q.whole);
    const auto absolute_difference=_mm256_sub_epi64(_mm256_xor_si256(signed_difference,q_larger),q_larger);
    // A true fractional mask is -1 in each 64-bit lane: adding it performs
    // the one-unit borrow without another mask/extract instruction.
    const auto difference=_mm256_add_epi64(absolute_difference,
        detail::choose(q_larger,p.fractional,q.fractional));
    const auto magnitude=detail::choose(equal_signs,_mm256_add_epi64(p.whole,q.whole),difference);
    const auto sign=detail::choose(q_larger,addend_sign,product_sign);

    const auto top=detail::highest_bits(magnitude);
    const auto exponent=_mm256_add_epi64(_mm256_add_epi64(scale,top),_mm256_set1_epi64x(127));
    const auto shift=_mm256_sub_epi64(top,_mm256_set1_epi64x(23));
    const auto significand=_mm256_or_si256(_mm256_srlv_epi64(magnitude,shift),
        _mm256_sllv_epi64(magnitude,_mm256_sub_epi64(zero,shift)));
    auto result=_mm256_or_si256(sign,_mm256_or_si256(_mm256_slli_epi64(exponent,23),
        _mm256_and_si256(significand,fraction_mask)));
    result=detail::choose(_mm256_cmpgt_epi64(exponent,exponent_mask),
        _mm256_or_si256(sign,_mm256_set1_epi64x(0x7fffffff)),result);
    // highest_bits(0) is -1023. The working scale is at most 195, so exact
    // cancellation also satisfies exponent<1 and needs no separate zero test.
    result=_mm256_andnot_si256(_mm256_cmpgt_epi64(one,exponent),result);
    const auto product_zero=_mm256_or_si256(_mm256_cmpeq_epi64(a_exponent,zero),
        _mm256_cmpeq_epi64(b_exponent,zero));
    result=detail::choose(product_zero,_mm256_andnot_si256(c_zero,c_words),result);
    const auto packed=_mm256_castsi256_si128(_mm256_permutevar8x32_epi32(result,
        _mm256_setr_epi32(0,2,4,6,0,2,4,6)));
    Float4 out;std::memcpy(out.data(),&packed,16);return out;
}
#endif

inline Float4 vector(const Float4& a,const Float4& b,const Float4& c) noexcept {
#if defined(__AVX2__)
    return avx2(a,b,c);
#else
    return portable(a,b,c);
#endif
}
}

#endif
