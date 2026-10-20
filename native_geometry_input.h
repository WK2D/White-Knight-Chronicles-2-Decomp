// Project-written typed geometry input adapter; platform acquisition is separate.
// Only original descriptor bytes, completed staged input spans and explicit
// platform/layout contracts enter. No context, register or memory-reader API.
#ifndef WKC_NATIVE_GEOMETRY_INPUT_H
#define WKC_NATIVE_GEOMETRY_INPUT_H

#include "native_geometry_pipeline.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace wkc::geometry::input {
namespace pipeline=wkc::geometry::pipeline;
using Byte=std::uint8_t;
inline constexpr unsigned dma_slot_count=19;
inline constexpr std::uint32_t compatibility_limit=0x40000;

enum class RootLayout : Byte { unavailable,image19_custom_geometry_v1 };
enum class SchedulerMode : Byte { unavailable,spurs };
enum class ZeroProvenance : Byte {
    unavailable,
    // The current scheduler initialized this interval to zero, and the
    // selected root has not written it before the palette read.
    initialized_untouched_workspace
};
struct ZeroRange {
    std::uint32_t begin=0,bytes=0;
    ZeroProvenance provenance=ZeroProvenance::unavailable;
};
enum class PreservedZeroContract : Byte {
    unavailable,
    // A platform-owned proof: the selected root preserves the three known
    // zero save cells. This is a semantic initialization contract, no payload.
    image19_selected_root_three_zero_cells
};
struct CompatibilityLayout {
    // Integer layout coordinates classify bounded aliases, not addresses that
    // the native pipeline can dereference. They are discarded after adaptation.
    std::uint32_t io_begin=0,scratch_begin=0;
    // Bounds of the selected skinning call's save frame. The platform derives
    // these from its constructor/call-layout facts, never from a later oracle.
    std::uint32_t save_frame_begin=0,save_frame_end=0;
    std::vector<ZeroRange> zero_ranges;
    PreservedZeroContract preserved_zero=PreservedZeroContract::unavailable;
    // Bits0..2 prove the selected three save cells initially zero. No opaque
    // register words or later save-frame contents cross this interface.
    std::uint8_t preserved_zero_mask=0;
};
struct PlatformFacts {
    RootLayout root=RootLayout::unavailable;
    SchedulerMode scheduler=SchedulerMode::unavailable;
    std::uint32_t image_id=0,staged_slot_count=0;
    std::uint32_t dma_tag=0,allocator_slot=0;
    CompatibilityLayout compatibility;
};
struct StagedSlot {
    // Exact completed payload of this descriptor slot, without alignment slack.
    // Empty/invalid descriptor entries have an empty payload. The caller keeps
    // every span stable until make_input returns.
    std::span<const Byte> bytes;
    std::uint32_t begin=0;
};
struct StagedInputs {
    // Original descriptor, before the root changes header byte5 or any records.
    std::span<const Byte> descriptor;
    std::array<StagedSlot,dma_slot_count> slots;
};
struct SlotIdentity {
    std::uint32_t descriptor_word=0,effective_source=0;
    std::uint32_t staged_begin=0,declared_bytes=0;
    bool staged=false;
};
struct AdapterMetadata {
    std::array<Byte,16> original_header{};
    std::array<SlotIdentity,dma_slot_count> slots{};
    std::uint32_t allocator_control=0;
    std::uint32_t publication_destination_high=0,publication_destination=0;
    std::uint32_t request_flags=0,allocator_slot=0;
    std::uint8_t packet_quadwords=0,dma_tag=0;
    std::uint32_t declared_scratch_bytes=0;
    std::size_t palette_count=0;
};
struct AdaptedJob {
    pipeline::GeometryInput geometry;
    AdapterMetadata metadata;
};
enum class Status : Byte {
    ready,unsupported_platform,short_descriptor,nineteen_slot_layout,unsupported_descriptor,
    invalid_publication,staging_alignment_or_range,staged_payload_mismatch,
    unclassified_active_slot,fixed_block_sizes,unclassified_header,
    logical_group_or_count,unowned_vertex_prefix,position_conversion_layout,
    partial_palette,unowned_influence_prefix,scratch_or_frame_layout,
    invalid_zero_contract,unbounded_palette_row,unrecovered_palette_row,
    bootstrap_direction_alias,unsupported_subpixel_padding
};

