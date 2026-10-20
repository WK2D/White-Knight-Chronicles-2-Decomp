// Decode complete packets independently and check draw coverage/bindings for
// every destination phase. Only project-owned synthetic values are used.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_geometry_commands.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <map>

namespace {
namespace gc=wkc::geometry::commands;
using wkc::geometry::VertexPackFormat;
unsigned word(const std::vector<std::uint8_t>& bytes,std::size_t offset) {
    return (unsigned(bytes[offset])<<24)|(unsigned(bytes[offset+1])<<16)|
           (unsigned(bytes[offset+2])<<8)|bytes[offset+3];
}
struct Backend {
    const gc::CommandPublication& packet;
    bool enabled;
    unsigned reads=0,disables=0,puts=0,restores=0,fences=0;
    void architectural_fence()noexcept{assert(!reads&&!fences);++fences;}
    bool interrupts_enabled()noexcept{assert(fences==1);++reads;return enabled;}
    void set_interrupts_enabled(bool value)noexcept {
        if(value){assert(disables==1&&puts==1);++restores;}
        else {assert(reads==1&&!puts);++disables;}
        enabled=value;
    }
    void put_fenced(std::span<const std::uint8_t> bytes,unsigned destination,unsigned tag)noexcept {
        assert(!enabled&&disables==1&&!puts&&bytes.size()==packet.bytes.size());
        assert(destination==packet.destination&&tag==packet.dma_tag);
        assert(std::equal(bytes.begin(),bytes.end(),packet.bytes.begin()));++puts;
    }
};
void decode_and_check(const gc::PublicationSpec& spec,const gc::GeometryAllocation& allocation,
                      const gc::CommandPublication& packet) {
    assert(packet.publish&&packet.destination==spec.destination&&packet.dma_tag==spec.dma_tag);
    assert(packet.bytes.size()==unsigned(spec.packet_quadwords)*16);
    std::map<unsigned,std::vector<unsigned>> writes;
    unsigned drawn=0,begin_end=0;
    for(std::size_t cursor=0;cursor<packet.bytes.size();) {
        const auto header=word(packet.bytes,cursor);
        if(!header){cursor+=4;continue;}
        const auto count=(header>>18)&2047u,method=header&65535u;
        const bool repeated=(header&0x40000000u)!=0;
        assert(count&&!(method&3u)&&!(header&0xb0000000u));
        assert(cursor+4*(count+1)<=packet.bytes.size());
        assert(((spec.destination+unsigned(cursor))&127u)+4*(count+1)<=128);
        for(unsigned i=0;i<count;++i) {
            const auto address=method+(repeated?0:i*4),value=word(packet.bytes,cursor+4*(i+1));
            if(address==0x1824) {
                assert(begin_end==1);
                assert((value&0xffffffu)==drawn);
                drawn+=(value>>24)+1;
            } else if(address==0x1808) {
                assert(value==(begin_end?0u:5u));++begin_end;
            } else {
                switch(address) {
                case 0x1710:case 0x1714:case 0x1740:case 0x1680:case 0x1748:
                case 0x1688:case 0x1778:case 0x16b8:case 0x181c:case 0x1820:
                case 0x1d6c:case 0x1d74:break;
                default:assert(false);
                }
                writes[address].push_back(value);
            }
        }
        cursor+=4*(count+1);
    }
    assert(begin_end==2&&drawn==spec.filtered_index_count);
    const unsigned stride=spec.format==VertexPackFormat::position?12:
        spec.format==VertexPackFormat::position_normal?16:24;
    const auto single=[&](unsigned address,unsigned value) {
        assert(writes[address]==std::vector<unsigned>{value});writes.erase(address);
    };
    single(0x1740,stride*256+0x32);
    single(0x1680,allocation.vertex_rsx|(allocation.location<<31));
    if(spec.format!=VertexPackFormat::position&&(spec.request_flags&4)) {
        single(0x1748,stride*256+0x16);
        single(0x1688,(allocation.vertex_rsx+12)|(allocation.location<<31));
    }
    if(spec.format==VertexPackFormat::position_normal_tangent&&(spec.request_flags&0x4000)) {
        single(0x1778,stride*256+0x43);
        single(0x16b8,(allocation.vertex_rsx+16)|(allocation.location<<31));
    }
    single(0x181c,allocation.index_rsx&0x1fffffffu);single(0x1820,allocation.location|16);
    const bool invalidate=allocation.vertex_payload_submitted&&allocation.consumer_ea;
    if(invalidate)single(0x1710,0);
    const unsigned invalidations=invalidate?4:1;
    assert(writes[0x1714]==std::vector<unsigned>(invalidations,0));writes.erase(0x1714);
    if(allocation.consumer_ea) {
        single(0x1d6c,allocation.consumer_ea&0xff0);single(0x1d74,allocation.reserved_end);
    }
    assert(writes.empty());
}
}

