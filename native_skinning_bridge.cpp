#include "native_skinning_bridge.h"
#include "native_skinning.h"
#include "spu_helpers.h"
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {
using namespace wkc::geometry;
Float4 unpack(u128 value) {Float4 out;std::memcpy(out.data(),&value,16);return out;}
u128 pack(const Float4& value) {u128 out;std::memcpy(&out,value.data(),16);return out;}
struct RuntimeMath {
    static Float4 multiply(const Float4& a,const Float4& b) {
        return unpack(spu_fm(pack(a),pack(b)));
    }
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        return unpack(spu_fma(pack(a),pack(b),pack(c)));
    }
};
bool broadcast_mask(u128 mask,unsigned lane) {
    const auto expected=spu_splat_u32(0x00010203u+lane*0x04040404u);
    return std::memcmp(&mask,&expected,16)==0;
}
}

extern "C" int wkc_native_blend_geometry(spu_context* ctx) {
    static const bool enabled=[] {const char* e=std::getenv("WKC_NATIVE_SKINNING");return !e||std::strcmp(e,"0");}();
    if(!enabled || !ctx || !ctx->ls || ctx->image_id!=19)return 0;
    // The decoded core uses direct weight lanes; other control masks retain
    // the original compiled prefix. Reject before changing architectural state.
    const unsigned masks[]={121,112,111,118};
    for(unsigned lane=0;lane<4;++lane)
        if(!broadcast_mask(ctx->gpr[masks[lane]],lane))return 0;

    const auto stack=ctx->gpr[1]._u32[0];
    const unsigned bases[]={spu_ls_read128(ctx,stack+0xd0)._u32[0],
        ctx->gpr[9]._u32[0],spu_ls_read128(ctx,stack+0xb0)._u32[0]};
    const unsigned weight_registers[]={74,54,53,52};
    // Destination registers at the recovered block boundary, by vertex/phase.
    const unsigned destinations[4][3]={{45,50,41},{23,49,32},{24,36,15},{42,13,14}};
    CyclicAffine blended[4];
    for(unsigned vertex=0;vertex<4;++vertex) {
        std::array<CyclicAffine,4> influences;
        const auto indices=ctx->gpr[vertex<2?33:35];
        for(unsigned influence=0;influence<4;++influence) {
            const auto word=indices._u32[(vertex%2)*2+influence/2];
            const auto index=(influence%2?word:word>>16)&0xffffu;
            // Original halfword shifts/add wrap at 16 bits before addressing.
            const auto offset=(index*48u)&0xffffu;
            for(unsigned phase=0;phase<3;++phase)
                influences[influence][phase]=unpack(spu_ls_read128(ctx,bases[phase]+offset));
        }
        blended[vertex]=blend_skin_matrix<RuntimeMath>(unpack(ctx->gpr[weight_registers[vertex]]),influences);
    }

    // Remaining inputs are decoded before register writes. There are no LS
    // stores in this prefix, so overlapping palette/stack inputs remain valid.
    const auto next=spu_a(ctx->gpr[119],ctx->gpr[127]);
    const auto next_a=spu_ls_read128(ctx,next._u32[0]+0x20);
    const auto next_b=spu_ls_read128(ctx,next._u32[0]+0x30);
    const auto output=ctx->gpr[64];const auto normal_source=ctx->gpr[16];
    const auto normal_pack=spu_shufb(ctx->gpr[79],ctx->gpr[78],ctx->gpr[89]);
    const unsigned stack_registers[]={6,10,21,22,31,46,48,61};
    const unsigned stack_offsets[]={0x50,0x90,0x60,0x70,0x80,0x40,0x30,0x20};
    u128 stack_values[8];
    for(unsigned i=0;i<8;++i)stack_values[i]=spu_ls_read128(ctx,stack+stack_offsets[i]);
    const auto transpose_mask=spu_ls_read128(ctx,0x103e0);
    for(unsigned vertex=0;vertex<4;++vertex)
        for(unsigned phase=0;phase<3;++phase)
            ctx->gpr[destinations[vertex][phase]]=pack(blended[vertex][phase]);
    for(unsigned i=0;i<8;++i)ctx->gpr[stack_registers[i]]=stack_values[i];
    ctx->gpr[30]=transpose_mask;
    ctx->gpr[54]=spu_shufb(next_a,next_a,ctx->gpr[125]);
    ctx->gpr[53]=spu_shufb(next_a,next_a,ctx->gpr[115]);
    ctx->gpr[52]=spu_shufb(next_b,next_b,ctx->gpr[125]);
    ctx->gpr[34]=spu_shufb(next_b,next_b,ctx->gpr[115]);
    ctx->gpr[33]=spu_shufb(next_a,next_a,ctx->gpr[124]);
    ctx->gpr[35]=spu_shufb(next_b,next_b,ctx->gpr[124]);
    ctx->gpr[55]=output;ctx->gpr[64]=spu_ai(output,64);
    ctx->gpr[72]=normal_source;ctx->gpr[63]=normal_pack;
    ctx->gpr[43]=spu_shufb(ctx->gpr[36],ctx->gpr[15],ctx->gpr[123]);

    static const bool trace=std::getenv("WKC_NATIVE_SKINNING_TRACE")!=nullptr;
    if(trace) {
        static std::atomic<unsigned long long> calls{0};
        const auto count=calls.fetch_add(1,std::memory_order_relaxed)+1;
        if(count<=4 || !(count&(count-1)))std::fprintf(stderr,"[native-skinning] blended_blocks=%llu\n",count);
    }
    return 1;
}
