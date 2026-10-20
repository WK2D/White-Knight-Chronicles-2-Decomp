// Project-written math recovered from the image-19 geometry block.
// This operates on decoded native values, never guest memory or SPU registers.
#ifndef WKC_NATIVE_SKINNING_H
#define WKC_NATIVE_SKINNING_H
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace wkc::geometry {
using Float4 = std::array<float,4>;
// Three cyclic coefficient vectors, each with a translation component in w:
//   [m00,m11,m22,tx], [m01,m12,m20,ty], [m02,m10,m21,tz].
// A decoder must establish this layout; ordinary row-major matrices differ.
using CyclicAffine = std::array<Float4,3>;
struct RowMajorAffine {
    std::array<Float4,3> rows;
};
// Palette preparation is a bit-preserving reorder, with no arithmetic.
constexpr CyclicAffine cyclic_skin_matrix(const RowMajorAffine& source) {
    const auto& m=source.rows;
    return {Float4{m[0][0],m[1][1],m[2][2],m[0][3]},
            Float4{m[0][1],m[1][2],m[2][0],m[1][3]},
            Float4{m[0][2],m[1][0],m[2][1],m[2][3]}};
}
struct SkinVertex {
    Float4 position;
    Float4 weights;
    std::array<CyclicAffine,4> influences;
};
struct SkinResult {
    CyclicAffine blended;
    Float4 position;
};
struct PositionStages {
    Float4 first;
    Float4 second;
    Float4 position;
};
struct CofactorStages {
    CyclicAffine products;
    CyclicAffine cofactors;
};
struct GeometryStages {
    PositionStages positions;
    CofactorStages normal_matrix;
    CyclicAffine homogeneous;
    CyclicAffine left;
    CyclicAffine right;
    Float4 first_normal_product;
};
using NormalBatch = std::array<Float4,4>;
struct NormalLengthStages {
    Float4 x, y, z;
    Float4 x_squared, zx_squared, length_squared;
};
struct NormalScaleStages {
    NormalBatch scales;
    NormalBatch normals;
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
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        Float4 out{};
        for(unsigned i=0;i<4;++i)out[i]=std::fma(a[i],b[i],-c[i]);
        return out;
    }
};

// Observed host-mode policy for the recovered position-only stream. Fused
// arithmetic selects a NaN from a, then b, then the accumulator, and quiets it
// without changing its sign/payload. Make this explicit rather than depend on
// the compiler's FMA operand allocation. Finite arithmetic delegates to Base.
// This is not the SPU extended-float policy; exact/audit adapters keep theirs.
template<class Base=HostScalarMath>
struct OrderedHostPositionStreamMath:Base {
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        auto out=Base::multiply_add(a,b,c);
        for(unsigned lane=0;lane<4;++lane) {
            const float* selected=std::isnan(a[lane])?&a[lane]:std::isnan(b[lane])?&b[lane]:std::isnan(c[lane])?&c[lane]:nullptr;
            if(selected)out[lane]=std::bit_cast<float>(std::bit_cast<std::uint32_t>(*selected)|0x00400000u);
        }
        return out;
    }
};

// Explicit compatibility policy for the recovered host-mode cofactor stage.
// Host compilers may commute NaN operands or fold -c into a fused instruction,
// changing payload/sign selection. Preserve the observed order instead: a
// two-NaN product selects a; fused subtraction selects a, then b, then c, with
// the selected sign intact. Quiet signaling NaNs. Finite arithmetic delegates
// to Base. This is host IEEE compatibility, not SPU extended-float semantics;
// an exact/audit adapter must keep using its own policy.
template<class Base=HostScalarMath>
struct OrderedHostCofactorMath:Base {
    static float quiet(float value) {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value)|0x00400000u);
    }
    static Float4 multiply(const Float4& a,const Float4& b) {
        auto out=Base::multiply(a,b);
        for(unsigned i=0;i<4;++i)if(std::isnan(a[i])&&std::isnan(b[i]))out[i]=quiet(a[i]);
        return out;
    }
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        auto out=Base::multiply_subtract(a,b,c);
        for(unsigned i=0;i<4;++i) {
            if(std::isnan(a[i]))out[i]=quiet(a[i]);
            else if(std::isnan(b[i]))out[i]=quiet(b[i]);
            else if(std::isnan(c[i]))out[i]=quiet(c[i]);
        }
        return out;
    }
};