namespace detail {
inline unsigned be16(const Byte* p) {
    return unsigned(p[0])*256u+p[1];
}
inline std::uint32_t be32(const Byte* p) {
    return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
           (std::uint32_t(p[2])<<8)|std::uint32_t(p[3]);
}
inline float floating(const Byte* p) {return std::bit_cast<float>(be32(p));}
inline Float4 float4(const Byte* p) {
    Float4 result;for(unsigned i=0;i<4;++i)result[i]=floating(p+4*i);return result;
}
inline bool range(std::uint64_t begin,std::uint64_t bytes) {
    return begin<=compatibility_limit&&bytes<=compatibility_limit-begin;
}
inline bool contains(std::uint64_t begin,std::uint64_t bytes,
                     std::uint64_t row) {
    return begin<=row&&row+16<=begin+bytes;
}
}

// DepthGreater supplies the same ordered comparison policy as projection. No
// host comparison/reciprocal policy is silently selected by this adapter.
// Every reject, allocation exception or comparison exception leaves output
// unchanged. This prepares owned inputs only; it issues no platform operation.
template<class DepthGreater>
Status make_input(const StagedInputs& staged,const PlatformFacts& platform,
                  DepthGreater depth_greater,AdaptedJob& output) {
    if(platform.root!=RootLayout::image19_custom_geometry_v1||
       platform.scheduler!=SchedulerMode::spurs||platform.image_id!=19)
        return Status::unsupported_platform;
    if(staged.descriptor.size()<220)return Status::short_descriptor;
    const auto descriptor=staged.descriptor.data();
    if(detail::be16(descriptor+10)!=dma_slot_count*8||
       platform.staged_slot_count!=dma_slot_count)
        return Status::nineteen_slot_layout;
    // The recovered constructor uses low32-bit sources and no cache list.
    // Higher-EA/cache layouts require their own acquisition and alias proof.
    if(detail::be32(descriptor)||detail::be32(descriptor+0x0c)||
       detail::be32(descriptor+0x20)||detail::be32(descriptor+0x24))
        return Status::unsupported_descriptor;
    if(platform.dma_tag>=32||detail::be32(descriptor+208)||
       (detail::be32(descriptor+212)&3u))return Status::invalid_publication;

    AdaptedJob next;
    auto& input=next.geometry;auto& metadata=next.metadata;
    metadata.allocator_control=detail::be32(descriptor+204);
    metadata.publication_destination_high=detail::be32(descriptor+208);
    metadata.publication_destination=detail::be32(descriptor+212);
    metadata.request_flags=detail::be32(descriptor+216);
    metadata.allocator_slot=platform.allocator_slot;
    metadata.dma_tag=std::uint8_t(platform.dma_tag);
    metadata.declared_scratch_bytes=std::uint32_t(detail::be16(descriptor+0x1e))<<4;
    std::array<std::span<const Byte>,dma_slot_count> slots;
    const auto& layout=platform.compatibility;
    std::uint32_t cursor=layout.io_begin;
    if(!detail::range(cursor,0))return Status::staging_alignment_or_range;
    for(unsigned i=0;i<dma_slot_count;++i) {
        const auto record=descriptor+48+i*8;
        const auto word=detail::be32(record),effective=detail::be32(record+4);
        const auto size=word&0x7fffu;
        const bool active=size&&effective;
        metadata.slots[i]={word,effective,cursor,size,active};
        if(!active) {
            if(!staged.slots[i].bytes.empty())return Status::staged_payload_mismatch;
            continue;
        }
        if((effective&15u)||!detail::range(cursor,size))
            return Status::staging_alignment_or_range;
        if(staged.slots[i].begin!=cursor||staged.slots[i].bytes.size()!=size)
            return Status::staged_payload_mismatch;
        slots[i]=staged.slots[i].bytes;
        cursor+=(size+15u)&~15u;
    }
    for(unsigned i:{0u,2u,6u,7u,9u,10u,11u,12u,13u,18u})
        if(!slots[i].empty())return Status::unclassified_active_slot;
    if(slots[14].size()!=128||slots[15].size()!=48||
       slots[16].size()!=16||slots[17].size()!=32)return Status::fixed_block_sizes;
    std::copy(slots[16].begin(),slots[16].end(),metadata.original_header.begin());
    const auto& header=metadata.original_header;
    const unsigned format=header[2],export_format=header[3],index_format=header[5]>>4;
    if((format!=32&&format!=33)||export_format!=format||
       header[0]!=(format==32?2:3)||header[4]||header[6]||header[7]||
       detail::be32(header.data()+12)!=0xffffffffu||(index_format!=2&&index_format!=3))
        return Status::unclassified_header;
    metadata.packet_quadwords=header[1];
    const auto count=detail::be16(header.data()+8);
    input.decode={static_cast<PackedVertexFormat>(format),count,{}};
    input.index_count=detail::be16(header.data()+10);
    input.export_format=static_cast<VertexPackFormat>(export_format);
    input.suppress_directions=(metadata.request_flags&0xfefeu)==0;
    if(!count||count%4||input.index_count%3)return Status::logical_group_or_count;
    const auto vertex_bytes=std::size_t(count)*(format==32?9u:12u);
    input.packed_vertices.assign(slots[8].begin(),slots[8].end());
    if(vertex_bytes>input.packed_vertices.size()) {
        const auto extra=vertex_bytes-input.packed_vertices.size();
        if(extra>slots[14].size()||
           metadata.slots[8].staged_begin+slots[8].size()!=metadata.slots[14].staged_begin)
            return Status::unowned_vertex_prefix;
        input.packed_vertices.insert(input.packed_vertices.end(),slots[14].begin(),slots[14].begin()+extra);
    }
    for(unsigned component=0;component<3;++component) {
        input.decode.position_bias[component]=std::bit_cast<std::int32_t>(
            detail::be32(slots[17].data()+component*4));
        const auto precision=std::bit_cast<std::int32_t>(
            detail::be32(slots[17].data()+16+component*4));
        // Recovered overlapping bit windows begin at byte offsets 0/2/4.
        // Seventeen-bit coordinates use the next byte's high carry bit;
        // extracting a u16 and rescaling would lose that contribution.
        // Wider controls need their own recovery before admission.
        if(precision < -12 || precision > 5)
            return Status::position_conversion_layout;
        input.decode.position_bits[component]=unsigned(12+precision);
    }
    input.packed_indices.assign(slots[1].begin(),slots[1].end());
    std::vector<Byte> palette_bytes(slots[3].begin(),slots[3].end());
    palette_bytes.insert(palette_bytes.end(),slots[4].begin(),slots[4].end());
    if(palette_bytes.size()%48)return Status::partial_palette;
    metadata.palette_count=palette_bytes.size()/48;
    input.palette.resize(metadata.palette_count);
    for(std::size_t matrix=0;matrix<input.palette.size();++matrix)
        for(unsigned row=0;row<3;++row)
            input.palette[matrix].rows[row]=detail::float4(palette_bytes.data()+matrix*48+row*16);
    input.skinning=metadata.palette_count&&(header[5]&15u)?
        pipeline::SkinningMode::linear_blend_variant3:pipeline::SkinningMode::disabled;
    if(input.skinning!=pipeline::SkinningMode::disabled) {
        std::vector<Byte> influence_bytes(slots[5].begin(),slots[5].end());
        const auto required=std::size_t(count)*8;
        if(required>influence_bytes.size()) {
            const auto extra=required-influence_bytes.size();
            if(extra>slots[8].size()||
               metadata.slots[5].staged_begin+slots[5].size()!=metadata.slots[8].staged_begin)
                return Status::unowned_influence_prefix;
            influence_bytes.insert(influence_bytes.end(),slots[8].begin(),slots[8].begin()+extra);
        }
        input.influences.resize(count);
        for(unsigned vertex=0;vertex<count;++vertex)for(unsigned joint=0;joint<4;++joint) {
            input.influences[vertex].weights[joint]=influence_bytes[vertex*8+joint*2];
            input.influences[vertex].joints[joint]=influence_bytes[vertex*8+joint*2+1];
        }
        auto raw_header=header;
        if(raw_header[5]&15u)raw_header[5]=Byte((raw_header[5]&0xf0u)|3u);
        const unsigned directions=input.suppress_directions?0u:format==32?1u:2u;
        const auto frame_bytes=directions?2224u:1472u;
        if((layout.scratch_begin&15u)||(layout.save_frame_begin&15u)||(layout.save_frame_end&15u)||
           !detail::range(layout.scratch_begin,metadata.declared_scratch_bytes)||
           metadata.declared_scratch_bytes!=(std::uint32_t(header[0]&15u)+1u)*count*16u||
           layout.save_frame_begin>layout.save_frame_end||
           layout.save_frame_end-layout.save_frame_begin!=frame_bytes||
           !detail::range(layout.save_frame_begin,frame_bytes)||
           std::uint64_t(layout.scratch_begin)+metadata.declared_scratch_bytes>layout.save_frame_begin)
            return Status::scratch_or_frame_layout;
        // Bounds keep initialization proofs finite and exclude arbitrary data
        // providers. Actual zero initialization remains a platform obligation.
        if(layout.zero_ranges.size()>64||(layout.preserved_zero_mask&~7u))
            return Status::invalid_zero_contract;
        for(const auto& zero:layout.zero_ranges)
            if(zero.provenance!=ZeroProvenance::initialized_untouched_workspace||
               !zero.bytes||!detail::range(zero.begin,zero.bytes)||
               zero.begin<layout.io_begin||
               std::uint64_t(zero.begin)+zero.bytes>layout.save_frame_begin)
                return Status::invalid_zero_contract;
        input.palette_bindings.resize(256);
        std::array<bool,256> prepared{};
        for(const auto& influence:input.influences)for(const auto joint:influence.joints) {
            if(joint<metadata.palette_count||prepared[joint])continue;
            for(unsigned row=0;row<3;++row) {
                const auto coordinate=std::uint64_t(metadata.slots[3].staged_begin)+joint*48u+row*16u;
                if(coordinate%16||!detail::range(coordinate,16))return Status::unbounded_palette_row;
                auto& binding=input.palette_bindings[joint][row];
                bool found=false;
                for(unsigned slot=0;slot<dma_slot_count;++slot) {
                    const auto begin=metadata.slots[slot].staged_begin;
                    if(detail::contains(begin,slots[slot].size(),coordinate)) {
                        const auto* source=slot==16?raw_header.data():slots[slot].data();
                        binding.source=pipeline::PaletteRowSource::literal;
                        binding.literal=detail::float4(source+std::size_t(coordinate-begin));
                        found=true;break;
                    }
                }
                if(found)continue;
                for(unsigned stream=0;stream<=directions;++stream) {
                    const auto begin=std::uint64_t(layout.scratch_begin)+std::uint64_t(stream)*count*16;
                    if(detail::contains(begin,std::uint64_t(count)*16,coordinate)) {
                        binding.source=stream==0?pipeline::PaletteRowSource::position:
                            stream==1?pipeline::PaletteRowSource::direction0:pipeline::PaletteRowSource::direction1;
                        binding.vertex=std::uint32_t((coordinate-begin)/16);found=true;break;
                    }
                }
                if(found)continue;
                if(directions&&detail::contains(layout.save_frame_begin,frame_bytes,coordinate)) {
                    const auto distance=std::uint64_t(layout.save_frame_end)-coordinate;
                    found=layout.preserved_zero==PreservedZeroContract::image19_selected_root_three_zero_cells&&
                          distance>=99*16u&&distance<=101*16u&&distance%16u==0&&
                          (layout.preserved_zero_mask&(1u<<unsigned(distance/16u-99u)));
                } else {
                    for(const auto& zero:layout.zero_ranges)
                        if(detail::contains(zero.begin,zero.bytes,coordinate)){found=true;break;}
                }
                if(!found)return Status::unrecovered_palette_row;
                binding.source=pipeline::PaletteRowSource::literal;binding.literal={};
            }
            prepared[joint]=true;
        }
        // The fixture constructor had an offline independence check. A native
        // adapter cannot import that check's result as a policy: reject reads
        // that would observe unspecified initial direction writes instead.
        const unsigned latency=directions==1?2u:1u;
        for(unsigned vertex=0;vertex<count;++vertex)
            for(const auto joint:input.influences[vertex].joints) {
                if(joint<metadata.palette_count)continue;
                for(const auto& binding:input.palette_bindings[joint]) {
                    const bool direction=binding.source==pipeline::PaletteRowSource::direction0||
                                         binding.source==pipeline::PaletteRowSource::direction1;
                    const auto group=vertex/4;
                    if(direction&&group&&binding.vertex<4&&group<=latency)
                        return Status::bootstrap_direction_alias;
                }
            }
    }

    for(unsigned row=0;row<3;++row)input.projection.model.rows[row]=detail::float4(slots[15].data()+row*16);
    for(unsigned row=0;row<4;++row)input.projection.projection[row]=detail::float4(slots[14].data()+0x10+row*16);
    input.projection.viewport_scale=detail::float4(slots[14].data()+0x50);
    input.projection.viewport_offset=detail::float4(slots[14].data()+0x60);
    input.projection.precision_state=slots[14][0x70];
    const auto x=detail::be16(slots[14].data()),y=detail::be16(slots[14].data()+2);
    const auto width=detail::be16(slots[14].data()+4),height=detail::be16(slots[14].data()+6);
    const auto z0=detail::floating(slots[14].data()+8),z1=detail::floating(slots[14].data()+12);
    const bool reverse_depth=depth_greater(z0,z1);
    input.projection.clip_minimum={float(x),float(y),reverse_depth?z1:z0,0};
    input.projection.clip_maximum={float((x+width)&65535),float((y+height)&65535),reverse_depth?z0:z1,0};
    const unsigned winding=(metadata.request_flags&0x10000u)?((metadata.request_flags&0x20000u)?3u:2u):1u;
    input.filter={static_cast<TriangleWinding>(winding),index_format==0||index_format==2,
                  input.projection.precision_state==5};
    if(input.filter.subpixel)return Status::unsupported_subpixel_padding;
    output=std::move(next);return Status::ready;
}

