#include "native_visibility.h"
#include "native_occlusion.h"
#include "native_frustum.h"
#include "native_visibility_interval.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace v=wkc::visibility;
namespace oc=v::occlusion;
struct FiniteMath {
    static float add(float a,float b){return a+b;}
    static float subtract(float a,float b){return a-b;}
    static float multiply(float a,float b){return a*b;}
    static float fused_multiply_add(float a,float b,float c){return std::fma(a,b,c);}
    static float negative_multiply_subtract(float a,float b,float c){return std::fma(-a,b,c);}
    // Geometry assertions below use ordinary finite math. Exact compatibility
    // with the selected runtime estimate policy is established separately.
    static float interpolated_reciprocal_sqrt(float x){return 1.0f/std::sqrt(x);}
    static bool greater(float a,float b){return a>b;}
};
void put32(std::vector<v::Byte>& bytes,unsigned offset,unsigned value) {
    bytes[offset]=v::Byte(value>>24);bytes[offset+1]=v::Byte(value>>16);
    bytes[offset+2]=v::Byte(value>>8);bytes[offset+3]=v::Byte(value);
}
int main() {
    std::vector<v::Byte> records(129*16);
    for(unsigned i=0;i<129;++i){put32(records,i*16,i+100);put32(records,i*16+4,i%3);}
    const auto original=records;
    v::Request request;request.query_count=129;request.input_mask=3;
    request.records=records.data();request.record_bytes=records.size();request.output_ea=0x10000;
    v::Plan output;output.accepted=23;
    assert(v::plan(request,output)==v::Status::missing_header_origin&&output.accepted==23);
    request.header_origin=v::HeaderOrigin::constructor_zeroed_untouched_workspace;
    assert(v::plan(request,output)==v::Status::complete&&output.accepted==43&&output.writes.size()==44);
    for(unsigned i=0;i<43;++i) {
        assert(output.writes[i].ea==0x10010+i*16);
        assert(v::be32(output.writes[i].bytes.data())==i*3+100);
    }
    assert(output.writes.back().ea==0x10000&&output.writes.back().bytes[1]==43);
    for(unsigned i=2;i<16;++i)assert(output.writes.back().bytes[i]==0);
    assert(output.final_cursor==0x10000+44*16&&records==original);
    request.input_mask=0;assert(v::plan(request,output)==v::Status::complete&&output.accepted==129);
    assert(output.writes.size()==130&&output.writes.back().bytes[0]==0&&output.writes.back().bytes[1]==129);
    const auto sentinel=output.writes;
    request.record_bytes=records.size()-1;
    assert(v::plan(request,output)==v::Status::short_records&&output.writes.size()==sentinel.size());
    request.record_bytes=records.size();request.raw_threshold_units=(0x10000+130*16)/16;
    assert(v::plan(request,output)==v::Status::threshold_rejected&&output.accepted==129);
    --request.raw_threshold_units;assert(v::plan(request,output)==v::Status::complete);
    request.output_ea=0xfffffff0u;assert(v::plan(request,output)==v::Status::invalid_output);
    request.query_count=0;request.records=nullptr;request.record_bytes=0;request.output_ea=0x10000;
    request.raw_threshold_units=0xffffffffu;
    assert(v::plan(request,output)==v::Status::complete&&output.accepted==0&&output.writes.size()==1);
    request.header_origin=static_cast<v::HeaderOrigin>(255);
    assert(v::plan(request,output)==v::Status::missing_header_origin&&output.writes.size()==1);

    using Kernel=oc::Kernel<FiniteMath>;
    const oc::QueryColumns identity{{{1,0,0},{0,1,0},{0,0,1},{0,0,0}}};
    const oc::AffineRows object{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
    oc::OccluderCorners quad{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}}};
    for(unsigned winding=0;winding<2;++winding) {
        if(winding)std::swap(quad[1],quad[3]);
        oc::PreparedScene scene;assert(Kernel::prepare_scene(identity,std::span(&quad,1),scene));
        const auto visible=[&](float x,float y,float z){return Kernel::visible(scene,Kernel::prepare_box(identity,object,{{x,y,z},{.1f,.1f,.1f}}));};
        assert(!visible(0,0,-2)); // Fully behind the face and inside all side planes.
        assert(visible(3,0,-2)&&visible(-3,0,-2)&&visible(0,3,-2)&&visible(0,-3,-2));
        assert(visible(0,0,0)&&visible(0,0,2)); // Face rejection and early-z skip.
        oc::PreparedScene empty;assert(Kernel::prepare_scene(identity,{},empty));
        assert(Kernel::visible(empty,Kernel::prepare_box(identity,object,{{0,0,-2},{1,1,1}})));
        std::array<oc::OccluderCorners,129> oversized{};scene.count=17;
        assert(!Kernel::prepare_scene(identity,oversized,scene)&&scene.count==17);
    }
    namespace f=v::frustum;namespace in=v::interval;
    using Frustum=f::Kernel<FiniteMath>;using Interval=in::Kernel<FiniteMath>;
    const f::Matrix4 matrix4{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    f::Input frustum{matrix4,matrix4};
    const auto planes=Frustum::prepare_scene(frustum);
    const f::Bounds center{{0,0,0,1},{.1f,.1f,.1f,0}};
    assert(Frustum::visible(planes,Frustum::prepare_box(planes,center,object)));
    for(unsigned axis=0;axis<3;++axis)for(float direction:{-1.f,1.f}) {
        auto moved=center;moved.center[axis]=3.f*direction;
        assert(!Frustum::visible(planes,Frustum::prepare_box(planes,moved,object)));
    }
    in::Input interval{matrix4,matrix4,-1.f,1.f};
    const auto projected=Interval::prepare_scene(interval);
    assert(Interval::visible(projected,center,object));
    auto distant=center;distant.center[2]=10;
    assert(!Interval::visible(projected,distant,object));
    // A strict open interval rejects equality at either edge.
    f::Bounds point{{0,0,0,1},{0,0,0,0}};
    const auto transform=Frustum::prepare_box(planes,point,object);
    const auto projection=Frustum::dot4(projected.axis,transform.center);
    auto touching=projected;touching.lower=projection;
    assert(!Interval::visible(touching,point,object));
    touching=projected;touching.upper=projection;
    assert(!Interval::visible(touching,point,object));
    touching=projected;touching.lower=std::numeric_limits<float>::quiet_NaN();
    assert(!Interval::visible(touching,point,object));
    std::puts("Native visibility: stable mask compaction, strict admission and occlusion geometry passed");
}
