// Project-written output transfer planning and ordered record publication.
// Complete caller-owned bytes are copied into a plan; no padding is invented.
#ifndef WKC_NATIVE_GEOMETRY_EXPORT_H
#define WKC_NATIVE_GEOMETRY_EXPORT_H
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <type_traits>
#include <vector>
namespace wkc::geometry::payload_export {
using Byte=std::uint8_t;
inline std::uint32_t word(const Byte* p) {
 return std::uint32_t(p[0])*0x1000000u+std::uint32_t(p[1])*65536u+
        std::uint32_t(p[2])*256u+p[3];
}
inline void put_word(Byte* p,std::uint32_t x) {
 p[0]=Byte(x>>24);p[1]=Byte(x>>16);p[2]=Byte(x>>8);p[3]=Byte(x);
}
struct AllocationRecord {
 std::array<Byte,32> bytes{};
 std::uint32_t effective_cursor()const{return word(bytes.data());}
 std::uint32_t mapped_cursor()const{return word(bytes.data()+4);}
 std::uint32_t location()const{return word(bytes.data()+8);}
 bool operator==(const AllocationRecord&)const=default;
};
struct BindingRecord {
 std::array<Byte,16> bytes{};
 bool operator==(const BindingRecord&)const=default;
};
enum class PayloadKind:Byte {indices,vertices};
struct Transfer {
 std::uint32_t source_offset=0,effective_destination=0,bytes=0,tag=0;
 bool operator==(const Transfer&)const=default;
};
struct ExportPlan {
 PayloadKind kind=PayloadKind::indices;
 bool active=false,fence_before_critical=false;
 AllocationRecord allocation_before,allocation_after_cursors,allocation_after;
 BindingRecord binding_before,binding_prepared;
 std::vector<Byte> payload;
 std::vector<Transfer> transfers;
};
enum class Status:Byte {ready,unsupported_count,unsupported_format,
 short_payload,invalid_tag,effective_range_wrap};
namespace detail {
inline Status make(PayloadKind kind,std::uint32_t byte_count,
                   std::span<const Byte> complete_payload,std::uint32_t tag,
                   const AllocationRecord& allocation,const BindingRecord& binding,
                   ExportPlan& output) {
 if(tag>=32)return Status::invalid_tag;
 if(complete_payload.size()<byte_count)return Status::short_payload;
 if(std::uint64_t(allocation.effective_cursor())+byte_count>0x100000000ull)
  return Status::effective_range_wrap;
 ExportPlan next;next.kind=kind;next.active=true;
 next.fence_before_critical=kind==PayloadKind::vertices;
 next.allocation_before=allocation;next.binding_before=binding;
 next.binding_prepared=binding;
 for(unsigned offset:{0u,4u,8u})
  put_word(next.binding_prepared.bytes.data()+offset,word(allocation.bytes.data()+offset));
 next.payload.assign(complete_payload.begin(),complete_payload.begin()+byte_count);
 for(std::uint32_t offset=0;offset<byte_count;) {
  const std::uint32_t size=(byte_count-offset)>16384?16384:byte_count-offset;
  next.transfers.push_back({offset,allocation.effective_cursor()+offset,size,tag});
  offset+=size;
 }
 next.allocation_after_cursors=allocation;
 put_word(next.allocation_after_cursors.bytes.data(),allocation.effective_cursor()+byte_count);
 // The mapped cursor is u32 arithmetic, including sentinel mapping0xffffffff.
 put_word(next.allocation_after_cursors.bytes.data()+4,allocation.mapped_cursor()+byte_count);
 next.allocation_after=next.allocation_after_cursors;next.allocation_after.bytes[20]=1;
 output=std::move(next);return Status::ready;
}
}
// Supported geometry count domain: u16 count and complete triangles. Zero is the
// real index helper's early no-op, preserving all records and doing no PUT.
inline Status make_index_plan(std::uint32_t index_count,std::span<const Byte> complete_payload,
 std::uint32_t tag,const AllocationRecord& allocation,const BindingRecord& binding,ExportPlan& output) {
 if(index_count>65535||index_count%3)return Status::unsupported_count;
 if(!index_count) {
  ExportPlan next;next.allocation_before=next.allocation_after_cursors=next.allocation_after=allocation;
  next.binding_before=next.binding_prepared=binding;output=std::move(next);return Status::ready;
 }
 const std::uint32_t bytes=2*((index_count+7)&~std::uint32_t(7));
 return detail::make(PayloadKind::indices,bytes,complete_payload,tag,allocation,binding,output);
}
// Metadata's custom output formats32/33/34 have recovered strides16/24/12.
// The root only calls vertex export with nonempty surviving geometry. A zero
// vertex count has an unverified do-once zero-size original command: decline.
inline Status make_vertex_plan(std::uint32_t vertex_count,unsigned format,
 std::span<const Byte> complete_payload,std::uint32_t tag,const AllocationRecord& allocation,
 const BindingRecord& binding,ExportPlan& output) {
 if(!vertex_count||vertex_count>65535)return Status::unsupported_count;
 const unsigned stride=format==32?16:format==33?24:format==34?12:0;
 if(!stride)return Status::unsupported_format;
 const std::uint32_t bytes=(vertex_count*stride+15)&~std::uint32_t(15);
 return detail::make(PayloadKind::vertices,bytes,complete_payload,tag,allocation,binding,output);
}
// Backend contract: no throwing/failing operations after preflight, explicit
// exclusive ownership of the two host records throughout this call, and no
// hidden final packet publication. put(span,destination,tag,command) enqueues a
// request in order. For asynchronous backends the caller retains the ExportPlan
// payload unchanged until every queued request has consumed its bytes.
// Execute only an unmodified plan returned by make_index_plan/make_vertex_plan.
// This is not a retry/rollback/concurrency implementation. An adapter that can
// fail submission must expose partial progress instead of pretending rollback.
template<class Backend>
bool execute(const ExportPlan& plan,AllocationRecord& allocation,BindingRecord& binding,Backend& backend) {
 static_assert(noexcept(backend.architectural_fence()));
 static_assert(noexcept(backend.interrupts_enabled()));
 static_assert(noexcept(backend.set_interrupts_enabled(false)));
 static_assert(noexcept(backend.put(std::span<const Byte>{},std::uint32_t{},std::uint32_t{},Byte{})));
 static_assert(std::is_same_v<decltype(backend.put(std::span<const Byte>{},std::uint32_t{},std::uint32_t{},Byte{})),void>);
 if(allocation!=plan.allocation_before||binding!=plan.binding_before)return false;
 if(!plan.active)return true;
 if(plan.fence_before_critical)backend.architectural_fence();
 const bool enabled=backend.interrupts_enabled();backend.set_interrupts_enabled(false);
 binding=plan.binding_prepared;
 for(const auto& transfer:plan.transfers)
  backend.put(std::span<const Byte>(plan.payload).subspan(transfer.source_offset,transfer.bytes),
              transfer.effective_destination,transfer.tag,Byte{0x20});
 allocation=plan.allocation_after_cursors;
 if(enabled)backend.set_interrupts_enabled(true);
 allocation=plan.allocation_after;
 return true;
}
}
#endif
