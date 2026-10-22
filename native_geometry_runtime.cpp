// Opt-in synchronous geometry/visibility integration, using descriptor/source
// bytes before SPURS staging. The public algorithms receive owned typed data.
#include "native_geometry_runtime.h"
#include "native_event_flag.h"
#include "job_capture.h"
#include "native_geometry_input.h"
#include "native_geometry_job.h"
#include "native_estimate_interpolation.h"
#include "native_geometry_host_math.h"
#include "native_geometry_negative_product.h"
#include "native_visibility.h"
#include "native_occlusion.h"
#include "native_frustum.h"
#include "native_visibility_interval.h"
#include "spu_helpers.h"
#include "spu_coherency.h"
#include "spu_workload.h"
#include "native_geometry_mapping_cache.h"
#include "native_geometry_order.h"
#include "vm_mapping_epoch.h"
#include "ps3emu/wkc_ab.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <immintrin.h>
#endif

extern "C" {
extern std::uint8_t* vm_base;
extern void (*g_spu_put_hook)(std::uint32_t,std::uint32_t);
extern void (*g_spu_put_trace_hook)(std::uint32_t,std::uint32_t,int,std::uint32_t,const std::uint8_t*);
#if defined(_WIN32)
extern __declspec(thread) std::uint32_t g_spurs_job_mbox,g_spurs_job_mbox_intr,g_spurs_job_cmd;
extern __declspec(thread) int g_spurs_job_mbox_valid,g_spurs_job_user_events,g_spurs_job_running;
#else
extern __thread std::uint32_t g_spurs_job_mbox,g_spurs_job_mbox_intr,g_spurs_job_cmd;
extern __thread int g_spurs_job_mbox_valid,g_spurs_job_user_events,g_spurs_job_running;
#endif
}

namespace {
using namespace wkc::geometry;
namespace gi=wkc::geometry::input;
namespace gj=wkc::geometry::job;
namespace ga=wkc::geometry::allocation;
using Byte=std::uint8_t;
constexpr unsigned native_tag=20,native_slot=0;
constexpr int declined=SPU_NATIVE_JOB_DECLINED,completed=SPU_NATIVE_JOB_COMPLETED,fatal=SPU_NATIVE_JOB_FATAL;
std::mutex image19_gate;
std::recursive_mutex put_hook_gate;
thread_local unsigned image19_scope_depth=0;
thread_local wkc::geometry::mapping::ScopedCache image19_mapping_cache;
spu_native_job_hook_fn previous_native=nullptr;
spu_job_scope_begin_fn previous_begin=nullptr;
spu_job_scope_end_fn previous_end=nullptr;
void (*previous_put)(std::uint32_t,std::uint32_t)=nullptr;
void (*previous_trace)(std::uint32_t,std::uint32_t,int,std::uint32_t,const Byte*)=nullptr;
bool enabled=false,installed=false,diagnostics=false;
bool event_flag_jobs=true; // WKC_NATIVE_EVENT_FLAG=0 keeps image 21 compiled
void drain_pipeline_this_thread()noexcept;
struct Census {
    std::atomic<std::uint64_t> seen{0},admitted{0},complete{0},decline{0},fatal{0};
} census;
bool sample(std::uint64_t n) {return n<=8||(n&&(n&(n-1))==0);}
void log_result(const char* stage,unsigned reason,int result,std::uint32_t ea,
                unsigned vertices=0,unsigned indices=0) noexcept {
    if(!diagnostics&&result!=fatal)return;
    const auto count=result==completed?++census.complete:result==fatal?++census.fatal:++census.decline;
    if((diagnostics&&sample(count))||result==fatal)
        std::fprintf(stderr,"[native-geometry-job] %s reason=%u result=%d job=%08X vertices=%u indices=%u "
            "seen=%llu admitted=%llu completed=%llu declined=%llu fatal=%llu\n",stage,reason,result,ea,
            vertices,indices,(unsigned long long)census.seen.load(),(unsigned long long)census.admitted.load(),
            (unsigned long long)census.complete.load(),(unsigned long long)census.decline.load(),
            (unsigned long long)census.fatal.load());
}

// The guest producer owns submitted descriptor/input/output mappings through
// job return, as in the SDK's synchronous MFC path. VirtualQuery admits exact
// requested ranges and demand-commits their reserved 64KiB guest pages using
// the SDK's first-DMA paging policy. It is not a GPU cache or
// arbitrary concurrent-unmapping proof; this route performs full physical
// overwrites and preserves the same installed GCM notifications as SDK PUT.
bool vm_range(std::uint32_t ea,std::size_t bytes,bool writing) noexcept {
    if(!vm_base||!bytes||std::uint64_t(ea)+bytes>0x100000000ull)return false;
#ifdef _WIN32
    wkc::geometry::mapping::WindowsBackend mapping{vm_base};
    return image19_mapping_cache.check(vm_base,ea,bytes,writing,mapping);
#else
    (void)writing;return false; // The live mapping adapter is currently Windows-only.
#endif
}
void log_vm_region(const char* label,std::uint32_t ea,std::uint64_t bytes)noexcept {
    if(!diagnostics)return;
#ifdef _WIN32
    for(unsigned endpoint=0;endpoint<2;++endpoint) {
        const auto coordinate=std::uint64_t(ea)+(endpoint&&bytes?bytes-1:0);
        MEMORY_BASIC_INFORMATION info{};
        const auto queried=coordinate<0x100000000ull&&vm_base?
            VirtualQuery(vm_base+coordinate,&info,sizeof(info)):0;
        std::fprintf(stderr,"[native-geometry-job] %s endpoint=%u ea=%08llX bytes=%llu query=%llu "
            "region=%p size=%llu state=%08lX protect=%08lX type=%08lX allocation=%p\n",label,
            endpoint,(unsigned long long)coordinate,(unsigned long long)bytes,(unsigned long long)queried,
            info.BaseAddress,(unsigned long long)info.RegionSize,info.State,info.Protect,info.Type,info.AllocationBase);
    }
#else
    (void)label;(void)ea;(void)bytes;
#endif
}
bool acquire_bytes(std::uint32_t ea,std::size_t bytes,std::vector<Byte>& output) {
    if(!vm_range(ea,bytes,false))return false;
    // Reuse the caller's capacity; resize has the strong guarantee and every
    // byte is overwritten. The same coherency lock excludes atomic/MFC writers
    // during each owned copy. The submitted-job producer contract excludes
    // unsynchronized edits.
    output.resize(bytes);
    spu_lockline_lock();std::memcpy(output.data(),vm_base+ea,bytes);spu_lockline_unlock();
    return true;
}
struct OwnedSources {
    std::vector<Byte> descriptor;
    std::array<std::vector<Byte>,gi::dma_slot_count> payloads;
    gi::ConstructorPlan plan;
    bool acquire(std::uint32_t ea,std::uint32_t descriptor_size,unsigned& reason) {
        const auto size=descriptor_size?descriptor_size:256u;
        if(size<220||size>1024||!acquire_bytes(ea,size,descriptor)){reason=1;return false;}
        const auto* d=descriptor.data();
        if(gi::detail::be16(d+10)!=gi::dma_slot_count*8||gi::detail::be32(d)||
           gi::detail::be32(d+12)||gi::detail::be32(d+32)||gi::detail::be32(d+36)) {
            reason=2;return false;
        }
        for(unsigned slot=0;slot<gi::dma_slot_count;++slot) {
            const auto* entry=d+48+slot*8;
            const auto bytes=gi::detail::be32(entry)&0x7fffu,source=gi::detail::be32(entry+4);
            if(!bytes||!source)continue;
            if((source&15u)||!acquire_bytes(source,bytes,payloads[slot])){reason=3+slot;return false;}
        }
        gi::ConstructorContract contract;
        contract.root=gi::RootLayout::image19_custom_geometry_v1;contract.image_id=19;
        contract.dma_tag=native_tag;contract.allocator_slot=native_slot;
        // Current SDK initializer and reuse/dirty-clear paths establish these
        // zeros. The selected image19 root preserves save cells 101..103 before
        // skinning; no LS/register payload is read to manufacture this proof.
        contract.initialized_untouched_workspace=true;contract.preserved_zero_cell_mask=7;
        // The constructor resets preserved inputs; this selected root retains
        // the bounded initialized-zero, descriptor and staging palette rows.
        // The adapter derives their owned values, without reading a save frame.
        contract.selected_root_palette_rows=true;
        const auto status=gi::plan_spurs_constructor(descriptor,payloads[16],contract,plan);
        reason=100u+unsigned(status);return status==gi::ConstructorStatus::ready;
    }
    void reset() {descriptor.clear();for(auto& payload:payloads)payload.clear();plan={};}
    gi::StagedInputs spans()const {
        gi::StagedInputs staged;staged.descriptor=descriptor;
        for(unsigned slot=0;slot<gi::dma_slot_count;++slot)
            staged.slots[slot]={payloads[slot],plan.slot_begins[slot]};
        return staged;
    }
};

u128 pack(const Float4& value) {u128 result;std::memcpy(&result,value.data(),16);return result;}
Float4 unpack(u128 value) {Float4 result;std::memcpy(result.data(),&value,16);return result;}
// Admission checks host mode once for the whole job. These ordinary arithmetic
// and comparison operations use our typed C++ policy without SDK mode dispatch.
// Extended negative-product arithmetic uses our typed compatibility policy.
struct RuntimeMath:HostGeometryMath {
    static Float4 negative_multiply_add(const Float4& a,const Float4& b,const Float4& c){return negative_product::vector(a,b,c);}
};
struct PositionMath:RuntimeMath {
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        return OrderedHostPositionStreamMath<HostScalarMath>::multiply_add(a,b,c);
    }
};
struct NormalMath:RuntimeMath {
    static Float4 multiply(const Float4& a,const Float4& b) {
        return OrderedHostCofactorMath<HostScalarMath>::multiply(a,b);
    }
};
struct CofactorMath:NormalMath {
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        return OrderedHostCofactorMath<HostScalarMath>::multiply_subtract(a,b,c);
    }
};
struct ReciprocalSquareRoot {
    Float4 operator()(const Float4& value)const {
        return FusedEstimateInterpolation{}(value,unpack(spu_frsqest(pack(value))));
    }
};
struct Reciprocal {
    Float4 operator()(const Float4& value)const {
        return FusedEstimateInterpolation{}(value,unpack(spu_frest(pack(value))));
    }
};
struct PositionConversion {
    Float4 operator()(const std::array<std::int32_t,3>& value)const {
        u128 raw{};for(unsigned lane=0;lane<3;++lane)raw._s32[lane]=value[lane];
        auto result=unpack(spu_csflt(raw,143));result[3]=1.f;return result;
    }
};
template<unsigned Immediate> struct SignedConversion {
    std::array<std::int32_t,4> operator()(const Float4& value)const {
        const auto raw=spu_cflts(pack(value),Immediate);
        std::array<std::int32_t,4> result;std::memcpy(result.data(),&raw,16);return result;
    }
};
struct HostPolicies {
    using DecodeMath=RuntimeMath;
    using PositionSkinMath=PositionMath;
    using GeometrySkinMath=RuntimeMath;
    using NormalMath=::NormalMath;
    using CofactorMath=::CofactorMath;
    using ProjectionMath=RuntimeMath;
    using PackingMath=RuntimeMath;
    ReciprocalSquareRoot decode_reciprocal,normal_reciprocal;
    Reciprocal projection_estimate;
    PositionConversion position_conversion;
    SignedConversion<170> screen_conversion;
    SignedConversion<171> subpixel_conversion;
    SignedConversion<142> normal_conversion;
    SkinningStreamPolicy skinning_policy{};
    HostPolicies() {
        const u128 zero{};skinning_policy.convert_weights.zero=spu_cuflt_mode(&zero,155)._f32[0];
        // make_input declines bootstrap direction aliases before this policy
        // exists. Initial pending-group directions cannot feed admitted reads.
        skinning_policy.initial_reciprocal_lengths={};
    }
};

