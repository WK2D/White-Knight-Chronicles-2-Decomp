#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_index_decode.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <random>
#include <vector>

namespace {
void put16(std::uint8_t* p,unsigned value){p[0]=std::uint8_t(value>>8);p[1]=std::uint8_t(value);}
void put_bits(std::vector<std::uint8_t>& bytes,std::size_t start,unsigned width,unsigned value) {
    for(unsigned i=0;i<width;++i)if((value>>(width-1-i))&1)bytes[(start+i)/8]|=1u<<(7-(start+i)%8);
}
}
int wkc2_test_native_index_decode() {
    using wkc::geometry::decode_triangle_indices;
    const std::array<std::uint8_t,10> simple{0,0,0,0,0,1,0,0,0,0xc6};
    const std::array<std::uint16_t,12> expected{0,1,2,0,2,3,3,2,4,2,3,5};
    std::array<std::uint16_t,13> output;output.fill(0xdead);
    assert(decode_triangle_indices(simple,12,output));
    assert(std::equal(expected.begin(),expected.end(),output.begin())&&output.back()==0xdead);
    assert(decode_triangle_indices({},0,{}));

    std::mt19937 random(0x78f0);
    unsigned trials=0,indices=0,declines=0;
    for(unsigned width=1;width<=15;++width)for(unsigned trial=0;trial<128;++trial) {
        const unsigned triangles=1+trial%41,count=triangles*3,commands=(triangles+3)/4;
        std::vector<unsigned> modes(triangles);
        unsigned tokens=0;
        for(auto& mode:modes){mode=random()&3;tokens+=mode==3?3:1;}
        const unsigned selector_bytes=(tokens+7)/8;
        std::vector<unsigned> explicit_positions;
        for(unsigned i=0;i<tokens;++i)if(random()&1)explicit_positions.push_back(i);
        const unsigned explicit_count=unsigned(explicit_positions.size()),bias=random()&65535;
        const unsigned prefix=8+selector_bytes+commands;
        std::vector<std::uint8_t> packed(prefix+(explicit_count*width+7)/8+1,0);
        // Deliberately unaligned packed input.
        put16(packed.data()+1,explicit_count);put16(packed.data()+3,bias);
        put16(packed.data()+5,selector_bytes);packed[7]=std::uint8_t(width);
        std::vector<std::uint16_t> explicit_values(explicit_count),token_values(tokens);
        for(unsigned i=0;i<explicit_count;++i) {
            put_bits(packed,9*8+explicit_positions[i],1,1);
            const unsigned delta=random()&((1u<<width)-1);
            put_bits(packed,(prefix+1)*8+i*width,width,delta);
            explicit_values[i]=std::uint16_t(delta-bias+(i<8?0:unsigned(explicit_values[i-8])));
        }
        unsigned cursor=0,sequential=0;
        for(unsigned i=0;i<tokens;++i) {
            if(cursor<explicit_count&&explicit_positions[cursor]==i)token_values[i]=explicit_values[cursor++];
            else token_values[i]=std::uint16_t(sequential++);
        }
        std::vector<std::uint16_t> want;
        std::array<std::uint16_t,3> previous{};cursor=0;
        for(unsigned i=0;i<triangles;++i) {
            put_bits(packed,(9+selector_bytes)*8+i*2,2,modes[i]);
            if(modes[i]==3) {
                for(auto& value:previous)value=token_values[cursor++];
            } else {
                const auto a=previous[0],b=previous[1],c=previous[2];
                previous[0]=modes[i]==0?a:modes[i]==1?c:b;
                previous[1]=modes[i]==0?c:modes[i]==1?b:a;
                previous[2]=token_values[cursor++];
            }
            want.insert(want.end(),previous.begin(),previous.end());
        }
        std::vector<std::uint16_t> got(count+1,0xdead);
        const auto original=got;
        const unsigned explicit_end=((prefix+15)&~15u)+explicit_count*2;
        if(explicit_count&&explicit_end>((count*2+15)&~15u)) {
            assert(!decode_triangle_indices(std::span(packed).subspan(1),count,got));
            assert(got==original);++declines;continue;
        }
        assert(decode_triangle_indices(std::span(packed).subspan(1),count,got));
        assert(std::equal(want.begin(),want.end(),got.begin())&&got.back()==0xdead);
        ++trials;indices+=count;
    }
    const auto saved=output;
    for(unsigned bytes=0;bytes<simple.size();++bytes)
        assert(!decode_triangle_indices(std::span(simple).first(bytes),12,output));
    assert(!decode_triangle_indices(simple,11,output));
    assert(!decode_triangle_indices(simple,65538,output));
    assert(!decode_triangle_indices(simple,12,std::span(output).first(11)));
    auto unsupported=simple;unsupported[6]=16;
    assert(!decode_triangle_indices(unsupported,12,output));
    unsupported=simple;unsupported[1]=1; // Zero-width explicit source is unresolved.
    assert(!decode_triangle_indices(unsupported,12,output));
    unsupported[6]=1; // Declared delta bits are missing from the byte span.
    assert(!decode_triangle_indices(unsupported,12,output));
    auto short_tokens=simple;short_tokens[9]=0xff;
    assert(!decode_triangle_indices(short_tokens,12,output));
    auto missing_explicit=simple;missing_explicit[6]=1;missing_explicit[8]=0x80;
    assert(!decode_triangle_indices(missing_explicit,12,output));
    assert(output==saved);
    std::array<std::uint16_t,12> overlapping{};
    std::memcpy(overlapping.data(),simple.data(),simple.size());
    const auto saved_alias=overlapping;
    assert(!decode_triangle_indices(std::span(reinterpret_cast<const std::uint8_t*>(overlapping.data()),simple.size()),12,overlapping));
    assert(overlapping==saved_alias);
    std::printf("Triangle index decode: %u randomized streams / %u indices, %u unchanged scratch-alias declines, command oracle, unaligned bytes, tails and reject-before-write guards pass.\n",trials,indices,declines);
    return 0;
}