// Four normals share one vector of squared lengths. Preserve the recovered
// x*x, fma(z,z,x*x), fma(y,y,...) order; w is not part of the length.
template<class Math=HostScalarMath>
NormalLengthStages normal_length_stages(const NormalBatch& normals) {
    NormalLengthStages out;
    for(unsigned vertex=0;vertex<4;++vertex) {
        out.x[vertex]=normals[vertex][0];
        out.y[vertex]=normals[vertex][1];
        out.z[vertex]=normals[vertex][2];
    }
    out.x_squared=Math::multiply(out.x,out.x);
    out.zx_squared=Math::multiply_add(out.z,out.z,out.x_squared);
    out.length_squared=Math::multiply_add(out.y,out.y,out.zx_squared);
    return out;
}

// Reciprocal lengths are supplied by the caller. The ABI bridge retains the
// SPU estimate/interpolation rules rather than substituting host sqrt().
// Multiplying w by one also retains the selected arithmetic policy.
template<class Math=HostScalarMath>
NormalScaleStages scale_skin_normals(const NormalBatch& normals,const Float4& reciprocal_lengths) {
    NormalScaleStages out;
    for(unsigned vertex=0;vertex<4;++vertex) {
        const float scale=reciprocal_lengths[vertex];
        out.scales[vertex]={scale,scale,scale,1};
        out.normals[vertex]=Math::multiply(normals[vertex],out.scales[vertex]);
    }
    return out;
}

// Cofactors transform a normal without dividing by the determinant. Keep the
// rounded subtrahend product distinct from the fused multiply-subtract. The w
// lanes retain homogeneous identity (1,0,0), rather than matrix translation.
template<class Math=HostScalarMath>
CofactorStages cofactor_skin_stages(const CyclicAffine& matrix) {
    const auto& a=matrix[0];const auto& b=matrix[1];const auto& c=matrix[2];
    const Float4 ay{a[1],a[2],a[0],1},az{a[2],a[0],a[1],1};
    const Float4 by{b[1],b[2],b[0],0},bz{b[2],b[0],b[1],0};
    const Float4 cy{c[1],c[2],c[0],0},cz{c[2],c[0],c[1],0};
    CofactorStages out;
    out.products={Math::multiply(cz,by),Math::multiply(cy,az),Math::multiply(bz,ay)};
    out.cofactors={Math::multiply_subtract(ay,az,out.products[0]),
                   Math::multiply_subtract(bz,by,out.products[1]),
                   Math::multiply_subtract(cy,cz,out.products[2])};
    return out;
}
template<class Math=HostScalarMath>
CyclicAffine cofactor_skin_matrix(const CyclicAffine& matrix) {
    return cofactor_skin_stages<Math>(matrix).cofactors;
}

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
PositionStages transform_skin_position(const CyclicAffine& blended,const Float4& p) {
    const auto& a=blended[0];const auto& b=blended[1];const auto& c=blended[2];
    const Float4 translation{a[3],b[3],c[3],0};
    const Float4 x{a[0],a[1],a[2],1};
    const Float4 y{b[0],b[1],b[2],0};
    const Float4 z{c[0],c[1],c[2],0};
    PositionStages out;
    out.first=Math::multiply_add(p,x,translation);
    out.second=Math::multiply_add(Float4{p[1],p[2],p[0],p[3]},y,out.first);
    out.position=Math::multiply_add(Float4{p[2],p[0],p[1],p[3]},z,out.second);
    return out;
}

// One vertex's arithmetic at the geometry batch boundary. The first normal
// product is carried to the next block, where the other two terms are added.
// Keep the intermediate products and cyclic rows for the narrow ABI bridge.
template<class Math=HostScalarMath>
GeometryStages prepare_skin_geometry(const CyclicAffine& matrix,const Float4& position,const Float4& normal) {
    GeometryStages out;
    out.positions=transform_skin_position<Math>(matrix,position);
    out.normal_matrix=cofactor_skin_stages<Math>(matrix);
    for(unsigned phase=0;phase<3;++phase) {
        const auto& row=matrix[phase];const float w=phase==0?1.f:0.f;
        out.homogeneous[phase]={row[0],row[1],row[2],w};
        out.left[phase]={row[1],row[2],row[0],w};
        out.right[phase]={row[2],row[0],row[1],w};
    }
    out.first_normal_product=Math::multiply(normal,out.normal_matrix.cofactors[0]);
    return out;
}
template<class Math=HostScalarMath>
SkinResult skin_vertex(const SkinVertex& input) {
    SkinResult out{};
    out.blended=blend_skin_matrix<Math>(input.weights,input.influences);
    out.position=transform_skin_position<Math>(out.blended,input.position).position;
    return out;
}
}
#endif
