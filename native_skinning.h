// Project-written math recovered from the image-19 geometry block.
// This operates on decoded native values, never guest memory or SPU registers.
#ifndef WKC_NATIVE_SKINNING_H
#define WKC_NATIVE_SKINNING_H
#include <array>
#include <cmath>

namespace wkc::geometry {
using Float4 = std::array<float,4>;
// Three cyclic coefficient vectors, each with a translation component in w:
//   [m00,m11,m22,tx], [m01,m12,m20,ty], [m02,m10,m21,tz].
// A decoder must establish this layout; ordinary row-major matrices differ.
using CyclicAffine = std::array<Float4,3>;
struct SkinVertex {
    Float4 position;
    Float4 weights;
    std::array<CyclicAffine,4> influences;
};
struct SkinResult {
    CyclicAffine blended;
    Float4 position;
};

// Arithmetic is explicit so a caller can preserve SPU extended-float behavior.
// This default implements host IEEE math; it is not an exact-SPU specification.
struct HostScalarMath {
    static Float4 multiply(const Float4& a,const Float4& b) {
        Float4 out{};
        for(unsigned i=0;i<4;++i) out[i]=a[i]*b[i];
        return out;
    }
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        Float4 out{};
        for(unsigned i=0;i<4;++i) out[i]=std::fma(a[i],b[i],c[i]);
        return out;
    }
};

// Preserve a rounded first product followed by three fused accumulations.
// Do not normalize weights or discard zero-weight influences: those changes
// can alter signed zero, rounding and exceptional-value behavior.
template<class Math=HostScalarMath>
CyclicAffine blend_skin_matrix(const Float4& weights,const std::array<CyclicAffine,4>& influences) {
    CyclicAffine out{};
    for(unsigned phase=0;phase<3;++phase) {
        const Float4 first_weight{weights[0],weights[0],weights[0],weights[0]};
        auto value=Math::multiply(first_weight,influences[0][phase]);
        for(unsigned influence=1;influence<4;++influence) {
            const float weight=weights[influence];
            value=Math::multiply_add(Float4{weight,weight,weight,weight},
                                     influences[influence][phase],value);
        }
        out[phase]=value;
    }
    return out;
}
template<class Math=HostScalarMath>
SkinResult skin_vertex(const SkinVertex& input) {
    SkinResult out{};
    out.blended=blend_skin_matrix<Math>(input.weights,input.influences);
    const auto& a=out.blended[0];const auto& b=out.blended[1];const auto& c=out.blended[2];
    const Float4 translation{a[3],b[3],c[3],0};
    const Float4 x{a[0],a[1],a[2],1};
    const Float4 y{b[0],b[1],b[2],0};
    const Float4 z{c[0],c[1],c[2],0};
    const auto& p=input.position;
    auto value=Math::multiply_add(p,x,translation);
    value=Math::multiply_add(Float4{p[1],p[2],p[0],p[3]},y,value);
    out.position=Math::multiply_add(Float4{p[2],p[0],p[1],p[3]},z,value);
    return out;
}
}
#endif
