// Project-written coordinator for typed geometry jobs. Platform input staging,
// shared slot leases and backend operations are explicit adapter obligations.
#ifndef WKC_NATIVE_GEOMETRY_JOB_H
#define WKC_NATIVE_GEOMETRY_JOB_H
#include "native_geometry_pipeline.h"
#include "native_geometry_allocation.h"
#include "native_geometry_export.h"
#include "native_geometry_commands.h"
#include <optional>

namespace wkc::geometry::job {
struct Config {
    std::uint32_t allocator_control=0,allocator_slot=0;
    std::uint32_t request_flags=0,command_destination=0;
    std::uint8_t packet_quadwords=0,dma_tag=0;
};
enum class PrepareStatus {ready,already_started,invalid_config,pipeline_declined,
    unsupported_vertex_tail,command_capacity};
enum class Progress {progressed,pending,complete,declined,failed};
enum class Failure {none,invalid_allocation,export_plan,command_plan,
    export_submission,command_submission,exception_after_effects};

// A prepared job owns all output buffers. Keep this nonmovable object AND its
// backend/slot lease alive through completion (or backend-specific fatal cleanup).
// In particular, accepted asynchronous writes may still reference its buffers.
// Never run a generated fallback after external_effects_started() becomes true.
// Backends obey AllocationTransaction and export/command submission contracts,
// plus these operations:
// optional<array<u8,4>> get_location0_fenced(advanced_effective_cursor, tag);
// bool complete_tag(tag);
//
// The location0 operation owns GETF4, IRQ preservation and its completed receipt;
// a pending repeat polls that outstanding command without re-enqueueing it.
// complete_tag must prove ALL preceding same-tag source consumption, not merely
// successful enqueue or command-packet visibility. The original synchronous
// runtime returns immediately; this ownership barrier is for an async backend,
// not a claim that the original job issues an extra tag wait.
class GeometryJob {
public:
    GeometryJob()=default;
    GeometryJob(const GeometryJob&)=delete;
    GeometryJob& operator=(const GeometryJob&)=delete;
    GeometryJob(GeometryJob&&)=delete;
    GeometryJob& operator=(GeometryJob&&)=delete;

    // No backend call or external write happens here. Exceptions leave the job
    // unprepared and may be handled before deciding whether to use a fallback.
    // The caller's input is not retained; all resulting data is owned by the job.
    template<class Policies>
    PrepareStatus prepare(const pipeline::GeometryInput& input,Config config,Policies& policies) {
        if(phase_!=Phase::unprepared)return PrepareStatus::already_started;
        if(config.dma_tag>=32||(config.command_destination&3u)) {
            phase_=Phase::declined;return PrepareStatus::invalid_config;
        }
        pipeline::GeometryOutput next;
        pipeline_status_=pipeline::run(input,next,policies);
        if(pipeline_status_!=pipeline::PipelineStatus::logical_buffers_ready) {
            phase_=Phase::declined;return PrepareStatus::pipeline_declined;
        }
        if(!next.payload.empty_geometry&&next.packed_vertices.size()!=next.payload.vertex_dma_bytes) {
            phase_=Phase::declined;return PrepareStatus::unsupported_vertex_tail;
        }
        commands::PublicationSpec spec{input.export_format,config.request_flags,
            std::uint32_t(next.filtered_indices.size()),config.command_destination,
            config.packet_quadwords,config.dma_tag,!next.payload.empty_geometry};
        // Check capacity before allocating externally. The two worst layouts
        // include an aligned index start and the smallest positive prefix. A
        // longer prefix only reduces remaining draw arguments; both include
        // submitted-vertex invalidation and a consumer release when nonempty.
        for(unsigned index:{0u,126u}) {
            commands::GeometryAllocation binding{index,0,1,0x10,0,true};
            commands::CommandPublication probe;
            const auto status=commands::build(spec,binding,probe);
            if(status!=commands::BuildStatus::packet_ready&&status!=commands::BuildStatus::no_publication) {
                phase_=Phase::declined;return PrepareStatus::command_capacity;
            }
        }
        config_=config;spec_=spec;geometry_=std::move(next);
        phase_=geometry_.payload.empty_geometry?Phase::command_prepare:Phase::allocation_begin;
        return PrepareStatus::ready;
    }