struct Range {std::uint32_t begin;std::uint64_t end;};
bool contains(Range range,std::uint32_t ea,std::size_t bytes) {
    return ea>=range.begin&&std::uint64_t(ea)+bytes<=range.end;
}
class GeometryJobBackend {
public:
    GeometryJobBackend(const gj::Config& config,const pipeline::PayloadLayout& payload)
        :config_(config),payload_(payload),base_(vm_base) {}
    ~GeometryJobBackend() {
        if(slot_token_)spu_native_release_reservation(slot_token_);
        if(global_token_)spu_native_release_reservation(global_token_);
    }
    bool preflight() {
        if(config_.command_destination) {
            const auto bytes=config_.packet_quadwords?unsigned(config_.packet_quadwords)*16u:4u;
            if(!vm_range(config_.command_destination,bytes,true))return false;
            publication_={config_.command_destination,std::uint64_t(config_.command_destination)+bytes};
        }
        if(payload_.empty_geometry)return true;
        if(config_.allocator_control&2u)
            return admit_geometry(config_.allocator_control&~3u,config_.allocator_control&1u);
        const auto slot=std::uint64_t(config_.allocator_control)+128u+(std::uint64_t(config_.allocator_slot)<<7);
        return slot<=0xffffffffu&&vm_range(config_.allocator_control&~127u,128,true)&&
            vm_range(std::uint32_t(slot)&~127u,128,true);
    }
    bool failed()const noexcept{return failed_;}
    unsigned failure_reason()const noexcept{return failure_reason_;}
    // Pipelined commits record every guest range this backend writes.
    void record_writes(std::vector<wkc::geometry::order::ByteRange>* writes)noexcept{writes_=writes;}
    std::optional<std::uint32_t> read_machine_status() {return irq_?1u:0u;}
    void set_interrupts(bool value) {irq_=value;}
    void fence() {architectural_fence();}
    std::optional<ga::AllocationLineReceipt> get_locked_line(std::uint32_t ea,ga::AllocationLineKind kind) {
        require(vm_base==base_&&vm_range(ea&~127u,128,true),10);
        auto& token=kind==ga::AllocationLineKind::slot?slot_token_:global_token_;
        if(token){spu_native_release_reservation(token);token=0;}
        ga::AllocationLine bytes{};std::uint64_t next=0;
        const auto status=spu_native_getllar(ea&~127u,bytes.data(),&next);
        require(status==4&&next,11);token=next;
        if(kind==ga::AllocationLineKind::slot)slot_line_=bytes;
        else {
            const auto begin=ga::allocation_word(bytes,12);
            const auto end=begin+payload_.reservation_bytes;
            const auto limit=ga::allocation_word(bytes,8),location=ga::allocation_word(bytes,16);
            if(begin&&std::uint64_t(begin)+payload_.reservation_bytes<=0xffffffffull&&end<=limit) {
                const bool admitted=admit_geometry(begin,location);
                if(!admitted&&enabled) {
                    std::fprintf(stderr,"[native-geometry-job] global-admit-failed control=%08X line=%08X "
                        "old=%08X candidate_end=%08X limit=%08X reservation=%u index_bytes=%u vertex_bytes=%u "
                        "location=%08X line32=",config_.allocator_control,ea&~127u,begin,end,limit,
                        payload_.reservation_bytes,payload_.index_dma_bytes,payload_.vertex_dma_bytes,location);
                    for(unsigned offset=0;offset<32;offset+=4)
                        std::fprintf(stderr,"%s%08X",offset?" ":"",ga::allocation_word(bytes,offset));
                    std::fputc('\n',stderr);
                }
                require(admitted,12);
            }
        }
        return ga::AllocationLineReceipt{bytes,std::uint32_t(status),token};
    }
    std::optional<std::uint32_t> put_conditional(std::uint32_t ea,const ga::AllocationLine& bytes,
                                               std::uint64_t token) {
        require(vm_base==base_&&vm_range(ea&~127u,128,true)&&token==global_token_,13);
        const auto status=spu_native_putllc(ea&~127u,bytes.data(),token);
        if(status==0)record(ea&~127u,128);
        // PUTLLC consumes its ticket for both success and conflict receipts.
        if(status>=0)global_token_=0;
        require(status==0||status==1,14);return std::uint32_t(status);
    }
    std::optional<std::uint32_t> put_unconditional(std::uint32_t ea,const ga::AllocationLine& bytes,
                                                 std::uint64_t token) {
        require(vm_base==base_&&vm_range(ea&~127u,128,true)&&token==slot_token_,15);
        const auto status=spu_native_putlluc(ea&~127u,bytes.data());record(ea&~127u,128);
        require(status==2,16);return 2u;
    }
    std::optional<ga::RingConsumerReceipt> get_consumer(std::uint32_t ea,unsigned tag) {
        const auto value=read4(ea,tag);
        const ga::RingState ring{ga::allocation_word(slot_line_,0),ga::allocation_word(slot_line_,4),
            ga::allocation_word(slot_line_,8),ga::allocation_word(slot_line_,12),
            ga::allocation_word(slot_line_,16),ga::allocation_word(slot_line_,20)};
        const auto decision=ga::decide_ring_reservation(ring,{payload_.reservation_bytes},{value});
        if(decision.kind==ga::RingDecisionKind::reserved)
            require(admit_geometry(decision.allocation.effective_address,decision.allocation.location),17);
        return ga::RingConsumerReceipt{value};
    }
    bool wait_decrementer_reads(unsigned count) {
        // The SDK decrementer reads host performance-counter state. Perform the
        // actual count of host timer reads; only the next GET yields a receipt.
#ifdef _WIN32
        LARGE_INTEGER tick{};for(unsigned i=0;i<count;++i)QueryPerformanceCounter(&tick);
#else
        for(unsigned i=0;i<count;++i)std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
        return true;
    }
    void architectural_fence() noexcept {std::atomic_thread_fence(std::memory_order_seq_cst);}
    bool interrupts_enabled()const noexcept{return irq_;}
    void set_interrupts_enabled(bool value)noexcept {irq_=value;}
    void put(std::span<const Byte> source,std::uint32_t ea,unsigned tag,Byte command)noexcept {
        if(failed_)return;
        if(command!=0x20||!contains(geometry_,ea,source.size())){mark_failure(20);return;}
        put_bytes(source,ea,tag,0x49e8);
    }
    std::optional<std::array<Byte,4>> get_location0_fenced(std::uint32_t ea,unsigned tag) {
        require(contains(geometry_,ea,4),21);architectural_fence();
        const bool enabled=irq_;irq_=false;
        const auto value=read4(ea,tag);irq_=enabled;
        return std::array<Byte,4>{Byte(value>>24),Byte(value>>16),Byte(value>>8),Byte(value)};
    }
    void put_fenced(std::span<const Byte> source,std::uint32_t ea,unsigned tag)noexcept {
        if(failed_)return;
        if(!contains(publication_,ea,source.size())){mark_failure(22);return;}
        architectural_fence();put_bytes(source,ea,tag,0x3040);
    }
    bool complete_tag(unsigned tag) {
        require(!failed_&&vm_base==base_&&tag<32,23);
        // Every accepted native atomic, GET and PUT above consumed its source
        // synchronously. This is source completion, never GPU completion.
        architectural_fence();return true;
    }
private:
    void mark_failure(unsigned reason)noexcept {failed_=true;failure_reason_=reason;}
    void record(std::uint32_t ea,std::size_t bytes) {if(writes_)writes_->push_back({ea,std::uint64_t(ea)+bytes});}
    void require(bool condition,unsigned reason) {
        if(!condition){mark_failure(reason);throw std::runtime_error("native geometry backend admission");}
    }
    bool admit_geometry(std::uint32_t ea,unsigned location) {
        if(location>1)return true; // Let the coordinator reject its typed record.
        const auto bytes=std::uint64_t(payload_.index_dma_bytes)+payload_.vertex_dma_bytes+(location==0?4u:0u);
        const char* failure=(ea&3u)?"unaligned":!bytes?"empty":
            std::uint64_t(ea)+bytes>0x100000000ull?"effective-wrap":
            !vm_range(ea,std::size_t(bytes),true)?"uncommitted-or-protected":nullptr;
        if(failure) {
            if(diagnostics)std::fprintf(stderr,"[native-geometry-job] geometry-admit-failed reason=%s ea=%08X "
                "bytes=%llu location=%u\n",failure,ea,(unsigned long long)bytes,location);
            log_vm_region("geometry-admit-region",ea,bytes);return false;
        }
        geometry_={ea,std::uint64_t(ea)+bytes};return true;
    }
    std::uint32_t read4(std::uint32_t ea,unsigned tag) {
        require(vm_base==base_&&tag<32&&!(ea&3u)&&vm_range(ea,4,false),24);
        std::array<Byte,4> bytes{};
        spu_lockline_lock();std::memcpy(bytes.data(),base_+ea,4);spu_lockline_unlock();
        return gi::detail::be32(bytes.data());
    }
    void put_bytes(std::span<const Byte> source,std::uint32_t ea,unsigned tag,std::uint32_t pc)noexcept {
        const auto size=source.size();
        const auto host=reinterpret_cast<std::uintptr_t>(source.data());
        const auto vm=reinterpret_cast<std::uintptr_t>(base_);
        if(vm_base!=base_||tag>=32||(ea&3u)||source.empty()||
           (size!=4&&(size<16||size>16384||(size&15u)))||!vm_range(ea,size,true)||
           (std::uint64_t(host)<std::uint64_t(vm)+0x100000000ull&&std::uint64_t(host)+size>vm)) {
            mark_failure(25);return;
        }
        spu_lockline_lock();std::memcpy(base_+ea,source.data(),size);record(ea,size);
        const auto last=std::uint32_t(std::uint64_t(ea)+size-1)&~127u;
        for(auto line=ea&~127u;;line+=128) {
            if(spu_coh_is_reserved(line))spu_coh_notify_write(line);
            if(line==last)break;
        }
        spu_lockline_unlock();
        // Same physical-copy/coherence -> installed GCM PUT hook -> trace order
        // as current synchronous MFC. Hook wrappers serialize mixed issuers.
        if(g_spu_put_hook)g_spu_put_hook(ea,std::uint32_t(size));
        if(g_spu_put_trace_hook)g_spu_put_trace_hook(ea,std::uint32_t(size),19,pc,source.data());
    }
    gj::Config config_;
    pipeline::PayloadLayout payload_;
    Byte* base_;
    ga::AllocationLine slot_line_{};
    Range geometry_{0,0},publication_{0,0};
    std::uint64_t slot_token_=0,global_token_=0;
    // The intercepted root enters with IRQ disabled. Native callbacks model
    // that standalone state; no fabricated context or emulated status is used.
    bool irq_=false,failed_=false;
    unsigned failure_reason_=0;
    std::vector<wkc::geometry::order::ByteRange>* writes_=nullptr;
};

