// Authored transformed-box projection against a strict open interval.
#ifndef WKC_NATIVE_VISIBILITY_INTERVAL_H
#define WKC_NATIVE_VISIBILITY_INTERVAL_H
#include "native_frustum.h"
namespace wkc::visibility::interval {
using Vec4=frustum::Vec4;
using Matrix4=frustum::Matrix4;
using Bounds=frustum::Bounds;
using AffineRows=frustum::AffineRows;
struct Input {
    Matrix4 axis_columns;
    Matrix4 transform_columns;
    float lower=0,upper=0;
};
struct PreparedScene {
    Vec4 axis;
    Matrix4 transform_columns;
    float lower=0,upper=0;
};
template<class Math>struct Kernel {
    using Shared=frustum::Kernel<Math>;
    static PreparedScene prepare_scene(const Input& input) {
        Vec4 axis;
        for(unsigned j=0;j<4;++j)
            axis[j]=Math::add(input.axis_columns[j][3],input.axis_columns[j][2]);
        return {Shared::normalize_plane(axis),input.transform_columns,input.lower,input.upper};
    }
    static bool visible(const PreparedScene& scene,const Bounds& bounds,const AffineRows& object) {
        frustum::PreparedScene transform;transform.transform_columns=scene.transform_columns;
        const auto box=Shared::prepare_box(transform,bounds,object);
        const auto x=Shared::absolute(Shared::dot4(scene.axis,box.half_axes[0]));
        const auto y=Shared::absolute(Shared::dot4(scene.axis,box.half_axes[1]));
        const auto z=Shared::absolute(Shared::dot4(scene.axis,box.half_axes[2]));
        const auto radius=Math::add(Math::add(x,y),z);
        const auto center=Shared::dot4(scene.axis,box.center);
        return Math::greater(Math::add(center,radius),scene.lower)&&
            Math::greater(scene.upper,Math::subtract(center,radius));
    }
};
}
#endif
