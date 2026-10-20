// Compose project-written geometry algorithms through owned native buffers.
// The result contains logical buffers and an export-size plan. Allocation,
// padded transfers, command publication and scheduling belong to the job adapter.
#ifndef WKC_NATIVE_GEOMETRY_PIPELINE_H
#define WKC_NATIVE_GEOMETRY_PIPELINE_H

#include "native_vertex_decode.h"
#include "native_index_decode.h"
#include "native_skinning_stream.h"
#include "native_geometry_filter.h"
#include "native_vertex_pack.h"
#include <functional>
#include <utility>
#include <vector>

namespace wkc::geometry::pipeline {

enum class SkinningMode : std::uint8_t {
    disabled,
    // With no active directions use the recovered position-only core. With
    // directions use the recovered variant-3 normal/tangent core (C968).
    linear_blend_variant3
};

// The producer owns these inputs and validates the original descriptor/staging
// layout before construction. The pipeline does not accept guest pointers,
// local-store snapshots, ABI spill cells or reference-derived output backing.
// In particular, influences/palette must cover every declared vertex/joint,
// including zero weights. Input vector storage remains stable during execution.
struct GeometryInput {
    VertexDecodeSpec decode{};
    VertexPackFormat export_format{};
    std::uint32_t index_count=0;
    bool suppress_directions=false;
    SkinningMode skinning=SkinningMode::disabled;
    GeometryProjection projection{};
    TriangleFilterSpec filter{};
    std::vector<std::uint8_t> packed_vertices;
    std::vector<std::uint8_t> packed_indices;
    // Explicit owned indices for absent lanes of a final subpixel group.
    // The job adapter must recover these tokens from its supported input layout.
    std::vector<std::uint16_t> subpixel_padding_indices;
    std::vector<PackedSkinInfluences> influences;
    std::vector<RowMajorAffine> palette;
};

// Byte counts are a plan, not completed transfer buffers. Padding is deliberately
// NOT manufactured from zeros or a private reference. Any index/vertex tail
// bytes must be independently recovered before an actual export transaction.
struct PayloadLayout {
    std::uint32_t logical_index_bytes=0,index_dma_bytes=0;
    std::uint32_t logical_vertex_bytes=0,vertex_dma_bytes=0;
    std::uint32_t reservation_bytes=0;
    bool empty_geometry=true;
    constexpr std::uint32_t unresolved_index_tail_bytes() const {
        return index_dma_bytes-logical_index_bytes;
    }
    constexpr std::uint32_t unresolved_vertex_tail_bytes() const {
        return vertex_dma_bytes-logical_vertex_bytes;
    }
};

struct GeometryOutput {
    std::vector<Float4> positions;
    std::array<std::vector<Float4>,2> directions;
    std::vector<CyclicAffine> prepared_palette;
    std::vector<ProjectedVertex> projected;
    std::vector<std::uint16_t> decoded_indices,filtered_indices;
    std::vector<std::uint8_t> packed_vertices;
    PayloadLayout payload;
};

enum class PipelineStatus : std::uint8_t {
    logical_buffers_ready,
    unsupported_vertex_format,
    unsupported_export_layout,
    unsupported_skinning_mode,
    unsupported_count,
    invalid_filter_mode,
    invalid_projection_filter_state,
    unsupported_subpixel_padding,
    short_packed_vertices,
    short_influences,
    missing_palette,
    unsupported_palette_backing,
    vertex_decode_declined,
    index_decode_declined,
    skinning_declined,
    projection_declined,
    filter_declined,
    packing_declined
};

namespace detail {
struct SkinViews {
    std::vector<Float4>& positions;
    std::array<std::vector<Float4>,2>& directions;
    const std::vector<PackedSkinInfluences>& influences;
    const std::vector<CyclicAffine>& palette;