void put_hook(std::uint32_t ea,std::uint32_t bytes) {
    std::lock_guard<std::recursive_mutex> lock(put_hook_gate);
    if(previous_put)previous_put(ea,bytes);
}
void trace_hook(std::uint32_t ea,std::uint32_t bytes,int image,std::uint32_t pc,const Byte* source) {
    wkc::job_capture::put(ea,bytes,image,pc,source);
    std::lock_guard<std::recursive_mutex> lock(put_hook_gate);
    if(previous_trace)previous_trace(ea,bytes,image,pc,source);
}
void refresh_put_hooks() {
    // Guest cellGcmInit can install its PUT observer after main's initial
    // registration. Refresh at every dispatch boundary, before either native
    // execution or fallback. Preserve its latest callback and never chain self.
    std::lock_guard<std::recursive_mutex> lock(put_hook_gate);
    bool rebound=false;
    if(g_spu_put_hook!=put_hook){previous_put=g_spu_put_hook;g_spu_put_hook=put_hook;rebound=true;}
    if(g_spu_put_trace_hook!=trace_hook){previous_trace=g_spu_put_trace_hook;g_spu_put_trace_hook=trace_hook;rebound=true;}
    if(rebound&&diagnostics)std::fprintf(stderr,"[native-geometry-job] rebound chained PUT observers at dispatch\n");
}
struct ScopeLease {void* previous=nullptr;bool active=false;int image=0;};
thread_local ScopeLease current_scope;
[[noreturn]] void scope_abort()noexcept {
    std::fputs("[native-geometry-job] fatal scope callback failure; refusing C-boundary unwind\n",stderr);
    std::abort();
}
void* scope_begin(int image,std::uint32_t ea,std::uint32_t bytes)noexcept {
    if(image!=19)drain_pipeline_this_thread();
    wkc::job_capture::begin(image,ea,bytes);
    try {
        refresh_put_hooks();
        if(image!=19&&image!=23)return previous_begin?previous_begin(image,ea,bytes):nullptr;
        // The SDK dispatches one synchronous job at a time on each worker.
        // Reuse its thread-local scope record without allocation or C unwind.
        if(current_scope.active||image19_scope_depth)scope_abort();
        // Only image19 owns a shared slot0 allocator. Visibility requests own
        // their inputs/output and require no process-wide compute lock.
        if(image==19)image19_gate.lock();current_scope.active=true;current_scope.image=image;++image19_scope_depth;
        // The producer pins submitted mappings until this job returns. Retain
        // positive checks across jobs only within the runtime mapping epoch;
        // protection changes and VM teardown/recreation invalidate this cache.
        image19_mapping_cache.begin_lifetime(vm_base,vm_mapping_epoch_read());
        current_scope.previous=previous_begin?previous_begin(image,ea,bytes):nullptr;
        return &current_scope;
    } catch(...) {scope_abort();}
}
void scope_end(int image,std::uint32_t ea,std::uint32_t bytes,void* token)noexcept {
    wkc::job_capture::end(image);
    try {
        if(image!=19&&image!=23){if(previous_end)previous_end(image,ea,bytes,token);return;}
        if(token!=&current_scope||!current_scope.active||current_scope.image!=image||image19_scope_depth!=1)scope_abort();
        if(previous_end)previous_end(image,ea,bytes,current_scope.previous);
        image19_mapping_cache.suspend();current_scope={};--image19_scope_depth;if(image==19)image19_gate.unlock();
    } catch(...) {scope_abort();}
}
// Image23 selection modes own the entire finite query, including ordered record and
// header publication. They do not construct a guest context or local store.
struct VisibilityMath {
    static Float4 scalar(float a){return {a,a,a,a};}
    static float add(float a,float b){return HostGeometryMath::add(scalar(a),scalar(b))[0];}
    static float subtract(float a,float b){return HostGeometryMath::subtract(scalar(a),scalar(b))[0];}
    static float multiply(float a,float b){return HostGeometryMath::multiply(scalar(a),scalar(b))[0];}
    static float fused_multiply_add(float a,float b,float c){return HostGeometryMath::multiply_add(scalar(a),scalar(b),scalar(c))[0];}
    static float negative_multiply_subtract(float a,float b,float c){return negative_product::scalar(a,b,c);}
    static float interpolated_reciprocal_sqrt(float a){const auto value=pack(scalar(a));return unpack(spu_fi(value,spu_frsqest(value)))[0];}
    static bool greater(float a,float b){return a>b;}
};
struct VisibilityRange {std::uint32_t ea;std::size_t bytes;};
bool overlaps(VisibilityRange a,VisibilityRange b) {
    return a.bytes&&b.bytes&&std::uint64_t(a.ea)<std::uint64_t(b.ea)+b.bytes&&
        std::uint64_t(b.ea)<std::uint64_t(a.ea)+a.bytes;
}
void visibility_log(unsigned mode,int result,std::size_t records=0)noexcept {
    if(!diagnostics)return;
    static std::atomic<std::uint64_t> complete{0},decline{0};
    const auto count=result==completed?++complete:++decline;
    if(sample(count))std::fprintf(stderr,"[native-visibility-job] mode=%u result=%d records=%zu completed=%llu declined=%llu\n",
        mode,result,records,(unsigned long long)complete.load(),(unsigned long long)decline.load());
}
int execute_visibility(std::uint32_t ea,std::uint32_t descriptor_size) {
    namespace v=wkc::visibility;namespace oc=v::occlusion;using Kernel=oc::Kernel<VisibilityMath>;
    bool external=false;unsigned mode=0;
    try {
        if(!image19_scope_depth)return declined;
        std::vector<Byte> descriptor,input,query,records;
        const auto size=descriptor_size?descriptor_size:256u;
        if(size!=256||!acquire_bytes(ea,size,descriptor))return declined;
        const auto* d=descriptor.data();mode=gi::detail::be32(d+60);
        if(mode<1||mode>4){visibility_log(mode,declined);return declined;}
        // One staged input, exact registered image23 binary, no cache/high-EA
        // or unclassified descriptor lanes. Other constructors keep fallback.
        if(gi::detail::be32(d)||gi::detail::be16(d+8)*16u!=11856u||gi::detail::be16(d+10)!=8||
           gi::detail::be32(d+12)||gi::detail::be32(d+32)||gi::detail::be32(d+36)||
           gi::detail::be32(d+56)||gi::detail::be32(d+64)||gi::detail::be32(d+72)||
           gi::detail::be32(d+80)||gi::detail::be32(d+88)||gi::detail::be32(d+92))return declined;
        const auto input_bytes=gi::detail::be32(d+48)&0x7fffu,input_ea=gi::detail::be32(d+52);
        const auto query_ea=gi::detail::be32(d+76),output_ea=gi::detail::be32(d+84);
        const unsigned minimum_input[]{0,128,80,16,144};
        if(input_bytes<minimum_input[mode]||!input_ea||(input_ea&15u)||(query_ea&15u)||
           output_ea<0x10000u||(output_ea&15u)||
           !acquire_bytes(input_ea,input_bytes,input)||!acquire_bytes(query_ea,16,query))return declined;
        const auto align=[](std::uint64_t n){return(n+1023u)&~std::uint64_t(1023u);};
        const auto stack=gi::detail::be16(d+28)?std::uint64_t(gi::detail::be16(d+28))*16u:8192u;
        const auto scratch=align(11856)+align(gi::detail::be32(d+20))+align(gi::detail::be32(d+24));
        const auto end=scratch+align(std::uint64_t(gi::detail::be16(d+30))*16u+stack);
        const auto entry_sp=(end-32u)&~std::uint64_t(15u);
        const unsigned frame_bytes[]{0,0x1470,0x33e0,0x1350,0x1430};
        const auto frame=frame_bytes[mode];
        // The SDK initializer/reuse dirty-clear contract establishes zero
        // workspace. The selected roots touch only their count halfword before
        // emitting this header; their other spills/DMAs do not overlap it.
        if(gi::detail::be32(d+20)<((input_bytes+15u)&~15u)||end>0x3fbc0u||
           end<32u+frame||entry_sp-frame<scratch)return declined;
        const auto count=gi::detail::be16(query.data());
        if(std::uint64_t(query_ea)+16u+std::size_t(count)*16u>0x100000000ull)return declined;
        if(count&&!acquire_bytes(query_ea+16u,std::size_t(count)*16u,records))return declined;
        const VisibilityRange query_range{query_ea,16u+std::size_t(count)*16u};
        const VisibilityRange maximum_output{output_ea,16u+std::size_t(count)*16u};
        // Snapshot semantics equal the original streamed reads only for
        // disjoint output or backward/in-place compaction of query records.
        if(std::uint64_t(query_ea)+query_range.bytes>0x100000000ull||
           std::uint64_t(output_ea)+maximum_output.bytes>0xffffffffull||
           (overlaps(maximum_output,query_range)&&output_ea>query_ea)||
           overlaps(maximum_output,{ea,size})||overlaps(maximum_output,{input_ea,input_bytes}))return declined;
        std::vector<v::Write> writes;
        if(mode==3) {
            v::Request request;request.input_mask=gi::detail::be16(input.data());request.query_count=count;
            request.records=records.data();request.record_bytes=records.size();request.output_ea=output_ea;
            request.raw_threshold_units=gi::detail::be32(d+68);
            request.header_origin=v::HeaderOrigin::constructor_zeroed_untouched_workspace;
            v::Plan plan;if(v::plan(request,plan)!=v::Status::complete)return declined;writes=std::move(plan.writes);
        } else if(mode==2) {
            if(g_spu_float_mode!=SPU_FLOAT_HOST)return declined;
            const auto value=[](const Byte* p){return std::bit_cast<float>(gi::detail::be32(p));};
            oc::QueryColumns columns;
            for(unsigned column=0;column<4;++column)for(unsigned lane=0;lane<3;++lane)columns[column][lane]=value(input.data()+column*16+lane*4);
            const auto n=gi::detail::be32(input.data()+64),quad_ea=gi::detail::be32(input.data()+68);
            // Zero submits a malformed zero-length GET in the compiled route;
            // do not silently reinterpret that constructor as a native empty set.
            if(!n||n>128||(quad_ea&15u)||overlaps(maximum_output,{quad_ea,std::size_t(n)*64u}))return declined;
            std::vector<Byte> quad_bytes;if(!acquire_bytes(quad_ea,std::size_t(n)*64u,quad_bytes))return declined;
            std::vector<oc::OccluderCorners> quads(n);
            for(unsigned q=0;q<n;++q)for(unsigned corner=0;corner<4;++corner)for(unsigned lane=0;lane<3;++lane)
                quads[q][corner][lane]=value(quad_bytes.data()+q*64+corner*16+lane*4);
            oc::PreparedScene scene;if(!Kernel::prepare_scene(columns,quads,scene))return declined;
            unsigned accepted=0;
            for(unsigned i=0;i<count;++i) {
                const auto* record=records.data()+i*16;const auto bound_ea=gi::detail::be32(record+8),matrix_ea=gi::detail::be32(record+12);
                if((bound_ea&15u)||(matrix_ea&15u)||overlaps(maximum_output,{bound_ea,32})||overlaps(maximum_output,{matrix_ea,48}))return declined;
                std::vector<Byte> bounds_bytes,matrix_bytes;
                if(!acquire_bytes(bound_ea,32,bounds_bytes)||!acquire_bytes(matrix_ea,48,matrix_bytes))return declined;
                oc::Bounds bounds;oc::AffineRows matrix;
                for(unsigned lane=0;lane<3;++lane){bounds.center[lane]=value(bounds_bytes.data()+lane*4);bounds.half_size[lane]=value(bounds_bytes.data()+16+lane*4);}
                for(unsigned row=0;row<3;++row)for(unsigned lane=0;lane<4;++lane)matrix[row][lane]=value(matrix_bytes.data()+row*16+lane*4);
                if(Kernel::visible(scene,Kernel::prepare_box(columns,matrix,bounds))) {
                    v::Write write;write.ea=output_ea+(++accepted)*16u;std::memcpy(write.bytes.data(),record,16);writes.push_back(write);
                }
            }
            if(count&&output_ea+(accepted+1u)*16u<=std::uint32_t(gi::detail::be32(d+68)<<4))return declined;
            v::Write header;header.ea=output_ea;header.bytes[0]=Byte(accepted>>8);header.bytes[1]=Byte(accepted);writes.push_back(header);
        } else {
            if(g_spu_float_mode!=SPU_FLOAT_HOST)return declined;
            namespace f=v::frustum;namespace in=v::interval;
            using Frustum=f::Kernel<VisibilityMath>;using Interval=in::Kernel<VisibilityMath>;
            const auto value=[](const Byte* p){return std::bit_cast<float>(gi::detail::be32(p));};
            f::Input frustum;in::Input interval;
            for(unsigned vector=0;vector<4;++vector)for(unsigned lane=0;lane<4;++lane) {
                const auto a=value(input.data()+vector*16+lane*4);
                const auto b=value(input.data()+64+vector*16+lane*4);
                frustum.plane_rows[vector][lane]=a;interval.axis_columns[vector][lane]=a;
                frustum.transform_columns[vector][lane]=b;interval.transform_columns[vector][lane]=b;
            }
            if(mode==4){interval.lower=value(input.data()+128);interval.upper=value(input.data()+132);}
            // Each request prepares fixed planes/axis once, then transforms its
            // owned objects. Preserve four-component arithmetic and strict
            // interval comparisons, including their nonfinite behavior.
            f::PreparedScene planes;in::PreparedScene axis;
            if(mode==1)planes=Frustum::prepare_scene(frustum);
            else axis=Interval::prepare_scene(interval);
            unsigned accepted=0;
            for(unsigned i=0;i<count;++i) {
                const auto* record=records.data()+i*16;
                const auto bound_ea=gi::detail::be32(record+8),matrix_ea=gi::detail::be32(record+12);
                if((bound_ea&15u)||(matrix_ea&15u)||overlaps(maximum_output,{bound_ea,32})||
                   overlaps(maximum_output,{matrix_ea,48}))return declined;
                std::vector<Byte> bounds_bytes,matrix_bytes;
                if(!acquire_bytes(bound_ea,32,bounds_bytes)||!acquire_bytes(matrix_ea,48,matrix_bytes))return declined;
                f::Bounds bounds;f::AffineRows matrix;
                for(unsigned lane=0;lane<4;++lane) {
                    bounds.center[lane]=value(bounds_bytes.data()+lane*4);
                    bounds.half_size[lane]=value(bounds_bytes.data()+16+lane*4);
                }
                for(unsigned row=0;row<3;++row)for(unsigned lane=0;lane<4;++lane)
                    matrix[row][lane]=value(matrix_bytes.data()+row*16+lane*4);
                const bool keep=mode==1?Frustum::visible(planes,Frustum::prepare_box(planes,bounds,matrix)):
                    Interval::visible(axis,bounds,matrix);
                if(keep) {
                    v::Write write;write.ea=output_ea+(++accepted)*16u;
                    std::memcpy(write.bytes.data(),record,16);writes.push_back(write);
                }
            }
            if(count&&output_ea+(accepted+1u)*16u<=std::uint32_t(gi::detail::be32(d+68)<<4))return declined;
            v::Write header;header.ea=output_ea;header.bytes[0]=Byte(accepted>>8);header.bytes[1]=Byte(accepted);writes.push_back(header);
        }
        // Resolve all mappings before the first external effect. Admission may
        // decline; publication never falls back after a partial write.
        for(const auto& write:writes)if(!vm_range(write.ea,16,true))return declined;
        for(const auto& write:writes) {
            external=true;spu_lockline_lock();std::memcpy(vm_base+write.ea,write.bytes.data(),16);
            const auto line=write.ea&~127u;if(spu_coh_is_reserved(line))spu_coh_notify_write(line);
            spu_lockline_unlock();
            if(g_spu_put_hook)g_spu_put_hook(write.ea,16);
            const unsigned logical_pc[]{0,0x05a0,0x27f8,0x16b8,0x0290};
            if(g_spu_put_trace_hook)g_spu_put_trace_hook(write.ea,16,23,logical_pc[mode],write.bytes.data());
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
        // Preserve the manager's per-thread completion state. These modes send
        // no user event or mailbox; the chain resumes synchronous continuation.
        g_spurs_job_mbox=g_spurs_job_mbox_intr=0;g_spurs_job_mbox_valid=0;
        g_spurs_job_user_events=0;g_spurs_job_running=0;
        g_spurs_job_cmd=gi::detail::be32(vm_base+query_ea)>>16;
        visibility_log(mode,completed,writes.size()-1);return completed;
    } catch(...) {visibility_log(mode,external?fatal:declined);return external?fatal:declined;}
}

// Pipelined image19 jobs.
//
// The chain thread copies each job's sources and adapts its input, then a
// worker runs the pure-compute prepare while the chain moves on. Commits
// (allocator lines, ring wait, exports, publication) stay on the chain thread
// in job order: inside the next image19 scope once more than `window` jobs
// wait, or at a drain point. Drain points are every other image's job
// (scope_begin), every non-job chain command and walker exit (SDK patch 0128).
// Each commit records the guest ranges it wrote; a job whose sources were
// written by a commit made after its copy is copied and prepared again.
// A job that cannot complete natively before any external effect runs the
// compiled job at its own commit turn, as the serial path would have.
// WKC_GEOM_PIPELINE=<workers> overrides the worker count (0 = serial);
// WKC_GEOM_PIPELINE_VERIFY=1 re-prepares every 64th job serially and aborts
// on any difference in the logical outputs.
namespace go=wkc::geometry::order;
// WKC_GEOM_WINDOW=<n> sets how many queued jobs the chain keeps ahead of its
// commits (default 8). WKC_AB_GEOM_WINDOW=<n> (private A/B) uses n only while
// the WKC_AB_GATE file is absent.
std::size_t pipeline_window_now() {
    static const std::size_t base=[]{const char* e=std::getenv("WKC_GEOM_WINDOW");
        const unsigned long n=e?std::strtoul(e,nullptr,10):8ul;return std::size_t(n?n:8ul);}();
    static const std::size_t ab=[]{const char* e=std::getenv("WKC_AB_GEOM_WINDOW");
        return std::size_t(e?std::strtoul(e,nullptr,10):0ul);}();
    return ab&&!g_wkc_ab_off?ab:base;
}
std::atomic<bool> pipeline_on{false};
bool pipeline_verify=false;
// WKC_GEOM_STATS=1: every 4096 commits on a chain thread, print where its
// time went (copy, waiting for prepare, commit) and why the front job was not
// ready (still queued, or still being prepared).
bool pipeline_stats=false;
std::int64_t stats_now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct ChainStats {
    std::uint64_t jobs=0,drained=0,waited=0,waited_queued=0;
    std::int64_t copy=0,wait=0,drain_wait=0,commit=0,queue=0,prepare=0;
};
thread_local ChainStats chain_stats;
thread_local bool chain_draining=false;
thread_local bool force_compiled=false;
go::WriteLog commit_log;
struct PipelineCensus {std::atomic<std::uint64_t> queued{0},committed{0},recopied{0},fallbacks{0},verified{0};} pipeline_census;
// WKC_AB_GEOM_CHAIN (private A/B): while the WKC_AB_GATE file exists, every
// job is allocated afresh and idle prepare workers sleep at once.
bool chain_ab_old() {
    static const bool ab=std::getenv("WKC_AB_GEOM_CHAIN")!=nullptr;
    return ab&&g_wkc_ab_off;
}
struct Pending {
    OwnedSources sources;
    gi::AdaptedJob adapted,spare_input;
    gj::Config config{};
    HostPolicies policies;
    std::optional<gj::GeometryJob> job{std::in_place};
    // The last job's output buffers. The worker that prepares this job
    // returns them to its own spare, so prepare reuses their capacity.
    std::optional<pipeline::GeometryOutput> spare_output;
    std::vector<go::ByteRange> reads;
    std::uint64_t copied_after=0;
    std::uint32_t ea=0,descriptor_size=0,binary_ea=0,binary_size=0;
    unsigned mxcsr=0,index=0;
    std::atomic<int> state{0}; // 0 queued, 1 prepared, 2 declined, 3 threw
    std::int64_t submitted_ns=0;std::atomic<std::int64_t> started_ns{0},ended_ns{0}; // WKC_GEOM_STATS
    // Ready for another job, keeping every buffer's capacity.
    void reuse() {
        sources.reset();job.emplace();policies=HostPolicies{};reads.clear();config={};
        copied_after=0;ea=descriptor_size=binary_ea=binary_size=0;mxcsr=index=0;
        state.store(0,std::memory_order_relaxed);submitted_ns=0;
        started_ns.store(0,std::memory_order_relaxed);ended_ns.store(0,std::memory_order_relaxed);
    }
};
// Finished jobs on this chain thread, ready for reuse.
thread_local std::vector<std::unique_ptr<Pending>> pending_pool;
std::unique_ptr<Pending> new_pending() {
    if(pending_pool.empty()||chain_ab_old())return std::make_unique<Pending>();
    auto p=std::move(pending_pool.back());pending_pool.pop_back();p->reuse();return p;
}
void recycle_pending(std::unique_ptr<Pending>& p)noexcept {
    if(!p||chain_ab_old()||pending_pool.size()>=32)return;
    try {
        if(p->job&&!p->spare_output)p->spare_output.emplace(p->job->release_geometry());
        pending_pool.push_back(std::move(p));
    } catch(...) {}
}
int prepare_state(Pending& job)noexcept {
    try {
        if(job.spare_output){pipeline::recycle_output(std::move(*job.spare_output));job.spare_output.reset();}
        return job.job->prepare(job.adapted.geometry,job.config,job.policies)==gj::PrepareStatus::ready?1:2;
    }
    catch(...) {return 3;}
}
class PreparePool {
public:
    void start(unsigned count) {
        // WKC_GEOM_AB_BASE=<n> (private A/B): while the WKC_AB_GATE file
        // exists, only the first n workers take jobs.
        if(const char* base=std::getenv("WKC_GEOM_AB_BASE"))ab_base_=unsigned(std::strtoul(base,nullptr,10));
        // WKC_GEOM_SPIN_US=<n>: how long an idle worker polls for the next
        // job before it sleeps (default 100; 0 sleeps at once). Waking a
        // sleeping worker costs more than a whole copy on the chain thread.
        if(const char* spin=std::getenv("WKC_GEOM_SPIN_US"))spin_us_=unsigned(std::strtoul(spin,nullptr,10));
        for(unsigned i=0;i<count;++i)threads_.emplace_back([this,i]{run(i);});
    }
    void submit(Pending* job) {
        std::unique_lock<std::mutex> lock(mutex_);
        if(stopping_||threads_.empty()) {
            lock.unlock();
            job->state.store(prepare_state(*job),std::memory_order_release); // stopped: prepare here
            return;
        }
        queue_.push_back(job);queued_.store(true,std::memory_order_release);
        // A polling worker takes the job without a wakeup. A benched worker
        // may take the one wakeup: wake them all then.
        const bool wake=sleepers_&&(chain_ab_old()||queue_.size()>unsigned(pollers_.load(std::memory_order_acquire)));
        lock.unlock();
        if(ab_base_!=~0u)ready_.notify_all(); else if(wake)ready_.notify_one();
    }
    void wait(Pending* job) {
        if(chain_ab_old()) {
            for(int spin=0;spin<4000;++spin){if(job->state.load(std::memory_order_acquire))return;_mm_pause();}
        } else {
            // The chain has nothing else to do; a sleep would add a wakeup.
            const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
            for(unsigned spin=1;;++spin) {
                if(job->state.load(std::memory_order_acquire))return;
                _mm_pause();
                if(!(spin&63u)&&std::chrono::steady_clock::now()>=until)break;
            }
        }
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock,[job]{return job->state.load(std::memory_order_acquire)!=0;});
    }
    // Stop admission, let workers finish everything queued, then join them.
    void stop() {
        {std::lock_guard<std::mutex> lock(mutex_);if(stopping_)return;stopping_=true;}
        ready_.notify_all();
        for(auto& thread:threads_)if(thread.joinable())thread.join();
    }
private:
    bool benched(unsigned id)const {return id>=ab_base_&&g_wkc_ab_off;}
    // Poll for queued work until it appears or spin_us_ passes.
    void poll() {
        pollers_.fetch_add(1,std::memory_order_acq_rel);
        const auto until=std::chrono::steady_clock::now()+std::chrono::microseconds(spin_us_);
        for(unsigned spin=1;!queued_.load(std::memory_order_acquire);++spin) {
            _mm_pause();
            if(!(spin&63u)&&std::chrono::steady_clock::now()>=until)break;
        }
        pollers_.fetch_sub(1,std::memory_order_acq_rel);
    }
    void run(unsigned id) {
        for(;;) {
            if(spin_us_&&ab_base_==~0u&&!chain_ab_old())poll();
            Pending* job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ++sleepers_;
                if(ab_base_!=~0u) {
                    while(!ready_.wait_for(lock,std::chrono::milliseconds(2),
                        [this,id]{return stopping_||(!queue_.empty()&&!benched(id));})) {}
                } else ready_.wait(lock,[this]{return stopping_||!queue_.empty();});
                --sleepers_;
                if(queue_.empty())return;
                job=queue_.front();queue_.pop_front();queued_.store(!queue_.empty(),std::memory_order_release);
            }
            // The chain thread's FP environment, including rounding and FTZ/DAZ.
            const unsigned saved=_mm_getcsr();_mm_setcsr(job->mxcsr);
            if(pipeline_stats)job->started_ns.store(stats_now(),std::memory_order_relaxed);
            const int state=prepare_state(*job);
            if(pipeline_stats)job->ended_ns.store(stats_now(),std::memory_order_relaxed);
            _mm_setcsr(saved);
            {std::lock_guard<std::mutex> lock(mutex_);job->state.store(state,std::memory_order_release);}
            done_.notify_all();
        }
    }
    std::mutex mutex_;std::condition_variable ready_,done_;std::deque<Pending*> queue_;
    std::vector<std::thread> threads_;bool stopping_=false;unsigned ab_base_=~0u,spin_us_=100,sleepers_=0;
    std::atomic<bool> queued_{false};std::atomic<int> pollers_{0};
};
// Never destroyed: chain threads may still use it while static destructors run.
PreparePool& prepare_pool=*new PreparePool;
void source_ranges(Pending& p) {
    p.reads.clear();
    p.reads.push_back({p.ea,std::uint64_t(p.ea)+(p.descriptor_size?p.descriptor_size:256u)});
    const auto* d=p.sources.descriptor.data();
    for(unsigned slot=0;slot<gi::dma_slot_count;++slot) {
        const auto* entry=d+48+slot*8;
        const auto bytes=gi::detail::be32(entry)&0x7fffu,source=gi::detail::be32(entry+4);
        if(bytes&&source)p.reads.push_back({source,std::uint64_t(source)+bytes});
    }
}
// Copy sources and adapt input. False leaves the decision to the serial path.
bool copy_and_adapt(Pending& p) {
    p.copied_after=commit_log.sequence();
    unsigned reason=0;
    if(!p.sources.acquire(p.ea,p.descriptor_size,reason))return false;
    if(gi::make_input(p.sources.spans(),p.sources.plan.platform,
        [](float a,float b){return RuntimeMath::greater(a,b);},p.adapted,
        chain_ab_old()?nullptr:&p.spare_input)!=gi::Status::ready)return false;
    const auto& m=p.adapted.metadata;
    p.config={m.allocator_control,m.allocator_slot,m.request_flags,m.publication_destination,m.packet_quadwords,m.dma_tag};
    source_ranges(p);return true;
}
[[noreturn]] void pipeline_abort(const char* why,std::uint32_t ea)noexcept {
    std::fprintf(stderr,"[native-geometry-job] pipeline: %s job=%08X\n",why,ea);
    std::fflush(stderr);std::abort();
}
bool same_output(const pipeline::GeometryOutput& a,const pipeline::GeometryOutput& b) {
    auto eq=[](const auto& x,const auto& y){return x.size()==y.size()&&(x.empty()||!std::memcmp(x.data(),y.data(),x.size()*sizeof(x[0])));};
    return eq(a.positions,b.positions)&&eq(a.directions[0],b.directions[0])&&eq(a.directions[1],b.directions[1])&&
        eq(a.prepared_palette,b.prepared_palette)&&eq(a.projected,b.projected)&&eq(a.decoded_indices,b.decoded_indices)&&
        eq(a.filtered_indices,b.filtered_indices)&&eq(a.triangle_decisions,b.triangle_decisions)&&
        eq(a.index_payload,b.index_payload)&&eq(a.packed_vertices,b.packed_vertices)&&
        a.payload.logical_index_bytes==b.payload.logical_index_bytes&&a.payload.index_dma_bytes==b.payload.index_dma_bytes&&
        a.payload.logical_vertex_bytes==b.payload.logical_vertex_bytes&&a.payload.vertex_dma_bytes==b.payload.vertex_dma_bytes&&
        a.payload.reservation_bytes==b.payload.reservation_bytes&&a.payload.empty_geometry==b.payload.empty_geometry;
}
thread_local std::deque<std::unique_ptr<Pending>> pipeline_pending;
// Hold slot0 and the mapping cache while committing outside an image19 scope.
struct CommitScope {
    bool own=false;
    CommitScope() {
        if(!(image19_scope_depth&&current_scope.image==19)) {
            own=true;image19_gate.lock();
            image19_mapping_cache.begin_lifetime(vm_base,vm_mapping_epoch_read());
        }
    }
    ~CommitScope() {if(own){image19_mapping_cache.suspend();image19_gate.unlock();}}
};
// Leave the caller's scope (and slot0) so the SDK can open one for a fallback.
struct ScopeSuspend {
    bool active=false,gate=false;ScopeLease saved;unsigned depth=0;
    ScopeSuspend() {
        if(!image19_scope_depth)return;
        active=true;saved=current_scope;depth=image19_scope_depth;gate=current_scope.image==19;
        current_scope={};image19_scope_depth=0;image19_mapping_cache.suspend();
        if(gate)image19_gate.unlock();
    }
    ~ScopeSuspend() {
        if(!active)return;
        if(gate)image19_gate.lock();
        current_scope=saved;image19_scope_depth=depth;
        image19_mapping_cache.begin_lifetime(vm_base,vm_mapping_epoch_read());
    }
};
void compiled_fallback(const Pending& job) {
    ++pipeline_census.fallbacks;
    {
        ScopeSuspend suspend;force_compiled=true;
        spu_workload_dispatch_job(vm_base+job.binary_ea,job.binary_size,job.ea,job.descriptor_size);
        force_compiled=false;
    }
    commit_log.commit_unknown();
}
void commit_front() {
    auto owned=std::move(pipeline_pending.front());pipeline_pending.pop_front();
    struct Recycle {std::unique_ptr<Pending>& p;~Recycle(){recycle_pending(p);}} recycle{owned};
    if(pipeline_stats) {
        auto& st=chain_stats;const bool ready=owned->state.load(std::memory_order_acquire)!=0;
        const bool queued=!ready&&!owned->started_ns.load(std::memory_order_relaxed);
        const auto w0=stats_now();prepare_pool.wait(owned.get());const auto waited=stats_now()-w0;
        ++st.jobs;st.wait+=waited;if(!ready)++st.waited;if(queued)++st.waited_queued;
        if(chain_draining){++st.drained;st.drain_wait+=waited;}
        const auto s0=owned->started_ns.load(std::memory_order_relaxed),e0=owned->ended_ns.load(std::memory_order_relaxed);
        if(s0&&e0){st.queue+=s0-owned->submitted_ns;st.prepare+=e0-s0;}
        if((st.jobs&4095u)==0) {
            const double n=double(st.jobs),us=1000.0*n;
            std::fprintf(stderr,"[geom-stats] jobs=%llu per job us: copy=%.1f wait=%.1f commit=%.1f queue=%.1f prepare=%.1f "
                "| waited=%.0f%% (still queued %.0f%%) drained=%.0f%% drain-wait=%.1f\n",(unsigned long long)st.jobs,
                st.copy/us,st.wait/us,st.commit/us,st.queue/us,st.prepare/us,100.0*st.waited/n,100.0*st.waited_queued/n,
                100.0*st.drained/n,st.drained?st.drain_wait/(1000.0*st.drained):0.0);
            st=ChainStats{};
        }
    } else prepare_pool.wait(owned.get());
    if(commit_log.written_since(owned->copied_after,owned->reads)) {
        ++pipeline_census.recopied;
        auto fresh=new_pending();
        fresh->ea=owned->ea;fresh->descriptor_size=owned->descriptor_size;fresh->index=owned->index;
        fresh->binary_ea=owned->binary_ea;fresh->binary_size=owned->binary_size;
        if(!copy_and_adapt(*fresh)){compiled_fallback(*fresh);return;}
        fresh->state=prepare_state(*fresh);owned=std::move(fresh);
    }
    Pending& q=*owned;
    if(q.state.load()!=1){compiled_fallback(q);return;}
    if(pipeline_verify&&(q.index&63u)==0) {
        HostPolicies policies;gj::GeometryJob serial;++pipeline_census.verified;
        if(serial.prepare(q.adapted.geometry,q.config,policies)!=gj::PrepareStatus::ready||
           !same_output(serial.geometry(),q.job->geometry()))pipeline_abort("verify mismatch",q.ea);
    }
    std::vector<go::ByteRange> writes;bool fallback=false;
    const auto c0=pipeline_stats?stats_now():0;
    struct CommitTime {std::int64_t c0;~CommitTime(){if(pipeline_stats)chain_stats.commit+=stats_now()-c0;}} commit_time{c0};
    {
        CommitScope scope;
        GeometryJobBackend backend(q.config,q.job->geometry().payload);backend.record_writes(&writes);
        if(!backend.preflight())fallback=true;
        else for(;;) {
            const auto progress=q.job->advance(backend);
            if(backend.failed()||progress==gj::Progress::failed)pipeline_abort("backend failure",q.ea);
            if(progress==gj::Progress::complete)break;
            if(progress==gj::Progress::declined) {
                if(q.job->external_effects_started())pipeline_abort("decline after external effects",q.ea);
                fallback=true;break;
            }
            if(progress==gj::Progress::pending)pipeline_abort("unexpected pending",q.ea);
        }
    } // backend tokens and slot0 are released before any fallback
    if(fallback){compiled_fallback(q);return;}
    commit_log.commit(std::move(writes));
    if(chain_ab_old())pipeline::recycle_output(q.job->release_geometry());
    const auto committed=++pipeline_census.committed;
    if(diagnostics&&sample(committed))
        std::fprintf(stderr,"[native-geometry-job] pipeline queued=%llu committed=%llu recopied=%llu fallbacks=%llu verified=%llu\n",
            (unsigned long long)pipeline_census.queued.load(),(unsigned long long)committed,
            (unsigned long long)pipeline_census.recopied.load(),(unsigned long long)pipeline_census.fallbacks.load(),
            (unsigned long long)pipeline_census.verified.load());
}
void drain_pipeline_this_thread()noexcept {
    if(pipeline_pending.empty())return;
    chain_draining=true;
    try {while(!pipeline_pending.empty())commit_front();}
    catch(...) {pipeline_abort("exception while committing",0);}
    chain_draining=false;
}
extern "C" void wkc_chain_drain(void) {drain_pipeline_this_thread();}
thread_local unsigned pipeline_index=0;
// Queue the job and report completion; nullopt asks for the serial path.
std::optional<int> pipelined_hook(std::uint32_t ea,std::uint32_t descriptor_size) {
    auto p=new_pending();p->ea=ea;p->descriptor_size=descriptor_size;
    const auto c0=pipeline_stats?stats_now():0;
    if(!copy_and_adapt(*p)){recycle_pending(p);drain_pipeline_this_thread();return std::nullopt;}
    if(pipeline_stats){p->submitted_ns=stats_now();chain_stats.copy+=p->submitted_ns-c0;}
    const auto* header=vm_base+ea; // CellSpursJobHeader: binary EA (low 3 bits flags), size>>4
    p->binary_ea=std::uint32_t(((std::uint64_t(gi::detail::be32(header))<<32)|gi::detail::be32(header+4))&~7ull);
    p->binary_size=((gi::detail::be32(header+8)>>16)&0xFFFFu)<<4;
    p->mxcsr=_mm_getcsr();p->index=pipeline_index++;
    prepare_pool.submit(p.get());pipeline_pending.push_back(std::move(p));++pipeline_census.queued;
    while(pipeline_pending.size()>pipeline_window_now())commit_front();
    return completed;
}
int execute_native_hook(int image,std::uint32_t ea,std::uint32_t descriptor_size) {
    if(image==23&&enabled)return execute_visibility(ea,descriptor_size);
    if(image==21&&enabled&&event_flag_jobs)return wkc::event_flag::execute_job(ea,descriptor_size);
    if(image!=19||!enabled||g_spu_float_mode!=SPU_FLOAT_HOST)
        return previous_native?previous_native(image,ea,descriptor_size):declined;
    if(diagnostics)++census.seen;
    if(!image19_scope_depth){log_result("missing-scope",0,declined,ea);return declined;}
    if(force_compiled)return declined; // nested dispatch from a pipelined fallback
    if(pipeline_on.load(std::memory_order_relaxed)) {
        try {if(const auto queued=pipelined_hook(ea,descriptor_size))return *queued;}
        catch(...) {pipeline_abort("exception while queueing",ea);}
    }
    bool external=false;
    try {
        // One reused source set per thread; a nested call uses its own.
        thread_local OwnedSources reused_sources;thread_local bool reused_busy=false;
        std::optional<OwnedSources> local_sources;OwnedSources* owner;
        const bool reuse=!reused_busy;
        if(reuse){reused_busy=true;reused_sources.reset();owner=&reused_sources;}
        else owner=&local_sources.emplace();
        struct Release{bool& busy;bool held;~Release(){if(held)busy=false;}} release{reused_busy,reuse};
        OwnedSources& sources=*owner;unsigned reason=0;
        if(!sources.acquire(ea,descriptor_size,reason)){log_result("acquire",reason,declined,ea);return declined;}
        gi::AdaptedJob adapted;
        const auto input_status=gi::make_input(sources.spans(),sources.plan.platform,
            [](float a,float b){return RuntimeMath::greater(a,b);},adapted);
        if(input_status!=gi::Status::ready){log_result("input",unsigned(input_status),declined,ea);return declined;}
        const auto& metadata=adapted.metadata;
        const gj::Config config{metadata.allocator_control,metadata.allocator_slot,metadata.request_flags,
            metadata.publication_destination,metadata.packet_quadwords,metadata.dma_tag};
        gj::GeometryJob job;HostPolicies policies;
        const auto prepared=job.prepare(adapted.geometry,config,policies);
        if(prepared!=gj::PrepareStatus::ready){log_result("prepare",unsigned(prepared),declined,ea);return declined;}
        GeometryJobBackend backend(config,job.geometry().payload);
        if(!backend.preflight()){log_result("mapping",0,declined,ea);return declined;}
        if(diagnostics)++census.admitted;
        for(;;) {
            const auto progress=job.advance(backend);external=job.external_effects_started();
            if(backend.failed()||progress==gj::Progress::failed) {
                log_result("backend",backend.failed()?backend.failure_reason():unsigned(job.failure()),fatal,ea);
                return fatal; // Synchronous sources and native tickets clean up; never replay.
            }
            if(progress==gj::Progress::complete) {
                log_result("complete",0,completed,ea,unsigned(job.geometry().positions.size()),
                    unsigned(job.geometry().filtered_indices.size()));
                pipeline::recycle_output(job.release_geometry());return completed;
            }
            if(progress==gj::Progress::declined) {
                const auto result=external?fatal:declined;log_result("advance",0,result,ea);return result;
            }
            // Current backend receipts are synchronous. A future asynchronous
            // backend must retain this ticket and owned job instead of polling
            // or destroying them here.
            if(progress==gj::Progress::pending){log_result("unexpected-pending",0,fatal,ea);return fatal;}
        }
    } catch(...) {
        const auto result=external?fatal:declined;log_result("exception",0,result,ea);return result;
    }
}
int native_hook(int image,std::uint32_t ea,std::uint32_t descriptor_size)noexcept {
    // execute_native_hook's local owners clean up before a fatal reaches here.
    // The current chain walker ignores negative dispatch returns; continuing
    // after partial publication would expose corrupted work to later jobs.
    const int result=execute_native_hook(image,ea,descriptor_size);
    if(result==fatal) {
        std::fputs("[native-geometry-job] unrecoverable partial job; stopping before chain continuation\n",stderr);
        std::abort();
    }
    return result;
}
}

