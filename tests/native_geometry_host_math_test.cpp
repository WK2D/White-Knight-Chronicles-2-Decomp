// Compare the project-owned host policy with the previous live SDK-HOST
// arithmetic. Compile this translation unit with implicit FP contraction off.
#include "native_geometry_host_math.h"
#include "spu_helpers.h"
#include <array>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <random>
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#define WKC_HOST_MATH_TEST_MXCSR 1
#endif

namespace {
using namespace wkc::geometry;
#if defined(_MSC_VER)
#define WKC_HOST_MATH_ORACLE_NOINLINE __declspec(noinline)
#else
#define WKC_HOST_MATH_ORACLE_NOINLINE __attribute__((noinline))
#endif

u128 pack(const Float4& value) {u128 out;std::memcpy(&out,value.data(),16);return out;}
Float4 unpack(u128 value) {Float4 out;std::memcpy(out.data(),&value,16);return out;}
// Keep the old expression shape behind a call boundary. Otherwise the
// optimizer can common the candidate and oracle and hide operand-order faults.
WKC_HOST_MATH_ORACLE_NOINLINE Float4 sdk_multiply(const Float4& a,const Float4& b) {
    return unpack(spu_fm(pack(a),pack(b)));
}
WKC_HOST_MATH_ORACLE_NOINLINE Float4 sdk_multiply_add(const Float4& a,const Float4& b,const Float4& c) {
    return unpack(spu_fma(pack(a),pack(b),pack(c)));
}
WKC_HOST_MATH_ORACLE_NOINLINE Float4 sdk_multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
    return unpack(spu_fms(pack(a),pack(b),pack(c)));
}
WKC_HOST_MATH_ORACLE_NOINLINE Float4 sdk_add(const Float4& a,const Float4& b) {
    return unpack(spu_fa(pack(a),pack(b)));
}
WKC_HOST_MATH_ORACLE_NOINLINE Float4 sdk_subtract(const Float4& a,const Float4& b) {
    return unpack(spu_fs(pack(a),pack(b)));
}
WKC_HOST_MATH_ORACLE_NOINLINE bool sdk_greater(float a,float b) {
    return spu_fcgt(pack(Float4{a,a,a,a}),pack(Float4{b,b,b,b}))._u32[0]!=0;
}
WKC_HOST_MATH_ORACLE_NOINLINE bool sdk_magnitude_equal(float a,float b) {
    return spu_fcmeq(pack(Float4{a,a,a,a}),pack(Float4{b,b,b,b}))._u32[0]!=0;
}

struct HostModeScope {
    const int saved=g_spu_float_mode;
    HostModeScope() {g_spu_float_mode=SPU_FLOAT_HOST;}
    ~HostModeScope() {g_spu_float_mode=saved;}
};
struct EnvironmentScope {
    std::fenv_t saved{};
    bool captured=std::fegetenv(&saved)==0;
#ifdef WKC_HOST_MATH_TEST_MXCSR
    unsigned mxcsr=_mm_getcsr();
#endif
    ~EnvironmentScope() {
        if(captured)std::fesetenv(&saved);
#ifdef WKC_HOST_MATH_TEST_MXCSR
        _mm_setcsr(mxcsr);
#endif
    }
};
std::uint32_t bits(float value) {return std::bit_cast<std::uint32_t>(value);}

bool compare_case(const Float4& a,const Float4& b,const Float4& c,unsigned phase,unsigned serial) {
    const std::array<Float4,5> reference={sdk_multiply(a,b),sdk_multiply_add(a,b,c),
        sdk_multiply_subtract(a,b,c),sdk_add(a,b),sdk_subtract(a,b)};
    const std::array<Float4,5> candidate={HostGeometryMath::multiply(a,b),HostGeometryMath::multiply_add(a,b,c),
        HostGeometryMath::multiply_subtract(a,b,c),HostGeometryMath::add(a,b),HostGeometryMath::subtract(a,b)};
    constexpr const char* names[]={"multiply","multiply_add","multiply_subtract","add","subtract"};
    for(unsigned op=0;op<reference.size();++op)for(unsigned lane=0;lane<4;++lane) {
        if(bits(reference[op][lane])!=bits(candidate[op][lane])) {
            std::fprintf(stderr,"[native-host-math] BIT MISMATCH phase=%u case=%u op=%s lane=%u "
                "a=%08X b=%08X c=%08X sdk=%08X native=%08X\n",phase,serial,names[op],lane,
                bits(a[lane]),bits(b[lane]),bits(c[lane]),bits(reference[op][lane]),bits(candidate[op][lane]));
            return false; // Includes NaN sign/payload; no numeric-only exception.
        }
    }
    for(unsigned lane=0;lane<4;++lane) {
        const std::array<bool,2> expected={sdk_greater(a[lane],b[lane]),sdk_magnitude_equal(a[lane],b[lane])};
        const std::array<bool,2> actual={HostGeometryMath::greater(a[lane],b[lane]),
            HostGeometryMath::magnitude_equal(a[lane],b[lane])};
        for(unsigned op=0;op<expected.size();++op)if(expected[op]!=actual[op]) {
            std::fprintf(stderr,"[native-host-math] COMPARISON MISMATCH phase=%u case=%u op=%s lane=%u "
                "a=%08X b=%08X sdk=%u native=%u\n",phase,serial,op?"magnitude_equal":"greater",lane,
                bits(a[lane]),bits(b[lane]),unsigned(expected[op]),unsigned(actual[op]));
            return false;
        }
    }
    return true;
}

