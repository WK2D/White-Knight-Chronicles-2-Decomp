#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_skinning_stream.h"
#include "native_skinning_simd.h"
#include <cassert>
#include <cstring>
#include <random>
#include <vector>

using namespace wkc::geometry;
namespace {
using Matrix=std::array<Float4,3>;
CyclicAffine cyclic(const Matrix& m) {
    return {Float4{m[0][0],m[1][1],m[2][2],m[0][3]},
            Float4{m[0][1],m[1][2],m[2][0],m[1][3]},
            Float4{m[0][2],m[1][0],m[2][1],m[2][3]}};
}
struct Streams {
    std::vector<Float4> positions,output_positions;
    std::array<std::vector<Float4>,2> directions,output_directions;
    std::vector<PackedSkinInfluences> influences;
    std::array<Matrix,4> matrices;
    unsigned palette_reads=0,writes=0;
    std::vector<Float4> first_direction_writes;
    bool live_palette=false,saw_joint255=false;
    explicit Streams(unsigned n):positions(n),output_positions(n),influences(n) {
        for(unsigned i=0;i<2;++i){directions[i].resize(n);output_directions[i].resize(n);}
    }
    PackedSkinInfluences read_influences(unsigned v){return influences[v];}
    Float4 read_position(unsigned v){return positions[v];}
    Float4 read_direction(unsigned stream,unsigned v){return directions[stream][v];}
    CyclicAffine read_palette(unsigned joint) {
        ++palette_reads;saw_joint255|=joint==255;
        if(live_palette) {
            Matrix m={Float4{1,0,0,output_directions[0][0][0]},Float4{0,1,0,0},Float4{0,0,1,0}};
            return cyclic(m);
        }
        return cyclic(matrices[joint]);
    }
    void write_position(unsigned v,const Float4& value){++writes;output_positions[v]=value;}
    void write_direction(unsigned stream,unsigned v,const Float4& value){++writes;output_directions[stream][v]=value;if(!stream&&!v)first_direction_writes.push_back(value);}
};
const auto no_normalization=[](const Float4&){return Float4{1,1,1,1};};
Float4 apply(const Matrix& m,const Float4& input,bool position) {
    Float4 result{};
    for(unsigned row=0;row<3;++row) {
        const unsigned a=row,b=(row+1)%3,c=(row+2)%3;
        const float first=position?std::fma(input[a],m[row][a],m[row][3]):input[a]*m[row][a];
        result[row]=std::fma(input[c],m[row][c],std::fma(input[b],m[row][b],first));
    }
    result[3]=input[3];return result;
}
}
int wkc2_test_native_skinning_stream() {
    // Invalid structure must be rejected without invoking storage callbacks.
    for(auto spec:{SkinningStreamSpec{0,1},SkinningStreamSpec{6,1},SkinningStreamSpec{4,0},SkinningStreamSpec{4,3}}) {
        Streams io(4);assert(!skin_geometry_stream<HostScalarMath>(io,spec,no_normalization));
        assert(io.palette_reads==0 && io.writes==0);
    }
    // A live palette aliases earlier normal outputs. Cached palettes and a
    // different stream latency produce different translations in later groups.
    for(unsigned directions=1;directions<=2;++directions)for(unsigned n:{4u,8u,12u,16u}) {
        Streams io(n);io.live_palette=true;
        for(unsigned v=0;v<n;++v) {
            io.positions[v]={0,0,0,1};io.influences[v]={{255,0,0,0},{0,0,0,255}};
            for(unsigned d=0;d<2;++d)io.directions[d][v]=io.output_directions[d][v]={1,0,0,1};
        }
        auto vector_io=io;
        assert(skin_geometry_stream<HostScalarMath>(io,{n,directions},no_normalization));
#ifdef WKC_NATIVE_SKINNING_HOST_FMA
        assert(skin_geometry_stream<HostFmaMath>(vector_io,{n,directions},no_normalization));
        assert(!std::memcmp(io.output_positions.data(),vector_io.output_positions.data(),n*sizeof(Float4)));
        for(unsigned d=0;d<directions;++d)assert(!std::memcmp(io.output_directions[d].data(),vector_io.output_directions[d].data(),n*sizeof(Float4)));
#endif
        const float weight=255.f*std::bit_cast<float>(std::uint32_t{0x3b808080});
        const float normal=weight*weight;const unsigned latency=directions==1?2:1;
        for(unsigned v=0;v<n;++v) {
            const unsigned group=v/4;
            const float expected=group==0?weight:group<=latency?0:weight*normal;
            assert(io.output_positions[v][0]==expected);
            for(unsigned d=0;d<directions;++d)assert(io.output_directions[d][v][0]==normal);
        }
        assert(io.palette_reads==n*4 && io.saw_joint255); // zero weights still read
    }
    // The two-group pipeline writes a normalized empty batch before its first
    // real result. A reciprocal policy that returns NaN for zero length makes
    // this visible to a live palette: the third group's positions read NaN.
    // Raw-zero initialization of both slots would hide it and fail this case.
    {
        Streams io(12);io.live_palette=true;
        for(unsigned v=0;v<12;++v) {
            io.positions[v]={0,0,0,1};io.influences[v]={{255,0,0,0},{0,0,0,0}};
            io.directions[0][v]=io.output_directions[0][v]={1,0,0,1};
        }
        const auto reciprocal=[](const Float4& lengths){
            Float4 result{};for(unsigned i=0;i<4;++i)
                result[i]=lengths[i]==0?std::bit_cast<float>(std::uint32_t{0x7ff50000}):1.f;
            return result;
        };
        assert(skin_geometry_stream<HostScalarMath>(io,{12,1},reciprocal));
        assert(io.first_direction_writes.size()==3);
        assert(io.first_direction_writes[0]==Float4{});
        for(unsigned i=0;i<3;++i)assert(std::bit_cast<std::uint32_t>(io.first_direction_writes[1][i])==0x7ff50000);
        assert(io.first_direction_writes[1][3]==0);
        for(unsigned v=8;v<12;++v)assert(std::isnan(io.output_positions[v][0]));
        assert(!std::isnan(io.first_direction_writes[2][0]));
    }
    // Independent row-major blend, signed-minor and cyclic-dot oracle.
    std::mt19937 random(0xC968);
    for(unsigned trial=0;trial<128;++trial) {
        const unsigned n=4*(1+trial%16),directions=1+trial%2;Streams io(n);
        for(auto& matrix:io.matrices)for(auto& row:matrix)for(float& value:row)value=float(int(random()%13)-6);
        for(unsigned v=0;v<n;++v) {
            for(unsigned lane=0;lane<4;++lane) {
                io.influences[v].weights[lane]=std::uint8_t(random());io.influences[v].joints[lane]=std::uint8_t(random()%4);
                io.positions[v][lane]=float(int(random()%17)-8);
                for(unsigned d=0;d<directions;++d)io.directions[d][v][lane]=float(int(random()%9)-4);
            }
            io.positions[v][3]=1;
        }
        auto vector_io=io;SkinningStreamSummary summary{};
        assert(skin_geometry_stream<HostScalarMath>(io,{n,directions},no_normalization,&summary));
#ifdef WKC_NATIVE_SKINNING_HOST_FMA
        SkinningStreamSummary vector_summary{};
        assert(skin_geometry_stream<HostFmaMath>(vector_io,{n,directions},no_normalization,&vector_summary));
        assert(!std::memcmp(&summary,&vector_summary,sizeof summary));
        assert(!std::memcmp(io.output_positions.data(),vector_io.output_positions.data(),n*sizeof(Float4)));
        for(unsigned d=0;d<directions;++d)assert(!std::memcmp(io.output_directions[d].data(),vector_io.output_directions[d].data(),n*sizeof(Float4)));
#endif
        for(unsigned v=0;v<n;++v) {
            Matrix blend{},cofactor{};float weights[4];
            for(unsigned j=0;j<4;++j)weights[j]=float(io.influences[v].weights[j])*std::bit_cast<float>(std::uint32_t{0x3b808080});
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
                float value=weights[0]*io.matrices[io.influences[v].joints[0]][row][col];
                for(unsigned j=1;j<4;++j)value=std::fma(weights[j],io.matrices[io.influences[v].joints[j]][row][col],value);
                blend[row][col]=value;
            }
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col) {
                unsigned rows[2],cols[2],nr=0,nc=0;
                for(unsigned i=0;i<3;++i){if(i!=row)rows[nr++]=i;if(i!=col)cols[nc++]=i;}
                const float a=blend[rows[0]][cols[0]],b=blend[rows[0]][cols[1]],c=blend[rows[1]][cols[0]],d=blend[rows[1]][cols[1]];
                cofactor[row][col]=(row+col)%2?std::fma(b,c,-(a*d)):std::fma(a,d,-(b*c));
            }
            const auto position=apply(blend,io.positions[v],true);
            assert(!std::memcmp(position.data(),io.output_positions[v].data(),16));
            for(unsigned d=0;d<directions;++d) {
                const auto direction=apply(cofactor,io.directions[d][v],false);
                assert(!std::memcmp(direction.data(),io.output_directions[d][v].data(),16));
            }
        }
    }
    return 0;
}
