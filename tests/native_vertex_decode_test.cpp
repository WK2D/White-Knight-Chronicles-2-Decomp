#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_vertex_decode.h"
#include "native_skinning_simd.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <random>
#include <vector>

using namespace wkc::geometry;
namespace {
const auto reciprocal=[](const Float4& value) {
    Float4 out;for(unsigned i=0;i<4;++i)out[i]=1.f/std::sqrt(value[i]);return out;
};
void direction_properties(unsigned encoded,const Float4& result) {
    unsigned axis=(encoded>>2)&3;if(axis==3)axis=0;
    const unsigned q[2]={(encoded>>14)&1023,(encoded>>4)&1023};
    // Independent double-precision evaluation of the two exactly represented
    // affine inputs; this checks field extraction, axis layout and handedness.
    unsigned component=0;
    for(unsigned i=0;i<3;++i)if(i!=axis) {
        const double input=1.+double(q[component++])/8388608.;
        const float expected=float(11595.62109375*input-11596.328125);
        assert(std::bit_cast<std::uint32_t>(result[i])==std::bit_cast<std::uint32_t>(expected));
    }
    assert(std::signbit(result[axis])==!(encoded&1));
    assert(result[3]==((encoded&2)?1.f:-1.f));
    const double length=double(result[0])*result[0]+double(result[1])*result[1]+double(result[2])*result[2];
    assert(std::abs(length-1.)<0.000001);
}
void be16(std::uint8_t* bytes,unsigned n){bytes[0]=std::uint8_t(n>>8);bytes[1]=std::uint8_t(n);}
unsigned direction_code(unsigned vertex,unsigned stream) {
    return (((vertex*113+stream*719)&1023)<<14)|
           (((vertex*631+stream*293)&1023)<<4)|((vertex+stream*7)&15);
}
}
int wkc2_test_native_vertex_decode() {
    using Scalar=VertexDecodeMath<>;
    unsigned directions=0,simd=0;
    const unsigned extremes[]={0,1,511,512,1022,1023};
    for(unsigned a:extremes)for(unsigned b:extremes)for(unsigned code=0;code<16;code+=4) {
        std::array<std::uint32_t,4> bits;
        for(unsigned lane=0;lane<4;++lane)bits[lane]=(a<<14)|(b<<4)|(code+lane);
        const auto result=decode_direction_batch<Scalar>(bits,reciprocal);
        for(unsigned lane=0;lane<4;++lane){direction_properties(bits[lane],result[lane]);++directions;}
    }
    std::mt19937 random(0x760);
    for(unsigned trial=0;trial<8000;++trial) {
        std::array<std::uint32_t,4> bits;for(auto& b:bits)b=random()&0xffffff;
        const auto scalar=decode_direction_batch<Scalar>(bits,reciprocal);
        for(unsigned lane=0;lane<4;++lane){direction_properties(bits[lane],scalar[lane]);++directions;}
#ifdef WKC_NATIVE_SKINNING_HOST_FMA
        const auto vector=decode_direction_batch<VertexDecodeMath<HostFmaMath>>(bits,reciprocal);
        assert(!std::memcmp(scalar.data(),vector.data(),sizeof scalar));simd+=4;
#endif
    }
    for(unsigned streams=1;streams<=2;++streams)for(unsigned count=0;count<10;++count) {
        const unsigned stride=6+3*streams;
        std::vector<std::uint8_t> bytes(count*stride+1,0);
        for(unsigned v=0;v<count;++v)for(unsigned c=0;c<3;++c)be16(bytes.data()+1+v*stride+c*2,(v*8191+c*32767)&65535);
        for(unsigned v=0;v<count;++v)for(unsigned stream=0;stream<streams;++stream) {
            const auto code=direction_code(v,stream);
            auto* encoded=bytes.data()+1+v*stride+6+stream*3;
            encoded[0]=std::uint8_t(code>>16);encoded[1]=std::uint8_t(code>>8);encoded[2]=std::uint8_t(code);
        }
        const Float4 sentinel{91,92,93,94};
        std::vector<Float4> p(count+1,sentinel),n(count+1,sentinel),t(count+1,sentinel);
        const VertexDecodeSpec spec{streams==1?PackedVertexFormat::position_direction:PackedVertexFormat::position_two_directions,count,{-65535,2147483647,-2147483647-1}};
        DecodedVertexStreams out{std::span(p).first(count),{std::span(n).first(count),streams==2?std::span(t).first(count):std::span<Float4>{}}};
        assert(decode_vertex_stream<Scalar>(std::span(bytes).subspan(1),spec,out,reciprocal));
        for(unsigned v=0;v<count;++v) {
            for(unsigned c=0;c<3;++c){const unsigned value=((v*8191+c*32767)&65535)+std::bit_cast<std::uint32_t>(spec.position_bias[c]);const float expected=float(std::bit_cast<std::int32_t>(value))/4096.f;assert(p[v][c]==expected);}
            assert(p[v][3]==1);direction_properties(direction_code(v,0),n[v]);
            if(streams==2)direction_properties(direction_code(v,1),t[v]);
        }
        assert(p.back()==sentinel&&n.back()==sentinel&&t.back()==sentinel);
    }
    std::array<std::uint8_t,24> bytes{};
    std::array<Float4,2> p{Float4{1,2,3,4},Float4{5,6,7,8}},n=p;
    const auto savedp=p,savedn=n;
    VertexDecodeSpec spec{PackedVertexFormat::position_two_directions,2,{}};
    DecodedVertexStreams out{p,{n,{}}};
    assert(!decode_vertex_stream<Scalar>(std::span(bytes).first(23),spec,out,reciprocal));
    auto shortout=out;shortout.positions=std::span(p).first(1);
    assert(!decode_vertex_stream<Scalar>(bytes,spec,shortout,reciprocal));
    shortout=out;shortout.directions[0]=std::span(n).first(1);
    assert(!decode_vertex_stream<Scalar>(bytes,spec,shortout,reciprocal));
    auto alias=out;alias.directions[0]=p;
    assert(!decode_vertex_stream<Scalar>(bytes,spec,alias,reciprocal));
    const auto input_alias=std::span(reinterpret_cast<const std::uint8_t*>(p.data()),24);
    assert(!decode_vertex_stream<Scalar>(input_alias,spec,out,reciprocal));
    spec.format=PackedVertexFormat::position_direction;out.directions[1]=n;
    assert(!decode_vertex_stream<Scalar>(bytes,spec,out,reciprocal));
    spec.format=static_cast<PackedVertexFormat>(34);
    assert(!decode_vertex_stream<Scalar>(bytes,spec,out,reciprocal));
    assert(p==savedp&&n==savedn);
    std::printf("Packed vertex decode: %u independent direction checks, %u x86 FMA comparisons, byte/position/tail and reject-before-write guards pass.\n",directions,simd);
    return 0;
}
