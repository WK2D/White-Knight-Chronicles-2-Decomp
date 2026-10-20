// Opt-in synchronous image19 integration, using original descriptor/source
// bytes before SPURS staging. The public algorithms receive owned typed data.
#include "native_geometry_runtime.h"
#include "native_geometry_input.h"
#include "native_geometry_job.h"
#include "native_estimate_interpolation.h"
#include "spu_helpers.h"
#include "spu_coherency.h"
#include "spu_workload.h"
#include "native_geometry_mapping_cache.h"
#include "vm_mapping_epoch.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

extern "C" {
extern std::uint8_t* vm_base;
extern void (*g_spu_put_hook)(std::uint32_t,std::uint32_t);
extern void (*g_spu_put_trace_hook)(std::uint32_t,std::uint32_t,int,std::uint32_t,const std::uint8_t*);
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
    std::vector<Byte> next(bytes);
    // The same coherency lock excludes atomic/MFC writers during each owned
    // copy. The submitted-job producer contract excludes unsynchronized edits.
    spu_lockline_lock();std::memcpy(next.data(),vm_base+ea,bytes);spu_lockline_unlock();
    output=std::move(next);return true;
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
        const auto status=gi::plan_spurs_constructor(descriptor,payloads[16],contract,plan);
        reason=100u+unsigned(status);return status==gi::ConstructorStatus::ready;
    }
    gi::StagedInputs spans()const {
        gi::StagedInputs staged;staged.descriptor=descriptor;
        for(unsigned slot=0;slot<gi::dma_slot_count;++slot)
            staged.slots[slot]={payloads[slot],plan.slot_begins[slot]};
        return staged;
    }
};

u128 pack(const Float4& value) {u128 result;std::memcpy(&result,value.data(),16);return result;}
Float4 unpack(u128 value) {Float4 result;std::memcpy(result.data(),&value,16);return result;}
Float4 splat(float value) {return {value,value,value,value};}
struct RuntimeMath {
    static Float4 multiply(const Float4& a,const Float4& b){return unpack(spu_fm(pack(a),pack(b)));}
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c){return unpack(spu_fma(pack(a),pack(b),pack(c)));}
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c){return unpack(spu_fms(pack(a),pack(b),pack(c)));}
    static Float4 add(const Float4& a,const Float4& b){return unpack(spu_fa(pack(a),pack(b)));}
    static Float4 subtract(const Float4& a,const Float4& b){return unpack(spu_fs(pack(a),pack(b)));}
    static Float4 negative_multiply_add(const Float4& a,const Float4& b,const Float4& c){return unpack(spu_fnms(pack(a),pack(b),pack(c)));}
    static bool greater(float a,float b){return spu_fcgt(pack(splat(a)),pack(splat(b)))._u32[0]!=0;}
    static bool magnitude_equal(float a,float b){return spu_fcmeq(pack(splat(a)),pack(splat(b)))._u32[0]!=0;}
};
struct PositionMath:RuntimeMath {
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        return OrderedHostPositionStreamMath<RuntimeMath>::multiply_add(a,b,c);
    }
};
struct NormalMath:RuntimeMath {
    static Float4 multiply(const Float4& a,const Float4& b) {
        return OrderedHostCofactorMath<RuntimeMath>::multiply(a,b);
    }
};
struct CofactorMath:NormalMath {
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        return OrderedHostCofactorMath<RuntimeMath>::multiply_subtract(a,b,c);
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
        // PUTLLC consumes its ticket for both success and conflict receipts.
        if(status>=0)global_token_=0;
        require(status==0||status==1,14);return std::uint32_t(status);
    }
    std::optional<std::uint32_t> put_unconditional(std::uint32_t ea,const ga::AllocationLine& bytes,
                                                 std::uint64_t token) {
        require(vm_base==base_&&vm_range(ea&~127u,128,true)&&token==slot_token_,15);
        const auto status=spu_native_putlluc(ea&~127u,bytes.data());require(status==2,16);return 2u;
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
        spu_lockline_lock();std::memcpy(base_+ea,source.data(),size);
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
};

void put_hook(std::uint32_t ea,std::uint32_t bytes) {
    std::lock_guard<std::recursive_mutex> lock(put_hook_gate);
    if(previous_put)previous_put(ea,bytes);
}
void trace_hook(std::uint32_t ea,std::uint32_t bytes,int image,std::uint32_t pc,const Byte* source) {
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
struct ScopeLease {void* previous=nullptr;bool active=false;};
thread_local ScopeLease current_scope;
[[noreturn]] void scope_abort()noexcept {
    std::fputs("[native-geometry-job] fatal scope callback failure; refusing C-boundary unwind\n",stderr);
    std::abort();
}
void* scope_begin(int image,std::uint32_t ea,std::uint32_t bytes)noexcept {
    try {
        refresh_put_hooks();
        if(image!=19)return previous_begin?previous_begin(image,ea,bytes):nullptr;
        // The SDK dispatches one synchronous job at a time on each worker.
        // Reuse its thread-local scope record without allocation or C unwind.
        if(current_scope.active||image19_scope_depth)scope_abort();
        image19_gate.lock();current_scope.active=true;++image19_scope_depth;
        // The producer pins submitted mappings until this job returns. Retain
        // positive checks across jobs only within the runtime mapping epoch;
        // protection changes and VM teardown/recreation invalidate this cache.
        image19_mapping_cache.begin_lifetime(vm_base,vm_mapping_epoch_read());
        current_scope.previous=previous_begin?previous_begin(image,ea,bytes):nullptr;
        return &current_scope;
    } catch(...) {scope_abort();}
}
void scope_end(int image,std::uint32_t ea,std::uint32_t bytes,void* token)noexcept {
    try {
        if(image!=19){if(previous_end)previous_end(image,ea,bytes,token);return;}
        if(token!=&current_scope||!current_scope.active||image19_scope_depth!=1)scope_abort();
        if(previous_end)previous_end(image,ea,bytes,current_scope.previous);
        image19_mapping_cache.suspend();current_scope={};--image19_scope_depth;image19_gate.unlock();
    } catch(...) {scope_abort();}
}
int execute_native_hook(int image,std::uint32_t ea,std::uint32_t descriptor_size) {
    if(image!=19||!enabled||g_spu_float_mode!=SPU_FLOAT_HOST)
        return previous_native?previous_native(image,ea,descriptor_size):declined;
    if(diagnostics)++census.seen;
    if(!image19_scope_depth){log_result("missing-scope",0,declined,ea);return declined;}
    bool external=false;
    try {
        OwnedSources sources;unsigned reason=0;
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
                    unsigned(job.geometry().filtered_indices.size()));return completed;
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
    if(!enabled)return;
    const auto* diagnostic_setting=std::getenv("WKC_NATIVE_GEOMETRY_DIAGNOSTICS");
    diagnostics=diagnostic_setting&&std::strcmp(diagnostic_setting,"1")==0;
    previous_native=g_spu_native_job_hook;previous_begin=g_spu_job_scope_begin;previous_end=g_spu_job_scope_end;
    previous_put=g_spu_put_hook;previous_trace=g_spu_put_trace_hook;
    g_spu_native_job_hook=native_hook;g_spu_job_scope_begin=scope_begin;g_spu_job_scope_end=scope_end;
    g_spu_put_hook=put_hook;g_spu_put_trace_hook=trace_hook;installed=true;
    if(diagnostics)std::fprintf(stderr,"[native-geometry-job] installed host-mode image19 synchronous owned route; "
        "native/fallback slot0 gate and chained PUT hooks active\n");
}
