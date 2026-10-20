// Project-owned allocation protocol tests. No game data, generated source,
// private captures or host-memory mappings are required.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_geometry_allocation.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace ga=wkc::geometry::allocation;
static_assert(!std::is_copy_constructible_v<ga::AllocationTransaction>);
static_assert(!std::is_move_constructible_v<ga::AllocationTransaction>);
namespace {
using ga::AllocationLine;
using ga::AllocationRecord;
bool same_ring(const ga::RingState& a,const ga::RingState& b) {
    return a.begin==b.begin&&a.mapped_base==b.mapped_base&&a.end==b.end&&
        a.producer==b.producer&&a.location==b.location&&a.consumer_address==b.consumer_address;
}
AllocationLine line(std::uint32_t begin,std::uint32_t mapped,std::uint32_t end,
                    std::uint32_t producer,std::uint32_t location,std::uint32_t last) {
    AllocationLine value;
    for(unsigned i=0;i<value.size();++i)value[i]=std::uint8_t(i*13u+0x31u);
    ga::allocation_word(value,0,begin);ga::allocation_word(value,4,mapped);
    ga::allocation_word(value,8,end);ga::allocation_word(value,12,producer);
    ga::allocation_word(value,16,location);ga::allocation_word(value,20,last);
    return value;
}
AllocationRecord record() {
    AllocationRecord value;
    for(unsigned i=0;i<value.size();++i)value[i]=std::uint8_t(0xa0u+i);
    return value;
}
AllocationRecord success_record(std::uint32_t effective,std::uint32_t mapped,
    std::uint32_t location,std::uint32_t reserved_end,std::uint32_t consumer) {
    auto value=record();
    ga::allocation_word(value,0,effective);ga::allocation_word(value,4,mapped);
    ga::allocation_word(value,8,location);ga::allocation_word(value,12,reserved_end);
    ga::allocation_word(value,16,consumer);value[20]=0;
    return value;
}
enum class Op {machine,slot,global,consumer,conditional,unconditional,delay};
char code(Op op) {
    switch(op) {
    case Op::machine:return 'M';case Op::slot:return 'S';case Op::global:return 'G';
    case Op::consumer:return 'C';case Op::conditional:return 'P';case Op::unconditional:return 'U';
    case Op::delay:return 'W';
    }
    return '?';
}
struct Step {
    Op op;std::uint32_t address=0,value=0,tag=0;
    std::uint64_t token=0;AllocationLine bytes{};
};
Step machine(std::uint32_t status){return {Op::machine,0,status};}
Step get_line(Op op,std::uint32_t address,AllocationLine bytes,std::uint64_t token) {
    Step value{op,address};value.bytes=bytes;value.token=token;value.value=0x55;return value;
}
Step consumer(std::uint32_t cursor){return {Op::consumer,0x5000,cursor,20};}
Step put_line(Op op,std::uint32_t address,AllocationLine bytes,std::uint64_t token,
              std::uint32_t status=0) {
    Step value{op,address,status};value.bytes=bytes;value.token=token;return value;
}
struct Backend {
    std::vector<Step> script;
    std::size_t cursor=0;
    bool pending_once=false,outstanding=false,irq=true;
    unsigned polls=0,submissions=0,slot_ownership=0;
    std::uint64_t slot_token=0;
    std::string events;
    std::vector<AllocationLine> published_slots,published_globals;
    const Step* take(Op op,std::uint32_t address=0,std::uint32_t tag=0,
                     const AllocationLine* bytes=nullptr,std::uint64_t token=0) {
        assert(cursor<script.size());const auto& step=script[cursor];
        assert(step.op==op&&step.address==address);
        if(op==Op::consumer)assert(tag==step.tag);
        if(op==Op::conditional||op==Op::unconditional)
            assert(bytes&&*bytes==step.bytes&&token==step.token);
        if(outstanding) {outstanding=false;++polls;++cursor;return &step;}
        events.push_back(code(op));++submissions;
        if(pending_once){outstanding=true;return nullptr;}
        ++cursor;return &step;
    }
    std::optional<std::uint32_t> read_machine_status() {
        const auto* step=take(Op::machine);if(!step)return std::nullopt;return step->value;
    }
    void set_interrupts(bool value){irq=value;events.push_back(value?'E':'D');}
    std::optional<ga::AllocationLineReceipt> get_locked_line(std::uint32_t address,
                                                           ga::AllocationLineKind kind) {
        const auto op=kind==ga::AllocationLineKind::slot?Op::slot:Op::global;
        const auto* step=take(op,address);if(!step)return std::nullopt;
        if(kind==ga::AllocationLineKind::slot){++slot_ownership;slot_token=step->token;}
        else assert(slot_ownership==1); // Global access must not discard slot ownership.
        return ga::AllocationLineReceipt{step->bytes,step->value,step->token};
    }
    void fence(){events.push_back('F');}
    std::optional<std::uint32_t> put_conditional(std::uint32_t address,
                                               const AllocationLine& bytes,std::uint64_t token) {
        const auto* step=take(Op::conditional,address,0,&bytes,token);if(!step)return std::nullopt;
        if(!(step->value&1))published_globals.push_back(bytes);
        return step->value;
    }
    std::optional<std::uint32_t> put_unconditional(std::uint32_t address,
                                                 const AllocationLine& bytes,std::uint64_t token) {
        assert(slot_ownership==1&&token==slot_token);
        const auto* step=take(Op::unconditional,address,0,&bytes,token);if(!step)return std::nullopt;
        published_slots.push_back(bytes);return step->value;
    }
    std::optional<ga::RingConsumerReceipt> get_consumer(std::uint32_t address,std::uint32_t tag) {
        const auto* step=take(Op::consumer,address,tag);if(!step)return std::nullopt;
        return ga::RingConsumerReceipt{step->value};
    }
    bool wait_decrementer_reads(unsigned count) {
        assert(count==25);return take(Op::delay)!=nullptr;
    }
};
void drive(ga::AllocationTransaction& transaction,Backend& backend,bool expected_success,
           const AllocationRecord& expected_result,const std::string& expected_events,
           unsigned expected_rounded) {
    unsigned advances=0,pending=0;
    while(!transaction.finished()) {
        assert(++advances<256);
        const auto progress=transaction.advance(backend);
        if(progress==ga::AllocationProgress::pending)++pending;
    }
    assert(transaction.success()==expected_success&&transaction.result()==expected_result);
    assert(transaction.rounded_bytes()==expected_rounded);
    assert(backend.cursor==backend.script.size()&&!backend.outstanding);
    assert(backend.submissions==backend.script.size()&&backend.events==expected_events);
    assert(pending==(backend.pending_once?backend.script.size():0));
    assert(backend.polls==pending);
    const auto submissions=backend.submissions;const auto events=backend.events;
    assert(transaction.advance(backend)==ga::AllocationProgress::finished);
    assert(backend.submissions==submissions&&backend.events==events);
}
}

