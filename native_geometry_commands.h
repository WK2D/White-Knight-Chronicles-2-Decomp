// Project-written rendering command construction for supported geometry jobs.
// No guest state, generated game source, captured command template or DMA code.
#ifndef WKC_NATIVE_GEOMETRY_COMMANDS_H
#define WKC_NATIVE_GEOMETRY_COMMANDS_H
#include "native_vertex_pack.h"
#include <algorithm>
#include <array>
#include <cstddef>
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
inline void put32(std::uint8_t* output,std::uint32_t value) {
    output[0]=std::uint8_t(value>>24);output[1]=std::uint8_t(value>>16);
    output[2]=std::uint8_t(value>>8);output[3]=std::uint8_t(value);
}
// Encode into the final zero-filled packet, with no method/argument or word
// staging allocations. Overflow only changes this private candidate packet.
// Preserve destination's 128-byte phase for split/advance/NOP decisions.
// Incrementing methods advance after a split; draw methods keep their address.
class PacketWriter {
    CommandPublication& packet_;
    std::size_t words_=0;
    bool overflow_=false;
    void word(std::uint32_t value)noexcept {
        put32(packet_.bytes.data()+words_*4,value);++words_;
    }
public:
    explicit PacketWriter(CommandPublication& packet)noexcept:packet_(packet){}
    void append(std::uint32_t address,std::span<const std::uint32_t> arguments,
                bool nonincrement=false)noexcept {
        if(overflow_)return;
        const auto capacity=packet_.bytes.size()/4;
        std::size_t offset=0;
        while(offset<arguments.size()) {
            const auto phase=(packet_.destination+std::uint32_t(words_*4))&127u;
            const auto available=(128u-phase)/4u;
            if(available==1) {
                if(words_==capacity){overflow_=true;return;}
                word(0);++packet_.inserted_nops;continue;
            }
            const auto count=(std::min)(arguments.size()-offset,std::size_t(available-1));
            if(count+1>capacity-words_){overflow_=true;return;}
            const auto method=address+(nonincrement?0u:std::uint32_t(offset*4));
            word((nonincrement?0x40000000u:0u)|(std::uint32_t(count)<<18)|method);
            for(std::size_t i=0;i<count;++i)word(arguments[offset+i]);
            offset+=count;if(offset<arguments.size())++packet_.method_splits;
        }
    }
    void single(std::uint32_t address,std::uint32_t value)noexcept {
        append(address,{&value,1});
    }
    [[nodiscard]] bool good()const noexcept{return !overflow_;}
};
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
    unsigned stride=0;
    if(spec.export_geometry) {
        stride=packed_vertex_stride(spec.format);
        if(!stride)return BuildStatus::unsupported_format;
        if(!spec.filtered_index_count||spec.filtered_index_count>65535||
           spec.filtered_index_count%3||allocation.location>1||
           !spec.packet_quadwords)
            return BuildStatus::invalid_input;
    }
    next.bytes.assign(capacity,0);
    detail::PacketWriter commands(next);
    if(spec.export_geometry) {
        if(allocation.vertex_payload_submitted) {
            commands.single(0x1710,0);
            for(unsigned i=0;i<3;++i)commands.single(0x1714,0);
        }
        const auto vertex_location=allocation.location<<31;
        commands.single(0x1740,stride*256u+0x32u);
        commands.single(0x1680,allocation.vertex_rsx|vertex_location);
        if(spec.format!=VertexPackFormat::position&&(spec.request_flags&4u)) {
            commands.single(0x1748,stride*256u+0x16u);
            commands.single(0x1688,(allocation.vertex_rsx+12u)|vertex_location);
        }
        if(spec.format==VertexPackFormat::position_normal_tangent&&(spec.request_flags&0x4000u)) {
            commands.single(0x1778,stride*256u+0x43u);
            commands.single(0x16b8,(allocation.vertex_rsx+16u)|vertex_location);
        }
        commands.single(0x1714,0);
        const auto index=allocation.index_rsx&0x1fffffffu;
        const std::array<std::uint32_t,2> index_binding{index,(allocation.location&255u)|16u};
        commands.append(0x181c,index_binding);
        commands.single(0x1808,5);
        auto remaining=spec.filtered_index_count;
        std::uint32_t first=0;
        const auto prefix=(((index+127u)&~127u)-index)/2u;
        if(prefix&&remaining>prefix) {
            commands.single(0x1824,(prefix-1u)<<24);
            first=prefix;remaining-=prefix;
        }
        // At most ceil(65535/256) arguments; the optional prefix is separate.
        std::array<std::uint32_t,256> draw;
        std::size_t draw_count=0;
        while(remaining) {
            const auto count=(std::min)(remaining,256u);
            draw[draw_count++]=((count-1u)<<24)|first;
            first+=count;remaining-=count;
        }
        commands.append(0x1824,{draw.data(),draw_count},true);
        commands.single(0x1808,0);
        if(allocation.consumer_ea) {
            commands.single(0x1d6c,allocation.consumer_ea&0xff0u);
            commands.single(0x1d74,allocation.reserved_end);
        }
    }

    if(!commands.good())return BuildStatus::insufficient_packet_capacity;
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
