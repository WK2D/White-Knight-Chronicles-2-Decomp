// Project-owned data only. Check transfer chunks, record visibility and IRQ order.
#ifdef NDEBUG
#undef NDEBUG
#endif
#ifndef WKC_PRIVATE_PAYLOAD_EXPORT_SYNTHETIC_H
#define WKC_PRIVATE_PAYLOAD_EXPORT_SYNTHETIC_H
#include "native_geometry_export.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
namespace wkc::geometry::payload_export {
struct SyntheticBackend {
 AllocationRecord* allocation;
 BindingRecord* binding;
 const ExportPlan* plan;
 bool enabled=true,failed=false;
 unsigned submitted=0,event_count=0;
 std::array<char,256> events{};
 void event(char value)noexcept {if(event_count<events.size())events[event_count++]=value;else failed=true;}
 void architectural_fence()noexcept {event('F');}
 bool interrupts_enabled()noexcept {event('R');return enabled;}
 void set_interrupts_enabled(bool value)noexcept {
  event(value?'E':'D');
  if(value&&(*allocation!=plan->allocation_after_cursors||*binding!=plan->binding_prepared))failed=true;
  enabled=value;
 }
 void put(std::span<const Byte> bytes,std::uint32_t destination,std::uint32_t tag,Byte command)noexcept {
  event('P');
  if(enabled||*allocation!=plan->allocation_before||*binding!=plan->binding_prepared||
     submitted>=plan->transfers.size()){failed=true;return;}
  const auto& expected=plan->transfers[submitted++];
  if(bytes.size()!=expected.bytes||destination!=expected.effective_destination||tag!=expected.tag||command!=0x20)failed=true;
  if(!std::equal(bytes.begin(),bytes.end(),plan->payload.begin()+expected.source_offset))failed=true;
 }
};
inline int run_synthetic_payload_export() {
 unsigned cases=0,declines=0;
 AllocationRecord allocation;
 for(unsigned i=0;i<allocation.bytes.size();++i)allocation.bytes[i]=Byte(0x90u+i);
 put_word(allocation.bytes.data(),0x100000u);put_word(allocation.bytes.data()+4,0xfffffff0u);
 put_word(allocation.bytes.data()+8,0x12345678u);allocation.bytes[20]=0x7e;
 BindingRecord binding;binding.bytes.fill(0x6b);
 const auto exercise=[&](PayloadKind kind,unsigned count,unsigned format,bool enabled) {
  unsigned size=kind==PayloadKind::indices?2*((count+7)&~7u):
   (count*(format==32?16:format==33?24:12)+15)&~15u;
  std::vector<Byte> payload(size+13);
  for(unsigned i=0;i<payload.size();++i)payload[i]=Byte((i*17u+0x35u)&255);
  ExportPlan plan;
  const auto status=kind==PayloadKind::indices?
   make_index_plan(count,payload,20,allocation,binding,plan):
   make_vertex_plan(count,format,payload,20,allocation,binding,plan);
  assert(status==Status::ready);
  if(kind==PayloadKind::indices&&!count)assert(!plan.active&&plan.transfers.empty()&&plan.payload.empty());
  else {
   assert(plan.active&&plan.payload.size()==size&&std::equal(plan.payload.begin(),plan.payload.end(),payload.begin()));
   assert(plan.transfers.size()==(size+16383)/16384);
   unsigned total=0;
   for(const auto& transfer:plan.transfers) {
    assert(transfer.source_offset==total&&transfer.effective_destination==allocation.effective_cursor()+total);
    assert(transfer.tag==20&&transfer.bytes>0&&transfer.bytes<=16384);
    if(total+transfer.bytes<size)assert(transfer.bytes==16384);
    total+=transfer.bytes;
   }
   assert(total==size);
   assert(word(plan.binding_prepared.bytes.data())==allocation.effective_cursor());
   assert(word(plan.binding_prepared.bytes.data()+4)==allocation.mapped_cursor());
   assert(word(plan.binding_prepared.bytes.data()+8)==allocation.location());
   assert(word(plan.binding_prepared.bytes.data()+12)==0x6b6b6b6bu);
   assert(plan.allocation_after.effective_cursor()==allocation.effective_cursor()+size);
   assert(plan.allocation_after.mapped_cursor()==allocation.mapped_cursor()+size);
   assert(plan.allocation_after.bytes[20]==1);
   for(unsigned i=8;i<32;++i)if(i!=20)assert(plan.allocation_after.bytes[i]==allocation.bytes[i]);
  }
  auto actual=allocation;auto actual_binding=binding;
  SyntheticBackend backend{&actual,&actual_binding,&plan,enabled};
  assert(execute(plan,actual,actual_binding,backend));assert(!backend.failed);
  assert(actual==plan.allocation_after&&actual_binding==plan.binding_prepared&&backend.enabled==enabled);
  assert(backend.submitted==plan.transfers.size());
  if(!plan.active)assert(backend.event_count==0);
  else {
   unsigned position=0;if(kind==PayloadKind::vertices)assert(backend.events[position++]=='F');
   assert(backend.events[position++]=='R'&&backend.events[position++]=='D');
   for(unsigned i=0;i<backend.submitted;++i)assert(backend.events[position++]=='P');
   if(enabled)assert(backend.events[position++]=='E');assert(position==backend.event_count);
  }
  ++cases;
 };
 for(bool enabled:{false,true}) {
  for(unsigned count:{0u,3u,6u,8190u,8193u,16383u,16386u,65535u})
   exercise(PayloadKind::indices,count,0,enabled);
  for(unsigned format:{32u,33u,34u})for(unsigned count:{1u,2u,682u,683u,1023u,1024u,1025u,1365u,1366u,65535u})
   exercise(PayloadKind::vertices,count,format,enabled);
 }
 const auto rejected=[&](Status expected,auto make) {
  ExportPlan output;output.payload={0xde,0xad};output.binding_prepared.bytes.fill(0xcd);
  assert(make(output)==expected);assert((output.payload==std::vector<Byte>{0xde,0xad}));
  assert(std::all_of(output.binding_prepared.bytes.begin(),output.binding_prepared.bytes.end(),[](Byte b){return b==0xcd;}));
  ++declines;
 };
 const std::array<Byte,16> full{};const std::array<Byte,15> short_input{};
 rejected(Status::unsupported_count,[&](ExportPlan& o){return make_index_plan(4,full,20,allocation,binding,o);});
 rejected(Status::unsupported_count,[&](ExportPlan& o){return make_index_plan(65538,full,20,allocation,binding,o);});
 rejected(Status::short_payload,[&](ExportPlan& o){return make_index_plan(3,short_input,20,allocation,binding,o);});
 rejected(Status::invalid_tag,[&](ExportPlan& o){return make_index_plan(3,full,32,allocation,binding,o);});
 rejected(Status::unsupported_count,[&](ExportPlan& o){return make_vertex_plan(0,32,full,20,allocation,binding,o);});
 rejected(Status::unsupported_format,[&](ExportPlan& o){return make_vertex_plan(1,35,full,20,allocation,binding,o);});
 rejected(Status::short_payload,[&](ExportPlan& o){return make_vertex_plan(1,32,short_input,20,allocation,binding,o);});
 auto near_end=allocation;put_word(near_end.bytes.data(),0xfffffff8u);
 rejected(Status::effective_range_wrap,[&](ExportPlan& o){return make_index_plan(3,full,20,near_end,binding,o);});
 // A stale plan must not invoke backend methods or mutate either record.
 ExportPlan plan;assert(make_index_plan(3,full,20,allocation,binding,plan)==Status::ready);
 auto changed=allocation;changed.bytes[31]^=1;const auto saved=changed;auto actual_binding=binding;
 SyntheticBackend backend{&changed,&actual_binding,&plan};
 assert(!execute(plan,changed,actual_binding,backend));assert(backend.event_count==0&&changed==saved&&actual_binding==binding);++declines;
 std::printf("Native payload export: %u ordered chunk/record/interrupt cases and %u unchanged declines pass.\n",cases,declines);
 return 0;
}
}
#endif

int wkc2_test_native_geometry_export() {return wkc::geometry::payload_export::run_synthetic_payload_export();}