    PackedSkinInfluences read_influences(unsigned vertex) const {
        return influences[vertex];
    }
    Float4 read_position(unsigned vertex) const {return positions[vertex];}
    Float4 read_direction(unsigned stream,unsigned vertex) const {
        return directions[stream][vertex];
    }
    CyclicAffine read_palette(unsigned joint) const {return palette[joint];}
    void write_position(unsigned vertex,const Float4& value) {
        positions[vertex]=value;
    }
    void write_direction(unsigned stream,unsigned vertex,const Float4& value) {
        directions[stream][vertex]=value;
    }
};

constexpr PayloadLayout payload_layout(std::uint32_t indices,
                                       std::uint32_t vertices,unsigned stride) {
    if(!indices)return {};
    const auto index_bytes=2*((std::uint64_t(indices)+7)&~std::uint64_t(7));
    const auto vertex_bytes=std::uint64_t(vertices)*stride;
    return {indices*2,std::uint32_t(index_bytes),std::uint32_t(vertex_bytes),
            std::uint32_t((vertex_bytes+15)&~std::uint64_t(15)),
            std::uint32_t((index_bytes+vertex_bytes+127)&~std::uint64_t(127)),false};
}
}

// Policies supplies distinct arithmetic types:
//   DecodeMath, PositionSkinMath, GeometrySkinMath, NormalMath, CofactorMath,
//   ProjectionMath, PackingMath.
// It also supplies callable objects:
//   decode_reciprocal, normal_reciprocal, projection_estimate,
//   position_conversion, screen_conversion, subpixel_conversion,
//   normal_conversion;
// and a SkinningStreamPolicy member named skinning_policy.
// Required reciprocal/FNMS/conversion semantics are explicit: there is no silent
// default to IEEE division or a standard half converter. A host-mode adapter
// must preserve the separate recovered position/cofactor NaN-selection policies;
// exact/audit adapters retain their own arithmetic. Policies do no buffer writes.
//
// Every reject leaves output unchanged. Local vectors own all intermediate
// results. The function may propagate allocation/policy exceptions, also before
// committing output. Success means logical buffers only; allocation, padded
// transfers, RSX packets, memory visibility and scheduling are still outside it.
template<class Policies>
PipelineStatus run(const GeometryInput& input,GeometryOutput& output,Policies& policy) {
    const auto count=input.decode.vertices;
    const auto format=input.decode.format;
    const unsigned encoded_directions=format==PackedVertexFormat::position_direction?1:
        format==PackedVertexFormat::position_two_directions?2:0;
    if(!encoded_directions)return PipelineStatus::unsupported_vertex_format;
    // The recovered full-input layouts use corresponding32/33 decode and
    // export formats. Logical format34 packing exists, but its input/job layout
    // is not recovered here; reject rather than invent a cross-format path.
    const auto expected_export=encoded_directions==1?VertexPackFormat::position_normal:
        VertexPackFormat::position_normal_tangent;
    if(input.export_format!=expected_export)
        return PipelineStatus::unsupported_export_layout;
    if(input.skinning!=SkinningMode::disabled&&
       input.skinning!=SkinningMode::linear_blend_variant3)
        return PipelineStatus::unsupported_skinning_mode;
    if(!count||count>65535||input.index_count>65535||input.index_count%3||
       (input.skinning!=SkinningMode::disabled&&count%4))
        return PipelineStatus::unsupported_count;
    if(unsigned(input.filter.winding)<1||unsigned(input.filter.winding)>3)
        return PipelineStatus::invalid_filter_mode;
    if(input.filter.subpixel!=(input.projection.precision_state==5))
        return PipelineStatus::invalid_projection_filter_state;
    const std::size_t padding_count=input.filter.subpixel?
        3*((4-input.index_count/3%4)%4):0;
    if(input.subpixel_padding_indices.size()<padding_count)
        return PipelineStatus::unsupported_subpixel_padding;
    for(std::size_t i=0;i<padding_count;++i)
        if(input.subpixel_padding_indices[i]>=count)
            return PipelineStatus::unsupported_subpixel_padding;
    const unsigned encoded_stride=6+encoded_directions*3;
    if(count>input.packed_vertices.size()/encoded_stride)
        return PipelineStatus::short_packed_vertices;
    const auto direction_streams=input.suppress_directions?0:encoded_directions;
    if(input.skinning!=SkinningMode::disabled) {
        if(input.influences.size()<count)return PipelineStatus::short_influences;
        if(input.palette.empty())return PipelineStatus::missing_palette;
        for(std::size_t vertex=0;vertex<count;++vertex)
            for(const auto joint:input.influences[vertex].joints)
                if(joint>=input.palette.size())
                    return PipelineStatus::unsupported_palette_backing;
    }

    GeometryOutput next;
    next.positions.resize(count);
    for(unsigned stream=0;stream<direction_streams;++stream)
        next.directions[stream].resize(count);
    const DecodedVertexStreams decoded_views{next.positions,
        {std::span<Float4>{next.directions[0]},std::span<Float4>{next.directions[1]}}};
    if(!decode_vertex_stream<typename Policies::DecodeMath>(input.packed_vertices,
            input.decode,decoded_views,std::ref(policy.decode_reciprocal),
            std::ref(policy.position_conversion)))
        return PipelineStatus::vertex_decode_declined;

    if(input.skinning!=SkinningMode::disabled) {
        next.prepared_palette.reserve(input.palette.size());
        for(const auto& matrix:input.palette)
            next.prepared_palette.push_back(cyclic_skin_matrix(matrix));
        detail::SkinViews views{next.positions,next.directions,input.influences,
            next.prepared_palette};
        bool skinned;
        if(!direction_streams)
            skinned=skin_positions_stream<typename Policies::PositionSkinMath>(
                views,count,policy.skinning_policy.convert_weights);
        else
            skinned=skin_geometry_stream<typename Policies::GeometrySkinMath,
                typename Policies::NormalMath,typename Policies::CofactorMath>(
                views,{count,direction_streams},std::ref(policy.normal_reciprocal),
                nullptr,policy.skinning_policy);
        if(!skinned)return PipelineStatus::skinning_declined;
    }

    // The old root decodes indices after skinning. Retain that stage order;
    // neither input is shared with the owned position/direction/palette vectors.
    next.decoded_indices.resize(input.index_count);
    if(!decode_triangle_indices(input.packed_indices,input.index_count,next.decoded_indices))
        return PipelineStatus::index_decode_declined;
    next.projected.resize(count);
    if(!project_geometry<typename Policies::ProjectionMath>(next.positions,input.projection,
            next.projected,std::ref(policy.projection_estimate),
            std::ref(policy.screen_conversion)))
        return PipelineStatus::projection_declined;
    next.filtered_indices.resize(input.index_count);
    std::size_t filtered_count=0;
    std::array<ProjectedVertex,9> padding;
    for(std::size_t i=0;i<padding_count;++i)
        padding[i]=next.projected[input.subpixel_padding_indices[i]];
    if(!filter_triangles<typename Policies::ProjectionMath>(next.projected,
            next.decoded_indices,input.filter,next.filtered_indices,filtered_count,
            std::ref(policy.subpixel_conversion),std::span(padding).first(padding_count)))
        return PipelineStatus::filter_declined;
    next.filtered_indices.resize(filtered_count);

    const auto stride=packed_vertex_stride(input.export_format);
    next.payload=detail::payload_layout(std::uint32_t(filtered_count),count,stride);
    // Zero surviving indices skip geometry allocation/packing/payload transfers
    // in the root, but still require a final command packet outside this kernel.
    if(filtered_count) {
        next.packed_vertices.resize(next.payload.logical_vertex_bytes);
        const VertexPackStreams pack_views{next.positions,next.directions[0],next.directions[1]};
        if(!pack_vertex_stream<typename Policies::PackingMath>(pack_views,
                {input.export_format,count,input.suppress_directions},next.packed_vertices,
                std::ref(policy.normal_conversion)))
            return PipelineStatus::packing_declined;
    }
    output=std::move(next);
    return PipelineStatus::logical_buffers_ready;
}

}
#endif
