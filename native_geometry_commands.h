// Project-written rendering command construction for supported geometry jobs.
// No guest state, generated game source, captured command template or DMA code.
#ifndef WKC_NATIVE_GEOMETRY_COMMANDS_H
#define WKC_NATIVE_GEOMETRY_COMMANDS_H
#include "native_vertex_pack.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace wkc::geometry::commands {
struct GeometryAllocation {
    std::uint32_t index_rsx=0,vertex_rsx=0;
    std::uint32_t location=0;
    std::uint32_t consumer_ea=0,reserved_end=0;
    bool vertex_payload_submitted=false;
};
struct PublicationSpec {
    VertexPackFormat format{};
    std::uint32_t request_flags=0;
    std::uint32_t filtered_index_count=0;
    std::uint32_t destination=0;
    std::uint8_t packet_quadwords=0;
    std::uint8_t dma_tag=0;
    // False includes empty filtered geometry and allocation failure. The root
    // still publishes a zero packet in either case when destination is nonzero.
    bool export_geometry=false;
};
struct CommandPublication {
    std::vector<std::uint8_t> bytes;
    std::uint32_t destination=0;
    std::uint8_t dma_tag=0;
    bool publish=false;
    unsigned method_splits=0,inserted_nops=0;
};
enum class BuildStatus : std::uint8_t {
    packet_ready,no_publication,unsupported_format,invalid_input,insufficient_packet_capacity
};
namespace detail {
struct Method {
    std::uint32_t address;
    std::vector<std::uint32_t> arguments;
    bool nonincrement=false;
};
inline void append(std::vector<Method>& out,std::uint32_t method,std::uint32_t value) {
    out.push_back({method,{value},false});
}
inline void put32(std::uint8_t* output,std::uint32_t value) {
    output[0]=std::uint8_t(value>>24);output[1]=std::uint8_t(value>>16);
    output[2]=std::uint8_t(value>>8);output[3]=std::uint8_t(value);
}
}