bool separate_rounding(unsigned phase) {
    const float a=std::bit_cast<float>(0x3f800001u),b=std::bit_cast<float>(0x3f7ffffeu);
    const Float4 va{a,a,a,a},vb{b,b,b,b},vc{-1,-1,-1,-1};
    const auto rounded=HostGeometryMath::add(HostGeometryMath::multiply(va,vb),vc);
    const auto fused=HostGeometryMath::multiply_add(va,vb,vc);
    const auto expected_rounded=sdk_add(sdk_multiply(va,vb),vc);
    const auto expected_fused=sdk_multiply_add(va,vb,vc);
    for(unsigned lane=0;lane<4;++lane)if(bits(rounded[lane])!=bits(expected_rounded[lane])||
        bits(fused[lane])!=bits(expected_fused[lane])||bits(rounded[lane])==bits(fused[lane])) {
        std::fprintf(stderr,"[native-host-math] contraction/rounding mismatch phase=%u lane=%u separate=%08X fused=%08X\n",
            phase,lane,bits(rounded[lane]),bits(fused[lane]));
        return false;
    }
    return true;
}

bool phase_cases(unsigned phase,unsigned& cases) {
    // Each lane visits the full Cartesian product, with different lane order.
    // Include signed zero, subnormal/normal boundaries, rounding neighbors,
    // overflow boundaries, infinities, and distinct quiet/signaling NaNs.
    constexpr std::uint32_t encodings[]={0,0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,
        0x00800000u,0x80800000u,0x00800001u,0x80800001u,0x3f7fffffu,0xbf7fffffu,
        0x3f800000u,0xbf800000u,0x3f800001u,0xbf800001u,0x7f7fffffu,0xff7fffffu,
        0x7f800000u,0xff800000u,0x7fc00001u,0xffc12345u,0x7f800001u,0xffa12345u};
    constexpr unsigned count=unsigned(std::size(encodings));
    if(!separate_rounding(phase))return false;
    unsigned serial=0;
    for(unsigned ai=0;ai<count;++ai)for(unsigned bi=0;bi<count;++bi)for(unsigned ci=0;ci<count;++ci) {
        Float4 a,b,c;
        for(unsigned lane=0;lane<4;++lane) {
            a[lane]=std::bit_cast<float>(encodings[(ai+lane)%count]);
            b[lane]=std::bit_cast<float>(encodings[(bi+3*lane)%count]);
            c[lane]=std::bit_cast<float>(encodings[(ci+7*lane)%count]);
        }
        if(!compare_case(a,b,c,phase,serial++))return false;
    }
    std::mt19937 random(0x4e415449u);
    for(unsigned trial=0;trial<64000;++trial) {
        Float4 a,b,c;
        for(unsigned lane=0;lane<4;++lane) {
            a[lane]=std::bit_cast<float>(std::uint32_t(random()));
            b[lane]=std::bit_cast<float>(std::uint32_t(random()));
            c[lane]=std::bit_cast<float>(std::uint32_t(random()));
        }
        if(!compare_case(a,b,c,phase,serial++))return false;
    }
    cases+=serial;
    return true;
}
}

int wkc2_test_native_geometry_host_math() {
    EnvironmentScope environment;
    if(!environment.captured) {
        std::fputs("[native-host-math] could not save caller floating environment\n",stderr);return 1;
    }
    HostModeScope mode;
    unsigned cases=0;
    // First preserve the exact caller environment, including inherited FTZ/DAZ.
    if(!phase_cases(0,cases))return 1;
    unsigned phases=1;
#ifdef WKC_HOST_MATH_TEST_MXCSR
    // Then compare the same corpus in all four x86 subnormal modes. Preserve
    // rounding, exception masks and every unrelated MXCSR bit in each phase.
    constexpr unsigned subnormal_mask=(1u<<15)|(1u<<6);
    for(unsigned flags=0;flags<4;++flags) {
        _mm_setcsr((environment.mxcsr&~subnormal_mask)|((flags&1u)?1u<<15:0u)|((flags&2u)?1u<<6:0u));
        if(!phase_cases(flags+1,cases))return 1;
        ++phases;
    }
#endif
    std::printf("[native-host-math] %u phases, %u vector cases; 7 SDK-HOST operations match raw bits/booleans "
        "(NaN policy: %s)\n",phases,cases,
        HostGeometryMath::nan_policy==HostGeometryNaNPolicy::windows_debug?"Windows debug":"Windows release");
    return 0;
}
