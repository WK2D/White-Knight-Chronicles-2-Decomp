// Project-written logical vertex export, using stable host views and byte output.
// No guest register/local-store interface, DMA, allocation or generated game code.
#ifndef WKC_NATIVE_VERTEX_PACK_H
#define WKC_NATIVE_VERTEX_PACK_H
#include "native_skinning.h"
#include <cstddef>
#include <limits>
#include <span>

namespace wkc::geometry {
enum class VertexPackFormat : std::uint8_t {
    position_normal=32,
    position_normal_tangent=33,
    position=34
};
struct VertexPackSpec {
    VertexPackFormat format;
    std::uint32_t vertices;
    bool suppress_directions=false;
};
struct VertexPackStreams {
    std::span<const Float4> positions,normals,tangents;
};
constexpr unsigned packed_vertex_stride(VertexPackFormat format) {
    return format==VertexPackFormat::position_normal?16:
           format==VertexPackFormat::position_normal_tangent?24:
           format==VertexPackFormat::position?12:0;
}

// Recovered truncating tangent conversion, intentionally distinct from IEEE
// round-to-nearest float-to-half: tiny values flush to +0, infinities/NaNs
// saturate to signed maximum finite. The threshold band preserves the original
// bit-bias rule, including signed zero at the exact lower threshold.
constexpr std::uint16_t pack_tangent_component(float value) {
    const auto bits=std::bit_cast<std::uint32_t>(value);
    const auto high=(bits>>16)&0x7fffu;
    if(high<0x3800)return 0;
    const auto magnitude=high>=0x4780?0x7bffu:((bits-0x38000000u)>>13)&0x7fffu;
    return std::uint16_t(magnitude|((bits>>16)&0x8000u));
}

// Host conversion policy for scaled normal components. This is not an exact-
// SPU arithmetic specification; exact/audit users supply their own conversion.
struct HostPackedNormalConversion {
    std::array<std::int32_t,4> operator()(const Float4& value) const {
        std::array<std::int32_t,4> out;
        for(unsigned lane=0;lane<4;++lane) {
            double scaled=double(value[lane])*0x1p31;
            if(std::isnan(scaled)||scaled<=-2147483648.)out[lane]=(-2147483647-1);
            else if(scaled>=2147483647.)out[lane]=2147483647;
            else out[lane]=std::int32_t(scaled);
        }
        return out;
    }
};
template<class Math=HostScalarMath,class Conversion=HostPackedNormalConversion>
std::uint32_t pack_normal_11_11_10(const Float4& normal,Conversion convert={}) {
    constexpr float xy=std::bit_cast<float>(0x3f7fe000u);
    constexpr float z=std::bit_cast<float>(0x3f7fc000u);
    const auto scaled=Math::multiply_add(normal,Float4{xy,xy,z,0},Float4{-0x1p-11f,-0x1p-11f,-0x1p-10f,0});
    const auto signed_value=convert(scaled);
    const auto x=std::bit_cast<std::uint32_t>(signed_value[0]);
    const auto y=std::bit_cast<std::uint32_t>(signed_value[1]);
    const auto zz=std::bit_cast<std::uint32_t>(signed_value[2]);
    return ((x>>21)&0x7ffu)|(((y>>21)&0x7ffu)<<11)|(((zz>>22)&0x3ffu)<<22);
}
namespace vertex_pack_detail {
inline void put16(std::uint8_t* p,std::uint16_t value) {
    p[0]=std::uint8_t(value>>8);p[1]=std::uint8_t(value);
}
inline void put32(std::uint8_t* p,std::uint32_t value) {
    p[0]=std::uint8_t(value>>24);p[1]=std::uint8_t(value>>16);
    p[2]=std::uint8_t(value>>8);p[3]=std::uint8_t(value);
}
inline bool overlaps(const void* a,std::size_t an,const void* b,std::size_t bn) {
    if(!an||!bn)return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    if(an>(std::numeric_limits<std::uintptr_t>::max)()-x||
       bn>(std::numeric_limits<std::uintptr_t>::max)()-y)return true;
    return x<y+bn&&y<x+an;
}
}

// Write logical records only, in big-endian word/halfword order. The old custom
// routine rounds to vertex pairs and the outer wrapper aligns its arena cursor;
// those padded writes, cursors and command publication are outside this kernel.
// Consumed inputs must remain stable and disjoint from written output. All
// structural rejects precede writes. Suppressed directions need no backing view.
template<class Math=HostScalarMath,class Conversion=HostPackedNormalConversion>
bool pack_vertex_stream(VertexPackStreams input,VertexPackSpec spec,
                        std::span<std::uint8_t> output,Conversion convert={}) {
    const auto stride=packed_vertex_stride(spec.format);
    if(!stride)return false;
    const std::size_t count=spec.vertices;
    const bool normals=!spec.suppress_directions&&spec.format!=VertexPackFormat::position;
    const bool tangents=normals&&spec.format==VertexPackFormat::position_normal_tangent;
    if(count>(std::numeric_limits<std::size_t>::max)()/sizeof(Float4)||
       count>output.size()/stride||input.positions.size()<count||
       (normals&&input.normals.size()<count)||(tangents&&input.tangents.size()<count))return false;
    const auto written=count*stride,read=count*sizeof(Float4);
    if(vertex_pack_detail::overlaps(output.data(),written,input.positions.data(),read)||
       (normals&&vertex_pack_detail::overlaps(output.data(),written,input.normals.data(),read))||
       (tangents&&vertex_pack_detail::overlaps(output.data(),written,input.tangents.data(),read)))return false;
    for(std::size_t vertex=0;vertex<count;++vertex) {
        auto* dest=output.data()+vertex*stride;
        for(unsigned lane=0;lane<3;++lane)
            vertex_pack_detail::put32(dest+lane*4,std::bit_cast<std::uint32_t>(input.positions[vertex][lane]));
        if(stride>=16)vertex_pack_detail::put32(dest+12,normals?pack_normal_11_11_10<Math>(input.normals[vertex],convert):0);
        if(stride==24)for(unsigned lane=0;lane<4;++lane)
            vertex_pack_detail::put16(dest+16+lane*2,tangents?pack_tangent_component(input.tangents[vertex][lane]):0);
    }
    return true;
}
}
#endif
