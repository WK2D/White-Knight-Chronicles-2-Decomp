#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_geometry_filter.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace wkc::geometry;
namespace {
GeometryProjection identity_projection() {
    GeometryProjection spec{};
    spec.model.rows={Float4{1,0,0,0},Float4{0,1,0,0},Float4{0,0,1,0}};
    spec.projection={Float4{1,0,0,0},Float4{0,1,0,0},Float4{0,0,1,0},Float4{0,0,0,1}};
    spec.viewport_scale={1,1,1,1};
    spec.clip_minimum={-1,-2,-3,0};spec.clip_maximum={1,2,3,0};
    return spec;
}
Float4 reciprocal(const Float4& value) {
    assert(value[0]==value[1]&&value[0]==value[2]&&value[0]==value[3]);
    return {1/value[0],1/value[1],1/value[2],1/value[3]};
}
ProjectedVertex projected(float x,float y,unsigned qx,unsigned qy,
                          bool positive_w=true,unsigned clip=0) {
    const auto packed=((qx<<16)|(qy&65535u))&0x7fffffffu;
    return {x,y,clip,packed|(positive_w?0x80000000u:0u)};
}
// This small oracle uses deliberately exact binary coordinates, a homogeneous
// identity transform, and expected integer bins. It is independent of matrix,
// vector arithmetic, reciprocal refinement and policy helpers in the kernel.
unsigned identity_bin(int x,int y,unsigned state,bool positive_w=true) {
    const unsigned quantum=state==5?1:state==3||state==4?2:4;
    const auto floor_bin=[&](int value) {
        int quotient=value/int(quantum);
        if(value<0&&value%int(quantum))--quotient;
        return unsigned(quotient*int(quantum));
    };
    return (((floor_bin(x)<<16)|(floor_bin(y)&65535u))&0x7fffffffu)|
        (positive_w?0x80000000u:0u);
}
struct FixedScreenConversion {
    std::array<std::int32_t,4> operator()(const Float4&) const {return {17,-19,0,0};}
};
struct ZeroSubpixelConversion {
    unsigned* calls;
    std::array<std::int32_t,4> operator()(const Float4&) const {
        ++*calls;return {};
    }
};
std::array<ProjectedVertex,3> small_triangle() {
    return {projected(0,0,0,0),projected(.125f,0,1,0),projected(0,.125f,0,1)};
}
std::array<ProjectedVertex,3> large_triangle(float y=0) {
    return {projected(0,y,0,0),projected(4,y,32,0),projected(0,y+4,0,32)};
}
}

