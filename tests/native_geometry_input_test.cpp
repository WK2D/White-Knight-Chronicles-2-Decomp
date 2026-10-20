// Project-owned synthetic descriptor/input fixtures. No platform SDK, binary,
// capture, generated implementation or legacy backing store is used here.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_geometry_input.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
namespace gi=wkc::geometry::input;
namespace gp=wkc::geometry::pipeline;
using Byte=std::uint8_t;
using wkc::geometry::Float4;

void put16(std::vector<Byte>& bytes,unsigned offset,unsigned value) {
    bytes[offset]=Byte(value>>8);bytes[offset+1]=Byte(value);
}
void put32(std::vector<Byte>& bytes,unsigned offset,std::uint32_t value) {
    bytes[offset]=Byte(value>>24);bytes[offset+1]=Byte(value>>16);
    bytes[offset+2]=Byte(value>>8);bytes[offset+3]=Byte(value);
}
void put_float(std::vector<Byte>& bytes,unsigned offset,float value) {
    put32(bytes,offset,std::bit_cast<std::uint32_t>(value));
}
std::array<std::uint32_t,4> bits(const Float4& values) {
    return {std::bit_cast<std::uint32_t>(values[0]),std::bit_cast<std::uint32_t>(values[1]),
            std::bit_cast<std::uint32_t>(values[2]),std::bit_cast<std::uint32_t>(values[3])};
}

// Serialize every public output field rather than compare struct padding or
// use floating equality (raw literal rows can deliberately contain NaNs).
std::vector<std::uint32_t> image(const gi::AdaptedJob& job) {
    std::vector<std::uint32_t> out;
    const auto add=[&](auto value){out.push_back(std::uint32_t(value));};
    const auto vector=[&](const Float4& value){for(auto word:bits(value))add(word);};
    const auto bytes=[&](const auto& value){add(value.size());for(auto byte:value)add(byte);};
    const auto& g=job.geometry;const auto& m=job.metadata;
    add(g.decode.format);add(g.decode.vertices);for(auto value:g.decode.position_bias)add(value);
    add(g.export_format);add(g.index_count);add(g.suppress_directions);add(g.skinning);
    for(const auto& row:g.projection.model.rows)vector(row);
    for(const auto& row:g.projection.projection)vector(row);
    vector(g.projection.viewport_scale);vector(g.projection.viewport_offset);
    vector(g.projection.clip_minimum);vector(g.projection.clip_maximum);add(g.projection.precision_state);
    add(g.filter.winding);add(g.filter.flip_winding);add(g.filter.subpixel);
    bytes(g.packed_vertices);bytes(g.packed_indices);bytes(g.subpixel_padding_indices);
    add(g.influences.size());for(const auto& influence:g.influences) {
        for(auto weight:influence.weights)add(weight);for(auto joint:influence.joints)add(joint);
    }
    add(g.palette.size());for(const auto& matrix:g.palette)for(const auto& row:matrix.rows)vector(row);
    add(g.palette_bindings.size());for(const auto& matrix:g.palette_bindings)for(const auto& row:matrix) {
        add(row.source);add(row.vertex);vector(row.literal);
    }
    bytes(m.original_header);for(const auto& slot:m.slots) {
        add(slot.descriptor_word);add(slot.effective_source);add(slot.staged_begin);add(slot.declared_bytes);add(slot.staged);
    }
    add(m.allocator_control);add(m.publication_destination_high);add(m.publication_destination);
    add(m.request_flags);add(m.allocator_slot);add(m.packet_quadwords);add(m.dma_tag);
    add(m.declared_scratch_bytes);add(m.palette_count);return out;
}
std::vector<std::uint32_t> image(const gi::ConstructorPlan& plan) {
    const auto& p=plan.platform;const auto& c=p.compatibility;
    std::vector<std::uint32_t> out{unsigned(p.root),unsigned(p.scheduler),p.image_id,p.staged_slot_count,
        p.dma_tag,p.allocator_slot,c.io_begin,c.scratch_begin,c.save_frame_begin,c.save_frame_end,
        unsigned(c.preserved_zero),c.preserved_zero_mask,plan.region_end};
    out.insert(out.end(),plan.slot_begins.begin(),plan.slot_begins.end());
    out.push_back(unsigned(c.zero_ranges.size()));
    for(const auto& zero:c.zero_ranges)out.insert(out.end(),{zero.begin,zero.bytes,unsigned(zero.provenance)});
    return out;
}

