// Local-store and call helpers shared by the C++ parts of SPU image 20
// (`zz_raw_job_01294400`, the render-command builder). See
// native_render_commands.cpp.
//
// Local store holds guest big-endian bytes. The SPU only loads and stores
// whole quads: a word or halfword access uses the quad that holds its
// address, and a misaligned value wraps within that quad. These helpers keep
// that addressing, so C++ written against them makes exactly the local-store
// accesses the compiled code makes, whatever the alignment.
#pragma once
#include "spu_context.h"
#include "spu_helpers.h"
#include <cstdint>

extern "C" {
typedef void (*wkc_spu_fn)(spu_context*);
wkc_spu_fn spu_lookup(uint32_t addr,int image_id);
}

namespace wkc_render {
#define R(n) ctx->gpr[n]

inline std::uint8_t* quad(spu_context* ctx,std::uint32_t address) {
    return ctx->ls+(address&SPU_LS_MASK&~15u);
}
inline u128 load(spu_context* ctx,std::uint32_t address) {return spu_ls_read128(ctx,address);}
inline void store(spu_context* ctx,std::uint32_t address,u128 value) {spu_ls_write128(ctx,address,value);}
// The word an SPU sees after `lqd` at `quad_address` and `rotqby` by
// `rotate`: four bytes from `rotate`, wrapping within that quad.
inline std::uint32_t word(spu_context* ctx,std::uint32_t quad_address,std::uint32_t rotate) {
    const std::uint8_t* q=quad(ctx,quad_address);std::uint32_t v=0;
    for(unsigned k=0;k<4;++k)v=v<<8|q[(rotate+k)&15];
    return v;
}
inline std::uint32_t word(spu_context* ctx,std::uint32_t address) {return word(ctx,address,address);}
// `cwd`/`shufb`/`stqd`: replace the word slot of `address` in its quad.
inline void put_word(spu_context* ctx,std::uint32_t address,std::uint32_t v) {
    std::uint8_t* q=quad(ctx,address)+(address&12u);
    q[0]=std::uint8_t(v>>24);q[1]=std::uint8_t(v>>16);q[2]=std::uint8_t(v>>8);q[3]=std::uint8_t(v);
}
inline u128 splat(u128 v,unsigned lane) {u128 r;for(int i=0;i<4;++i)r._u32[i]=v._u32[lane];return r;}

// A compiled function of this image, resolved once per address.
inline wkc_spu_fn compiled(spu_context* ctx,std::uint32_t address) {
    struct Slot {std::uint32_t address;wkc_spu_fn fn;};
    static thread_local Slot slots[16];
    for(auto& s:slots) {
        if(s.address==address&&s.fn)return s.fn;
        if(!s.fn){s.address=address;s.fn=spu_lookup(address,ctx->image_id);return s.fn;}
    }
    return spu_lookup(address,ctx->image_id);
}
// A lifted `brsl` to compiled code: link, nested host frame, drain to the link.
inline void call_compiled(spu_context* ctx,std::uint32_t target,std::uint32_t link) {
    R(0)=spu_link(link);
    const std::int32_t saved=ctx->image_id;ctx->host_depth++;
    compiled(ctx,target)(ctx);
    spu_drain_call(ctx,link);
    ctx->host_depth--;spu_img_restore(ctx,saved);
}
}