int wkc2_test_native_geometry_filter() {
    const auto infinity=std::numeric_limits<float>::infinity();
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    const auto int_min=(-2147483647-1);
    {
        const auto screen=HostScreenConversion{}(Float4{-infinity,infinity,nan,-0.f});
        assert((screen==std::array<std::int32_t,4>{int_min,2147483647,int_min,0}));
        const auto quarter=HostSubpixelConversion{}(Float4{-1.375f,.49f,infinity,nan});
        assert((quarter==std::array<std::int32_t,4>{-5,1,2147483647,int_min}));
    }
    {
        const std::array<Float4,8> input{
            Float4{-1,-2,-3,99},Float4{1,2,3,-99},Float4{-1.125f,0,0,0},
            Float4{1.125f,0,0,0},Float4{0,-2.125f,0,0},Float4{0,2.125f,0,0},
            Float4{0,0,-3.125f,0},Float4{0,0,3.125f,0}};
        std::array<ProjectedVertex,8> output{};
        assert(project_geometry<HostProjectionMath>(input,identity_projection(),output,reciprocal));
        const unsigned clips[]={0,0,0x40,0x10,0x20,0x08,0x80,0x04};
        for(unsigned i=0;i<input.size();++i) {
            assert(output[i].x==input[i][0]&&output[i].y==input[i][1]);
            assert(output[i].clip_code==clips[i]*0x1000000u);
            assert(output[i].quantized_xy==identity_bin(int(input[i][0]*8),int(input[i][1]*8),0));
        }
    }
    {
        auto spec=identity_projection();
        spec.clip_minimum={-100,-100,-100,0};spec.clip_maximum={100,100,100,0};
        const std::array<Float4,2> input{Float4{.875f,1.375f,0,999},Float4{-.875f,-1.375f,0,-999}};
        std::array<ProjectedVertex,3> output{};output[2]=projected(123,456,7,8);
        const auto tail=output[2];
        for(unsigned state:{0u,1u,2u,3u,4u,5u,6u,255u}) {
            spec.precision_state=std::uint8_t(state);
            assert(project_geometry<HostProjectionMath>(input,spec,output,reciprocal));
            assert(output[0].quantized_xy==identity_bin(7,11,state));
            assert(output[1].quantized_xy==identity_bin(-7,-11,state));
            assert(!std::memcmp(&output[2],&tail,sizeof tail));
        }
        spec.precision_state=5;
        assert(project_geometry<HostProjectionMath>(input,spec,output,reciprocal,FixedScreenConversion{}));
        assert(output[0].quantized_xy==identity_bin(17,-19,5));
    }
    {
        auto spec=identity_projection();
        spec.model.rows={Float4{2,0,0,1},Float4{0,4,0,-2},Float4{0,0,.5f,3}};
        spec.viewport_scale={2,-1,8,1};spec.viewport_offset={.5f,4,-1,0};
        spec.clip_minimum={-100,-100,-100,0};spec.clip_maximum={100,100,100,0};
        const std::array<Float4,1> input{Float4{1,.5f,2,777}};
        std::array<ProjectedVertex,1> output{};
        assert(project_geometry<HostProjectionMath>(input,spec,output,reciprocal));
        // model=(3,0,4), viewport=(6.5,4,31). The input's w is ignored.
        assert(output[0].x==6.5f&&output[0].y==4&&output[0].clip_code==0);
        assert(output[0].quantized_xy==identity_bin(52,32,0));
        spec=identity_projection();spec.projection[3]={0,0,1,1};
        spec.clip_minimum={-100,-100,-100,0};spec.clip_maximum={100,100,100,0};
        const std::array<Float4,2> perspective{Float4{4,2,1,1},Float4{4,2,-3,1}};
        std::array<ProjectedVertex,2> result{};
        assert(project_geometry<HostProjectionMath>(perspective,spec,result,reciprocal));
        assert(result[0].x==2&&result[0].y==1&&result[0].quantized_xy==identity_bin(16,8,0,true));
        assert(result[1].x==2&&result[1].y==1&&result[1].quantized_xy==identity_bin(16,8,0,false));
        unsigned calls=0;
        const auto deliberately_low=[&](const Float4& w) {
            ++calls;assert((w==Float4{2,2,2,2}));return Float4{.25f,.25f,.25f,.25f};
        };
        assert(project_geometry<HostProjectionMath>(std::span(perspective).first(1),spec,result,deliberately_low));
        // The explicit correction refines .25 to .375 for W=2.
        assert(calls==1&&result[0].x==1.5f&&result[0].y==.75f);
    }
    {
        const std::array<Float4,2> input{Float4{0,0,0,1},Float4{1,1,1,1}};
        std::array<ProjectedVertex,2> output{projected(9,8,7,6),projected(5,4,3,2)};
        const auto saved=output;
        assert(!project_geometry<HostProjectionMath>(input,identity_projection(),std::span(output).first(1),reciprocal));
        assert(!std::memcmp(output.data(),saved.data(),sizeof output));
        auto aliased=input;const auto original=aliased;
        assert(!project_geometry<HostProjectionMath>(aliased,identity_projection(),
            std::span(reinterpret_cast<ProjectedVertex*>(aliased.data()),aliased.size()),reciprocal));
        assert(!std::memcmp(aliased.data(),original.data(),sizeof aliased));
        unsigned calls=0;
        const auto unused=[&](const Float4& w){++calls;return reciprocal(w);};
        assert(project_geometry<HostProjectionMath>(std::span<const Float4>{},identity_projection(),output,unused));
        assert(calls==0&&!std::memcmp(output.data(),saved.data(),sizeof output));
    }
    const std::array<std::uint16_t,3> forward{0,1,2},reverse{0,2,1};
    const auto base=large_triangle();
    for(auto winding:{TriangleWinding::either,TriangleWinding::negative,TriangleWinding::positive})
        for(bool flip:{false,true})for(bool reversed:{false,true}) {
            const auto& input=reversed?reverse:forward;
            std::array<std::uint16_t,3> output{99,99,99};std::size_t written=99;
            assert(filter_triangles(base,input,{winding,flip,false},output,written));
            const bool positive=!(reversed^flip);
            const bool keep=winding==TriangleWinding::either||
                (positive==(winding==TriangleWinding::positive));
            assert(written==(keep?3u:0u));
            if(keep)assert(output==input);else assert((output==std::array<std::uint16_t,3>{99,99,99}));
        }
    {
        std::array<std::uint16_t,3> output{};std::size_t written=99;
        auto vertices=base;
        vertices[0].quantized_xy&=0x7fffffffu;
        assert(filter_triangles(vertices,forward,{TriangleWinding::negative},output,written)&&written==3);
        vertices=base;vertices[1].clip_code=0x80000000u;
        assert(filter_triangles(vertices,forward,{TriangleWinding::negative},output,written)&&written==3);
        for(unsigned bit:{0x80u,0x40u,0x20u,0x10u,0x08u,0x04u}) {
            vertices=base;for(auto& v:vertices)v.clip_code=bit*0x1000000u;
            assert(filter_triangles(vertices,forward,{TriangleWinding::either},output,written)&&written==0);
        }
        vertices=base;vertices[0].clip_code=0x40000000;vertices[1].clip_code=0x20000000;vertices[2].clip_code=0x10000000;
        assert(filter_triangles(vertices,forward,{TriangleWinding::either},output,written)&&written==3);
        vertices={projected(0,0,0,0),projected(1,1,8,8),projected(2,2,16,16)};
        assert(filter_triangles(vertices,forward,{TriangleWinding::either},output,written)&&written==0);
        vertices=base;for(unsigned i=0;i<3;++i)vertices[i].quantized_xy=0x80000000u+i;
        assert(filter_triangles(vertices,forward,{TriangleWinding::either},output,written)&&written==0);
        vertices=base;for(unsigned i=0;i<3;++i)vertices[i].quantized_xy=0x80000000u+(i<<16);
        assert(filter_triangles(vertices,forward,{TriangleWinding::either},output,written)&&written==0);
        vertices=base;vertices[0].x=nan;
        for(auto winding:{TriangleWinding::either,TriangleWinding::negative,TriangleWinding::positive})
            assert(filter_triangles(vertices,forward,{winding},output,written)&&written==3);
    }
    {
        std::array<ProjectedVertex,6> vertices{};
        std::copy(base.begin(),base.end(),vertices.begin());
        const auto translated=large_triangle(8);std::copy(translated.begin(),translated.end(),vertices.begin()+3);
        const std::array<std::uint16_t,9> input{0,1,2,0,2,1,3,4,5};
        std::array<std::uint16_t,9> output{};output.fill(99);std::size_t written=99;
        assert(filter_triangles(vertices,input,{TriangleWinding::positive},output,written)&&written==6);
        assert((std::array<std::uint16_t,6>{output[0],output[1],output[2],output[3],output[4],output[5]}==
                std::array<std::uint16_t,6>{0,1,2,3,4,5}));
        assert(output[6]==99&&output[7]==99&&output[8]==99);
        auto inplace=input;
        assert(filter_triangles(vertices,inplace,{TriangleWinding::positive},inplace,written)&&written==6);
        assert(std::equal(inplace.begin(),inplace.begin()+6,output.begin()));
    }
    {
        std::array<std::uint16_t,4> output{11,12,13,14};const auto saved=output;std::size_t written=77;
        const auto declined=[&](auto indices,TriangleFilterSpec spec,std::span<std::uint16_t> destination) {
            assert(!filter_triangles(base,indices,spec,destination,written));
            assert(written==77&&output==saved);
        };
        declined(forward,{TriangleWinding(0)},output);
        declined(forward,{TriangleWinding(4)},output);
        declined(std::array<std::uint16_t,4>{0,1,2,0},{TriangleWinding::either},output);
        declined(std::array<std::uint16_t,3>{0,1,3},{TriangleWinding::either},output);
        declined(forward,{TriangleWinding::either},std::span(output).first(2));
        std::array<std::uint16_t,6> shifted{0,1,2,9,9,9};const auto before=shifted;
        assert(!filter_triangles(base,std::span(shifted).first(3),{TriangleWinding::either},
                                 std::span(shifted).subspan(1,3),written));
        assert(written==77&&shifted==before);
        auto alias=base;const auto saved_alias=alias;
        assert(!filter_triangles(alias,forward,{TriangleWinding::either},
            std::span(reinterpret_cast<std::uint16_t*>(alias.data()),3),written));
        assert(written==77&&!std::memcmp(alias.data(),saved_alias.data(),sizeof alias));
        assert(filter_triangles(std::span<const ProjectedVertex>{},std::span<const std::uint16_t>{},
            {TriangleWinding::either},output,written)&&written==0&&output==saved);
    }
    {
        std::array<ProjectedVertex,12> vertices{};
        for(unsigned i=0;i<4;++i) {
            const auto triangle=i?large_triangle():small_triangle();
            std::copy(triangle.begin(),triangle.end(),vertices.begin()+3*i);
        }
        std::array<std::uint16_t,12> input{};for(unsigned i=0;i<12;++i)input[i]=std::uint16_t(i);
        std::array<std::uint16_t,12> output{};std::size_t written=77;
        assert(filter_triangles(vertices,input,{TriangleWinding::either,false,true},output,written)&&written==9);
        assert(output[0]==3&&output[8]==11);
        // State 5 retains a dependency on the following lane's quarter-grid Y
        // coordinate. Here that lane selects zero thresholds for the tiny
        // first triangle, which would otherwise be discarded.
        const auto following=large_triangle(131072);
        std::copy(following.begin(),following.end(),vertices.begin()+3);
        assert(filter_triangles(vertices,input,{TriangleWinding::either,false,true},output,written)&&written==12);
        assert(output==input);
        const auto with_cross_lane=vertices;
        std::copy(following.begin(),following.end(),vertices.begin());
        const auto ordinary=large_triangle(),tiny=small_triangle();
        std::copy(ordinary.begin(),ordinary.end(),vertices.begin()+3);
        std::copy(tiny.begin(),tiny.end(),vertices.begin()+9);
        // The final lane receives zero-filled shifted bytes; it must not wrap
        // around and use the first lane's large minimum Y.
        assert(filter_triangles(vertices,input,{TriangleWinding::either,false,true},output,written)&&written==9);
        for(unsigned i=0;i<9;++i)assert(output[i]==i);
        vertices=with_cross_lane;
        unsigned calls=0;
        assert((filter_triangles<HostProjectionMath,ZeroSubpixelConversion>(vertices,input,
            {TriangleWinding::either,false,true},output,written,ZeroSubpixelConversion{&calls})));
        assert(calls>0&&written==0);
        std::array<ProjectedVertex,9> padding{};std::copy(vertices.begin()+3,vertices.end(),padding.begin());
        output.fill(99);const auto saved=output;written=77;
        assert(!filter_triangles(vertices,forward,{TriangleWinding::either,false,true},output,written));
        assert(written==77&&output==saved);
        assert(!filter_triangles(vertices,forward,{TriangleWinding::either,false,true},output,written,
                                 HostSubpixelConversion{},std::span(padding).first(8)));
        assert(written==77&&output==saved);
        assert(filter_triangles(vertices,forward,{TriangleWinding::either,false,true},output,written,
                                HostSubpixelConversion{},padding)&&written==3);
        assert(output[0]==0&&output[1]==1&&output[2]==2&&output[3]==99);
        for(unsigned triangles:{2u,3u}) {
            assert(filter_triangles(vertices,std::span(input).first(3*triangles),
                {TriangleWinding::either,false,true},output,written,HostSubpixelConversion{},
                std::span(vertices).subspan(3*triangles))&&written==3*triangles);
            for(unsigned i=0;i<written;++i)assert(output[i]==i);
        }
        auto aliased_padding=padding;const auto original=aliased_padding;written=77;
        assert(!filter_triangles(vertices,forward,{TriangleWinding::either,false,true},
            std::span(reinterpret_cast<std::uint16_t*>(aliased_padding.data()),3),written,
            HostSubpixelConversion{},aliased_padding));
        assert(written==77&&!std::memcmp(aliased_padding.data(),original.data(),sizeof original));
    }
    std::printf("Geometry projection/filter: identity and perspective oracles, clip boundaries, precision bins, winding gates, NaNs, subpixel cross-lane padding, stable in-place compaction and unchanged declines pass.\n");
    return 0;
}
