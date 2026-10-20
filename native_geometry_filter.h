// Project-written projection and stable triangle filtering on owned host views.
// No guest registers, local store, DMA, allocation or generated game source.
// Compile callers without implicit FP contraction: area products round separately.
#ifndef WKC_NATIVE_GEOMETRY_FILTER_H
#define WKC_NATIVE_GEOMETRY_FILTER_H
#include "native_skinning.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

namespace wkc::geometry {
struct GeometryProjection {
    RowMajorAffine model;
    std::array<Float4,4> projection;
    Float4 viewport_scale,viewport_offset;
    Float4 clip_minimum,clip_maximum;
    std::uint8_t precision_state=0;
};
struct ProjectedVertex {
    float x,y;
    std::uint32_t clip_code,quantized_xy;
};
static_assert(sizeof(ProjectedVertex)==16);
struct HostProjectionMath:HostScalarMath {
    static Float4 subtract(const Float4& a,const Float4& b) {
        Float4 out;for(unsigned i=0;i<4;++i)out[i]=a[i]-b[i];return out;
    }
    static Float4 negative_multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        Float4 out;for(unsigned i=0;i<4;++i)out[i]=std::fma(-a[i],b[i],c[i]);return out;
    }
    static bool greater(float a,float b){return a>b;}
    static bool magnitude_equal(float a,float b){return std::fabs(a)==std::fabs(b);}
};
// Host signed truncate/saturate(screen*8). Exact/audit adapters supply their
// selected conversion policy instead. NaNs map to INT_MIN in this host policy.
template<unsigned FractionBits> struct HostScaledScreenConversion {
    std::array<std::int32_t,4> operator()(const Float4& input) const {
        std::array<std::int32_t,4> out;
        for(unsigned i=0;i<4;++i) {
            const double value=std::ldexp(double(input[i]),FractionBits);
            out[i]=std::isnan(value)||value<=-2147483648.?(-2147483647-1):
                value>=2147483647.?2147483647:std::int32_t(value);
        }
        return out;
    }
};
using HostScreenConversion=HostScaledScreenConversion<3>;
using HostSubpixelConversion=HostScaledScreenConversion<2>;
namespace geometry_filter_detail {
inline bool overlaps(const void* a,std::size_t an,const void* b,std::size_t bn) {
    if(!an||!bn)return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    if(an>(std::numeric_limits<std::uintptr_t>::max)()-x||
       bn>(std::numeric_limits<std::uintptr_t>::max)()-y)return true;
    return x<y+bn&&y<x+an;
}
inline Float4 splat(float value){return {value,value,value,value};}
}