int wkc2_test_native_geometry_allocation() {
    unsigned ring_cases=0,transactions=0;
    const ga::RingState ordinary{1000,5000,2000,1200,7,88};
    const auto ring_case=[&](ga::RingState state,unsigned bytes,unsigned receipt,
        ga::RingDecisionKind expected,unsigned producer,unsigned effective,unsigned mapped,bool wrapped) {
        const auto decision=ga::decide_ring_reservation(state,{bytes},{receipt});
        assert(decision.kind==expected&&decision.wrapped==wrapped);
        auto preserved=state;if(expected==ga::RingDecisionKind::reserved)preserved.producer=producer;
        assert(same_ring(decision.state,preserved));
        if(expected==ga::RingDecisionKind::reserved)
            assert(decision.allocation.effective_address==effective&&
                decision.allocation.mapped_address==mapped&&decision.allocation.location==state.location);
        ++ring_cases;
    };
    ring_case(ordinary,100,1200,ga::RingDecisionKind::reserved,1300,1200,5200,false);
    ring_case(ordinary,100,1301,ga::RingDecisionKind::reserved,1300,1200,5200,false);
    ring_case(ordinary,100,1201,ga::RingDecisionKind::consumer_wait,0,0,0,false);
    ring_case(ordinary,100,1300,ga::RingDecisionKind::consumer_wait,0,0,0,false);
    auto edge=ordinary;edge.producer=1900;
    ring_case(edge,100,1900,ga::RingDecisionKind::reserved,2000,1900,5900,false);
    edge.producer=1950;
    ring_case(edge,100,1100,ga::RingDecisionKind::consumer_wait,0,0,0,true);
    ring_case(edge,100,1101,ga::RingDecisionKind::reserved,1100,1000,5000,true);
    ring_case(edge,100,2000,ga::RingDecisionKind::consumer_wait,0,0,0,true);
    ring_case(edge,100,2051,ga::RingDecisionKind::reserved,1100,1000,5000,true);
    ring_case(ordinary,501,1200,ga::RingDecisionKind::capacity_rejected,0,0,0,false);
    assert(!ga::ring_requires_consumer(ordinary,{501}));
    assert(ga::ring_requires_consumer(ordinary,{500}));
    ring_case({0xfffff000u,0x5000,0x1000,0xfffffff0u,3,44},0x20,0xfffffff0u,
        ga::RingDecisionKind::reserved,0x10,0xfffffff0u,0x5ff0,false);
    // Doubling the request is deliberately u32, rather than widened arithmetic.
    ring_case({0x1000,0x9000,0x2000,0x1000,3,44},0x80000000u,0xffffffffu,
        ga::RingDecisionKind::reserved,0x80001000u,0x1000,0x9000,true);

    for(bool pending:{false,true})for(unsigned machine_bit:{0u,1u})for(bool initial_irq:{false,true}) {
        const auto restore=machine_bit?std::string("E"):std::string{};
        // Direct address leaves backend untouched, irrespective of initial IRQ.
        for(unsigned control:{0x1234567au,0x1234567bu}) {
            ga::AllocationTransaction transaction({control,0xffffff81u,9,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            drive(transaction,backend,true,success_record(0x12345678u,0xffffffffu,control&1u,0,0),"",0);
            assert(backend.irq==initial_irq&&backend.slot_ownership==0);++transactions;
        }
        const auto slot=line(0x1000,0x9000,0x1400,0x1100,7,0x5000);
        auto reserved=slot;ga::allocation_word(reserved,12,0x1180);
        {
            ga::AllocationTransaction transaction({0x8000,1,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,slot,11),consumer(0x1100),
                put_line(Op::unconditional,0x8080,reserved,11,0xdead)};
            drive(transaction,backend,true,success_record(0x1100,0x9100,7,0x1180,0x5000),"MDSFCFFU"+restore,128);
            assert(backend.irq==bool(machine_bit)&&backend.published_slots==std::vector<AllocationLine>{reserved});
            assert(transaction.slot_line()==reserved);++transactions;
        }
        {
            // A real wrap reserves at begin; consumer==wrapped_end must block,
            // but the next byte beyond it permits the entire wrapped range.
            auto wrapping=slot;ga::allocation_word(wrapping,12,0x13c0);
            auto wrapped=wrapping;ga::allocation_word(wrapped,12,0x1080);
            ga::AllocationTransaction transaction({0x8000,128,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,wrapping,12),consumer(0x1081),
                put_line(Op::unconditional,0x8080,wrapped,12)};
            drive(transaction,backend,true,success_record(0x1000,0x9000,7,0x1080,0x5000),"MDSFCFFU"+restore,128);
            assert(backend.irq==bool(machine_bit)&&transaction.slot_line()==wrapped);++transactions;
        }
        {
            // Blocked ring -> successful global fallback, with no slot publish.
            auto global=line(0x2000,0x6000,0x2800,0x2200,9,0x40);
            auto committed=global;ga::allocation_word(committed,12,0x2280);
            ga::AllocationTransaction transaction({0x8000,128,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,slot,13),consumer(0x1140),
                get_line(Op::global,0x8000,global,101),put_line(Op::conditional,0x8000,committed,101)};
            drive(transaction,backend,true,success_record(0x2200,0x6200,9,0,0),"MDSFCFGFFP"+restore,128);
            assert(backend.irq==bool(machine_bit)&&backend.published_slots.empty());
            assert(backend.published_globals==std::vector<AllocationLine>{committed});++transactions;
        }
        {
            // CAS failure requires a fresh line/token. Its changed producer and
            // location must determine the second candidate; no stale retry.
            auto empty=slot;ga::allocation_word(empty,8,0x1000);
            auto first=line(0x2000,0x6000,0x2800,0x2200,9,0x40);
            auto failed=first;ga::allocation_word(failed,12,0x2280);
            auto fresh=line(0x3000,0xa000,0x3800,0x3300,11,0x50);
            auto committed=fresh;ga::allocation_word(committed,12,0x3380);
            ga::AllocationTransaction transaction({0x8000,128,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,empty,14),
                get_line(Op::global,0x8000,first,102),put_line(Op::conditional,0x8000,failed,102,1),
                get_line(Op::global,0x8000,fresh,103),put_line(Op::conditional,0x8000,committed,103)};
            drive(transaction,backend,true,success_record(0x3300,0xa300,11,0,0),"MDSFGFFPGFFP"+restore,128);
            assert(backend.published_globals==std::vector<AllocationLine>{committed});
            assert(backend.published_slots.empty()&&backend.irq==bool(machine_bit));++transactions;
        }
        {
            // A failed CAS already wrote location9. A fresh overflowing receipt
            // preserves that location; unsuccessful finish preserves result+12/+16.
            auto empty=slot;ga::allocation_word(empty,8,0x1000);
            auto first=line(0x2000,0x6000,0x2800,0x2200,9,0x40);
            auto failed=first;ga::allocation_word(failed,12,0x2280);
            auto overflow=line(0x2000,0x5000,0x2100,0x20e0,77,0x40);
            auto committed=overflow;ga::allocation_word(committed,20,0xc0);
            auto expected=record();ga::allocation_word(expected,0,0);ga::allocation_word(expected,4,0x3000);
            ga::allocation_word(expected,8,9);expected[20]=0;
            ga::AllocationTransaction transaction({0x8000,128,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,empty,15),
                get_line(Op::global,0x8000,first,104),put_line(Op::conditional,0x8000,failed,104,1),
                get_line(Op::global,0x8000,overflow,105),put_line(Op::conditional,0x8000,committed,105)};
            drive(transaction,backend,false,expected,"MDSFGFFPGFFP"+restore,128);
            assert(transaction.global_line()==committed&&backend.irq==bool(machine_bit));++transactions;
        }
        {
            // Ring blocked, global overflow counter commits, blocking ring still
            // blocked, actual25-read delay, then a genuinely fresh consumer GET.
            auto global=line(0x8000,0x5000,0x8100,0x80f0,9,0x100);
            auto overflow=global;ga::allocation_word(overflow,20,0x180);
            ga::AllocationTransaction transaction({0x8000,128,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,slot,16),consumer(0x1140),
                get_line(Op::global,0x8000,global,106),put_line(Op::conditional,0x8000,overflow,106),
                consumer(0x1140),Step{Op::delay},consumer(0x1100),
                put_line(Op::unconditional,0x8080,reserved,16)};
            drive(transaction,backend,true,success_record(0x1100,0x9100,7,0x1180,0x5000),"MDSFCFGFFPCFWCFFU"+restore,128);
            assert(backend.published_globals==std::vector<AllocationLine>{overflow});
            assert(backend.published_slots==std::vector<AllocationLine>{reserved});
            assert(transaction.slot_line()==reserved&&transaction.global_line()==overflow&&backend.irq==bool(machine_bit));
            ++transactions;
        }
        {
            // Capacity rejection performs no consumer GET. After global
            // overflow the blocking attempt rejects too, but the unchanged
            // slot still gets its unconditional publication before false.
            auto small=slot;ga::allocation_word(small,8,0x1080);
            auto global=line(0x8000,0x5000,0x8100,0x80f0,9,0x100);
            auto overflow=global;ga::allocation_word(overflow,20,0x180);
            auto expected=record();ga::allocation_word(expected,0,0);
            ga::allocation_word(expected,4,0xffffd000u);expected[20]=0;
            ga::AllocationTransaction transaction({0x8000,128,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,small,18),
                get_line(Op::global,0x8000,global,107),put_line(Op::conditional,0x8000,overflow,107),
                put_line(Op::unconditional,0x8080,small,18)};
            drive(transaction,backend,false,expected,"MDSFGFFPFU"+restore,128);
            assert(backend.published_globals==std::vector<AllocationLine>{overflow});
            assert(backend.published_slots==std::vector<AllocationLine>{small});
            assert(transaction.slot_line()==small&&backend.irq==bool(machine_bit));++transactions;
        }
        {
            // Global allocation at address0 is numerically in range and its
            // cursor/location writes commit. The zero start is nevertheless
            // the returned failure sentinel; continuation fields stay opaque.
            auto empty=slot;ga::allocation_word(empty,8,0x1000);
            auto global=line(0,0x7000,0x1000,0,23,0x20);
            auto committed=global;ga::allocation_word(committed,12,0x80);
            auto expected=record();ga::allocation_word(expected,0,0);
            ga::allocation_word(expected,4,0x7000);ga::allocation_word(expected,8,23);expected[20]=0;
            ga::AllocationTransaction transaction({0x8000,128,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,empty,19),
                get_line(Op::global,0x8000,global,108),put_line(Op::conditional,0x8000,committed,108)};
            drive(transaction,backend,false,expected,"MDSFGFFP"+restore,128);
            assert(backend.published_globals==std::vector<AllocationLine>{committed});
            assert(backend.published_slots.empty()&&transaction.global_line()==committed);
            assert(backend.irq==bool(machine_bit));++transactions;
        }
        {
            // Request rounding is u32 too:0xffffff81 rounds to0. The protocol
            // still reads the consumer and publishes an unchanged slot line.
            ga::AllocationTransaction transaction({0x8000,0xffffff81u,0,20},record());
            Backend backend;backend.pending_once=pending;backend.irq=initial_irq;
            backend.script={machine(machine_bit),get_line(Op::slot,0x8080,slot,17),consumer(0x1100),
                put_line(Op::unconditional,0x8080,slot,17)};
            drive(transaction,backend,true,success_record(0x1100,0x9100,7,0x1100,0x5000),"MDSFCFFU"+restore,0);
            assert(transaction.slot_line()==slot&&backend.irq==bool(machine_bit));++transactions;
        }
    }
    std::printf("Native allocation: %u directed ring/boundary/u32 cases and %u full-record "
        "transaction/receipt/retry/pending/IRQ cases pass.\n",ring_cases,transactions);
    return 0;
}