int wkc2_test_native_geometry_commands() {
    unsigned cases=0,declines=0,splits=0,nops=0;
    for(unsigned phase=0;phase<128;phase+=4)
    for(auto format:{VertexPackFormat::position,VertexPackFormat::position_normal,VertexPackFormat::position_normal_tangent})
    for(unsigned count:{3u,255u,258u,513u,65535u})for(unsigned location:{0u,1u})
    for(unsigned flags:{0u,4u,0x4000u,0x4004u}) {
        gc::PublicationSpec spec{format,flags,count,0x8000+phase,255,31,true};
        gc::GeometryAllocation allocation{0x120002,0x240000,location,0x303c,0x12345678,true};
        gc::CommandPublication packet;
        assert(gc::build(spec,allocation,packet)==gc::BuildStatus::packet_ready);
        decode_and_check(spec,allocation,packet);++cases;
        splits+=packet.method_splits;nops+=packet.inserted_nops;
    }
    // Hand-authored minimal position-only packet: no invalidation preamble,
    // no consumer release, three indices, first-fit draw count encoded as2.
    gc::PublicationSpec spec{VertexPackFormat::position,0,3,0x8000,4,20,true};
    gc::GeometryAllocation allocation{0x120000,0x240000,0,0,0,false};
    gc::CommandPublication packet;
    assert(gc::build(spec,allocation,packet)==gc::BuildStatus::packet_ready);
    constexpr std::array<unsigned,16> expected{0x41740,0xc32,0x41680,0x240000,
        0x41714,0,0x8181c,0x120000,16,0x41808,5,0x40041824,0x2000000,0x41808,0,0};
    for(unsigned i=0;i<expected.size();++i)assert(word(packet.bytes,i*4)==expected[i]);
    decode_and_check(spec,allocation,packet);++cases;
    // Direct/global payloads also set the submitted marker, but have no ring
    // consumer. Their exact minimal packet must still omit the preamble.
    allocation.vertex_payload_submitted=true;
    gc::CommandPublication submitted_without_consumer;
    assert(gc::build(spec,allocation,submitted_without_consumer)==gc::BuildStatus::packet_ready);
    assert(submitted_without_consumer.bytes==packet.bytes);
    for(unsigned i=0;i<expected.size();++i)assert(word(submitted_without_consumer.bytes,i*4)==expected[i]);
    decode_and_check(spec,allocation,submitted_without_consumer);++cases;
    // The incrementing two-word index binding either splits at phase 120 or
    // moves past a lone header slot at phase 124. Its second method is 0x1820.
    for(unsigned phase:{96u,100u}) {
        auto phased=spec;phased.destination+=phase;
        gc::CommandPublication boundary;
        assert(gc::build(phased,allocation,boundary)==gc::BuildStatus::packet_ready);
        if(phase==96) {
            assert(word(boundary.bytes,24)==0x4181c&&word(boundary.bytes,32)==0x41820);
            assert(boundary.method_splits==1&&boundary.inserted_nops==0);
        } else {
            assert(word(boundary.bytes,24)==0&&word(boundary.bytes,28)==0x8181c);
            assert(boundary.method_splits==0&&boundary.inserted_nops==1);
        }
        decode_and_check(phased,allocation,boundary);++cases;
    }
    for(bool enabled:{false,true}) {
        Backend backend{packet,enabled};assert(gc::submit(packet,backend));
        assert(backend.enabled==enabled&&backend.fences==1&&backend.reads==1&&backend.disables==1&&
               backend.puts==1&&backend.restores==unsigned(enabled));++cases;
    }
    // Empty geometry retains the final zero packet, including zero-quadword
    // header's four-byte publication. No destination means no publication.
    spec.export_geometry=false;
    for(unsigned capacity:{0u,1u,255u}) {
        spec.packet_quadwords=std::uint8_t(capacity);
        assert(gc::build(spec,allocation,packet)==gc::BuildStatus::packet_ready);
        assert(packet.publish&&packet.bytes==std::vector<std::uint8_t>(capacity?capacity*16:4,0));++cases;
    }
    spec.destination=0;
    assert(gc::build(spec,allocation,packet)==gc::BuildStatus::no_publication);
    assert(!packet.publish&&packet.bytes.empty());++cases;
    Backend quiet{packet,true};assert(gc::submit(packet,quiet));
    assert(!quiet.fences&&!quiet.reads&&!quiet.disables&&!quiet.puts&&!quiet.restores&&quiet.enabled);++cases;
    packet.publish=true;packet.destination=0x8001;
    assert(!gc::submit(packet,quiet));assert(!quiet.fences&&!quiet.reads&&!quiet.puts);++declines;
    const auto reject=[&](gc::BuildStatus expected_status) {
        gc::CommandPublication out;out.destination=0xdead;out.bytes={0x12,0x34};out.publish=true;
        out.dma_tag=30;out.method_splits=97;out.inserted_nops=83;
        assert(gc::build(spec,allocation,out)==expected_status);
        assert(out.destination==0xdead&&out.publish&&out.dma_tag==30&&
               out.method_splits==97&&out.inserted_nops==83&&
               (out.bytes==std::vector<std::uint8_t>{0x12,0x34}));++declines;
    };
    spec.destination=0x8001;reject(gc::BuildStatus::invalid_input);
    spec.destination=0x8000;spec.dma_tag=32;reject(gc::BuildStatus::invalid_input);
    spec.dma_tag=20;spec.export_geometry=true;spec.packet_quadwords=0;reject(gc::BuildStatus::invalid_input);
    spec.packet_quadwords=1;reject(gc::BuildStatus::insufficient_packet_capacity);
    spec.packet_quadwords=255;spec.filtered_index_count=4;reject(gc::BuildStatus::invalid_input);
    spec.filtered_index_count=65538;reject(gc::BuildStatus::invalid_input);
    spec.filtered_index_count=3;allocation.location=2;reject(gc::BuildStatus::invalid_input);
    allocation.location=0;spec.format=static_cast<VertexPackFormat>(35);reject(gc::BuildStatus::unsupported_format);
    // Format validation retains precedence over other export errors; a missing
    // destination still bypasses all export validation after alignment/tag.
    spec.filtered_index_count=0;spec.packet_quadwords=0;reject(gc::BuildStatus::unsupported_format);
    spec.destination=0;
    assert(gc::build(spec,allocation,packet)==gc::BuildStatus::no_publication);
    assert(packet.bytes.empty()&&!packet.publish&&!packet.method_splits&&!packet.inserted_nops);++cases;
    // Find the minimum quadword capacity from a complete maximum-draw packet.
    // A nonzero final consumer-release value makes trailing padding unambiguous.
    spec={VertexPackFormat::position_normal_tangent,0x4004,65535,0x807c,255,20,true};
    allocation={0x120002,0x240000,1,0x303c,0x12345678,true};
    gc::CommandPublication full;
    assert(gc::build(spec,allocation,full)==gc::BuildStatus::packet_ready);
    auto used=full.bytes.size();
    while(used&&!word(full.bytes,used-4))used-=4;
    assert(used>=4&&word(full.bytes,used-4)==allocation.reserved_end);
    const auto minimum_quadwords=(used+15)/16;
    assert(minimum_quadwords>1&&minimum_quadwords<=255);
    spec.packet_quadwords=std::uint8_t(minimum_quadwords);
    gc::CommandPublication fitted;
    assert(gc::build(spec,allocation,fitted)==gc::BuildStatus::packet_ready);
    assert(std::equal(fitted.bytes.begin(),fitted.bytes.end(),full.bytes.begin()));
    assert(fitted.method_splits==full.method_splits&&fitted.inserted_nops==full.inserted_nops);
    decode_and_check(spec,allocation,fitted);++cases;
    --spec.packet_quadwords;reject(gc::BuildStatus::insufficient_packet_capacity);
    std::printf("Native geometry commands: %u decoded draw/binding/page/zero cases, %u unchanged declines; %u splits, %u NOPs.\n",cases,declines,splits,nops);
    return 0;
}
