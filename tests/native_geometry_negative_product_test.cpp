// Compare authored extended negative-product arithmetic with an independent
// exact 576-bit integer oracle; no SDK, game data or runtime is required.
#include "native_geometry_negative_product.h"
#include <algorithm>
#include <array>
#include <cfenv>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <vector>
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#define WKC_NEGATIVE_PRODUCT_MXCSR 1
#endif

namespace candidate=wkc::geometry::negative_product;
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
static_assert(candidate::bits(0,0x3f800000u,0x80000000u)==0u);
static_assert(candidate::bits(1,0x3f800000u,0x7f800001u)==0x7f800001u);
static_assert(candidate::bits(0x3f800000u,0x3f800000u,0x3f800000u)==0u);
static_assert(candidate::bits(0x3f800000u,0x3f800000u,0)==0xbf800000u);
static_assert(candidate::bits(0x00800000u,0x3f000000u,0)==0u);
static_assert(candidate::bits(0x7fffffffu,0x40000000u,0)==0xffffffffu);
static_assert(candidate::bits(0x7f800000u,0x3f800000u,0)==0xff800000u);
static_assert(candidate::bits(0x7fc12345u,0x3f800000u,0)==0xffc12345u);
struct Triple {std::uint32_t a,b,c;};
static std::uint64_t rng_state=0x56387019ca562b41ull;
static std::uint32_t random_word() {
    rng_state^=rng_state<<13;rng_state^=rng_state>>7;rng_state^=rng_state<<17;
    return std::uint32_t(rng_state>>15);
}

// Independent mathematical oracle: form exact integer magnitudes with common
// unit 2^-298 in 576 bits, then add/subtract before truncating. No sticky/jam,
// leading-bit alignment, floating-point operation, or SDK function is used.
using Wide=std::array<std::uint64_t,9>;
static Wide wide_term(std::uint64_t significand,unsigned shift) {
    Wide out{};
    const unsigned word=shift/64,bit=shift%64;
    out[word]=significand<<bit;
    if(bit!=0&&word+1<out.size())out[word+1]=significand>>(64-bit);
    return out;
}
static int compare(const Wide& a,const Wide& b) {
    for(unsigned i=9;i--!=0;)if(a[i]!=b[i])return a[i]>b[i]?1:-1;
    return 0;
}
static Wide plus(const Wide& a,const Wide& b) {
    Wide out{};std::uint64_t carry=0;
    for(unsigned i=0;i<9;++i) {
        const auto partial=a[i]+b[i];const bool first=partial<a[i];
        const auto full=partial+carry;const bool second=full<partial;
        out[i]=full;carry=first||second;
    }
    return out;
}
static Wide minus(const Wide& a,const Wide& b) {
    Wide out{};std::uint64_t borrow=0;
    for(unsigned i=0;i<9;++i) {
        const auto partial=a[i]-b[i];const bool first=a[i]<b[i];
        const auto full=partial-borrow;const bool second=partial<borrow;
        out[i]=full;borrow=first||second;
    }
    return out;
}
static std::uint32_t exact_wide(std::uint32_t a,std::uint32_t b,std::uint32_t c) {
    const unsigned ae=(a>>23)&255u,be=(b>>23)&255u,ce=(c>>23)&255u;
    if(!ae||!be)return ce?c:0u;
    const auto p=wide_term(std::uint64_t((a&0x7fffffu)|0x800000u)*
        ((b&0x7fffffu)|0x800000u),ae+be-2);
    const auto q=ce?wide_term((c&0x7fffffu)|0x800000u,ce+148):Wide{};
    const auto ps=(a^b^0x80000000u)&0x80000000u,qs=c&0x80000000u;
    Wide magnitude{};std::uint32_t sign;
    if(ps==qs){magnitude=plus(p,q);sign=ps;}
    else if(compare(p,q)>=0){magnitude=minus(p,q);sign=ps;}
    else{magnitude=minus(q,p);sign=qs;}
    int top=-1;
    for(int i=575;i>=0;--i)if((magnitude[unsigned(i)/64]>>(unsigned(i)%64))&1u){top=i;break;}
    if(top<0)return 0;
    const int exponent=top-298+127;
    if(exponent<=0)return 0;
    if(exponent>255)return sign|0x7fffffffu;
    std::uint32_t sig=0;
    for(unsigned i=0;i<24;++i) {
        const int bit=top-int(i);
        sig=(sig<<1)|std::uint32_t((magnitude[unsigned(bit)/64]>>(unsigned(bit)%64))&1u);
    }
    return sign|(std::uint32_t(exponent)<<23)|(sig&0x7fffffu);
}