void wkc2_init_native_geometry_jobs() {
    if(installed)return;
    const auto* setting=std::getenv("WKC_NATIVE_GEOMETRY_JOB");enabled=setting&&std::strcmp(setting,"1")==0;
    if(const char* flag=std::getenv("WKC_NATIVE_EVENT_FLAG"))event_flag_jobs=std::strcmp(flag,"0")!=0;
    if(!enabled)return;
    const auto* diagnostic_setting=std::getenv("WKC_NATIVE_GEOMETRY_DIAGNOSTICS");
    diagnostics=diagnostic_setting&&std::strcmp(diagnostic_setting,"1")==0;
    previous_native=g_spu_native_job_hook;previous_begin=g_spu_job_scope_begin;previous_end=g_spu_job_scope_end;
    previous_put=g_spu_put_hook;previous_trace=g_spu_put_trace_hook;
    g_spu_native_job_hook=native_hook;g_spu_job_scope_begin=scope_begin;g_spu_job_scope_end=scope_end;
    g_spu_put_hook=put_hook;g_spu_put_trace_hook=trace_hook;installed=true;
    wkc::job_capture::configure();
    // Pipeline workers: WKC_GEOM_PIPELINE=<n> (0 = serial), default by core count.
    const unsigned cores=std::thread::hardware_concurrency();
    unsigned workers=cores>=16?7u:cores>=8?3u:cores>=4?1u:0u;
    if(const char* requested=std::getenv("WKC_GEOM_PIPELINE"))workers=unsigned(std::strtoul(requested,nullptr,10));
    if(workers) {
        pipeline_verify=std::getenv("WKC_GEOM_PIPELINE_VERIFY")!=nullptr;
        pipeline_stats=std::getenv("WKC_GEOM_STATS")!=nullptr;
        prepare_pool.start(workers);g_spu_job_chain_drain=wkc_chain_drain;pipeline_on=true;
        std::atexit([]{prepare_pool.stop();});
    }
    if(diagnostics)std::fprintf(stderr,"[native-geometry-job] installed image19 host geometry and image23 selection owned routes; "
        "native/fallback slot0 gate and chained PUT hooks active; pipeline workers=%u\n",workers);
}
