// Project-owned regression values for filtered index payloads and keep decisions.
// No game captures, generated code or reference-produced padding.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_index_payload.h"
#include "native_geometry_filter.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>

using namespace wkc::geometry;
namespace {
unsigned be16(const std::uint8_t* bytes) {
    return unsigned(bytes[0])*256u+bytes[1];
}
std::vector<std::uint16_t> logical_selection(std::span<const std::uint16_t> input,
                                           std::span<const std::uint8_t> keep) {
    std::vector<std::uint16_t> result;
    for(std::size_t triangle=0;triangle<keep.size();++triangle)
        if(keep[triangle])for(unsigned vertex=0;vertex<3;++vertex)
            result.push_back(input[triangle*3+vertex]);
    return result;
}
void check_prefix(std::span<const std::uint8_t> payload,std::uint32_t count,
                  std::span<const std::uint16_t> expected) {
    assert(count==expected.size());
    assert(payload.size()==((expected.size()*2+15)&~std::size_t(15)));
    for(std::size_t index=0;index<expected.size();++index)
        assert(be16(payload.data()+index*2)==expected[index]);
}
std::array<ProjectedVertex,3> projected_triangle() {
    return {ProjectedVertex{0,0,0,0x80000000u},
        ProjectedVertex{4,0,0,0x80200000u},
        ProjectedVertex{0,4,0,0x80000020u}};
}
}

