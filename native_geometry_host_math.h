// Project-written host arithmetic for the owned geometry pipeline.
// No SDK types, instruction helpers, SPU registers or mode dispatch.
#ifndef WKC_NATIVE_GEOMETRY_HOST_MATH_H
#define WKC_NATIVE_GEOMETRY_HOST_MATH_H
#include "native_skinning.h"

namespace wkc::geometry {
namespace host_geometry_math_detail {
inline float quiet_nan(float value) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value)|0x00400000u);
}
inline Float4 select_nan(Float4 result,const Float4& first,const Float4& second) {
    for(unsigned lane=0;lane<4;++lane) {
        if(host_binary32_is_nan(first[lane]))result[lane]=quiet_nan(first[lane]);
        else if(host_binary32_is_nan(second[lane]))result[lane]=quiet_nan(second[lane]);
    }
    return result;
}
inline Float4 select_nan(Float4 result,const Float4& first,const Float4& second,const Float4& third,
                         std::uint32_t third_sign_xor=0) {
    for(unsigned lane=0;lane<4;++lane) {
        if(host_binary32_is_nan(first[lane]))result[lane]=quiet_nan(first[lane]);
        else if(host_binary32_is_nan(second[lane]))result[lane]=quiet_nan(second[lane]);
        else if(host_binary32_is_nan(third[lane]))result[lane]=std::bit_cast<float>(
            (std::bit_cast<std::uint32_t>(third[lane])^third_sign_xor)|0x00400000u);
    }
    return result;
}
}
// This policy implements host IEEE arithmetic, not SPU extended floats.
// FM/FMA/FMS use HostScalarMath's separately rounded multiply and explicit
// fused operations. Callers must disable implicit FP contraction and retain
// their selected FP environment, including subnormal handling. The ordered
// position/cofactor NaN policies remain separate wrappers around this policy.
// The Windows SDK-HOST oracle's NaN choices differ with optimization: release
// FM/FA select b,a, while debug selects a,b. Both select b,a,c for FMA; debug
// FMS negates a selected c NaN's sign while release retains it. FS selects a,b.
// These are observed build contracts, not guarantees of C++ or the SPU ISA.
// Specify them explicitly rather than depend on register allocation/FMA opcode.
// Windows compatibility requires the SDK-HOST bitwise regression; other
// compilers/architectures, including ARM, need their own validation before use.
// FNMS, reciprocal estimates and integer conversions intentionally are absent.
enum class HostGeometryNaNPolicy {windows_release,windows_debug};
template<HostGeometryNaNPolicy Policy>
struct HostGeometryMathFor:HostScalarMath {
    static constexpr HostGeometryNaNPolicy nan_policy=Policy;
    static Float4 multiply(const Float4& a,const Float4& b) {
        const auto result=HostScalarMath::multiply(a,b);
        if constexpr(Policy==HostGeometryNaNPolicy::windows_debug)
            return host_geometry_math_detail::select_nan(result,a,b);
        else return host_geometry_math_detail::select_nan(result,b,a);
    }
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        return host_geometry_math_detail::select_nan(HostScalarMath::multiply_add(a,b,c),b,a,c);
    }
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        return host_geometry_math_detail::select_nan(HostScalarMath::multiply_subtract(a,b,c),b,a,c,
            Policy==HostGeometryNaNPolicy::windows_debug?0x80000000u:0u);
    }
    static Float4 add(const Float4& a,const Float4& b) {
        Float4 out;
        for(unsigned lane=0;lane<4;++lane)out[lane]=a[lane]+b[lane];
        if constexpr(Policy==HostGeometryNaNPolicy::windows_debug)
            return host_geometry_math_detail::select_nan(out,a,b);
        else return host_geometry_math_detail::select_nan(out,b,a);
    }
    static Float4 subtract(const Float4& a,const Float4& b) {
        Float4 out;
        for(unsigned lane=0;lane<4;++lane)out[lane]=a[lane]-b[lane];
        return host_geometry_math_detail::select_nan(out,a,b);
    }
    static bool greater(float a,float b) {return a>b;}
    static bool magnitude_equal(float a,float b) {return std::fabs(a)==std::fabs(b);}
};
#if defined(_DEBUG)
using HostGeometryMath=HostGeometryMathFor<HostGeometryNaNPolicy::windows_debug>;
#else
using HostGeometryMath=HostGeometryMathFor<HostGeometryNaNPolicy::windows_release>;
#endif
}
#endif