// Produces only the final owned big-endian command packet. The caller must
// finish/publish allocation and payload stores before submitting these bytes
// with a same-tag fenced PUT. Location0's separate4588 four-byte GETF and all
// memory ordering/ownership/interrupt semantics remain transaction obligations.
// Structural declines leave output unchanged. No captured packet bytes are read.
inline BuildStatus build(const PublicationSpec& spec,const GeometryAllocation& allocation,
                         CommandPublication& output) {
    if(spec.destination&3u||spec.dma_tag>=32)
        return BuildStatus::invalid_input;
    CommandPublication next;
    next.destination=spec.destination;next.dma_tag=spec.dma_tag;
    if(!spec.destination) {
        output=std::move(next);return BuildStatus::no_publication;
    }
    const auto capacity=spec.packet_quadwords?unsigned(spec.packet_quadwords)*16u:4u;
    std::vector<detail::Method> commands;
    if(spec.export_geometry) {
        const auto stride=packed_vertex_stride(spec.format);
        if(!stride)return BuildStatus::unsupported_format;
        if(!spec.filtered_index_count||spec.filtered_index_count>65535||
           spec.filtered_index_count%3||allocation.location>1||
           !spec.packet_quadwords)
            return BuildStatus::invalid_input;
        if(allocation.vertex_payload_submitted) {
            detail::append(commands,0x1710,0);
            for(unsigned i=0;i<3;++i)detail::append(commands,0x1714,0);
        }
        const auto vertex_location=allocation.location<<31;
        detail::append(commands,0x1740,stride*256u+0x32u);
        detail::append(commands,0x1680,allocation.vertex_rsx|vertex_location);
        if(spec.format!=VertexPackFormat::position&&(spec.request_flags&4u)) {
            detail::append(commands,0x1748,stride*256u+0x16u);
            detail::append(commands,0x1688,(allocation.vertex_rsx+12u)|vertex_location);
        }
        if(spec.format==VertexPackFormat::position_normal_tangent&&(spec.request_flags&0x4000u)) {
            detail::append(commands,0x1778,stride*256u+0x43u);
            detail::append(commands,0x16b8,(allocation.vertex_rsx+16u)|vertex_location);
        }
        detail::append(commands,0x1714,0);
        const auto index=allocation.index_rsx&0x1fffffffu;
        commands.push_back({0x181c,{index,(allocation.location&255u)|16u},false});
        detail::append(commands,0x1808,5);
        auto remaining=spec.filtered_index_count;
        std::uint32_t first=0;
        const auto prefix=(((index+127u)&~127u)-index)/2u;
        if(prefix&&remaining>prefix) {
            detail::append(commands,0x1824,(prefix-1u)<<24);
            first=prefix;remaining-=prefix;
        }
        detail::Method draw{0x1824,{},true};
        while(remaining) {
            const auto count=(std::min)(remaining,256u);
            draw.arguments.push_back(((count-1u)<<24)|first);
            first+=count;remaining-=count;
        }
        commands.push_back(std::move(draw));
        detail::append(commands,0x1808,0);
        if(allocation.consumer_ea) {
            detail::append(commands,0x1d6c,allocation.consumer_ea&0xff0u);
            detail::append(commands,0x1d74,allocation.reserved_end);
        }
    }

    // The old staging packet preserves destination's128-byte phase. Encoding
    // with that phase directly gives the same split/advance/NOP decisions in an
    // owned byte vector. An incrementing method advances after a split;
    // a nonincrementing draw method retains its address.
    std::vector<std::uint32_t> words;
    for(const auto& command:commands) {
        std::size_t offset=0;
        while(offset<command.arguments.size()) {
            const auto phase=(spec.destination+std::uint32_t(words.size()*4))&127u;
            const auto available=(128u-phase)/4u;
            if(available==1) {words.push_back(0);++next.inserted_nops;continue;}
            const auto count=(std::min)(command.arguments.size()-offset,std::size_t(available-1));
            const auto method=command.address+(command.nonincrement?0u:std::uint32_t(offset*4));
            words.push_back((command.nonincrement?0x40000000u:0u)|
                            (std::uint32_t(count)<<18)|method);
            words.insert(words.end(),command.arguments.begin()+offset,
                         command.arguments.begin()+offset+count);
            offset+=count;if(offset<command.arguments.size())++next.method_splits;
        }
    }
    if(words.size()>capacity/4)return BuildStatus::insufficient_packet_capacity;
    next.bytes.assign(capacity,0);
    for(std::size_t i=0;i<words.size();++i)detail::put32(next.bytes.data()+i*4,words[i]);
    next.publish=true;output=std::move(next);return BuildStatus::packet_ready;
}

// Submit only an unmodified packet returned by build, after allocation and all
// payload requests are accepted in the same ordered DMA stream/tag. put_fenced
// enqueues MFC PUTF semantics: the command bytes must not become visible before
// earlier same-tag payload writes. It must not fail or throw after preflight.
// The caller retains packet.bytes until the backend has consumed the source.
// Acceptance does not imply DMA/GPU completion. Location0's separate GETF, when
// required, remains a job-adapter obligation before this publication step.
template<class Backend>
bool submit(const CommandPublication& packet,Backend& backend) {
    static_assert(noexcept(backend.architectural_fence()));
    static_assert(noexcept(backend.interrupts_enabled()));
    static_assert(noexcept(backend.set_interrupts_enabled(false)));
    static_assert(noexcept(backend.put_fenced(std::span<const std::uint8_t>{},std::uint32_t{},std::uint32_t{})));
    static_assert(std::is_same_v<decltype(backend.put_fenced(std::span<const std::uint8_t>{},std::uint32_t{},std::uint32_t{})),void>);
    if(!packet.publish)return true;
    if(!packet.destination||(packet.destination&3u)||packet.dma_tag>=32||
       packet.bytes.empty()||(packet.bytes.size()&3u))return false;
    backend.architectural_fence();
    const bool enabled=backend.interrupts_enabled();
    backend.set_interrupts_enabled(false);
    backend.put_fenced(packet.bytes,packet.destination,packet.dma_tag);
    if(enabled)backend.set_interrupts_enabled(true);
    return true;
}
}
#endif
