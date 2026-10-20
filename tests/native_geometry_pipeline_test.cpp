// Composition regression built entirely from project-owned synthetic
// values. No captures, guest registers, local store or generated game code.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_geometry_pipeline.h"
#include "native_estimate_interpolation.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>

using namespace wkc::geometry;
namespace oc=wkc::geometry::pipeline;
namespace {
struct ReciprocalSquareRoot {
    unsigned calls=0;
    Float4 operator()(const Float4& input) {
        ++calls;Float4 output;
        for(unsigned lane=0;lane<4;++lane)output[lane]=1.f/std::sqrt(input[lane]);
        return output;
    }
};
struct ReciprocalW {
    unsigned calls=0;
    Float4 operator()(const Float4& input) {
        ++calls;Float4 output;
        for(unsigned lane=0;lane<4;++lane)output[lane]=1.f/input[lane];
        return output;
    }
};
struct FiniteHostPolicies {
    using DecodeMath=VertexDecodeMath<HostScalarMath>;
    using PositionSkinMath=OrderedHostPositionStreamMath<>;
    using GeometrySkinMath=HostScalarMath;
    using NormalMath=HostScalarMath;
    using CofactorMath=OrderedHostCofactorMath<>;
    using ProjectionMath=HostProjectionMath;
    using PackingMath=HostScalarMath;
    ReciprocalSquareRoot decode_reciprocal,normal_reciprocal;
    ReciprocalW projection_estimate;
    HostPositionConversion position_conversion;
    HostScreenConversion screen_conversion;
    HostSubpixelConversion subpixel_conversion;
    HostPackedNormalConversion normal_conversion;
    SkinningStreamPolicy skinning_policy{};
};
struct ThrowingPositionConversion {
    unsigned calls=0;
    Float4 operator()(const std::array<std::int32_t,3>& input) {
        if(++calls==3)throw std::runtime_error("synthetic conversion failure");
        return HostPositionConversion{}(input);
    }
};
struct ThrowingPolicies:FiniteHostPolicies {
    ThrowingPositionConversion position_conversion;
};
void put16(std::uint8_t* bytes,unsigned value) {
    bytes[0]=std::uint8_t(value>>8);bytes[1]=std::uint8_t(value);
}
void put24(std::uint8_t* bytes,unsigned value) {
    bytes[0]=std::uint8_t(value>>16);bytes[1]=std::uint8_t(value>>8);bytes[2]=std::uint8_t(value);
}
unsigned be16(const std::uint8_t* bytes) {return unsigned(bytes[0])*256+bytes[1];}
std::uint32_t be32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0])*0x1000000u+std::uint32_t(bytes[1])*65536u+
        std::uint32_t(bytes[2])*256u+bytes[3];
}
Float4 original_position(unsigned vertex) {
    constexpr std::array<Float4,4> first{
        Float4{0,0,.25f,1},Float4{1,0,.25f,1},
        Float4{0,1,.25f,1},Float4{.25f,.25f,.5f,1}};
    auto value=first[vertex%4];
    value[0]+=float(vertex/4)*.25f;value[1]+=float(vertex/4)*.25f;
    return value;
}
GeometryProjection identity_projection() {
    GeometryProjection projection{};
    projection.model.rows={Float4{1,0,0,0},Float4{0,1,0,0},Float4{0,0,1,0}};
    projection.projection={Float4{1,0,0,0},Float4{0,1,0,0},Float4{0,0,1,0},Float4{0,0,0,1}};
    projection.viewport_scale={1,1,1,1};
    projection.clip_minimum={-16,-16,-16,0};projection.clip_maximum={16,16,16,0};
    return projection;
}
oc::GeometryInput input_example(unsigned vertices,unsigned directions,bool suppress,
                                bool skinning,bool translate) {
    assert(vertices>=3&&directions>=1&&directions<=2);
    oc::GeometryInput input;
    input.decode={directions==1?PackedVertexFormat::position_direction:
        PackedVertexFormat::position_two_directions,vertices,{-4096,-4096,0}};
    input.export_format=directions==1?VertexPackFormat::position_normal:
        VertexPackFormat::position_normal_tangent;
    input.index_count=3;input.suppress_directions=suppress;
    input.skinning=skinning?oc::SkinningMode::linear_blend_variant3:oc::SkinningMode::disabled;
    input.projection=identity_projection();input.filter={TriangleWinding::positive};
    const unsigned stride=6+directions*3;
    input.packed_vertices.resize(vertices*stride);
    for(unsigned vertex=0;vertex<vertices;++vertex) {
        auto* bytes=input.packed_vertices.data()+vertex*stride;
        const auto position=original_position(vertex);
        for(unsigned axis=0;axis<3;++axis)
            put16(bytes+axis*2,unsigned(int(position[axis]*4096)-input.decode.position_bias[axis]));
        // Two distinct finite directions, with opposite handedness. Their
        // coordinates lie inside the encoding's reconstructed unit disk.
        put24(bytes+6,(512u<<14)|(512u<<4)|(2u<<2)|3u);
        if(directions==2)put24(bytes+9,(640u<<14)|(384u<<4));
    }
    // No explicit values or delta bits, one zero selector byte, command 3:
    // three sequential tokens, producing the independently known triangle.
    input.packed_indices={0,0,0,0,0,1,0,0,0,0xc0};
    input.influences.resize(vertices,PackedSkinInfluences{{255,0,0,0},{0,0,0,0}});
    input.palette.resize(1);
    input.palette[0].rows={Float4{1,0,0,translate?.25f:0},
        Float4{0,1,0,translate?-.125f:0},Float4{0,0,1,translate?.5f:0}};
    return input;
}
bool same_projection(const GeometryProjection& a,const GeometryProjection& b) {
    return a.model.rows==b.model.rows&&a.projection==b.projection&&
        a.viewport_scale==b.viewport_scale&&a.viewport_offset==b.viewport_offset&&
        a.clip_minimum==b.clip_minimum&&a.clip_maximum==b.clip_maximum&&a.precision_state==b.precision_state;
}
bool same_input(const oc::GeometryInput& a,const oc::GeometryInput& b) {
    if(a.decode.format!=b.decode.format||a.decode.vertices!=b.decode.vertices||
       a.decode.position_bias!=b.decode.position_bias||a.export_format!=b.export_format||
       a.index_count!=b.index_count||a.suppress_directions!=b.suppress_directions||a.skinning!=b.skinning||
       !same_projection(a.projection,b.projection)||a.filter.winding!=b.filter.winding||
       a.filter.flip_winding!=b.filter.flip_winding||a.filter.subpixel!=b.filter.subpixel||
       a.packed_vertices!=b.packed_vertices||a.packed_indices!=b.packed_indices||
       a.subpixel_padding_indices!=b.subpixel_padding_indices||
       a.influences.size()!=b.influences.size()||a.palette.size()!=b.palette.size())return false;
    for(unsigned i=0;i<a.influences.size();++i)
        if(a.influences[i].weights!=b.influences[i].weights||a.influences[i].joints!=b.influences[i].joints)return false;
    for(unsigned i=0;i<a.palette.size();++i)if(a.palette[i].rows!=b.palette[i].rows)return false;
    return true;
}
bool same_payload(const oc::PayloadLayout& a,const oc::PayloadLayout& b) {
    return a.logical_index_bytes==b.logical_index_bytes&&a.index_dma_bytes==b.index_dma_bytes&&
        a.logical_vertex_bytes==b.logical_vertex_bytes&&a.vertex_dma_bytes==b.vertex_dma_bytes&&
        a.reservation_bytes==b.reservation_bytes&&a.empty_geometry==b.empty_geometry;
}
bool same_output(const oc::GeometryOutput& a,const oc::GeometryOutput& b) {
    if(a.positions!=b.positions||a.directions!=b.directions||a.prepared_palette!=b.prepared_palette||
       a.projected.size()!=b.projected.size()||a.decoded_indices!=b.decoded_indices||
       a.filtered_indices!=b.filtered_indices||a.packed_vertices!=b.packed_vertices||!same_payload(a.payload,b.payload))return false;
    for(unsigned i=0;i<a.projected.size();++i) {
        const auto& x=a.projected[i];const auto& y=b.projected[i];
        if(std::bit_cast<std::uint32_t>(x.x)!=std::bit_cast<std::uint32_t>(y.x)||
           std::bit_cast<std::uint32_t>(x.y)!=std::bit_cast<std::uint32_t>(y.y)||
           x.clip_code!=y.clip_code||x.quantized_xy!=y.quantized_xy)return false;
    }
    return true;
}
oc::GeometryOutput sentinel_output() {
    oc::GeometryOutput output;
    output.positions={Float4{101,102,103,104}};
    output.directions[0]={Float4{201,202,203,204}};
    output.directions[1]={Float4{301,302,303,304}};
    output.prepared_palette={CyclicAffine{Float4{1,2,3,4},Float4{5,6,7,8},Float4{9,10,11,12}}};
    output.projected={{7,8,9,10}};output.decoded_indices={21,22};output.filtered_indices={31};
    output.packed_vertices={41,42,43};output.payload={51,52,53,54,55,false};
    return output;
}
void check_result(const oc::GeometryInput& input,const oc::GeometryOutput& output,bool empty) {
    const unsigned vertices=input.decode.vertices;
    const unsigned streams=input.suppress_directions?0:input.decode.format==PackedVertexFormat::position_direction?1:2;
    const bool skinning=input.skinning!=oc::SkinningMode::disabled;
    assert(output.positions.size()==vertices&&output.projected.size()==vertices);
    assert(output.prepared_palette.size()==(skinning?input.palette.size():0));
    if(skinning) {
        const auto& rows=input.palette[0].rows;
        const CyclicAffine expected{Float4{1,1,1,rows[0][3]},
            Float4{0,0,0,rows[1][3]},Float4{0,0,0,rows[2][3]}};
        assert(output.prepared_palette[0]==expected);
    }
    // The recovered byte conversion deliberately uses the lower reciprocal
    // coefficient; do not silently normalize a 255 byte weight back to 1.
    const float weight=255.f*std::bit_cast<float>(std::uint32_t{0x3b808080u});
    assert(weight<1&&weight>.99999f);
    for(unsigned vertex=0;vertex<vertices;++vertex) {
        auto expected=original_position(vertex);
        if(skinning)for(unsigned axis=0;axis<3;++axis)
            expected[axis]=std::fma(expected[axis],weight,input.palette[0].rows[axis][3]*weight);
        assert(output.positions[vertex]==expected);
        assert(output.projected[vertex].x==expected[0]&&output.projected[vertex].y==expected[1]);
        assert(output.projected[vertex].clip_code==0&&(output.projected[vertex].quantized_xy&0x80000000u));
    }
    for(unsigned stream=0;stream<2;++stream) {
        assert(output.directions[stream].size()==(stream<streams?vertices:0));
        for(const auto& direction:output.directions[stream]) {
            for(const float value:direction)assert(std::isfinite(value));
            const double length=double(direction[0])*direction[0]+double(direction[1])*direction[1]+double(direction[2])*direction[2];
            assert(std::fabs(length-1)<.00001);
            assert(direction[3]==(stream?-1.f:1.f));
            if(!stream)assert(direction[2]>.99f&&std::fabs(direction[0])<.01f&&std::fabs(direction[1])<.01f);
            else assert(direction[0]<-.9f);
        }
    }
    assert((output.decoded_indices==(input.index_count?std::vector<std::uint16_t>{0,1,2}:std::vector<std::uint16_t>{})));
    assert((output.filtered_indices==(empty?std::vector<std::uint16_t>{}:std::vector<std::uint16_t>{0,1,2})));
    assert(output.payload.empty_geometry==empty);
    if(empty) {
        assert(same_payload(output.payload,oc::PayloadLayout{}));assert(output.packed_vertices.empty());return;
    }
    const unsigned stride=streams==2||input.export_format==VertexPackFormat::position_normal_tangent?24:16;
    assert(output.payload.logical_index_bytes==6&&output.payload.index_dma_bytes==16);
    assert(output.payload.unresolved_index_tail_bytes()==10);
    assert(output.payload.logical_vertex_bytes==vertices*stride);
    assert(output.payload.vertex_dma_bytes==((vertices*stride+15)&~15u));
    assert(output.payload.reservation_bytes==((16+vertices*stride+127)&~127u));
    assert(output.payload.unresolved_vertex_tail_bytes()==output.payload.vertex_dma_bytes-vertices*stride);
    assert(output.packed_vertices.size()==vertices*stride);
    for(unsigned vertex=0;vertex<vertices;++vertex) {
        const auto* bytes=output.packed_vertices.data()+vertex*stride;
        for(unsigned axis=0;axis<3;++axis)
            assert(be32(bytes+4*axis)==std::bit_cast<std::uint32_t>(output.positions[vertex][axis]));
        if(input.suppress_directions)for(unsigned byte=12;byte<stride;++byte)assert(bytes[byte]==0);
        else {
            assert(be32(bytes+12)!=0);
            if(stride==24)assert(be16(bytes+22)==0xbc00u); // tangent handedness -1
        }
    }
}
}