// The estimate callback receives four copies of homogeneous W and must return
// the caller's reciprocal estimate/interpolation result. Refinement is explicit
// below. Ordinary IEEE FNMS/division is not a bitwise SPU-extended specification;
// policy adapters must retain their required arithmetic, comparison and conversion.
template<class Math,class Estimate,class Conversion=HostScreenConversion>
bool project_geometry(std::span<const Float4> positions,const GeometryProjection& spec,
                      std::span<ProjectedVertex> output,Estimate estimate,Conversion convert={}) {
    using namespace geometry_filter_detail;
    const auto count=positions.size();
    if(count>output.size()||count>(std::numeric_limits<std::size_t>::max)()/sizeof(Float4)||
       overlaps(positions.data(),count*sizeof(Float4),output.data(),count*sizeof(ProjectedVertex)))return false;
    std::array<Float4,4> combined;
    for(unsigned row=0;row<4;++row) {
        const auto& p=spec.projection[row];
        auto value=Math::multiply(splat(p[0]),spec.model.rows[0]);
        value=Math::multiply_add(splat(p[1]),spec.model.rows[1],value);
        value=Math::multiply_add(splat(p[2]),spec.model.rows[2],value);
        combined[row]=Math::multiply_add(splat(p[3]),Float4{0,0,0,1},value);
    }
    std::array<Float4,4> coefficients;
    for(unsigned component=0;component<4;++component)for(unsigned row=0;row<4;++row)
        coefficients[component][row]=combined[row][component];
    const std::uint32_t mask=spec.precision_state==3||spec.precision_state==4?0xfffffffeu:
        spec.precision_state==5?0xffffffffu:0xfffffffcu;
    for(std::size_t vertex=0;vertex<count;++vertex) {
        const auto& p=positions[vertex];
        auto homogeneous=Math::multiply_add(splat(p[0]),coefficients[0],coefficients[3]);
        homogeneous=Math::multiply_add(splat(p[1]),coefficients[1],homogeneous);
        homogeneous=Math::multiply_add(splat(p[2]),coefficients[2],homogeneous);
        const auto w=splat(homogeneous[3]),initial=estimate(w);
        const auto correction=Math::negative_multiply_add(initial,w,Float4{1,1,1,1});
        const auto inverse=Math::multiply_add(correction,initial,initial);
        auto normalized=Math::multiply(homogeneous,inverse);
        const auto sign=std::bit_cast<std::uint32_t>(homogeneous[3])&0x80000000u;
        for(auto& value:normalized)value=std::bit_cast<float>(std::bit_cast<std::uint32_t>(value)^sign);
        const auto screen=Math::multiply_add(normalized,spec.viewport_scale,spec.viewport_offset);
        std::uint32_t clip=0;
        if(Math::greater(spec.clip_minimum[2],screen[2]))clip|=0x80;
        if(Math::greater(spec.clip_minimum[0],screen[0]))clip|=0x40;
        if(Math::greater(spec.clip_minimum[1],screen[1]))clip|=0x20;
        if(Math::greater(screen[0],spec.clip_maximum[0]))clip|=0x10;
        if(Math::greater(screen[1],spec.clip_maximum[1]))clip|=0x08;
        if(Math::greater(screen[2],spec.clip_maximum[2]))clip|=0x04;
        const auto integer=convert(screen);
        const auto x=std::bit_cast<std::uint32_t>(integer[0])&mask;
        const auto y=std::bit_cast<std::uint32_t>(integer[1])&mask;
        auto packed=((x<<16)|(y&0xffffu))&0x7fffffffu;
        if(Math::greater(homogeneous[3],0))packed|=0x80000000u;
        output[vertex]={screen[0],screen[1],clip<<24,packed};
    }
    return true;
}

