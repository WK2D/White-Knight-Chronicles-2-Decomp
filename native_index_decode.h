// Project-written decoder for the recovered compressed triangle-index stream.
// Stable packed bytes in, logical host u16 indices out. No registers or LS ABI.
#ifndef WKC_NATIVE_INDEX_DECODE_H
#define WKC_NATIVE_INDEX_DECODE_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace wkc::geometry {
namespace index_decode_detail {
inline unsigned be16(const std::uint8_t* p) { return unsigned(p[0])*256+p[1]; }
inline unsigned bits(std::span<const std::uint8_t> bytes,std::size_t start,unsigned width) {
    unsigned value=0;
    for(unsigned i=0;i<width;++i) {
        const auto bit=start+i;
        value=(value<<1)|((bytes[bit/8]>>(7-bit%8))&1);
    }
    return value;
}
inline bool overlaps(const void* a,std::size_t an,const void* b,std::size_t bn) {
    if(!an||!bn)return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    if(an>(std::numeric_limits<std::uintptr_t>::max)()-x||
       bn>(std::numeric_limits<std::uintptr_t>::max)()-y)return true;
    return x<y+bn&&y<x+an;
}
}

// Header: BE u16 explicit-value count, BE u16 delta bias, BE u16 selector-byte
// count, u8 delta bit width (1..15, or 0 with no explicit values), one reserved
// byte. Selectors and two-bit
// triangle commands follow, then fixed-width deltas, all MSB first.
// The caller selects this compressed format; count is the metadata's u16 index
// count. Only logical indices are written. Pipeline scratch and padded output
// triangles are outside this interface, and still need whole-job liveness proof.
// Layouts where the old token writes can overwrite unread explicit values decline
// until their scratch-order dependency is reconstructed separately.
// Structural validation completes before writes. Input/output consumed ranges
// must be disjoint. No allocation is needed: explicit prediction uses eight lanes.
inline bool decode_triangle_indices(std::span<const std::uint8_t> input,
                                    std::uint32_t count,std::span<std::uint16_t> output) {
    using namespace index_decode_detail;
    if(count>65535||count%3||output.size()<count)return false;
    if(!count)return true;
    if(input.size()<8)return false;
    const auto explicit_count=be16(input.data()),bias=be16(input.data()+2);
    const auto selector_bytes=be16(input.data()+4),width=unsigned(input[6]);
    // Width 16 and width-zero explicit values have unresolved differences from
    // the reference's extraction/scratch behavior. Do not silently approximate.
    if(width>15||(!width&&explicit_count))return false;
    const std::size_t triangles=count/3,command_bytes=(triangles+3)/4;
    const std::size_t prefix=8+selector_bytes+command_bytes;
    const std::size_t delta_bytes=(std::size_t(explicit_count)*width+7)/8;
    if(prefix>input.size()||delta_bytes>input.size()-prefix)return false;
    const auto explicit_scratch_start=(prefix+15)&~std::size_t(15);
    const auto token_scratch_start=(std::size_t(count)*2+15)&~std::size_t(15);
    if(explicit_count&&explicit_scratch_start+std::size_t(explicit_count)*2>token_scratch_start)return false;
    const auto selectors=input.subspan(8,selector_bytes);
    const auto commands=input.subspan(8+selector_bytes,command_bytes);
    const auto deltas=input.subspan(prefix,delta_bytes);
    std::size_t tokens=0;
    for(std::size_t i=0;i<triangles;++i)tokens+=bits(commands,i*2,2)==3?3:1;
    if(tokens>std::size_t(selector_bytes)*8)return false;
    unsigned required_explicit=0;
    for(std::size_t i=0;i<tokens;++i)required_explicit+=bits(selectors,i,1);
    if(required_explicit>explicit_count||
       overlaps(input.data(),prefix+delta_bytes,output.data(),std::size_t(count)*2))return false;

    std::array<std::uint16_t,8> prediction{};
    std::uint16_t sequential=0;
    std::size_t token_cursor=0;
    unsigned explicit_cursor=0;
    const auto next=[&]() -> std::uint16_t {
        if(!bits(selectors,token_cursor++,1))return sequential++;
        const auto delta=bits(deltas,std::size_t(explicit_cursor)*width,width);
        auto& previous=prediction[explicit_cursor++%8];
        previous=std::uint16_t(unsigned(previous)+delta-bias);
        return previous;
    };
    std::array<std::uint16_t,3> previous{};
    for(std::size_t i=0;i<triangles;++i) {
        switch(bits(commands,i*2,2)) {
        case 0:previous={previous[0],previous[2],next()};break;
        case 1:previous={previous[2],previous[1],next()};break;
        case 2:previous={previous[1],previous[0],next()};break;
        case 3:previous={next(),next(),next()};break;
        }
        for(unsigned j=0;j<3;++j)output[i*3+j]=previous[j];
    }
    return true;
}
}
#endif
