// Project-owned typed job composition tests. The input and host math policies
// reproduce the synthetic fixture in native_geometry_pipeline_test.cpp. No
// SDK, game data, generated code, guest registers or captured buffers are used.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_geometry_job.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace wkc::geometry;
namespace gj=wkc::geometry::job;
namespace gp=wkc::geometry::pipeline;
namespace ga=wkc::geometry::allocation;
namespace gc=wkc::geometry::commands;
static_assert(!std::is_copy_constructible_v<gj::GeometryJob>);
static_assert(!std::is_move_constructible_v<gj::GeometryJob>);
namespace {
using Byte=std::uint8_t;
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
struct ThrowingPolicies:FiniteHostPolicies {ThrowingPositionConversion position_conversion;};
void put16(Byte* bytes,unsigned value) {
    bytes[0]=Byte(value>>8);bytes[1]=Byte(value);
}
void put24(Byte* bytes,unsigned value) {
    bytes[0]=Byte(value>>16);bytes[1]=Byte(value>>8);bytes[2]=Byte(value);
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
gp::GeometryInput input_example(unsigned vertices=4,unsigned directions=1) {
    assert(vertices>=3&&(directions==1||directions==2));
    gp::GeometryInput input;
    input.decode={directions==1?PackedVertexFormat::position_direction:
        PackedVertexFormat::position_two_directions,vertices,{-4096,-4096,0}};
    input.export_format=directions==1?VertexPackFormat::position_normal:
        VertexPackFormat::position_normal_tangent;
    input.index_count=3;input.skinning=gp::SkinningMode::disabled;
    input.projection=identity_projection();input.filter={TriangleWinding::positive};
    const unsigned stride=6+directions*3;
    input.packed_vertices.resize(vertices*stride);
    for(unsigned vertex=0;vertex<vertices;++vertex) {
        auto* bytes=input.packed_vertices.data()+vertex*stride;
        const auto position=original_position(vertex);
        for(unsigned axis=0;axis<3;++axis)
            put16(bytes+axis*2,unsigned(int(position[axis]*4096)-input.decode.position_bias[axis]));
        put24(bytes+6,(512u<<14)|(512u<<4)|(2u<<2)|3u);
        if(directions==2)put24(bytes+9,(640u<<14)|(384u<<4));
    }
    input.packed_indices={0,0,0,0,0,1,0,0,0,0xc0};
    input.influences.resize(vertices,PackedSkinInfluences{{255,0,0,0},{0,0,0,0}});
    input.palette.resize(1);
    input.palette[0].rows={Float4{1,0,0,0},Float4{0,1,0,0},Float4{0,0,1,0}};
    return input;
}
gj::Config config_example() {return {0x8000,2,0x4004,0x607c,16,7};}
ga::AllocationLine line(std::uint32_t begin,std::uint32_t mapped,std::uint32_t end,
    std::uint32_t producer,std::uint32_t location,std::uint32_t last) {
    ga::AllocationLine value;
    for(unsigned i=0;i<value.size();++i)value[i]=Byte(i*13u+0x31u);
    ga::allocation_word(value,0,begin);ga::allocation_word(value,4,mapped);
    ga::allocation_word(value,8,end);ga::allocation_word(value,12,producer);
    ga::allocation_word(value,16,location);ga::allocation_word(value,20,last);
    return value;
}
enum class Op {machine,slot,global,consumer,conditional,unconditional,delay};
char code(Op op) {
    switch(op) {
    case Op::machine:return 'M';case Op::slot:return 'S';case Op::global:return 'G';
    case Op::consumer:return 'C';case Op::conditional:return 'P';
    case Op::unconditional:return 'U';case Op::delay:return 'W';
    }
    return '?';
}
struct Step {
    Op op;std::uint32_t address=0,value=0,tag=0;
    std::uint64_t token=0;ga::AllocationLine bytes{};
};
Step machine(unsigned status) {return {Op::machine,0,status};}
Step locked(Op op,std::uint32_t address,ga::AllocationLine bytes,std::uint64_t token) {
    Step step{op,address};step.bytes=bytes;step.token=token;return step;
}
Step publish(Op op,std::uint32_t address,ga::AllocationLine bytes,std::uint64_t token,unsigned status=0) {
    auto step=locked(op,address,bytes,token);step.value=status;return step;
}
Step consumer(std::uint32_t cursor,unsigned tag) {return {Op::consumer,0x5000,cursor,tag};}
struct ExpectedSource {
    std::vector<Byte> bytes;
    std::uint32_t destination=0,tag=0;
    char kind=0;
};
struct BorrowedSource {std::span<const Byte> bytes;};

// Pending optional callbacks have one outstanding operation. A repeat validates
// identical arguments and completes its receipt; it never queues another one.
// DMA enqueue methods retain only source spans. Their bytes are first consumed
// at the completed GETF or final tag receipt, after multiple job advances.
struct Backend {
    std::vector<Step> script;
    std::size_t cursor=0;
    bool pending_once=false,outstanding=false,irq=true;
    unsigned submissions=0,polls=0,slot_ownership=0;
    std::uint64_t slot_token=0;
    std::string events;
    std::array<ExpectedSource,3> expected{};
    std::array<BorrowedSource,3> borrowed{};
    unsigned expected_sources=0,queued=0,consumed=0,payload_puts=0,packet_puts=0;
    unsigned location_submissions=0,location_polls=0,complete_polls=0;
    bool location_outstanding=false,completion_started=false;
    unsigned expected_tag=7;
    std::uint32_t expected_location_cursor=0;
    bool throw_machine=false,throw_location=false,throw_complete=false;
    unsigned exceptions=0;
    Backend() {events.reserve(4096);}
    const Step* take(Op op,std::uint32_t address=0,unsigned tag=0,
        const ga::AllocationLine* bytes=nullptr,std::uint64_t token=0) {
        assert(cursor<script.size());const auto& step=script[cursor];
        assert(step.op==op&&step.address==address);
        if(op==Op::consumer)assert(step.tag==tag);
        if(op==Op::conditional||op==Op::unconditional)
            assert(bytes&&*bytes==step.bytes&&token==step.token);
        if(outstanding) {outstanding=false;++polls;++cursor;return &step;}
        events.push_back(code(op));++submissions;
        if(pending_once) {outstanding=true;return nullptr;}
        ++cursor;return &step;
    }
    std::optional<std::uint32_t> read_machine_status() {
        if(throw_machine){++exceptions;events.push_back('X');throw std::runtime_error("machine receipt");}
        const auto* step=take(Op::machine);if(!step)return std::nullopt;return step->value;
    }
    void set_interrupts(bool value) {irq=value;events.push_back(value?'E':'D');}
    std::optional<ga::AllocationLineReceipt> get_locked_line(std::uint32_t address,ga::AllocationLineKind kind) {
        const auto* step=take(kind==ga::AllocationLineKind::slot?Op::slot:Op::global,address);
        if(!step)return std::nullopt;
        if(kind==ga::AllocationLineKind::slot){++slot_ownership;slot_token=step->token;}
        else assert(slot_ownership==1);
        return ga::AllocationLineReceipt{step->bytes,step->value,step->token};
    }
    void fence() {events.push_back('F');}
    std::optional<std::uint32_t> put_conditional(std::uint32_t address,const ga::AllocationLine& bytes,
        std::uint64_t token) {
        const auto* step=take(Op::conditional,address,0,&bytes,token);
        if(!step)return std::nullopt;return step->value;
    }
    std::optional<std::uint32_t> put_unconditional(std::uint32_t address,const ga::AllocationLine& bytes,
        std::uint64_t token) {
        assert(slot_ownership==1&&token==slot_token);
        const auto* step=take(Op::unconditional,address,0,&bytes,token);
        if(!step)return std::nullopt;return step->value;
    }
    std::optional<ga::RingConsumerReceipt> get_consumer(std::uint32_t address,unsigned tag) {
        const auto* step=take(Op::consumer,address,tag);
        if(!step)return std::nullopt;return ga::RingConsumerReceipt{step->value};
    }
    bool wait_decrementer_reads(unsigned count) {assert(count==25);return take(Op::delay)!=nullptr;}
    void architectural_fence() noexcept {events.push_back('f');}
    bool interrupts_enabled() noexcept {events.push_back('r');return irq;}
    void set_interrupts_enabled(bool value) noexcept {irq=value;events.push_back(value?'e':'d');}
    void enqueue(std::span<const Byte> bytes,std::uint32_t destination,unsigned tag,char kind) noexcept {
        assert(!irq&&queued<expected_sources);
        const auto& source=expected[queued];
        assert(source.destination==destination&&source.tag==tag&&source.kind==kind);
        assert(bytes.size()==source.bytes.size()&&!bytes.empty());
        borrowed[queued++].bytes=bytes;events.push_back(kind);
    }
    void put(std::span<const Byte> bytes,std::uint32_t destination,unsigned tag,Byte command) noexcept {
        assert(command==0x20&&payload_puts<2&&packet_puts==0);
        enqueue(bytes,destination,tag,payload_puts++?'V':'I');
    }
    void put_fenced(std::span<const Byte> bytes,std::uint32_t destination,unsigned tag) noexcept {
        assert(packet_puts++==0);
        enqueue(bytes,destination,tag,'Q');
    }
    void consume_queued() {
        while(consumed<queued) {
            const auto& source=borrowed[consumed].bytes;
            const auto& wanted=expected[consumed].bytes;
            assert(source.size()==wanted.size()&&std::equal(source.begin(),source.end(),wanted.begin()));
            ++consumed;
        }
    }
    std::optional<std::array<Byte,4>> get_location0_fenced(std::uint32_t cursor_value,unsigned tag) {
        assert(cursor_value==expected_location_cursor&&tag==expected_tag);
        assert(payload_puts==2&&packet_puts==0&&queued==2);
        if(throw_location){++exceptions;events.push_back('X');throw std::runtime_error("location0 receipt");}
        if(!location_outstanding) {
            assert(location_submissions++==0);
            const bool enabled=interrupts_enabled();set_interrupts_enabled(false);
            events.push_back('L');if(enabled)set_interrupts_enabled(true);
            if(pending_once){location_outstanding=true;assert(consumed==0);return std::nullopt;}
        } else {location_outstanding=false;++location_polls;}
        // The completed fenced read also proves the preceding payload requests
        // consumed their sources. The later command source remains borrowed.
        consume_queued();return std::array<Byte,4>{0x11,0x22,0x33,0x44};
    }
    bool complete_tag(unsigned tag) {
        assert(tag==expected_tag&&packet_puts<=1&&queued==expected_sources);
        if(!completion_started){completion_started=true;events.push_back('T');}
        ++complete_polls;
        if(throw_complete){++exceptions;events.push_back('X');throw std::runtime_error("tag receipt");}
        if(pending_once&&complete_polls<=2)return false;
        consume_queued();return true;
    }
};
enum class Path {direct,global,ring,global_retry,ring_fallback,ring_blocking};
struct AllocationExpectation {
    std::uint32_t effective=0,mapped=0,location=0,reserved_end=0,consumer_address=0;
    std::string events;
};
AllocationExpectation scenario(Path path,unsigned location,unsigned machine_status,
    gj::Config& config,Backend& backend) {
    if(path==Path::direct) {
        config.allocator_control=0x4002u|location;
        return {0x4000,0xffffffffu,location,0,0,{}};
    }
    const auto slot_address=config.allocator_control+128u+(config.allocator_slot<<7);
    auto slot=line(0x1000,0x9000,path==Path::global||path==Path::global_retry?0x1000:0x1400,
        0x1100,location,0x5000);
    backend.script={machine(machine_status),locked(Op::slot,slot_address,slot,11)};
    const auto restore=machine_status?std::string("E"):std::string{};
    if(path==Path::ring) {
        auto committed=slot;ga::allocation_word(committed,12,0x1180);
        backend.script.push_back(consumer(0x1100,config.dma_tag));
        backend.script.push_back(publish(Op::unconditional,slot_address,committed,11));
        return {0x1100,0x9100,location,0x1180,0x5000,"MDSFCFFU"+restore};
    }
    if(path==Path::ring_fallback||path==Path::ring_blocking)
        backend.script.push_back(consumer(0x1140,config.dma_tag));
    auto global=line(0x2000,0x6000,0x2800,0x2200,location,0x40);
    auto committed=global;ga::allocation_word(committed,12,0x2280);
    if(path==Path::ring_blocking) {
        global=line(0x2000,0x6000,0x2100,0x20f0,location,0x40);
        committed=global;ga::allocation_word(committed,20,0xc0);
    }
    backend.script.push_back(locked(Op::global,config.allocator_control,global,101));
    backend.script.push_back(publish(Op::conditional,config.allocator_control,committed,101,
        path==Path::global_retry?1:0));
    if(path==Path::global_retry) {
        auto fresh=line(0x3000,0xa000,0x3800,0x3300,location,0x50);
        auto fresh_committed=fresh;ga::allocation_word(fresh_committed,12,0x3380);
        backend.script.push_back(locked(Op::global,config.allocator_control,fresh,102));
        backend.script.push_back(publish(Op::conditional,config.allocator_control,fresh_committed,102));
        return {0x3300,0xa300,location,0,0,"MDSFGFFPGFFP"+restore};
    }
    if(path==Path::ring_blocking) {
        auto slot_committed=slot;ga::allocation_word(slot_committed,12,0x1180);
        backend.script.push_back(consumer(0x1140,config.dma_tag));
        backend.script.push_back(Step{Op::delay});
        backend.script.push_back(consumer(0x1100,config.dma_tag));
        backend.script.push_back(publish(Op::unconditional,slot_address,slot_committed,11));
        return {0x1100,0x9100,location,0x1180,0x5000,"MDSFCFGFFPCFWCFFU"+restore};
    }
    return {0x2200,0x6200,location,0,0,
        std::string(path==Path::ring_fallback?"MDSFCFGFFP":"MDSFGFFP")+restore};
}
gc::CommandPublication expected_packet(const gj::Config& config,VertexPackFormat format,
    unsigned count,const AllocationExpectation& allocation,bool nonempty) {
    gc::CommandPublication packet;
    const gc::PublicationSpec spec{format,config.request_flags,count,config.command_destination,
        config.packet_quadwords,config.dma_tag,nonempty};
    const gc::GeometryAllocation binding{allocation.mapped,allocation.mapped+16,
        allocation.location,allocation.consumer_address,allocation.reserved_end,nonempty};
    const auto status=gc::build(spec,binding,packet);
    assert(status==gc::BuildStatus::packet_ready||status==gc::BuildStatus::no_publication);
    return packet;
}
void expect_sources(Backend& backend,const gj::GeometryJob& job,const gj::Config& config,
    const AllocationExpectation& allocation,const gc::CommandPublication& packet,bool nonempty) {
    if(nonempty) {
        backend.expected[0]={job.geometry().index_payload,allocation.effective,config.dma_tag,'I'};
        backend.expected[1]={job.geometry().packed_vertices,allocation.effective+16,config.dma_tag,'V'};
        backend.expected_sources=2;
        backend.expected_location_cursor=allocation.effective+16+unsigned(job.geometry().packed_vertices.size());
    }
    if(packet.publish)
        backend.expected[backend.expected_sources++]={packet.bytes,config.command_destination,config.dma_tag,'Q'};
    backend.expected_tag=config.dma_tag;
}
void assert_terminal_quiet(gj::GeometryJob& job,Backend& backend,gj::Progress terminal) {
    const auto events=backend.events;
    const auto cursor=backend.cursor;
    const std::array<unsigned,10> counts{backend.submissions,backend.polls,backend.queued,backend.consumed,
        backend.payload_puts,backend.packet_puts,backend.location_submissions,backend.location_polls,
        backend.complete_polls,backend.exceptions};
    for(unsigned repeat=0;repeat<4;++repeat)assert(job.advance(backend)==terminal);
    assert(backend.events==events&&backend.cursor==cursor);
    assert((counts==std::array<unsigned,10>{backend.submissions,backend.polls,backend.queued,backend.consumed,
        backend.payload_puts,backend.packet_puts,backend.location_submissions,backend.location_polls,
        backend.complete_polls,backend.exceptions}));
}
unsigned drive(gj::GeometryJob& job,Backend& backend,gj::Progress terminal) {
    unsigned advances=0,pending=0;
    for(;;) {
        assert(++advances<512);
        const auto progress=job.advance(backend);
        if(progress==gj::Progress::pending) {
            ++pending;assert(!job.complete()&&!job.failed());
            // Allocation callbacks can retain 128-byte line storage and the
            // slot lease before any payload is queued. Every pending external
            // operation therefore holds the job through its completed receipt.
            assert(job.external_effects_started()&&job.requires_retention()&&!job.may_fallback());
            if(backend.outstanding) {
                assert(job.allocation_transaction()&&!job.allocation_transaction()->finished());
                assert(backend.queued==0&&job.requires_retention());
            }
            // Neither a packet nor a payload may be re-enqueued merely because
            // an allocation, GETF or final tag receipt is not ready yet.
            assert(backend.payload_puts<=2&&backend.packet_puts<=1);
        }
        if(progress==gj::Progress::complete||progress==gj::Progress::failed||progress==gj::Progress::declined) {
            assert(progress==terminal);break;
        }
    }
    assert_terminal_quiet(job,backend,terminal);return pending;
}
std::string dma_events(bool irq,unsigned location,bool packet=true) {
    const auto restore=irq?std::string("e"):std::string{};
    auto result=std::string("rdI")+restore+"frdV"+restore;
    if(location==0)result+="rdL"+restore;
    if(packet)result+="frdQ"+restore;
    return result+"T";
}
void nonempty_case(Path path,unsigned location,bool pending,unsigned machine_status,bool initial_irq,
    bool publish_packet=true) {
    auto config=config_example();if(!publish_packet)config.command_destination=0;
    Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
    const auto allocation=scenario(path,location,machine_status,config,backend);
    gj::GeometryJob job;FiniteHostPolicies policies;
    const auto format=location?VertexPackFormat::position_normal_tangent:VertexPackFormat::position_normal;
    {
        auto input=input_example(4,location?2:1);
        assert(job.prepare(input,config,policies)==gj::PrepareStatus::ready);
        assert(job.geometry().positions.size()==4&&job.geometry().filtered_indices==std::vector<std::uint16_t>({0,1,2}));
        assert(job.geometry().index_payload==std::vector<Byte>({0,0,0,1,0,2,0,0,0,0,0,0,0,0,0,0}));
        for(unsigned vertex=0;vertex<4;++vertex)assert(job.geometry().positions[vertex]==original_position(vertex));
        assert(job.geometry().payload.reservation_bytes==128&&!job.geometry().payload.empty_geometry);
        assert(policies.projection_estimate.calls==4&&policies.decode_reciprocal.calls!=0);
        const auto saved=job.geometry().packed_vertices;
        std::fill(input.packed_vertices.begin(),input.packed_vertices.end(),Byte{0xa5});
        input.packed_indices.clear();input.palette.clear();input.influences.clear();
        assert(job.geometry().packed_vertices==saved);
        assert(job.prepare(input,config,policies)==gj::PrepareStatus::already_started);
    } // All producer input backing is destroyed before any backend call.
    assert(backend.events.empty()&&job.may_fallback()&&!job.external_effects_started());
    const auto packet=expected_packet(config,format,3,allocation,true);
    expect_sources(backend,job,config,allocation,packet,true);
    const auto pending_count=drive(job,backend,gj::Progress::complete);
    assert(job.complete()&&!job.failed()&&!job.may_fallback()&&!job.requires_retention());
    assert(job.external_effects_started()&&job.failure()==gj::Failure::none);
    assert(job.pipeline_status()==gp::PipelineStatus::logical_buffers_ready);
    assert(backend.cursor==backend.script.size()&&!backend.outstanding&&!backend.location_outstanding);
    assert(backend.submissions==backend.script.size()&&backend.polls==(pending?backend.script.size():0));
    assert(backend.location_submissions==(location==0?1u:0u));
    assert(backend.location_polls==(pending&&location==0?1u:0u));
    assert(backend.complete_polls==(pending?3u:1u));
    assert(pending_count==(pending?backend.script.size()+(location==0?1u:0u)+2:0));
    assert(backend.queued==backend.expected_sources&&backend.consumed==backend.queued);
    const bool final_irq=path==Path::direct?initial_irq:bool(machine_status);
    assert(backend.irq==final_irq);
    assert(backend.events==allocation.events+dma_events(final_irq,location,publish_packet));
    assert(job.packet().bytes==packet.bytes&&job.packet().publish==packet.publish);
    const auto& record=job.allocation_record();
    const auto advanced=16u+unsigned(job.geometry().packed_vertices.size());
    assert(record.effective_cursor()==allocation.effective+advanced&&record.mapped_cursor()==allocation.mapped+advanced);
    assert(record.location()==location&&record.bytes[20]==1);
    assert(payload_export::word(record.bytes.data()+12)==allocation.reserved_end);
    assert(payload_export::word(record.bytes.data()+16)==allocation.consumer_address);
    assert(job.allocation_transaction()&&job.allocation_transaction()->success());
}
void empty_case(bool pending,bool no_indices,bool publish_packet,bool initial_irq) {
    auto config=config_example();config.packet_quadwords=0;
    if(!publish_packet)config.command_destination=0;
    Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
    gj::GeometryJob job;FiniteHostPolicies policies;
    auto input=input_example();
    if(no_indices){input.index_count=0;input.packed_indices.clear();}
    else input.filter.winding=TriangleWinding::negative;
    assert(job.prepare(input,config,policies)==gj::PrepareStatus::ready);
    assert(job.geometry().payload.empty_geometry&&job.geometry().filtered_indices.empty());
    const auto packet=expected_packet(config,input.export_format,0,{},false);
    expect_sources(backend,job,config,{},packet,false);
    const auto pending_count=drive(job,backend,gj::Progress::complete);
    assert(job.complete()&&!job.failed()&&!job.requires_retention()&&!job.may_fallback());
    assert(!job.allocation_transaction()&&backend.script.empty()&&backend.submissions==0&&backend.payload_puts==0);
    assert(backend.location_submissions==0&&backend.irq==initial_irq);
    assert(job.external_effects_started()==publish_packet&&backend.packet_puts==unsigned(publish_packet));
    assert(pending_count==(pending&&publish_packet?2u:0u));
    assert(backend.complete_polls==(publish_packet?(pending?3u:1u):0u));
    assert(backend.queued==backend.consumed&&job.packet().bytes==packet.bytes);
    if(publish_packet) {
        assert(job.packet().bytes==std::vector<Byte>(4,0));
        assert(backend.events==std::string("frdQ")+(initial_irq?"e":"")+"T");
    } else assert(backend.events.empty());
}
void allocation_failure_case(bool pending) {
    auto config=config_example();Backend backend;backend.pending_once=pending;
    const auto slot_address=config.allocator_control+128u+(config.allocator_slot<<7);
    const auto empty=line(0x1000,0x9000,0x1000,0x1100,1,0x5000);
    const auto global=line(0x2000,0x6000,0x2100,0x20f0,1,0x40);
    auto overflow=global;ga::allocation_word(overflow,20,0xc0);
    backend.script={machine(1),locked(Op::slot,slot_address,empty,11),
        locked(Op::global,config.allocator_control,global,101),
        publish(Op::conditional,config.allocator_control,overflow,101)};
    gj::GeometryJob job;FiniteHostPolicies policies;const auto input=input_example();
    assert(job.prepare(input,config,policies)==gj::PrepareStatus::ready);
    const auto packet=expected_packet(config,input.export_format,0,{},false);
    expect_sources(backend,job,config,{},packet,false);
    const auto pending_count=drive(job,backend,gj::Progress::complete);
    assert(job.complete()&&!job.failed()&&!job.may_fallback()&&job.external_effects_started());
    assert(job.allocation_transaction()&&!job.allocation_transaction()->success());
    assert(backend.payload_puts==0&&backend.location_submissions==0&&backend.packet_puts==1);
    assert(job.packet().bytes==std::vector<Byte>(256,0)&&backend.queued==backend.consumed);
    assert(backend.events=="MDSFGFFPEfrdQeT"&&backend.irq);
    assert(backend.submissions==4&&backend.polls==(pending?4u:0u));
    assert(pending_count==(pending?6u:0u)&&!job.requires_retention());
}
void invalid_allocation_case(bool overflow,bool pending) {
    auto config=config_example();Backend backend;backend.pending_once=pending;
    if(overflow)config.allocator_control=0xffffffc3u;
    else scenario(Path::global,2,1,config,backend);
    gj::GeometryJob job;FiniteHostPolicies policies;const auto input=input_example();
    assert(job.prepare(input,config,policies)==gj::PrepareStatus::ready);
    drive(job,backend,gj::Progress::failed);
    assert(job.failed()&&!job.complete()&&job.failure()==gj::Failure::invalid_allocation);
    // Fatal allocation keeps allocator line storage/slot ownership alive even
    // when no payload request was accepted. Only backend cleanup can release it.
    assert(job.external_effects_started()&&!job.may_fallback()&&job.requires_retention());
    assert(backend.payload_puts==0&&backend.packet_puts==0&&backend.location_submissions==0);
    assert(backend.complete_polls==0&&backend.irq);
    assert(backend.events==(overflow?"":"MDSFGFFPE"));
}
void fatal_callback_case(unsigned fault) {
    auto config=config_example();Backend backend;
    if(fault==0)backend.throw_machine=true;
    else config.allocator_control=fault==1?0x4002:0x4003;
    gj::GeometryJob job;FiniteHostPolicies policies;const auto input=input_example();
    assert(job.prepare(input,config,policies)==gj::PrepareStatus::ready);
    const AllocationExpectation allocation{0x4000,0xffffffffu,fault==1?0u:1u,0,0,{}};
    const auto packet=expected_packet(config,input.export_format,3,allocation,true);
    expect_sources(backend,job,config,allocation,packet,true);
    backend.throw_location=fault==1;backend.throw_complete=fault==2;
    drive(job,backend,gj::Progress::failed);
    assert(job.failure()==gj::Failure::exception_after_effects&&job.external_effects_started()&&!job.may_fallback());
    // This also covers an exception from the first allocation callback: a
    // backend may retain transaction storage or ownership before throwing.
    assert(backend.exceptions==1&&job.requires_retention());
    assert(backend.payload_puts==(fault?2u:0u)&&backend.packet_puts==(fault==2?1u:0u));
    assert(backend.consumed==0&&backend.irq);
}
unsigned capacity_bound_cases() {
    unsigned cases=0;
    // prepare() probes aligned indices and the smallest positive prefix before
    // allocating. Exercise every even index phase against that bound, including
    // draw-count boundaries, destination-boundary splits and optional streams.
    for(auto format:{VertexPackFormat::position_normal,VertexPackFormat::position_normal_tangent,
                     VertexPackFormat::position}) // Custom formats 32, 33, 34.
        for(unsigned flags:{0u,0x4004u})
            for(unsigned count:{3u,63u,66u,255u,258u,513u,65535u})
                for(unsigned destination_phase=0;destination_phase<128;destination_phase+=4) {
                    const gc::PublicationSpec spec{format,flags,count,0x6000u+destination_phase,255,7,true};
                    const auto used=[&](unsigned index_phase) {
                        gc::CommandPublication packet;
                        // A nonzero consumer release is the final method, and
                        // its nonzero last argument makes trailing-zero trimming
                        // measure all encoded words, including inserted NOPs.
                        const gc::GeometryAllocation binding{index_phase,0x9000,1,0x10,0x1180,true};
                        assert(gc::build(spec,binding,packet)==gc::BuildStatus::packet_ready);
                        assert(packet.publish&&packet.bytes.size()==255u*16u);
                        auto bytes=unsigned(packet.bytes.size());
                        while(bytes&&payload_export::word(packet.bytes.data()+bytes-4)==0)bytes-=4;
                        assert(bytes>=8&&payload_export::word(packet.bytes.data()+bytes-4)==0x1180);
                        return bytes;
                    };
                    const auto bound=(std::max)(used(0),used(126));
                    for(unsigned index_phase=0;index_phase<128;index_phase+=2) {
                        assert(used(index_phase)<=bound);++cases;
                    }
                }
    return cases;
}
}

int wkc2_test_native_geometry_job() {
    unsigned nonempty=0,empty=0,allocation_failures=0,declines=0,fatals=0;
    for(auto path:{Path::direct,Path::global,Path::ring})
        for(unsigned location:{0u,1u})for(bool pending:{false,true})
            for(unsigned machine_status:{0u,1u})for(bool initial_irq:{false,true}) {
                nonempty_case(path,location,pending,machine_status,initial_irq);++nonempty;
            }
    for(auto path:{Path::global_retry,Path::ring_fallback,Path::ring_blocking})
        for(unsigned location:{0u,1u})for(bool pending:{false,true}) {
            nonempty_case(path,location,pending,1,true);++nonempty;
        }
    // A payload-only job still owns asynchronous sources until its tag receipt.
    for(unsigned location:{0u,1u})for(bool pending:{false,true}) {
        nonempty_case(Path::direct,location,pending,0,false,false);++nonempty;
    }
    for(bool pending:{false,true})for(bool no_indices:{false,true})
        for(bool publish_packet:{false,true})for(bool initial_irq:{false,true}) {
            empty_case(pending,no_indices,publish_packet,initial_irq);++empty;
        }
    for(bool pending:{false,true}){allocation_failure_case(pending);++allocation_failures;}
    for(bool overflow:{false,true})for(bool pending:{false,true}) {
        invalid_allocation_case(overflow,pending);++fatals;
    }
    for(unsigned fault=0;fault<3;++fault){fatal_callback_case(fault);++fatals;}
    for(unsigned fault=0;fault<7;++fault) {
        gj::GeometryJob job;Backend backend;FiniteHostPolicies policies;
        auto config=config_example();auto input=input_example();
        auto expected=gj::PrepareStatus::invalid_config;
        switch(fault) {
        case 0:config.dma_tag=32;break;
        case 1:config.command_destination|=1;break;
        case 2:input.decode.format=PackedVertexFormat(34);expected=gj::PrepareStatus::pipeline_declined;break;
        case 3:input.packed_vertices.pop_back();expected=gj::PrepareStatus::pipeline_declined;break;
        case 4:input=input_example(3,2);expected=gj::PrepareStatus::unsupported_vertex_tail;break;
        case 5:config.packet_quadwords=1;expected=gj::PrepareStatus::command_capacity;break;
        case 6:config.packet_quadwords=0;expected=gj::PrepareStatus::command_capacity;break;
        }
        assert(job.prepare(input,config,policies)==expected);
        assert(!job.external_effects_started()&&job.may_fallback()&&!job.requires_retention());
        assert(!job.allocation_transaction()&&job.geometry().positions.empty()&&job.packet().bytes.empty());
        assert_terminal_quiet(job,backend,gj::Progress::declined);
        assert(backend.events.empty());
        if(fault<2)assert(policies.projection_estimate.calls==0);
        if(fault==2)assert(job.pipeline_status()==gp::PipelineStatus::unsupported_vertex_format);
        if(fault==3)assert(job.pipeline_status()==gp::PipelineStatus::short_packed_vertices);
        assert(job.prepare(input,config,policies)==gj::PrepareStatus::already_started);++declines;
    }
    {
        gj::GeometryJob job;Backend backend;const auto input=input_example();
        assert_terminal_quiet(job,backend,gj::Progress::declined);
        ThrowingPolicies throwing;bool threw=false;
        try {job.prepare(input,config_example(),throwing);}catch(const std::runtime_error&){threw=true;}
        assert(threw&&throwing.position_conversion.calls==3&&job.may_fallback());
        assert(!job.external_effects_started()&&!job.allocation_transaction()&&job.geometry().positions.empty());
        assert_terminal_quiet(job,backend,gj::Progress::declined);
        FiniteHostPolicies policies;
        assert(job.prepare(input,config_example(),policies)==gj::PrepareStatus::ready);
        assert(backend.events.empty());++declines;
    }
    const auto capacity_cases=capacity_bound_cases();
    assert(capacity_cases==86016);
    std::printf("Typed geometry jobs: %u nonempty, %u empty, %u zero-packet allocation failures, "
        "%u pre-effect declines/exceptions and %u terminal fatal cases; owned delayed sources, "
        "pending receipts, fences/IRQ restoration and quiet terminal advances pass.\n",
        nonempty,empty,allocation_failures,declines,fatals);
    std::printf("Typed geometry capacity bound: %u phase/count/format/flag cases pass.\n",capacity_cases);
    return 0;
}
