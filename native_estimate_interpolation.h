// Explicit fused interpolation policy for encoded reciprocal/rsqrt estimates.
// This matches the current Windows reference policy; hardware-wide equivalence
// and execution on other architectures require their own validation.
#ifndef WKC_NATIVE_ESTIMATE_INTERPOLATION_H
#define WKC_NATIVE_ESTIMATE_INTERPOLATION_H
#include "native_skinning.h"
#include <bit>
#include <cmath>

namespace wkc::geometry {
struct FusedEstimateInterpolation {
    Float4 operator()(const Float4& operand,const Float4& encoded_estimate) const {
        Float4 result;
        for(unsigned lane=0;lane<4;++lane) {
            const auto encoded=std::bit_cast<std::uint32_t>(encoded_estimate[lane]);
            const auto input=std::bit_cast<std::uint32_t>(operand[lane]);
            const auto base=std::bit_cast<float>(encoded&0xfffffc00u);
            // The eight-bit exponent makes this power range -140..115.
            // Construct its bits so a subnormal power remains an input to the
            // separate rounded multiply under the caller's FTZ/DAZ settings.
            const int exponent=int((encoded>>23)&255u)-140;
            const auto power_bits=exponent>=-126?
                std::uint32_t(exponent+127)<<23:
                std::uint32_t{1}<<unsigned(exponent+149);
            float step=float(encoded&1023u)*std::bit_cast<float>(power_bits);
            // Ordered numeric comparison matters for negative zero and NaNs.
            if(base<0)step=-step;
            const float fraction=float(input&0x7ffffu)*0x1p-19f;
            result[lane]=std::fma(-step,fraction,base);
        }
        return result;
    }
};
}
#endif