int wkc2_test_native_geometry_pipeline() {
    unsigned successful=0,declines=0;
    {
        const Float4 operand{std::bit_cast<float>(0x40000u),std::bit_cast<float>(0x40000u),
            std::bit_cast<float>(0x40000u),0};
        const Float4 encoded{std::bit_cast<float>(0x3f800000u),std::bit_cast<float>(0x3f800001u),
            std::bit_cast<float>(0xbf800001u),std::bit_cast<float>(0x3f800001u)};
        const auto interpolated=FusedEstimateInterpolation{}(operand,encoded);
        const std::array<std::uint32_t,4> expected{0x3f800000u,0x3f7ffc00u,0xbf7ffc00u,0x3f800000u};
        for(unsigned lane=0;lane<4;++lane)assert(std::bit_cast<std::uint32_t>(interpolated[lane])==expected[lane]);
        auto ignored_high_bits=operand;
        for(auto& value:ignored_high_bits)value=std::bit_cast<float>(std::bit_cast<std::uint32_t>(value)|0xfff80000u);
        assert(FusedEstimateInterpolation{}(ignored_high_bits,encoded)==interpolated);
    }
    for(unsigned count:{4u,8u})for(unsigned streams:{1u,2u})for(bool suppress:{false,true})
        for(bool skinning:{false,true})for(bool translate:{false,true}) {
            const auto input=input_example(count,streams,suppress,skinning,translate),saved=input;
            auto output=sentinel_output();FiniteHostPolicies policies;
            assert(oc::run(input,output,policies)==oc::PipelineStatus::logical_buffers_ready);
            assert(same_input(input,saved));check_result(input,output,false);
            assert(policies.projection_estimate.calls==count);
            assert((policies.decode_reciprocal.calls==0)==suppress);
            assert((policies.normal_reciprocal.calls!=0)==(skinning&&!suppress));
            ++successful;
        }
    // A non-skinned odd count has a logical tangent payload with an unresolved
    // eight-byte transfer tail. It is a size plan, not manufactured DMA bytes.
    for(unsigned streams:{1u,2u}) {
        auto input=input_example(3,streams,false,false,false);
        input.influences.clear();input.palette.clear();const auto saved=input;
        auto output=sentinel_output();FiniteHostPolicies policies;
        assert(oc::run(input,output,policies)==oc::PipelineStatus::logical_buffers_ready);
        assert(same_input(input,saved));check_result(input,output,false);
        assert(output.payload.unresolved_vertex_tail_bytes()==(streams==2?8u:0u));++successful;
    }
    for(bool no_indices:{false,true}) {
        auto input=input_example(4,2,false,true,true);
        if(no_indices){input.index_count=0;input.packed_indices.clear();}
        else input.filter.winding=TriangleWinding::negative;
        const auto saved=input;auto output=sentinel_output();FiniteHostPolicies policies;
        assert(oc::run(input,output,policies)==oc::PipelineStatus::logical_buffers_ready);
        assert(same_input(input,saved));check_result(input,output,true);++successful;
    }
    {
        auto input=input_example(4,1,false,true,false);
        input.palette.resize(256,input.palette[0]);
        for(auto& influence:input.influences)influence.joints={0,255,255,255};
        const auto saved=input;auto output=sentinel_output();FiniteHostPolicies policies;
        assert(oc::run(input,output,policies)==oc::PipelineStatus::logical_buffers_ready);
        assert(same_input(input,saved));check_result(input,output,false);++successful;
    }
    {
        auto input=input_example(4,1,false,true,false);
        input.filter.subpixel=true;input.projection.precision_state=5;
        input.subpixel_padding_indices={0,1,2,0,1,2,0,1,2};
        const auto saved=input;auto output=sentinel_output();FiniteHostPolicies policies;
        assert(oc::run(input,output,policies)==oc::PipelineStatus::logical_buffers_ready);
        assert(same_input(input,saved));check_result(input,output,false);++successful;
    }
    const auto rejected=[&](const oc::GeometryInput& input,oc::PipelineStatus expected) {
        const auto saved_input=input;auto output=sentinel_output();const auto saved_output=output;
        const auto* positions=output.positions.data();const auto* packed=output.packed_vertices.data();
        FiniteHostPolicies policies;
        assert(oc::run(input,output,policies)==expected);
        assert(same_input(input,saved_input)&&same_output(output,saved_output));
        assert(output.positions.data()==positions&&output.packed_vertices.data()==packed);++declines;
    };
    for(unsigned fault=0;fault<17;++fault) {
        auto input=input_example(4,1,false,true,false);
        auto expected=oc::PipelineStatus::unsupported_count;
        switch(fault) {
        case 0:input.decode.format=PackedVertexFormat(34);expected=oc::PipelineStatus::unsupported_vertex_format;break;
        case 1:input.export_format=VertexPackFormat::position_normal_tangent;expected=oc::PipelineStatus::unsupported_export_layout;break;
        case 2:input.skinning=oc::SkinningMode(2);expected=oc::PipelineStatus::unsupported_skinning_mode;break;
        case 3:input.decode.vertices=0;break;
        case 4:input.decode.vertices=5;break;
        case 5:input.decode.vertices=65536;break;
        case 6:input.index_count=65536;break;
        case 7:input.index_count=4;break;
        case 8:input.filter.winding=TriangleWinding(0);expected=oc::PipelineStatus::invalid_filter_mode;break;
        case 9:input.filter.subpixel=true;input.projection.precision_state=5;expected=oc::PipelineStatus::unsupported_subpixel_padding;break;
        case 10:input.packed_vertices.pop_back();expected=oc::PipelineStatus::short_packed_vertices;break;
        case 11:input.influences.pop_back();expected=oc::PipelineStatus::short_influences;break;
        case 12:input.palette.clear();expected=oc::PipelineStatus::missing_palette;break;
        case 13:input.influences[2].joints[3]=255;expected=oc::PipelineStatus::unsupported_palette_backing;break;
        case 14:input.influences[3].joints[0]=1;expected=oc::PipelineStatus::unsupported_palette_backing;break;
        case 15:input.packed_indices.clear();expected=oc::PipelineStatus::index_decode_declined;break;
        case 16:input.index_count=6;input.packed_indices.back()=0xf0;expected=oc::PipelineStatus::filter_declined;break;
        }
        rejected(input,expected);
    }
    {
        auto input=input_example(4,1,false,true,false);input.filter.subpixel=true;input.projection.precision_state=5;
        input.subpixel_padding_indices={0,1,2,0,1,2,0,1,4};
        rejected(input,oc::PipelineStatus::unsupported_subpixel_padding);
    }
    for(bool subpixel:{false,true}) {
        auto input=input_example(4,1,false,true,false);
        input.filter.subpixel=subpixel;input.projection.precision_state=subpixel?0:5;
        rejected(input,oc::PipelineStatus::invalid_projection_filter_state);
    }
    {
        const auto input=input_example(4,1,false,true,false),saved_input=input;
        auto output=sentinel_output();const auto saved_output=output;
        ThrowingPolicies policies;bool threw=false;
        try {oc::run(input,output,policies);}catch(const std::runtime_error&){threw=true;}
        assert(threw&&policies.position_conversion.calls==3);
        assert(same_input(input,saved_input)&&same_output(output,saved_output));
    }
    std::printf("Owned logical geometry pipeline: %u synthetic successes, %u unchanged declines, immutable inputs, known positions/indices/export bytes/payload plans and exception-before-commit pass.\n",successful,declines);
    return 0;
}