struct Fixture {
    std::vector<Byte> descriptor=std::vector<Byte>(220);
    std::array<std::vector<Byte>,19> slots;
    gi::ConstructorContract contract;
    gi::ConstructorPlan plan;

    explicit Fixture(unsigned format=32,unsigned count=4,bool suppress=false) {
        // Known geometry: binary2KiB, input1KiB, output512B, explicit4KiB
        // stack. The format/count determine only declared native scratch views.
        put16(descriptor,8,0x80);put16(descriptor,10,152);
        put32(descriptor,0x14,0x400);put32(descriptor,0x18,0x200);
        put16(descriptor,0x1c,0x100);put16(descriptor,0x1e,(format==32?3:4)*count);
        put32(descriptor,204,0x12340000);put32(descriptor,212,0x9084);
        put32(descriptor,216,suppress?0x30000u:0x3000cu);
        slots[1].resize(16);for(unsigned i=0;i<16;++i)slots[1][i]=Byte(0x40+i);
        std::vector<Byte> palette(96);
        for(unsigned i=0;i<24;++i)put_float(palette,i*4,float(i+1));
        // The first matrix straddles the two declared palette source blocks.
        slots[3].assign(palette.begin(),palette.begin()+32);
        slots[4].assign(palette.begin()+32,palette.end());
        slots[5].resize(count*8);
        for(unsigned vertex=0;vertex<count;++vertex) {
            const std::array<Byte,8> influence{255,0,0,1,17,0,42,1};
            std::copy(influence.begin(),influence.end(),slots[5].begin()+vertex*8);
        }
        slots[8].resize(count*(format==32?9:12));
        for(unsigned i=0;i<slots[8].size();++i)slots[8][i]=Byte(0xa0+i);
        slots[14].resize(128);put16(slots[14],0,65530);put16(slots[14],2,10);
        put16(slots[14],4,20);put16(slots[14],6,30);
        put_float(slots[14],8,0.75f);put_float(slots[14],12,-0.25f);
        for(unsigned i=0;i<16;++i)put_float(slots[14],16+i*4,float(i)+0.25f);
        for(unsigned i=0;i<4;++i) {
            put_float(slots[14],0x50+i*4,float(2+2*i));
            put_float(slots[14],0x60+i*4,-float(i+1));
        }
        slots[14][0x70]=3;
        slots[15].resize(48);for(unsigned i=0;i<12;++i)put_float(slots[15],i*4,-float(i+1));
        slots[16]={Byte(format==32?2:3),7,Byte(format),Byte(format),0,0x22,0,0,
                   Byte(count>>8),Byte(count),0,3,0xff,0xff,0xff,0xff};
        slots[17].resize(32);put32(slots[17],0,0xfffffff9);put32(slots[17],4,0x10203);
        put32(slots[17],8,0x80000000);
        for(unsigned offset:{16u,20u,24u})put32(slots[17],offset,4);
        put32(slots[17],28,0xdeadbeef);
        contract.root=gi::RootLayout::image19_custom_geometry_v1;contract.image_id=19;
        contract.dma_tag=31;contract.allocator_slot=5;
        contract.initialized_untouched_workspace=true;contract.preserved_zero_cell_mask=7;
        describe();replan();
    }
    void describe() {
        for(unsigned i=0;i<19;++i) {
            put32(descriptor,48+i*8,0xa5000000u|std::uint32_t(slots[i].size()));
            put32(descriptor,52+i*8,slots[i].empty()?0u:0x200000u+i*0x1000u);
        }
    }
    void replan() {
        assert(gi::plan_spurs_constructor(descriptor,slots[16],contract,plan)==gi::ConstructorStatus::ready);
    }
    gi::StagedInputs staged()const {
        gi::StagedInputs result;result.descriptor=descriptor;
        for(unsigned i=0;i<19;++i)result.slots[i]={slots[i],plan.slot_begins[i]};
        return result;
    }
    void joint(unsigned vertex,unsigned lane,Byte joint) {slots[5][vertex*8+lane*2+1]=joint;}
};

