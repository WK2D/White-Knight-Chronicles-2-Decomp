// Project-written packed vertex decoder for the recovered geometry job formats.
// Host byte/float views only: no SPU registers, local store or guest addresses.
// Compile callers without implicit FP contraction. Math policies distinguish
// rounded multiply/add/subtract from explicit fused multiply-add operations.
#ifndef WKC_NATIVE_VERTEX_DECODE_H
#define WKC_NATIVE_VERTEX_DECODE_H
#include "native_skinning.h"
#include <algorithm>
#include <span>
#include <limits>

namespace wkc::geometry {
enum class PackedVertexFormat : std::uint8_t {
    position_direction=32,
    position_two_directions=33
};
struct VertexDecodeSpec {
    PackedVertexFormat format;
    std::uint32_t vertices;
    std::array<std::int32_t,3> position_bias;
    // Read this many bits from each position's fixed 16-bit container start,
    // then add its wrapping bias. The 17-bit layout includes the next byte's
    // high bit; its low seven bits are irrelevant to this coordinate.
    std::array<unsigned,3> position_bits{16,16,16};
};
struct DecodedVertexStreams {
    std::span<Float4> positions;
    std::array<std::span<Float4>,2> directions;
};
template<class Base=HostScalarMath>
struct VertexDecodeMath:Base {
    static Float4 add(const Float4& a,const Float4& b) {
        Float4 out;for(unsigned i=0;i<4;++i)out[i]=a[i]+b[i];return out;
    }
    static Float4 subtract(const Float4& a,const Float4& b) {
        Float4 out;for(unsigned i=0;i<4;++i)out[i]=a[i]-b[i];return out;
    }
};
struct HostPositionConversion {
    Float4 operator()(const std::array<std::int32_t,3>& value) const {
        return {float(value[0])*0x1p-12f,float(value[1])*0x1p-12f,
                float(value[2])*0x1p-12f,1.f};
    }
};

// Four independent encoded directions, one per arithmetic lane. The supplied
// reciprocal policy preserves the caller's approximation/refinement behavior;
// replacing it with a different sqrt implementation can change output bits.
template<class Math,class Reciprocal>
std::array<Float4,4> decode_direction_batch(const std::array<std::uint32_t,4>& encoded,
                                           Reciprocal reciprocal) {
    Float4 first,second;
    for(unsigned i=0;i<4;++i) {
        first[i]=std::bit_cast<float>(0x3f800000u|((encoded[i]>>14)&1023u));
        second[i]=std::bit_cast<float>(0x3f800000u|((encoded[i]>>4)&1023u));
    }
    constexpr float scale=std::bit_cast<float>(0x46352e7cu);
    constexpr float offset=std::bit_cast<float>(0xc6353150u);
    const Float4 scales{scale,scale,scale,scale},offsets{offset,offset,offset,offset};
    first=Math::multiply_add(scales,first,offsets);
    second=Math::multiply_add(scales,second,offsets);
    const auto sum=Math::add(Math::multiply(first,first),Math::multiply(second,second));
    const auto remaining=Math::subtract(Float4{1,1,1,1},sum);
    const auto magnitude=Math::multiply(remaining,reciprocal(remaining));
    std::array<Float4,4> out;
    for(unsigned i=0;i<4;++i) {
        unsigned axis=(encoded[i]>>2)&3u;
        if(axis==3)axis=0; // The fourth selector uses the first-axis layout.
        const float signed_magnitude=std::bit_cast<float>(
            (std::bit_cast<std::uint32_t>(magnitude[i])&0x7fffffffu)|
            ((encoded[i]&1u)?0u:0x80000000u));
        unsigned component=0;
        for(unsigned j=0;j<3;++j)
            out[i][j]=j==axis?signed_magnitude:(component++==0?first[i]:second[i]);
        out[i][3]=(encoded[i]&2u)?1.f:-1.f;
    }
    return out;
}

namespace vertex_decode_detail {
inline bool overlaps(const void* a,std::size_t an,const void* b,std::size_t bn) {
    if(!an||!bn)return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    if(an>(std::numeric_limits<std::uintptr_t>::max)()-x||
       bn>(std::numeric_limits<std::uintptr_t>::max)()-y)return true;
    return x<y+bn&&y<x+an;
}
}

// Input is a stable big-endian byte stream: three u16 positions followed by one
// or two 24-bit directions. Each coordinate reads 0..17 bits beginning at its
// fixed two-byte position offset, adds a wrapping i32 bias and converts from
// 12 fractional bits. A 17-bit Z coordinate also reads the first direction
// byte's high bit, even when direction output is suppressed. This byte remains
// inside the 9/12-byte record. Empty direction views omit outputs the job does
// not request. Consumed input and written output ranges must be disjoint.
// All structural rejects occur before writes. This is a decoded-buffer kernel,
// not the old routine's register/scratch ABI or a complete job replacement.
template<class Math,class Reciprocal,class Conversion=HostPositionConversion>
bool decode_vertex_stream(std::span<const std::uint8_t> input,const VertexDecodeSpec& spec,
                          DecodedVertexStreams output,Reciprocal reciprocal,
                          Conversion convert={}) {
    const unsigned streams=spec.format==PackedVertexFormat::position_direction?1u:
        spec.format==PackedVertexFormat::position_two_directions?2u:0u;
    if(!streams)return false;
    for(const auto bits:spec.position_bits)if(bits>17)return false;
    const unsigned stride=6+streams*3;
    const std::size_t count=spec.vertices;
    if(count>(std::numeric_limits<std::size_t>::max)()/sizeof(Float4)||
       count>input.size()/stride||output.positions.size()<count||
       (streams==1&&!output.directions[1].empty()))return false;
    for(const auto view:output.directions)if(!view.empty()&&view.size()<count)return false;
    const std::array<std::span<Float4>,3> views{output.positions,output.directions[0],output.directions[1]};
    for(unsigned i=0;i<views.size();++i) {
        const auto bytes=views[i].empty()?0:count*sizeof(Float4);
        if(vertex_decode_detail::overlaps(views[i].data(),bytes,input.data(),count*stride))return false;
        for(unsigned j=0;j<i;++j)if(vertex_decode_detail::overlaps(views[i].data(),bytes,
            views[j].data(),views[j].empty()?0:count*sizeof(Float4)))return false;
    }
    for(std::size_t start=0;start<count;start+=4) {
        const auto lanes=std::min<std::size_t>(4,count-start);
        for(std::size_t lane=0;lane<lanes;++lane) {
            const auto* bytes=input.data()+(start+lane)*stride;
            std::array<std::int32_t,3> coordinates;
            for(unsigned axis=0;axis<3;++axis) {
                const auto container=std::uint32_t(bytes[axis*2])*256+bytes[axis*2+1];
                const auto bits=spec.position_bits[axis];
                const auto quantized=bits<=16?container>>(16-bits):
                    (container<<1)|(bytes[axis*2+2]>>7);
                coordinates[axis]=std::bit_cast<std::int32_t>(quantized+
                    std::bit_cast<std::uint32_t>(spec.position_bias[axis]));
            }
            output.positions[start+lane]=convert(coordinates);
        }
        for(unsigned stream=0;stream<streams;++stream)if(!output.directions[stream].empty()) {
            std::array<std::uint32_t,4> encoded{};
            for(std::size_t lane=0;lane<lanes;++lane) {
                const auto* bytes=input.data()+(start+lane)*stride+6+stream*3;
                encoded[lane]=std::uint32_t(bytes[0])*65536+std::uint32_t(bytes[1])*256+bytes[2];
            }
            const auto decoded=decode_direction_batch<Math>(encoded,reciprocal);
            for(std::size_t lane=0;lane<lanes;++lane)output.directions[stream][start+lane]=decoded[lane];
        }
    }
    return true;
}
}
#endif