static void fail(const char* kind,unsigned mode,std::size_t index,const Triple& input,
                 std::uint32_t expected,std::uint32_t actual) {
    std::fprintf(stderr,"%s: mode=%u index=%zu a=%08x b=%08x c=%08x want=%08x got=%08x\n",
        kind,mode,index,input.a,input.b,input.c,expected,actual);
    std::exit(1);
}

int main() {
    constexpr std::uint32_t edge[]={
        0,0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,
        0x00800000u,0x80800000u,0x00800001u,0x00ffffffu,
        0x3f000000u,0x3f7fffffu,0x3f800000u,0x3f800001u,
        0xbf800000u,0xbf800001u,0x40000000u,0xc0000000u,
        0x7f000000u,0xff000000u,0x7f7fffffu,0xff7fffffu,
        0x7f800000u,0xff800000u,0x7f800001u,0xff800001u,
        0x7fc00000u,0xffc00000u,0x7fc12345u,0xffc54321u,
        0x7fffffffu,0xffffffffu};
    std::vector<Triple> corpus;
    for(auto a:edge)for(auto b:edge)for(auto c:edge)corpus.push_back({a,b,c});
    const auto edge_count=corpus.size();
    // Exponent-difference boundaries, signs and low-significand borrow paths.
    for(unsigned ae=1;ae<256;++ae)for(unsigned be:std::array<unsigned,5>{1,2,126,254,255})
        for(unsigned ce=1;ce<256;ce+=7)for(unsigned sign=0;sign<4;++sign) {
            const auto a=(ae<<23)|((sign&1)?0x80000000u:0)|((ae*811u)&0x7fffffu);
            const auto b=(be<<23)|((be*541u)&0x7fffffu);
            const auto c=(ce<<23)|((sign&2)?0x80000000u:0)|((ce*1789u)&0x7fffffu);
            corpus.push_back({a,b,c});
        }
    const auto boundary_count=corpus.size()-edge_count;
    for(unsigned i=0;i<262144;++i) {
        auto a=random_word(),b=random_word(),c=random_word();
        switch(i&7u) {
        case 1:a=0x3f800000u;c=b;break; // Exact cancellation, or zero-flush.
        case 2:a=0x3f800001u;b=0x3f7fffffu;c=0x3f800000u+(i%3u)-1;break;
        case 3:a&=0x807fffffu;break;
        case 4:c&=0x807fffffu;break;
        case 5:a|=0x7f800000u;break;
        case 6:a=(a&0x807fffffu)|((110+random_word()%35)<<23);
               b=(b&0x807fffffu)|((110+random_word()%35)<<23);
               c=(c&0x807fffffu)|((110+random_word()%35)<<23);break;
        default:break;
        }
        corpus.push_back({a,b,c});
    }
    std::vector<std::uint32_t> expected;
    expected.reserve(corpus.size());
    for(const auto& item:corpus)expected.push_back(exact_wide(item.a,item.b,item.c));
    std::fenv_t saved_environment{};
    if(std::fegetenv(&saved_environment)!=0)return 1;
#ifdef WKC_NEGATIVE_PRODUCT_MXCSR
    const unsigned original=_mm_getcsr();
    constexpr unsigned modes=16;
#else
    constexpr unsigned modes=4;
    constexpr int rounding_modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
#endif
    std::uint64_t lanes=0,wide_cases=expected.size(),leading_bit_cases=0;
#if defined(__AVX2__)
    std::puts("Selected owned AVX2 vector path");
#else
    std::puts("Selected owned portable fallback path");
#endif
    for(unsigned mode=0;mode<modes;++mode) {
#ifdef WKC_NEGATIVE_PRODUCT_MXCSR
        const unsigned control=(original&~(0x6000u|0x8040u|0x3fu))|((mode&3u)<<13)|
            ((mode&4u)?0x8000u:0)|((mode&8u)?0x40u:0)|((mode&1u)?0x3fu:0);
        _mm_setcsr(control);
#else
        if(std::fesetround(rounding_modes[mode])!=0)return 1;
        std::feclearexcept(FE_ALL_EXCEPT);
        const int control_flags=std::fetestexcept(FE_ALL_EXCEPT);
#endif
#if defined(__AVX2__)
        for(unsigned block=0;block<16384;++block) {
            std::array<std::uint64_t,4> values{},indices{};
            for(unsigned lane=0;lane<4;++lane) {
                const unsigned k=block*4+lane;
                values[lane]=k<192?((std::uint64_t{1}<<(k/3))+int(k%3)-1):
                    ((std::uint64_t(random_word())<<32)|random_word());
            }
            __m256i input;std::memcpy(&input,values.data(),32);
            const auto result=candidate::detail::highest_bits(input);
            std::memcpy(indices.data(),&result,32);
            for(unsigned lane=0;lane<4;++lane)if(values[lane]!=0&&
                indices[lane]!=std::uint64_t(63-std::countl_zero(values[lane]))) {
                std::fprintf(stderr,"uint64 leading-bit mismatch mode=%u block=%u lane=%u\n",mode,block,lane);return 1;
            }
            leading_bit_cases+=4;
            if(_mm_getcsr()!=control){std::fprintf(stderr,"leading-bit conversion changed MXCSR\n");return 1;}
        }
#endif
        for(std::size_t offset=0;offset<corpus.size();offset+=4) {
            std::array<float,4> fa{},fb{},fc{};
            for(unsigned lane=0;lane<4;++lane) {
                const auto& item=corpus[(offset+lane)%corpus.size()];
                fa[lane]=std::bit_cast<float>(item.a);fb[lane]=std::bit_cast<float>(item.b);
                fc[lane]=std::bit_cast<float>(item.c);
            }
            const auto portable_vector=candidate::portable(fa,fb,fc);
            const auto native_vector=candidate::vector(fa,fb,fc);
            for(unsigned lane=0;lane<4;++lane) {
                const auto& item=corpus[(offset+lane)%corpus.size()];
                const auto want=expected[(offset+lane)%corpus.size()];
                const auto got=candidate::bits(item.a,item.b,item.c);
                if(want!=got)fail("portable bits",mode,offset+lane,item,want,got);
                const auto portable_bits=std::bit_cast<std::uint32_t>(portable_vector[lane]);
                if(want!=portable_bits)fail("portable Float4",mode,offset+lane,item,want,portable_bits);
                const auto native_bits=std::bit_cast<std::uint32_t>(native_vector[lane]);
                if(want!=native_bits)fail("native Float4",mode,offset+lane,item,want,native_bits);
                ++lanes;
            }
#ifdef WKC_NEGATIVE_PRODUCT_MXCSR
            if(_mm_getcsr()!=control){std::fprintf(stderr,"MXCSR changed\n");return 1;}
#else
            if(std::fegetround()!=rounding_modes[mode] || std::fetestexcept(FE_ALL_EXCEPT)!=control_flags) {
                std::fprintf(stderr,"FP environment changed\n");return 1;
            }
#endif
        }
    }
    std::fesetenv(&saved_environment);
#ifdef WKC_NEGATIVE_PRODUCT_MXCSR
    _mm_setcsr(original);
#endif
    std::printf("PASS lanes=%llu per_mode=%zu modes=%u edge_triples=%zu exponent_boundary_triples=%zu wide_exact_cases=%llu leading_bit_cases=%llu\n",
        (unsigned long long)lanes,corpus.size(),modes,edge_count,boundary_count,(unsigned long long)wide_cases,
        (unsigned long long)leading_bit_cases);
    return 0;
}
