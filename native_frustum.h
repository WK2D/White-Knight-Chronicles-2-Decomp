// Authored transformed-box frustum selection with an injected numeric policy.
// Standard owned data only; no SDK, guest registers, local store or decoder.
#ifndef WKC_NATIVE_FRUSTUM_H
#define WKC_NATIVE_FRUSTUM_H
#include <array>
#include <bit>
#include <span>

namespace wkc::visibility::frustum {
using Vec4 = std::array<float, 4>;
using Matrix4 = std::array<Vec4, 4>;
using AffineRows = std::array<Vec4, 3>;
struct Input {
    // The first64 input bytes are four rows; the next64 are four columns.
    Matrix4 plane_rows;
    Matrix4 transform_columns;
};
struct Bounds { Vec4 center; Vec4 half_size; };
struct PreparedScene {
    // +y, -y, -x, +x, -z, +z, with cyclic component normalization.
    std::array<Vec4, 6> planes;
    Matrix4 transform_columns;
};
struct PreparedBox { Vec4 center; std::array<Vec4, 3> half_axes; };

// Math is the pure scalar policy used by the owned occlusion kernel. Its
// reciprocal-sqrt result includes estimate interpolation but not refinement.
template<class Math> struct Kernel {
    static float absolute(float x) {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(x)&0x7fffffffu);
    }
    static float negate(float x) {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(x)^0x80000000u);
    }
    static float refined_inverse_length(float squared) {
        const auto inverse = Math::interpolated_reciprocal_sqrt(squared);
        const auto length = Math::multiply(inverse, squared);
        const auto residual = Math::negative_multiply_subtract(inverse, length,
            std::bit_cast<float>(0x3f800001u));
        return Math::fused_multiply_add(residual,
            Math::multiply(inverse, 0.5f), inverse);
    }
    static Vec4 normalize_plane(const Vec4& plane) {
        Vec4 pair, result;
        for(unsigned j=0;j<4;++j) {
            const auto next=(j+1)&3u;
            pair[j]=Math::fused_multiply_add(plane[next],plane[next],
                Math::multiply(plane[j],plane[j]));
        }
        // The original does not broadcast a single norm. Each component has
        // cyclic grouping, which can differ after rounding/nonfinite values.
        for(unsigned j=0;j<4;++j) {
            const auto squared=Math::add(pair[j],pair[(j+2)&3u]);
            result[j]=Math::multiply(plane[j],refined_inverse_length(squared));
        }
        return result;
    }
    static PreparedScene prepare_scene(const Input& input) {
        PreparedScene scene; scene.transform_columns=input.transform_columns;
        constexpr std::array<unsigned,6> column{1,1,0,0,2,2};
        constexpr std::array<bool,6> plus{true,false,false,true,false,true};
        for(unsigned p=0;p<6;++p) {
            Vec4 coefficients;
            for(unsigned row=0;row<4;++row) {
                const auto a=input.plane_rows[row][3];
                const auto b=input.plane_rows[row][column[p]];
                coefficients[row]=plus[p]?Math::add(a,b):Math::subtract(a,b);
            }
            scene.planes[p]=normalize_plane(coefficients);
        }
        return scene;
    }
    static PreparedBox prepare_box(const PreparedScene& scene,
                                   const Bounds& bounds,
                                   const AffineRows& affine) {
        Matrix4 combined;
        const auto& columns=scene.transform_columns;
        for(unsigned j=0;j<4;++j) for(unsigned i=0;i<4;++i) {
            const auto even=Math::fused_multiply_add(columns[2][i],affine[2][j],
                Math::multiply(columns[0][i],affine[0][j]));
            const auto odd=Math::fused_multiply_add(columns[3][i],j==3?1.0f:0.0f,
                Math::multiply(columns[1][i],affine[1][j]));
            combined[j][i]=Math::add(even,odd);
        }
        PreparedBox box;
        for(unsigned i=0;i<4;++i) {
            const auto xz=Math::fused_multiply_add(combined[2][i],bounds.center[2],
                Math::multiply(combined[0][i],bounds.center[0]));
            const auto yt=Math::fused_multiply_add(combined[1][i],bounds.center[1],
                combined[3][i]);
            box.center[i]=Math::add(xz,yt);
            for(unsigned j=0;j<3;++j)
                box.half_axes[j][i]=Math::multiply(combined[j][i],bounds.half_size[j]);
        }
        return box;
    }
    static float dot4(const Vec4& plane,const Vec4& value) {
        const auto xy=Math::fused_multiply_add(plane[1],value[1],
            Math::multiply(plane[0],value[0]));
        const auto zw=Math::fused_multiply_add(plane[3],value[3],
            Math::multiply(plane[2],value[2]));
        return Math::add(xy,zw);
    }
    static bool visible(const PreparedScene& scene,const PreparedBox& box) {
        for(const auto& plane:scene.planes) {
            const auto xy=Math::add(absolute(dot4(plane,box.half_axes[0])),
                                    absolute(dot4(plane,box.half_axes[1])));
            const auto radius=Math::add(xy,absolute(dot4(plane,box.half_axes[2])));
            if(Math::greater(negate(radius),dot4(plane,box.center)))return false;
        }
        return true;
    }
};

}
#endif