// Pure pre-staging constructor plan. A platform boundary may snapshot/copy
// each descriptor source range directly from VM, then tag those exact spans
// with slot_begins. Neither this helper nor make_input allocates a shadow store.
struct ConstructorContract {
    RootLayout root=RootLayout::unavailable;
    std::uint32_t image_id=0,dma_tag=0,allocator_slot=0;
    // Assert only for a scheduler preserving job_ctx_acquire/job_ls_clear's
    // initialization semantics, and the statically selected root's untouched
    // regions. This flag is proof supplied by the platform, not inferred data.
    bool initialized_untouched_workspace=false;
    std::uint8_t preserved_zero_cell_mask=0;
};
struct ConstructorPlan {
    PlatformFacts platform;
    std::array<std::uint32_t,dma_slot_count> slot_begins{};
    std::uint32_t region_end=0;
};
enum class ConstructorStatus : Byte {
    ready,unsupported_contract,short_inputs,unsupported_header,
    unsupported_descriptor,constructor_range_or_overlap
};
inline ConstructorStatus plan_spurs_constructor(
    std::span<const Byte> descriptor,std::span<const Byte> original_header,
    const ConstructorContract& contract,ConstructorPlan& output) {
    if(contract.root!=RootLayout::image19_custom_geometry_v1||contract.image_id!=19||
       contract.dma_tag>=32||(contract.preserved_zero_cell_mask&~7u))
        return ConstructorStatus::unsupported_contract;
    if(descriptor.size()<220||original_header.size()!=16)return ConstructorStatus::short_inputs;
    const auto* d=descriptor.data();
    if(detail::be16(d+10)!=dma_slot_count*8)return ConstructorStatus::unsupported_descriptor;
    if(detail::be32(d)||detail::be32(d+0x0c)||detail::be32(d+0x20)||detail::be32(d+0x24))
        return ConstructorStatus::unsupported_descriptor;
    const unsigned format=original_header[2],count=detail::be16(original_header.data()+8);
    if((format!=32&&format!=33)||!count||count%4||
       original_header[0]!=(format==32?2:3))return ConstructorStatus::unsupported_header;
    const auto align1024=[](std::uint64_t value){return (value+1023u)&~std::uint64_t(1023u);};
    const auto binary_bytes=std::uint32_t(detail::be16(d+8))<<4;
    const auto io_bytes=detail::be32(d+0x14),out_bytes=detail::be32(d+0x18);
    const auto scratch_bytes=std::uint32_t(detail::be16(d+0x1e))<<4;
    const auto supplied_stack=std::uint32_t(detail::be16(d+0x1c))<<4;
    const auto stack_bytes=supplied_stack?supplied_stack:8192u;
    const auto io_begin=align1024(binary_bytes);
    const auto scratch_begin=io_begin+align1024(io_bytes)+align1024(out_bytes);
    const auto stack_top=scratch_begin+align1024(std::uint64_t(scratch_bytes)+stack_bytes);
    const unsigned directions=(detail::be32(d+216)&0xfefeu)==0?0u:format==32?1u:2u;
    const unsigned frame_bytes=directions?2224u:1472u;
    // 32-byte caller area, root's768-byte frame and wrapper's96-byte frame.
    // These are selected-root layout facts, not general calling conventions.
    if(!binary_bytes||binary_bytes>compatibility_limit||stack_top>0x3fbc0u||
       stack_top<32u+768u+96u+frame_bytes)
        return ConstructorStatus::constructor_range_or_overlap;
    const auto save_end=((stack_top-32u)&~std::uint64_t(15u))-768u-96u;
    const auto save_begin=save_end-frame_bytes;
    const auto active_scratch_end=scratch_begin+std::uint64_t(directions+1u)*count*16u;
    if(scratch_begin+scratch_bytes>save_begin||active_scratch_end>scratch_begin+scratch_bytes)
        return ConstructorStatus::constructor_range_or_overlap;

    ConstructorPlan next;
    auto& platform=next.platform;auto& layout=platform.compatibility;
    platform.root=contract.root;platform.scheduler=SchedulerMode::spurs;
    platform.image_id=contract.image_id;platform.staged_slot_count=dma_slot_count;
    platform.dma_tag=contract.dma_tag;platform.allocator_slot=contract.allocator_slot;
    layout.io_begin=std::uint32_t(io_begin);layout.scratch_begin=std::uint32_t(scratch_begin);
    layout.save_frame_begin=std::uint32_t(save_begin);layout.save_frame_end=std::uint32_t(save_end);
    layout.preserved_zero=PreservedZeroContract::image19_selected_root_three_zero_cells;
    layout.preserved_zero_mask=contract.preserved_zero_cell_mask;
    next.region_end=std::uint32_t(stack_top);
    auto zero_cursor=io_begin;
    auto cursor=io_begin;
    const auto add_zero=[&](std::uint64_t begin,std::uint64_t end) {
        if(contract.initialized_untouched_workspace&&begin<end)
            layout.zero_ranges.push_back({std::uint32_t(begin),std::uint32_t(end-begin),
                ZeroProvenance::initialized_untouched_workspace});
    };
    for(unsigned slot=0;slot<dma_slot_count;++slot) {
        next.slot_begins[slot]=std::uint32_t(cursor);
        const auto* record=d+48+slot*8;
        const auto size=detail::be32(record)&0x7fffu,effective=detail::be32(record+4);
        if(!size||!effective)continue;
        if((effective&15u)||!detail::range(cursor,size)||cursor+size>scratch_begin)
            return ConstructorStatus::constructor_range_or_overlap;
        add_zero(zero_cursor,cursor);zero_cursor=cursor+size;
        cursor+=(size+15u)&~std::uint64_t(15u);
    }
    add_zero(zero_cursor,scratch_begin);
    add_zero(active_scratch_end,save_begin);
    output=std::move(next);return ConstructorStatus::ready;
}
}
#endif