    template<class Backend>
    Progress advance(Backend& backend) {
        if(phase_==Phase::unprepared||phase_==Phase::declined)return Progress::declined;
        if(phase_==Phase::complete)return Progress::complete;
        if(phase_==Phase::failed)return Progress::failed;
        try {
            switch(phase_) {
            case Phase::allocation_begin:
                allocator_.emplace(allocation::AllocationRequest{config_.allocator_control,
                    geometry_.payload.reservation_bytes,config_.allocator_slot,config_.dma_tag},
                    allocation::AllocationRecord{});
                phase_=Phase::allocation_wait;
                break;
            case Phase::allocation_wait: {
                // Pending atomic writes can borrow allocator line storage;
                // the slot lease also remains owned before payload submission.
                external_started_=true;requires_retention_=true;
                const auto progress=allocator_->advance(backend);
                if(progress==allocation::AllocationProgress::pending)return Progress::pending;
                if(!allocator_->finished())break;
                if(!allocator_->success()) {
                    // The original publishes a zero command packet if geometry
                    // allocation fails. Preserve that branch; do not fall back.
                    spec_.export_geometry=false;phase_=Phase::command_prepare;break;
                }
                record_.bytes=allocator_->result();
                if(record_.location()>1||std::uint64_t(record_.effective_cursor())+
                    geometry_.payload.index_dma_bytes+geometry_.payload.vertex_dma_bytes>0x100000000ull)
                    return fail(Failure::invalid_allocation);
                phase_=Phase::exports_prepare;break;
            }
            case Phase::exports_prepare:
                if(payload_export::make_index_plan(spec_.filtered_index_count,geometry_.index_payload,
                    config_.dma_tag,record_,index_binding_,index_plan_)!=payload_export::Status::ready)
                    return fail(Failure::export_plan);
                if(payload_export::make_vertex_plan(std::uint32_t(geometry_.positions.size()),
                    unsigned(spec_.format),geometry_.packed_vertices,config_.dma_tag,
                    index_plan_.allocation_after,vertex_binding_,vertex_plan_)!=payload_export::Status::ready)
                    return fail(Failure::export_plan);
                binding_={payload_export::word(index_plan_.binding_prepared.bytes.data()+4),
                    payload_export::word(vertex_plan_.binding_prepared.bytes.data()+4),record_.location(),
                    payload_export::word(record_.bytes.data()+16),payload_export::word(record_.bytes.data()+12),true};
                phase_=Phase::command_prepare;break;
            case Phase::command_prepare: {
                const auto status=commands::build(spec_,binding_,packet_);
                if(status!=commands::BuildStatus::packet_ready&&status!=commands::BuildStatus::no_publication)
                    return fail(Failure::command_plan);
                phase_=spec_.export_geometry?Phase::index_submit:Phase::command_submit;break;
            }
            case Phase::index_submit:
                external_started_=true;requires_retention_=true;
                if(!payload_export::execute(index_plan_,record_,index_binding_,backend))
                    return fail(Failure::export_submission);
                phase_=Phase::vertex_submit;break;
            case Phase::vertex_submit:
                if(!payload_export::execute(vertex_plan_,record_,vertex_binding_,backend))
                    return fail(Failure::export_submission);
                phase_=record_.location()==0?Phase::location0_read:Phase::command_submit;break;
            case Phase::location0_read: {
                auto receipt=backend.get_location0_fenced(record_.effective_cursor(),config_.dma_tag);
                if(!receipt)return Progress::pending;
                location0_receipt_=*receipt;phase_=Phase::command_submit;break;
            }
            case Phase::command_submit:
                if(packet_.publish){external_started_=true;requires_retention_=true;}
                if(!commands::submit(packet_,backend))return fail(Failure::command_submission);
                phase_=requires_retention_?Phase::completion_wait:Phase::complete;
                if(phase_==Phase::complete)return Progress::complete;
                break;
            case Phase::completion_wait:
                if(!backend.complete_tag(config_.dma_tag))return Progress::pending;
                requires_retention_=false;phase_=Phase::complete;return Progress::complete;
            default:break;
            }
            return Progress::progressed;
        } catch(...) {
            // A shared reservation may already be published. Retain this job
            // and arrange platform cleanup; replaying the old job is unsafe.
            return fail(Failure::exception_after_effects);
        }
    }

    [[nodiscard]] bool complete()const noexcept{return phase_==Phase::complete;}
    [[nodiscard]] bool failed()const noexcept{return phase_==Phase::failed;}
    [[nodiscard]] bool may_fallback()const noexcept{return !external_started_&&!complete();}
    [[nodiscard]] bool external_effects_started()const noexcept{return external_started_;}
    [[nodiscard]] bool requires_retention()const noexcept{return requires_retention_;}
    [[nodiscard]] Failure failure()const noexcept{return failure_;}
    [[nodiscard]] pipeline::PipelineStatus pipeline_status()const noexcept{return pipeline_status_;}
    [[nodiscard]] const pipeline::GeometryOutput& geometry()const noexcept{return geometry_;}
    [[nodiscard]] const commands::CommandPublication& packet()const noexcept{return packet_;}
    [[nodiscard]] const payload_export::AllocationRecord& allocation_record()const noexcept{return record_;}
    [[nodiscard]] const allocation::AllocationTransaction* allocation_transaction()const noexcept{return allocator_?&*allocator_:nullptr;}
private:
    enum class Phase {unprepared,declined,allocation_begin,allocation_wait,exports_prepare,
        command_prepare,index_submit,vertex_submit,location0_read,command_submit,completion_wait,complete,failed};
    Progress fail(Failure reason)noexcept{failure_=reason;phase_=Phase::failed;return Progress::failed;}
    Phase phase_=Phase::unprepared;Failure failure_=Failure::none;
    pipeline::PipelineStatus pipeline_status_=pipeline::PipelineStatus::unsupported_count;
    Config config_{};commands::PublicationSpec spec_{};
    pipeline::GeometryOutput geometry_;
    std::optional<allocation::AllocationTransaction> allocator_;
    payload_export::AllocationRecord record_;
    payload_export::BindingRecord index_binding_,vertex_binding_;
    payload_export::ExportPlan index_plan_,vertex_plan_;
    commands::GeometryAllocation binding_{};commands::CommandPublication packet_;
    std::array<std::uint8_t,4> location0_receipt_{};
    bool external_started_=false,requires_retention_=false;
};
}
#endif