gi::Status adapt(const Fixture& fixture,gi::AdaptedJob& output) {
    return gi::make_input(fixture.staged(),fixture.plan.platform,[](float a,float b){return a>b;},output);
}
void check_immutable(const Fixture& fixture,const std::vector<Byte>& descriptor,
                     const std::array<std::vector<Byte>,19>& slots,const std::vector<std::uint32_t>& plan) {
    assert(fixture.descriptor==descriptor&&fixture.slots==slots&&image(fixture.plan)==plan);
}
}

int wkc2_test_native_geometry_input() {
    unsigned successes=0,declines=0,constructor_declines=0;
    Fixture base;gi::AdaptedJob accepted;
    const auto saved_descriptor=base.descriptor;const auto saved_slots=base.slots;const auto saved_plan=image(base.plan);
    unsigned comparisons=0;
    assert(gi::make_input(base.staged(),base.plan.platform,[&](float a,float b) {
        ++comparisons;assert(a==0.75f&&b==-0.25f);return true;
    },accepted)==gi::Status::ready);
    assert(comparisons==1);check_immutable(base,saved_descriptor,saved_slots,saved_plan);++successes;
    {
        // Independently specified expected coordinates for the authored layout.
        constexpr std::array<std::uint32_t,19> expected{0x800,0x800,0x810,0x810,0x830,0x870,
            0x890,0x890,0x890,0x8c0,0x8c0,0x8c0,0x8c0,0x8c0,0x8c0,0x940,0x970,0x980,0x9a0};
        assert(base.plan.slot_begins==expected&&base.plan.region_end==0x2400);
        const auto& c=base.plan.platform.compatibility;
        assert(c.io_begin==0x800&&c.scratch_begin==0x1000&&c.save_frame_begin==0x17d0&&c.save_frame_end==0x2080);
        assert(c.zero_ranges.size()==3);
        assert(c.zero_ranges[0].begin==0x8b4&&c.zero_ranges[0].bytes==12);
        assert(c.zero_ranges[1].begin==0x9a0&&c.zero_ranges[1].bytes==0x660);
        assert(c.zero_ranges[2].begin==0x1080&&c.zero_ranges[2].bytes==0x750);
        assert(c.preserved_zero_mask==7&&base.plan.platform.allocator_slot==5&&base.plan.platform.dma_tag==31);
        const auto& m=accepted.metadata;const auto& g=accepted.geometry;
        assert(m.allocator_control==0x12340000&&m.publication_destination_high==0&&m.publication_destination==0x9084);
        assert(m.request_flags==0x3000c&&m.allocator_slot==5&&m.packet_quadwords==7&&m.dma_tag==31);
        assert(m.declared_scratch_bytes==192&&m.palette_count==2&&m.original_header[5]==0x22);
        for(unsigned i=0;i<19;++i) {
            assert(m.slots[i].staged_begin==expected[i]&&m.slots[i].declared_bytes==base.slots[i].size());
            assert(m.slots[i].descriptor_word==(0xa5000000u|base.slots[i].size()));
            assert(m.slots[i].staged==!base.slots[i].empty());
            assert(m.slots[i].effective_source==(base.slots[i].empty()?0u:0x200000u+i*0x1000u));
        }
        assert(g.decode.format==wkc::geometry::PackedVertexFormat::position_direction&&g.decode.vertices==4);
        assert((g.decode.position_bias==std::array<std::int32_t,3>{-7,0x10203,(std::numeric_limits<std::int32_t>::min)()}));
        assert(g.index_count==3&&g.export_format==wkc::geometry::VertexPackFormat::position_normal);
        assert(!g.suppress_directions&&g.skinning==gp::SkinningMode::linear_blend_variant3);
        assert(unsigned(g.filter.winding)==3&&g.filter.flip_winding&&!g.filter.subpixel);
        assert((g.projection.clip_minimum==Float4{65530,10,-0.25f,0}));
        assert((g.projection.clip_maximum==Float4{14,40,0.75f,0}));
        assert((g.projection.viewport_scale==Float4{2,4,6,8}));
        assert((g.projection.viewport_offset==Float4{-1,-2,-3,-4}));
        assert(g.projection.precision_state==3&&g.packed_vertices==base.slots[8]&&g.packed_indices==base.slots[1]);
        for(unsigned matrix=0;matrix<2;++matrix)for(unsigned row=0;row<3;++row)for(unsigned lane=0;lane<4;++lane)
            assert(g.palette[matrix].rows[row][lane]==float(matrix*12+row*4+lane+1));
        for(unsigned row=0;row<3;++row)for(unsigned lane=0;lane<4;++lane)
            assert(g.projection.model.rows[row][lane]==-float(row*4+lane+1));
        for(unsigned row=0;row<4;++row)for(unsigned lane=0;lane<4;++lane)
            assert(g.projection.projection[row][lane]==float(row*4+lane)+0.25f);
        for(const auto& influence:g.influences) {
            assert((influence.weights==std::array<Byte,4>{255,0,17,42}));
            assert((influence.joints==std::array<Byte,4>{0,1,0,1}));
        }
    }
    const auto reject=[&](const gi::StagedInputs& staged,const gi::PlatformFacts& platform,gi::Status expected) {
        auto output=accepted;const auto before=image(output);
        const auto* vertices=output.geometry.packed_vertices.data();const auto* bindings=output.geometry.palette_bindings.data();
        assert(gi::make_input(staged,platform,[](float a,float b){return a>b;},output)==expected);
        assert(image(output)==before&&output.geometry.packed_vertices.data()==vertices&&output.geometry.palette_bindings.data()==bindings);
        ++declines;
    };
    const auto rejected_fixture=[&](const Fixture& fixture,gi::Status status){reject(fixture.staged(),fixture.plan.platform,status);};
    const auto constructor_reject=[&](const std::vector<Byte>& descriptor,std::span<const Byte> header,
                                    const gi::ConstructorContract& contract,gi::ConstructorStatus expected) {
        auto output=base.plan;const auto before=image(output);const auto* zeros=output.platform.compatibility.zero_ranges.data();
        assert(gi::plan_spurs_constructor(descriptor,header,contract,output)==expected);
        assert(image(output)==before&&output.platform.compatibility.zero_ranges.data()==zeros);++constructor_declines;
    };
    for(unsigned format:{32u,33u})for(bool suppress:{false,true}) {
        Fixture fixture(format,4,suppress);gi::AdaptedJob output;
        assert(adapt(fixture,output)==gi::Status::ready);
        assert(output.geometry.suppress_directions==suppress&&unsigned(output.geometry.export_format)==format);
        assert(output.metadata.declared_scratch_bytes==(format==32?192u:256u));
        assert(fixture.plan.platform.compatibility.save_frame_begin==(suppress?0x1ac0u:0x17d0u));++successes;
    }
    {
        Fixture fixture;put16(fixture.descriptor,0x1c,0);fixture.replan();
        assert(fixture.plan.region_end==0x3400&&fixture.plan.platform.compatibility.save_frame_begin==0x27d0);
        auto contract=fixture.contract;contract.initialized_untouched_workspace=false;contract.preserved_zero_cell_mask=0;
        gi::ConstructorPlan without_proof;
        assert(gi::plan_spurs_constructor(fixture.descriptor,fixture.slots[16],contract,without_proof)==gi::ConstructorStatus::ready);
        assert(without_proof.platform.compatibility.zero_ranges.empty()&&!without_proof.platform.compatibility.preserved_zero_mask);
        ++successes;
    }
    {
        // The producer may disappear immediately after construction: all raw
        // vectors, typed rows and metadata belong to the returned job.
        auto fixture=base;gi::AdaptedJob output;assert(adapt(fixture,output)==gi::Status::ready);
        const auto before=image(output);
        fixture.descriptor.clear();for(auto& slot:fixture.slots)slot.assign(1,0xee);
        assert(image(output)==before);++successes;
    }
    {
        // Both supported adjacency continuations in a single authored input.
        Fixture fixture;fixture.slots[5].resize(16);fixture.slots[8].resize(32);
        constexpr std::array<Byte,16> continuation{1,1,2,0,3,1,4,0,5,0,6,1,7,0,8,1};
        std::copy(continuation.begin(),continuation.end(),fixture.slots[8].begin());
        fixture.describe();fixture.replan();gi::AdaptedJob output;
        assert(adapt(fixture,output)==gi::Status::ready&&output.geometry.packed_vertices.size()==36);
        assert((output.geometry.influences[2].weights==std::array<Byte,4>{1,2,3,4}));
        assert((output.geometry.influences[2].joints==std::array<Byte,4>{1,0,1,0}));
        assert((output.geometry.influences[3].weights==std::array<Byte,4>{5,6,7,8}));
        assert((std::array<Byte,4>{output.geometry.packed_vertices[32],output.geometry.packed_vertices[33],
            output.geometry.packed_vertices[34],output.geometry.packed_vertices[35]}==std::array<Byte,4>{0xff,0xfa,0,10}));
        ++successes;
    }
    {
        Fixture fixture;fixture.joint(0,0,2);
        constexpr std::array<std::uint32_t,4> raw{0x80000000,0x7fc12345,0xff800001,0x3f800000};
        for(unsigned i=0;i<4;++i)put32(fixture.slots[8],i*4,raw[i]);
        gi::AdaptedJob output;assert(adapt(fixture,output)==gi::Status::ready);
        const auto& binding=output.geometry.palette_bindings[2];
        assert(binding[0].source==gp::PaletteRowSource::literal&&binding[2].source==gp::PaletteRowSource::literal);
        assert((bits(binding[0].literal)==std::array<std::uint32_t,4>{0xff020001,0x11002a01,0xff000001,0x11002a01}));
        assert(bits(binding[2].literal)==raw);++successes;
    }
    {
        // Joint7 spans model's last row, the rewritten header and decode bias.
        Fixture fixture;fixture.joint(0,0,7);const auto header=fixture.slots[16];
        gi::AdaptedJob output;assert(adapt(fixture,output)==gi::Status::ready);
        assert((bits(output.geometry.palette_bindings[7][1].literal)==
            std::array<std::uint32_t,4>{0x02072020,0x00230000,0x00040003,0xffffffff}));
        assert((bits(output.geometry.palette_bindings[7][2].literal)==
            std::array<std::uint32_t,4>{0xfffffff9,0x00010203,0x80000000,0}));
        assert(fixture.slots[16]==header&&output.metadata.original_header[5]==0x22);++successes;
    }
    {
        Fixture fixture;fixture.joint(0,0,9);gi::AdaptedJob output;
        assert(adapt(fixture,output)==gi::Status::ready);
        for(const auto& row:output.geometry.palette_bindings[9])
            assert(row.source==gp::PaletteRowSource::literal&&(bits(row.literal)==std::array<std::uint32_t,4>{}));
        fixture.plan.platform.compatibility.zero_ranges.clear();
        rejected_fixture(fixture,gi::Status::unrecovered_palette_row);++successes;
    }
    {
        // Removing the index block moves palette's phase so joint97 selects
        // exactly the three preserved-zero cells, with no neighboring row.
        Fixture fixture;fixture.slots[1].clear();put16(fixture.slots[16],10,0);
        fixture.joint(0,0,97);fixture.describe();fixture.replan();gi::AdaptedJob output;
        assert(adapt(fixture,output)==gi::Status::ready&&output.geometry.index_count==0);
        assert(fixture.plan.slot_begins[3]==0x800);
        for(const auto& row:output.geometry.palette_bindings[97])
            assert(row.source==gp::PaletteRowSource::literal&&(bits(row.literal)==std::array<std::uint32_t,4>{}));
        fixture.plan.platform.compatibility.preserved_zero_mask=5;
        rejected_fixture(fixture,gi::Status::unrecovered_palette_row);
        fixture.plan.platform.compatibility.preserved_zero_mask=7;
        fixture.plan.platform.compatibility.preserved_zero=gi::PreservedZeroContract::unavailable;
        rejected_fixture(fixture,gi::Status::unrecovered_palette_row);++successes;
    }
    {
        Fixture fixture;fixture.joint(0,0,43);gi::AdaptedJob output;
        assert(adapt(fixture,output)==gi::Status::ready);
        const auto& rows=output.geometry.palette_bindings[43];
        assert(rows[0].source==gp::PaletteRowSource::position&&rows[0].vertex==2);
        assert(rows[1].source==gp::PaletteRowSource::position&&rows[1].vertex==3);
        assert(rows[2].source==gp::PaletteRowSource::direction0&&rows[2].vertex==0);++successes;
        Fixture tangent(33);tangent.joint(0,0,45);
        assert(adapt(tangent,output)==gi::Status::ready);
        for(unsigned row=0;row<3;++row)
            assert(output.geometry.palette_bindings[45][row].source==gp::PaletteRowSource::direction1&&
                   output.geometry.palette_bindings[45][row].vertex==row);
        ++successes;
        Fixture bootstrap(32,8);bootstrap.joint(4,0,45);
        rejected_fixture(bootstrap,gi::Status::bootstrap_direction_alias);
    }
    {
        Fixture fixture;fixture.slots[16][5]=0x30;gi::AdaptedJob output;
        assert(adapt(fixture,output)==gi::Status::ready&&!output.geometry.filter.flip_winding&&
            output.geometry.skinning==gp::SkinningMode::disabled);++successes;
        unsigned called=0;
        assert(gi::make_input(base.staged(),base.plan.platform,[&](float,float){++called;return false;},output)==gi::Status::ready);
        assert(called==1&&output.geometry.projection.clip_minimum[2]==0.75f&&
            output.geometry.projection.clip_maximum[2]==-0.25f);++successes;
    }
    // Unsupported layout and ownership cases must leave the complete prior
    // output, including vector storage identities, unchanged.
    for(unsigned fault=0;fault<27;++fault) {
        auto fixture=base;auto staged=fixture.staged();auto platform=fixture.plan.platform;
        auto expected=gi::Status::unclassified_header;
        switch(fault) {
        case 0:platform.root=gi::RootLayout::unavailable;expected=gi::Status::unsupported_platform;break;
        case 1:platform.scheduler=gi::SchedulerMode::unavailable;expected=gi::Status::unsupported_platform;break;
        case 2:platform.image_id=18;expected=gi::Status::unsupported_platform;break;
        case 3:staged.descriptor=staged.descriptor.first(219);expected=gi::Status::short_descriptor;break;
        case 4:put16(fixture.descriptor,10,144);expected=gi::Status::nineteen_slot_layout;break;
        case 5:platform.staged_slot_count=18;expected=gi::Status::nineteen_slot_layout;break;
        case 6:platform.dma_tag=32;expected=gi::Status::invalid_publication;break;
        case 7:put32(fixture.descriptor,208,1);expected=gi::Status::invalid_publication;break;
        case 8:put32(fixture.descriptor,212,0x9085);expected=gi::Status::invalid_publication;break;
        case 9:put32(fixture.descriptor,52+8,0x201001);expected=gi::Status::staging_alignment_or_range;break;
        case 10:platform.compatibility.io_begin=0x40000;expected=gi::Status::staging_alignment_or_range;break;
        case 11:staged.slots[1].begin+=16;expected=gi::Status::staged_payload_mismatch;break;
        case 12:staged.slots[1].bytes=staged.slots[1].bytes.first(15);expected=gi::Status::staged_payload_mismatch;break;
        case 13:staged.slots[0].bytes=staged.slots[1].bytes;expected=gi::Status::staged_payload_mismatch;break;
        case 14:fixture.slots[16][2]=34;break;
        case 15:fixture.slots[16][3]=33;break;
        case 16:fixture.slots[16][0]=3;break;
        case 17:fixture.slots[16][4]=1;break;
        case 18:fixture.slots[16][7]=1;break;
        case 19:fixture.slots[16][12]=0;break;
        case 20:fixture.slots[16][5]=0x12;break;
        case 21:put16(fixture.slots[16],8,0);expected=gi::Status::logical_group_or_count;break;
        case 22:put16(fixture.slots[16],8,6);expected=gi::Status::logical_group_or_count;break;
        case 23:put16(fixture.slots[16],10,4);expected=gi::Status::logical_group_or_count;break;
        case 24:put32(fixture.slots[17],20,8);expected=gi::Status::position_conversion_layout;break;
        case 25:platform.compatibility.save_frame_begin+=16;expected=gi::Status::scratch_or_frame_layout;break;
        case 26:fixture.slots[14][0x70]=5;expected=gi::Status::unsupported_subpixel_padding;break;
        }
        reject(staged,platform,expected);
    }
    for(unsigned fault=0;fault<5;++fault) {
        Fixture fixture;auto expected=gi::Status::invalid_zero_contract;
        switch(fault) {
        case 0:fixture.plan.platform.compatibility.zero_ranges[0].provenance=gi::ZeroProvenance::unavailable;break;
        case 1:fixture.plan.platform.compatibility.zero_ranges[0].bytes=0;break;
        case 2:fixture.plan.platform.compatibility.zero_ranges[0].begin=0x7f0;break;
        case 3:fixture.plan.platform.compatibility.preserved_zero_mask=8;break;
        case 4:fixture.plan.platform.compatibility.zero_ranges.resize(65);break;
        }
        rejected_fixture(fixture,expected);
    }
    for(unsigned fault=0;fault<6;++fault) {
        Fixture fixture;auto expected=gi::Status::partial_palette;
        switch(fault) {
        case 0:fixture.slots[3].resize(31);break;
        case 1:fixture.slots[14].resize(127);expected=gi::Status::fixed_block_sizes;break;
        case 2:fixture.slots[0].resize(16);expected=gi::Status::unclassified_active_slot;break;
        case 3:fixture.slots[8].resize(31);expected=gi::Status::unowned_vertex_prefix;break;
        case 4:fixture.slots[5].resize(15);expected=gi::Status::unowned_influence_prefix;break;
        case 5:fixture=Fixture(32,32);fixture.slots[8].resize(16);
               expected=gi::Status::unowned_vertex_prefix;break;
        }
        fixture.describe();fixture.replan();rejected_fixture(fixture,expected);
    }
    {
        auto output=accepted;const auto before=image(output);const auto* storage=output.geometry.packed_vertices.data();
        bool threw=false;
        try {gi::make_input(base.staged(),base.plan.platform,[](float,float)->bool {
            throw std::runtime_error("authored depth policy failed");
        },output);}catch(const std::runtime_error&){threw=true;}
        assert(threw&&image(output)==before&&output.geometry.packed_vertices.data()==storage);
        check_immutable(base,saved_descriptor,saved_slots,saved_plan);
    }
    for(unsigned fault=0;fault<17;++fault) {
        auto fixture=base;auto descriptor=fixture.descriptor;auto header=fixture.slots[16];auto contract=fixture.contract;
        auto expected=gi::ConstructorStatus::constructor_range_or_overlap;
        switch(fault) {
        case 0:contract.root=gi::RootLayout::unavailable;expected=gi::ConstructorStatus::unsupported_contract;break;
        case 1:contract.image_id=20;expected=gi::ConstructorStatus::unsupported_contract;break;
        case 2:contract.dma_tag=32;expected=gi::ConstructorStatus::unsupported_contract;break;
        case 3:contract.preserved_zero_cell_mask=8;expected=gi::ConstructorStatus::unsupported_contract;break;
        case 4:descriptor.resize(219);expected=gi::ConstructorStatus::short_inputs;break;
        case 5:header.resize(15);expected=gi::ConstructorStatus::short_inputs;break;
        case 6:put16(descriptor,10,144);expected=gi::ConstructorStatus::unsupported_descriptor;break;
        case 7:header[2]=34;expected=gi::ConstructorStatus::unsupported_header;break;
        case 8:put16(header,8,0);expected=gi::ConstructorStatus::unsupported_header;break;
        case 9:put16(header,8,6);expected=gi::ConstructorStatus::unsupported_header;break;
        case 10:put16(descriptor,8,0);break;
        case 11:put16(descriptor,8,0xffff);break;
        case 12:put32(descriptor,0x14,0xffffffff);break;
        case 13:put32(descriptor,0x18,0xffffffff);break;
        case 14:put16(descriptor,0x1e,0xffff);break;
        case 15:put32(descriptor,0x14,0);put32(descriptor,0x18,0);break;
        case 16:put32(descriptor,52+8,0x201001);break;
        }
        constructor_reject(descriptor,header,contract,expected);
    }
    for(unsigned offset:{0u,0x0cu,0x20u,0x24u}) {
        Fixture fixture;put32(fixture.descriptor,offset,1);
        rejected_fixture(fixture,gi::Status::unsupported_descriptor);
        constructor_reject(fixture.descriptor,fixture.slots[16],fixture.contract,
                           gi::ConstructorStatus::unsupported_descriptor);
    }
    std::printf("Native geometry inputs: %u authored successes, %u unchanged adapter declines, %u unchanged constructor declines; owned source bytes, provenance, aliases, prefixes and policy exception pass.\n",
        successes,declines,constructor_declines);
    return 0;
}