enum class TriangleWinding : std::uint8_t { either=1,negative=2,positive=3 };
struct TriangleFilterSpec {
    TriangleWinding winding;
    bool flip_winding=false;
    bool subpixel=false;
};
// Validate every index and range before writes. Full-sized output capacity is
// required; the result count identifies its logical prefix. Same-start in-place
// index compaction is allowed; other input/output overlaps decline unchanged.
// Subpixel filtering processes four triangles together. Its legacy selector
// reads the next lane's Y minimum, so a final partial group needs three explicit
// projected vertices for each unused triangle lane. The caller owns that padding;
// it is not silently invented or read past the logical index span.
template<class Math=HostProjectionMath,class Conversion=HostSubpixelConversion>
bool filter_triangles(std::span<const ProjectedVertex> vertices,
                      std::span<const std::uint16_t> input,TriangleFilterSpec spec,
                      std::span<std::uint16_t> output,std::size_t& written,
                      Conversion convert={},std::span<const ProjectedVertex> padded_triangles={},
                      std::span<std::uint8_t> decisions={}) {
    using namespace geometry_filter_detail;
    if(unsigned(spec.winding)<1||unsigned(spec.winding)>3||input.size()%3||
       output.size()<input.size()||input.size()>(std::numeric_limits<std::size_t>::max)()/2||
       vertices.size()>(std::numeric_limits<std::size_t>::max)()/sizeof(ProjectedVertex))return false;
    const std::size_t padding=spec.subpixel?3*((4-input.size()/3%4)%4):0;
    if(padded_triangles.size()<padding||
       overlaps(padded_triangles.data(),padding*sizeof(ProjectedVertex),output.data(),input.size()*2))return false;
    const std::size_t decision_bytes=decisions.empty()?0:input.size()/3;
    if(!decisions.empty()&&(decisions.size()<decision_bytes||
       overlaps(decisions.data(),decision_bytes,vertices.data(),vertices.size()*sizeof(ProjectedVertex))||
       overlaps(decisions.data(),decision_bytes,padded_triangles.data(),padding*sizeof(ProjectedVertex))||
       overlaps(decisions.data(),decision_bytes,input.data(),input.size()*2)||
       overlaps(decisions.data(),decision_bytes,output.data(),input.size()*2)))return false;
    if(overlaps(vertices.data(),vertices.size()*sizeof(ProjectedVertex),output.data(),input.size()*2)||
       (input.data()!=output.data()&&overlaps(input.data(),input.size()*2,output.data(),input.size()*2)))return false;
    for(const auto index:input)if(index>=vertices.size())return false;
    std::size_t count=0;
    for(std::size_t base=0;base<input.size();base+=12) {
      const auto lanes=(std::min)(std::size_t{4},(input.size()-base)/3);
      std::array<std::array<std::int32_t,4>,4> bounds{};
      if(spec.subpixel)for(unsigned lane=0;lane<4;++lane) {
        const auto at=[&](unsigned v)->const ProjectedVertex& {
            return lane<lanes?vertices[input[base+lane*3+v]]:padded_triangles[(lane-lanes)*3+v];
        };
        const auto& a=at(0);const auto& b=at(1);const auto& c=at(2);
        Float4 box;
        for(unsigned axis=0;axis<2;++axis) {
            const float av=axis?a.y:a.x,bv=axis?b.y:b.x,cv=axis?c.y:c.x;
            const bool greater=Math::greater(av,bv);
            const float low=greater?bv:av,high=greater?av:bv;
            box[axis]=Math::greater(low,cv)?cv:low;
            box[axis+2]=Math::greater(high,cv)?high:cv;
        }
        bounds[lane]=convert(box);
      }
      for(std::size_t lane=0;lane<lanes;++lane) {
        const auto i=base+lane*3;
        const auto ia=input[i],ib=input[i+1],ic=input[i+2];
        const auto& a=vertices[ia];const auto& b=vertices[ib];const auto& c=vertices[ic];
        const auto ac=Math::subtract(Float4{c.x,c.y,0,0},Float4{a.x,a.y,0,0});
        const auto ab=Math::subtract(Float4{b.x,b.y,0,0},Float4{a.x,a.y,0,0});
        const auto products=Math::multiply(ab,Float4{ac[1],ac[0],0,0});
        float area=Math::subtract(splat(products[0]),splat(products[1]))[0];
        if(spec.flip_winding)area=std::bit_cast<float>(std::bit_cast<std::uint32_t>(area)^0x80000000u);
        const bool wrong=spec.winding==TriangleWinding::negative?Math::greater(area,0):
            spec.winding==TriangleWinding::positive?Math::greater(0,area):false;
        const bool all_positive=(a.quantized_xy&b.quantized_xy&c.quantized_xy&0x80000000u)!=0;
        const bool any_near=((a.clip_code|b.clip_code|c.clip_code)&0x80000000u)!=0;
        const bool equal_x=(a.quantized_xy>>16)==(b.quantized_xy>>16)&&(a.quantized_xy>>16)==(c.quantized_xy>>16);
        const bool equal_y=(a.quantized_xy&0xffffu)==(b.quantized_xy&0xffffu)&&(a.quantized_xy&0xffffu)==(c.quantized_xy&0xffffu);
        bool subpixel_reject=false;
        if(spec.subpixel) {
            const auto& b=bounds[lane];
            const auto selector=(std::uint32_t(b[0])&3u)|
                (lane<3?(std::uint32_t(bounds[lane+1][1])>>16)&12u:0);
            // These small thresholds are the recovered subpixel coverage rule.
            constexpr std::array<std::int32_t,16> threshold_x{1,0,3,2,3,1,1,0,0,3,2,1,1,1,0,2};
            constexpr std::array<std::int32_t,16> threshold_y{2,0,1,1,1,3,2,0,0,1,1,3,3,1,0,2};
            const auto width=std::bit_cast<std::int32_t>(std::uint32_t(b[2])-std::uint32_t(b[0]));
            const auto height=std::bit_cast<std::int32_t>(std::uint32_t(b[3])-std::uint32_t(b[1]));
            subpixel_reject=threshold_x[selector]>width&&threshold_y[selector]>height;
        }
        const bool keep=!(subpixel_reject||Math::magnitude_equal(area,0)||
            (wrong&&all_positive&&!any_near)||equal_x||equal_y||(a.clip_code&b.clip_code&c.clip_code));
        if(!decisions.empty())decisions[i/3]=std::uint8_t(keep);
        if(!keep)continue;
        output[count++]=ia;output[count++]=ib;output[count++]=ic;
      }
    }
    written=count;return true;
}
}
#endif