int wkc2_test_native_index_payload() {
    unsigned payload_cases=0,rejected_cases=0,decision_cases=0;
    // Independent logical specification: retain the selected complete triangles
    // in order. Aligned payload size is part of the contract; these tests do not
    // reproduce the implementation's stores to invent an expected padding oracle.
    for(unsigned triangles=1;triangles<=20;++triangles)for(unsigned mask=0;mask<16;++mask)
        for(bool repeated:{false,true}) {
            std::vector<std::uint16_t> input(triangles*3);
            for(unsigned triangle=0;triangle<triangles;++triangle)
                for(unsigned vertex=0;vertex<3;++vertex)
                    input[triangle*3+vertex]=std::uint16_t(repeated?
                        0x4200u+vertex:0x1200u+triangle*3+vertex);
            std::vector<std::uint8_t> keep(triangles);
            for(unsigned triangle=0;triangle<triangles;++triangle)
                keep[triangle]=std::uint8_t((mask>>(triangle%4))&1);
            const auto original_input=input;const auto original_keep=keep;
            const auto expected=logical_selection(input,keep);
            std::vector<std::uint8_t> payload{0xde,0xad,0xbe,0xef};std::uint32_t count=~0u;
            assert(index_payload::reconstruct(input,keep,payload,count));
            assert(input==original_input&&keep==original_keep);
            check_prefix(payload,count,expected);++payload_cases;
        }
    {
        // Five preceding survivors put the final group at byte30 (offset14).
        // Its four survivors occupy bytes30..53. The circular last-line store
        // then exposes eight zero bytes and repeats the first tail word0x1020.
        std::vector<std::uint16_t> input(36);
        for(unsigned i=0;i<input.size();++i)input[i]=std::uint16_t(0x1000u+i);
        const std::array<std::uint8_t,12> keep{1,1,1,1,1,0,0,0,1,1,1,1};
        std::vector<std::uint8_t> payload;std::uint32_t count=0;
        assert(index_payload::reconstruct(input,keep,payload,count));
        check_prefix(payload,count,logical_selection(input,keep));
        assert(count==27&&payload.size()==64);
        const std::array<std::uint8_t,10> expected_tail{0,0,0,0,0,0,0,0,0x10,0x20};
        assert(std::equal(expected_tail.begin(),expected_tail.end(),payload.begin()+54));
        ++payload_cases;
    }
    {
        std::vector<std::uint8_t> payload{1,2,3};std::uint32_t count=91;
        assert(index_payload::reconstruct({}, {},payload,count));
        assert(payload.empty()&&count==0);++payload_cases;
    }
    {
        // Exercise the declared maximum u16 index-count boundary separately
        // from the ordinary small cases, without a mirrored padding model.
        std::vector<std::uint16_t> input(65535);
        std::iota(input.begin(),input.end(),std::uint16_t{0});
        std::vector<std::uint8_t> keep(input.size()/3,1),payload;
        std::uint32_t count=0;
        assert(index_payload::reconstruct(input,keep,payload,count));
        check_prefix(payload,count,input);++payload_cases;
    }
    const auto rejected=[&](std::span<const std::uint16_t> input,
                            std::span<const std::uint8_t> keep) {
        std::vector<std::uint8_t> payload{0x61,0x72,0x83,0x94};
        const auto saved=payload;const auto* allocation=payload.data();
        std::uint32_t count=0x98765432u;
        assert(!index_payload::reconstruct(input,keep,payload,count));
        assert(payload==saved&&payload.data()==allocation&&count==0x98765432u);
        ++rejected_cases;
    };
    {
        const std::array<std::uint16_t,4> incomplete{1,2,3,4};
        const std::array<std::uint16_t,6> input{1,2,3,4,5,6};
        rejected(incomplete,std::array<std::uint8_t,1>{1});
        rejected(input,std::array<std::uint8_t,1>{1});
        rejected(input,std::array<std::uint8_t,3>{1,0,1});
        rejected(input,std::array<std::uint8_t,2>{1,2});
        rejected(input,std::array<std::uint8_t,2>{255,0});
        rejected({},std::array<std::uint8_t,1>{0});
        std::vector<std::uint16_t> excessive(65538);
        std::vector<std::uint8_t> decisions(excessive.size()/3,1);
        rejected(excessive,decisions);
    }
    {
        // Both source views use the old output allocation. Explicit construction
        // starts the u16 objects in its byte storage; the helper must finish all
        // source reads before replacing that allocation with the new payload.
        const std::array<std::uint16_t,9> input{0x1112,0x2122,0x3132,
            0x4142,0x5152,0x6162,0x7172,0x8182,0x9192};
        const std::array<std::uint8_t,3> keep{1,0,1};
        std::vector<std::uint8_t> payload(input.size()*2+keep.size());
        assert(reinterpret_cast<std::uintptr_t>(payload.data())%alignof(std::uint16_t)==0);
        auto* words=reinterpret_cast<std::uint16_t*>(payload.data());
        for(unsigned i=0;i<input.size();++i)
            std::construct_at(reinterpret_cast<std::uint16_t*>(payload.data()+i*2),input[i]);
        std::copy(keep.begin(),keep.end(),payload.begin()+input.size()*2);
        const std::span<const std::uint16_t> input_view(words,input.size());
        const std::span<const std::uint8_t> keep_view(payload.data()+input.size()*2,keep.size());
        std::uint32_t count=0;
        assert(index_payload::reconstruct(input_view,keep_view,payload,count));
        check_prefix(payload,count,logical_selection(input,keep));++payload_cases;
    }
    {
        const auto vertices=projected_triangle();
        const std::array<std::uint16_t,12> input{0,1,2,0,2,1,0,0,1,0,1,2};
        const std::array<std::uint8_t,4> expected_decisions{1,0,0,1};
        const std::array<std::uint16_t,6> expected_indices{0,1,2,0,1,2};
        for(bool in_place:{false,true}) {
            std::vector<std::uint16_t> source(input.begin(),input.end());
            std::vector<std::uint16_t> separate(input.size()+2,0xdead);
            auto output=in_place?std::span(source):std::span(separate).first(input.size());
            std::array<std::uint8_t,6> decisions;decisions.fill(0xcd);
            std::size_t written=91;
            assert(filter_triangles(vertices,source,{TriangleWinding::positive},output,
                written,HostSubpixelConversion{}, {},decisions));
            assert(written==expected_indices.size());
            assert(std::equal(expected_indices.begin(),expected_indices.end(),output.begin()));
            assert(std::equal(expected_decisions.begin(),expected_decisions.end(),decisions.begin()));
            assert(decisions[4]==0xcd&&decisions[5]==0xcd);
            assert(separate[input.size()]==0xdead&&separate.back()==0xdead);
            ++decision_cases;
        }
        const auto reject_decisions=[&](unsigned fault) {
            auto points=vertices;std::vector<std::uint16_t> source(input.begin(),input.end());
            std::vector<std::uint16_t> output(input.size(),0xdead);
            std::array<std::uint8_t,4> decisions;decisions.fill(0xcd);
            std::array<ProjectedVertex,9> padding{};
            auto input_view=std::span<const std::uint16_t>(source);
            auto decision_view=std::span<std::uint8_t>(decisions);
            TriangleFilterSpec spec{TriangleWinding::positive};
            std::span<const ProjectedVertex> pad_view;
            switch(fault) {
            case 0:decision_view=decision_view.first(3);break;
            case 1:decision_view={reinterpret_cast<std::uint8_t*>(source.data()),4};break;
            case 2:decision_view={reinterpret_cast<std::uint8_t*>(output.data()),4};break;
            case 3:decision_view={reinterpret_cast<std::uint8_t*>(points.data()),4};break;
            case 4:
                spec.subpixel=true;input_view=input_view.first(3);pad_view=padding;
                decision_view={reinterpret_cast<std::uint8_t*>(padding.data()),1};break;
            case 5:source.back()=99;break;
            }
            const auto saved_source=source,saved_output=output;const auto saved_points=points;
            const auto saved_decisions=decisions;const auto saved_padding=padding;
            std::size_t written=0x1234;
            assert(!filter_triangles(points,input_view,spec,output,written,
                HostSubpixelConversion{},pad_view,decision_view));
            assert(source==saved_source&&output==saved_output&&written==0x1234);
            assert(decisions==saved_decisions);
            assert(std::memcmp(points.data(),saved_points.data(),sizeof points)==0);
            assert(std::memcmp(padding.data(),saved_padding.data(),sizeof padding)==0);
            ++rejected_cases;
        };
        for(unsigned fault=0;fault<6;++fault)reject_decisions(fault);
    }
    std::printf("Native index payload: %u logical-prefix/alignment/circular-tail/alias cases, "
        "%u stable decision cases, %u unchanged structural or alias rejects pass.\n",
        payload_cases,decision_cases,rejected_cases);
    return 0;
}
