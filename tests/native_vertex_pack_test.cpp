#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_vertex_pack.h"
#include "native_skinning_simd.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <random>
#include <vector>

using namespace wkc::geometry;
namespace {
unsigned be32(const std::uint8_t* p){return unsigned(p[0])*0x1000000u+unsigned(p[1])*65536+unsigned(p[2])*256+p[3];}
unsigned be16(const std::uint8_t* p){return unsigned(p[0])*256+p[1];}
unsigned independent_normal(const Float4& n) {
    unsigned result=0;
    for(unsigned lane=0;lane<3;++lane) {
        const unsigned width=lane==2?10:11,shift=lane==2?22:21;
        const float denominator=float(1u<<width);
        const double scaled=double(std::fma(n[lane],1.f-1.f/denominator,-1.f/denominator))*2147483648.;
        const auto value=std::int32_t(scaled<=-2147483648.||std::isnan(scaled)?-2147483648.:scaled>=2147483647.?2147483647.:scaled);
        result|=((std::uint32_t(value)>>shift)&((1u<<width)-1))<<(lane==0?0:lane==1?11:22);
    }
    return result;
}
}
int wkc2_test_native_vertex_pack() {
    const std::array<std::array<unsigned,2>,18> half_cases{{
        {0,0},{0x80000000,0},{0x37ffffff,0},{0xb7ffffff,0},
        {0x38000000,0},{0xb8000000,0x8000},{0x38002000,1},{0xb8002000,0x8001},
        {0x3f800000,0x3c00},{0xbf800000,0xbc00},{0x3fc00000,0x3e00},
        {0x477fe000,0x7bff},{0x47800000,0x7bff},{0xc7800000,0xfbff},
        {0x7f800000,0x7bff},{0xff800000,0xfbff},{0x7fa12345,0x7bff},{0xffc12345,0xfbff}
    }};
    for(const auto pair:half_cases)assert(pack_tangent_component(std::bit_cast<float>(pair[0]))==pair[1]);
    assert(pack_normal_11_11_10(Float4{1,1,1,0})==(1023u|(1023u<<11)|(511u<<22)));
    assert(pack_normal_11_11_10(Float4{-1,-1,-1,0})==(1024u|(1024u<<11)|(512u<<22)));
    assert(pack_normal_11_11_10(Float4{})==0xffffffffu);
    const auto converted=HostPackedNormalConversion{}(Float4{
        std::bit_cast<float>(0x7f800000u),std::bit_cast<float>(0xff800000u),
        std::bit_cast<float>(0x7fa12345u),-0.f});
    assert(converted[0]==2147483647&&converted[1]==(-2147483647-1)&&
           converted[2]==(-2147483647-1)&&converted[3]==0);
    std::mt19937 random(0x58);unsigned normals=0,simd=0,records=0;
    for(unsigned i=0;i<32000;++i) {
        Float4 n;for(auto& lane:n)lane=float(int(random()%8193)-4096)/2048.f;
        const auto value=pack_normal_11_11_10(n);
        assert(value==independent_normal(n));++normals;
#ifdef WKC_NATIVE_SKINNING_HOST_FMA
        assert(value==pack_normal_11_11_10<HostFmaMath>(n));++simd;
#endif
    }
    for(auto format:{VertexPackFormat::position_normal,VertexPackFormat::position_normal_tangent,VertexPackFormat::position})
        for(bool suppressed:{false,true})for(unsigned count=0;count<10;++count) {
            std::vector<Float4> positions(count),normal(count),tangent(count);
            for(unsigned v=0;v<count;++v) {
                positions[v]={std::bit_cast<float>(0x80000000u+v),float(v)+0.25f,std::bit_cast<float>(0x7f812345u+v),9};
                normal[v]={float(v)/8.f,-float(v)/8.f,1,0};tangent[v]={1,-1,1.5f,0};
            }
            const unsigned stride=packed_vertex_stride(format);
            std::vector<std::uint8_t> bytes(count*stride+2,0xcd);
            const VertexPackStreams views{positions,suppressed?std::span<const Float4>{}:normal,suppressed?std::span<const Float4>{}:tangent};
            assert(pack_vertex_stream(views,{format,count,suppressed},std::span(bytes).subspan(1,count*stride)));
            assert(bytes.front()==0xcd&&bytes.back()==0xcd);
            for(unsigned v=0;v<count;++v) {
                const auto* dest=bytes.data()+1+v*stride;++records;
                for(unsigned c=0;c<3;++c)assert(be32(dest+c*4)==std::bit_cast<unsigned>(positions[v][c]));
                if(stride>=16)assert(be32(dest+12)==(suppressed?0:independent_normal(normal[v])));
                constexpr unsigned tangent_expected[]={0x3c00,0xbc00,0x3e00,0};
                if(stride==24)for(unsigned c=0;c<4;++c)assert(be16(dest+16+c*2)==(suppressed?0:tangent_expected[c]));
            }
        }
    std::array<Float4,2> source{};std::array<std::uint8_t,48> output;output.fill(0xa5);const auto saved=output;
    const VertexPackSpec spec{VertexPackFormat::position_normal_tangent,2,false};
    const VertexPackStreams input{source,source,source};
    assert(!pack_vertex_stream(input,spec,std::span(output).first(47)));
    auto short_input=input;short_input.positions=std::span(source).first(1);assert(!pack_vertex_stream(short_input,spec,output));
    short_input=input;short_input.normals={};assert(!pack_vertex_stream(short_input,spec,output));
    short_input=input;short_input.tangents={};assert(!pack_vertex_stream(short_input,spec,output));
    assert(!pack_vertex_stream(input,{static_cast<VertexPackFormat>(35),2,false},output));
    assert(output==saved);
    std::array<Float4,3> alias{};const auto saved_alias=alias;
    const auto alias_bytes=std::span(reinterpret_cast<std::uint8_t*>(alias.data()),sizeof alias);
    for(unsigned stream=0;stream<3;++stream) {
        auto views=input;if(stream==0)views.positions=alias;if(stream==1)views.normals=alias;if(stream==2)views.tangents=alias;
        assert(!pack_vertex_stream(views,spec,alias_bytes));assert(!std::memcmp(alias.data(),saved_alias.data(),sizeof alias));
    }
    std::printf("Vertex export: %u normal oracles, %u x86 FMA comparisons, %u logical records, tangent edges and reject-before-write guards pass.\n",normals,simd,records);
    return 0;
}
