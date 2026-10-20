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
void position_bit_properties() {
    using Scalar=VertexDecodeMath<>;
    constexpr unsigned count=5;
    constexpr std::array<std::array<unsigned,3>,count> encoded{{
        {0xabcd,0xffff,0x8001},{0xffff,1,0x1234},{0,0xabcd,0xffff},
        {0x8000,0x7654,0x3210},{0x000f,0x8001,0x7fff}}};
    constexpr std::array<std::array<std::int32_t,3>,2> biases{{
        {0,0,0},
        {-0x1234,(std::numeric_limits<std::int32_t>::max)(),
         (std::numeric_limits<std::int32_t>::min)()}}};
    const Float4 sentinel{91,92,93,94};
    for(const auto& bias:biases)for(unsigned streams=1;streams<=2;++streams) {
        const auto format=streams==1?PackedVertexFormat::position_direction:
            PackedVertexFormat::position_two_directions;
        const unsigned stride=6+3*streams;
        std::vector<std::uint8_t> bytes(count*stride);
        for(unsigned vertex=0;vertex<count;++vertex) {
            for(unsigned axis=0;axis<3;++axis)
                be16(bytes.data()+vertex*stride+axis*2,encoded[vertex][axis]);
            for(unsigned stream=0;stream<streams;++stream) {
                // Include both values of the first direction byte's high bit:
                // the 17-bit Z coordinate also consumes that bit.
                const auto code=direction_code(vertex+3,stream);
                auto* target=bytes.data()+vertex*stride+6+stream*3;
                target[0]=std::uint8_t(code>>16);target[1]=std::uint8_t(code>>8);
                target[2]=std::uint8_t(code);
            }
        }
        std::array<Float4,count> baseline_positions,baseline_normal,baseline_tangent;
        const VertexDecodeSpec baseline{format,count,bias};
        assert((baseline.position_bits==std::array<unsigned,3>{16,16,16}));
        DecodedVertexStreams baseline_out{baseline_positions,{baseline_normal,
            streams==2?std::span(baseline_tangent):std::span<Float4>{}}};
        assert(decode_vertex_stream<Scalar>(bytes,baseline,baseline_out,reciprocal));
        // Each axis exercises every width, including the zero-contribution
        // endpoint and the 17-bit overlapping layout. All combinations
        // of requested direction streams must preserve the same direction bits.
        for(unsigned first=0;first<=17;++first)for(unsigned mask=0;mask<(1u<<streams);++mask) {
            const std::array<unsigned,3> widths{first,(first+5)%18,(first+11)%18};
            const VertexDecodeSpec spec{format,count,bias,widths};
            std::array<Float4,count+1> positions,normal,tangent;
            positions.fill(sentinel);normal.fill(sentinel);tangent.fill(sentinel);
            DecodedVertexStreams out{std::span(positions).first(count),{
                (mask&1)?std::span(normal).first(count):std::span<Float4>{},
                (mask&2)?std::span(tangent).first(count):std::span<Float4>{}}};
            assert(decode_vertex_stream<Scalar>(bytes,spec,out,reciprocal));
            for(unsigned vertex=0;vertex<count;++vertex) {
                for(unsigned axis=0;axis<3;++axis) {
                    // Read one bit at a time to independently specify the
                    // overlapping field; use i64 to specify signed wrapping.
                    std::uint32_t quantized=0;
                    for(unsigned bit=0;bit<widths[axis];++bit) {
                        const auto input_bit=axis*16+bit;
                        const auto byte=bytes[vertex*stride+input_bit/8];
                        quantized=quantized*2+((byte>>(7-input_bit%8))&1u);
                    }
                    std::int64_t value=std::int64_t(quantized)+bias[axis];
                    if(value>(std::numeric_limits<std::int32_t>::max)())value-=0x100000000ll;
                    const float expected=float(value)/4096.f;
                    assert(positions[vertex][axis]==expected);
                }
                assert(positions[vertex][3]==1.f);
                if(mask&1)assert(!std::memcmp(&normal[vertex],&baseline_normal[vertex],sizeof(Float4)));
                else assert(normal[vertex]==sentinel);
                if(mask&2)assert(!std::memcmp(&tangent[vertex],&baseline_tangent[vertex],sizeof(Float4)));
                else assert(tangent[vertex]==sentinel);
            }
            assert(positions.back()==sentinel&&normal.back()==sentinel&&tangent.back()==sentinel);
        }
        if(bias==std::array<std::int32_t,3>{})for(unsigned axis=0;axis<3;++axis) {
            auto changed=bytes;
            VertexDecodeSpec spec{format,count,bias,{0,0,0}};
            spec.position_bits[axis]=17;
            std::array<Float4,count> original,discarded,carried;
            assert(decode_vertex_stream<Scalar>(bytes,spec,{original,{}},reciprocal));
            bool zero=false,one=false;
            for(unsigned vertex=0;vertex<count;++vertex) {
                auto& byte=changed[vertex*stride+axis*2+2];
                if(byte&128)one=true;else zero=true;
                byte^=0x7f;
            }
            assert(zero&&one);
            assert(decode_vertex_stream<Scalar>(changed,spec,{discarded,{}},reciprocal));
            assert(original==discarded);
            for(unsigned vertex=0;vertex<count;++vertex)changed[vertex*stride+axis*2+2]^=0x80;
            assert(decode_vertex_stream<Scalar>(changed,spec,{carried,{}},reciprocal));
            for(unsigned vertex=0;vertex<count;++vertex) {
                const bool carry=(bytes[vertex*stride+axis*2+2]&128)!=0;
                const auto difference=carry?-0x1p-12f:0x1p-12f;
                assert(carried[vertex][axis]==original[vertex][axis]+difference);
                for(unsigned other=0;other<3;++other)if(other!=axis)
                    assert(carried[vertex][other]==original[vertex][other]);
            }
        }
    }
}
void invalid_position_bits() {
    using Scalar=VertexDecodeMath<>;
    const std::array<unsigned,4> invalid{18,31,32,(std::numeric_limits<unsigned>::max)()};
    const Float4 sentinel{91,92,93,94};
    std::array<std::uint8_t,24> bytes{};
    for(unsigned streams=1;streams<=2;++streams)for(unsigned count:{0u,2u})
        for(unsigned axis=0;axis<3;++axis)for(const auto bad:invalid) {
            VertexDecodeSpec spec{streams==1?PackedVertexFormat::position_direction:
                PackedVertexFormat::position_two_directions,count,{}};
            spec.position_bits[axis]=bad;
            std::array<Float4,2> positions{sentinel,sentinel},normal=positions,tangent=positions;
            DecodedVertexStreams out{positions,{normal,streams==2?std::span(tangent):std::span<Float4>{}}};
            unsigned converted=0,reciprocated=0;
            const auto convert=[&](const std::array<std::int32_t,3>&) {++converted;return Float4{};};
            const auto estimate=[&](const Float4&) {++reciprocated;return Float4{};};
            assert(!decode_vertex_stream<Scalar>(bytes,spec,out,estimate,convert));
            assert(!converted&&!reciprocated);
            for(unsigned vertex=0;vertex<2;++vertex)
                assert(positions[vertex]==sentinel&&normal[vertex]==sentinel&&tangent[vertex]==sentinel);
        }
}
}
int wkc2_test_native_vertex_decode() {
    using Scalar=VertexDecodeMath<>;
    position_bit_properties();
    invalid_position_bits();
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
    std::printf("Packed vertex decode: %u independent direction checks, %u x86 FMA comparisons, per-axis 0..17 position bits, byte/position/tail and reject-before-write guards pass.\n",directions,simd);
    return 0;
}
