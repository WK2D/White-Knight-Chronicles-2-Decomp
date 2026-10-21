// Project-written handlers for commands of SPU image 20 (`zz_raw_job_01294400`),
// the render-command builder. The image interprets a display list: its loop at
// LS 0x1308 fetches each command, takes the 12-bit command id and jumps through
// a table at LS 0x137C. A handler here replaces one table entry's compiled code
// at a hash-pinned boundary (tools/apply_native_render_commands.py) and returns
// to the interpreter loop exactly as the compiled handler does.
//
// Handlers work on the job's local store and register file, so they keep the
// SPU's local-store addressing: a 16-byte access uses the quad that contains
// its address, and a misaligned word wraps within that quad. They decline to
// the compiled reference whenever an LS debug watch is armed.
#include "native_render_spu.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace wkc_render;
// WKC_NATIVE_RENDER_COMMANDS=0 keeps every command on its compiled handler.
bool native_commands_enabled() {
    static const bool enabled=[]{const char* v=std::getenv("WKC_NATIVE_RENDER_COMMANDS");return !v||std::strcmp(v,"0")!=0;}();
    return enabled;
}
bool debug_watch_armed() {
    return !native_commands_enabled()||g_spu_ls_watch_n!=0||g_spu_smc_watch!=0||g_spu_ls_probe!=0;
}
}

// Command 25 (handler LS 0x1C70, body 0x4870): upload a run of 16-byte table
// entries. The command's halfword at +2 holds count << 4 and is copied to +6;
// the word at +8 is the first slot. Entries follow the 16-byte header and are
// copied to state + 0xB40 + slot * 16, re-reading the count at +6 after each
// entry. A byte at state + 0xAD0 then marks the table dirty.
extern "C" int wkc_native_render_cmd25(spu_context* ctx) {
    if(debug_watch_armed())return 0;
    const std::uint32_t sp=ctx->gpr[1]._u32[0];
    const std::uint32_t state=ctx->gpr[82]._u32[0],cmd=ctx->gpr[81]._u32[0];
    // The handler links to 0x1C88; the callee saves r80-r82, the link and the
    // stack pointer below its frame and restores them from there on return.
    const std::uint32_t header=word(ctx,cmd,cmd); // read before the saves, as compiled
    ctx->gpr[0]=u128{};ctx->gpr[0]._u32[0]=0x1C88; // spu_link: preferred word only
    spu_ls_write128(ctx,sp-0x10,ctx->gpr[80]);
    spu_ls_write128(ctx,sp-0x20,ctx->gpr[81]);
    spu_ls_write128(ctx,sp-0x30,ctx->gpr[82]);
    spu_ls_write128(ctx,sp+0x10,ctx->gpr[0]);
    spu_ls_write128(ctx,sp-0x50,ctx->gpr[1]);
    {   // copy the count halfword to +6
        std::uint8_t* q=quad(ctx,cmd+6);const unsigned at=(cmd+6)&14u;
        q[at]=std::uint8_t(header>>8);q[at+1]=std::uint8_t(header);
    }
    if((header&0xFFFFu)>>4) {
        const std::uint32_t slot=word(ctx,cmd+8,cmd+8);
        std::uint32_t source=cmd+16,target=state+720u+2064u+(slot<<4);
        std::uint32_t copied=0;
        do {
            ++copied;
            std::memcpy(quad(ctx,target),quad(ctx,source),16);
            source+=16;target+=16;
        } while(((word(ctx,cmd+6,cmd+20)&0xFFFFu)>>4)>copied);
    }
    quad(ctx,state+720u+0x800u)[(sp-80u)&15u]=1;
    ctx->gpr[0]=spu_ls_read128(ctx,sp+0x10);
    ctx->gpr[80]=spu_ls_read128(ctx,sp-0x10);
    ctx->gpr[81]=spu_ls_read128(ctx,sp-0x20);
    ctx->gpr[82]=spu_ls_read128(ctx,sp-0x30);
    return 1;
}

// Command 12 (handler 0x1AE0) first flushes dirty render state (0xAD50), then
// builds the draw (0x9D58). Parts of both run here.
namespace {
// One row of a 4x4 product, as the SPU code computes it:
// (b0*m.x + b2*m.z) + (b1*m.y + b3*m.w), with the SPU's single-precision ops
// and its operand order (`w_first` puts m.w before b3 in that product).
u128 product_row(const u128 b[4],u128 m,bool w_first) {
    const u128 even=spu_fma(b[2],splat(m,2),spu_fm(b[0],splat(m,0)));
    const u128 w=splat(m,3);
    const u128 odd=w_first?spu_fma(w,b[3],spu_fm(b[1],splat(m,1))):spu_fma(b[3],w,spu_fm(b[1],splat(m,1)));
    return spu_fa(even,odd);
}
}

// 0xA8E0, called from the state flush when the transform block is dirty:
// upload transform constants. With S = state + 720, the block holds three
// matrices: M1 (S + 2944, rows 0-2; row 3 is the constant quad at LS 0xD2D0),
// M2 (S + 2816) and M3 (S + 2880). It forms A = M1, T = M2 * A and
// U = M3 * T on its stack, then walks the upload list in the program block
// (r4): the halfword at +20 counts entries and the one at +22 is the first
// quad of 4-byte entries {kind, constant slot} from +32. Kind 1, 2 or 3
// uploads A, T or U to that slot with SET_TRANSFORM_CONSTANT_LOAD (method
// 0x1EFC, 17 words: slot then 16 floats) at the output cursor; other kinds
// are skipped. A nonempty list first makes sure 1 KB of output space is free
// (compiled 0xFF0, which may flush).
extern "C" int wkc_native_render_transform_constants(spu_context* ctx) {
    if(debug_watch_armed())return 0;
    const std::uint32_t sp=R(1)._u32[0],prog=R(4)._u32[0];
    // Prologue: callee-saved registers, link and back chain.
    store(ctx,sp-0x10,R(80));store(ctx,sp-0x80,R(87));store(ctx,sp-0x90,R(88));
    store(ctx,sp-0x60,R(85));store(ctx,sp-0x70,R(86));store(ctx,sp+0x10,R(0));
    store(ctx,sp-0x20,R(81));store(ctx,sp-0x30,R(82));store(ctx,sp-0x40,R(83));
    store(ctx,sp-0x50,R(84));store(ctx,sp-0xA0,R(126));store(ctx,sp-0x180,R(1));
    R(80)=R(5);R(87)=R(4);R(88)=spu_il(20);R(86)=spu_a(R(3),spu_il(720));
    R(1)=spu_ai(R(1),-384);
    R(85)=spu_a(R(80),spu_il(4124));                  // 0x9B0: the output cursor block
    const std::uint32_t frame=sp-384,S=R(86)._u32[0],cursor=R(85)._u32[0]+8;
    if(word(ctx,prog+20)!=0) {
        R(3)=R(80);R(4)=spu_il(1024);
        call_compiled(ctx,0xFF0,0xAD48);
    }
    u128 a[4]={load(ctx,S+2944),load(ctx,S+2960),load(ctx,S+2976),load(ctx,0xD2D0)};
    store(ctx,frame+0x50,a[3]);store(ctx,frame+0x40,a[2]);store(ctx,frame+0x20,a[0]);store(ctx,frame+0x30,a[1]);
    u128 t[4],u[4];
    std::uint32_t count=word(ctx,prog+20,prog+34);    // read before the products, as compiled
    const std::uint32_t first=word(ctx,prog+22,prog+36)&0xFFFFu;
    for(int j=0;j<4;++j)t[j]=product_row(a,load(ctx,S+2816+16*j),true);
    for(int j=0;j<4;++j)store(ctx,frame+0x60+16*j,t[j]);
    for(int j=0;j<4;++j)u[j]=product_row(t,load(ctx,S+2880+16*j),false);
    for(int j=0;j<4;++j)store(ctx,frame+0xA0+16*j,u[j]);
    if(count&0xFFFFu) {
        std::uint32_t entry=prog+32+(first<<4);
        for(std::uint32_t i=0;;) {
            const std::uint32_t kind=word(ctx,entry,entry+14)&0xFFFFu;
            std::uint32_t source=kind==2?frame+0x60:kind==3?frame+0xA0:kind==1?frame+0x20:0;
            if(source) {
                const std::uint32_t out=word(ctx,cursor);
                put_word(ctx,out,0x00441EFCu);
                put_word(ctx,out+4,word(ctx,entry+2,entry+16)&0xFFFFu);
                put_word(ctx,cursor,out+8);
                std::uint32_t to=out+8;
                for(const std::uint32_t end=source+64;source!=end;source+=16) {
                    for(unsigned k=0;k<16;k+=4)put_word(ctx,to+k,word(ctx,source+k));
                    to=word(ctx,cursor)+16;put_word(ctx,cursor,to);
                }
                count=word(ctx,prog+20,prog+34);
            }
            if(!((count&0xFFFFu)>++i))break;
            entry+=4;
        }
    }
    // Epilogue.
    R(0)=load(ctx,frame+0x190);R(1)=spu_ai(R(1),384);
    R(80)=load(ctx,sp-0x10);R(81)=load(ctx,sp-0x20);R(82)=load(ctx,sp-0x30);R(83)=load(ctx,sp-0x40);
    R(84)=load(ctx,sp-0x50);R(85)=load(ctx,sp-0x60);R(86)=load(ctx,sp-0x70);R(87)=load(ctx,sp-0x80);
    R(88)=load(ctx,sp-0x90);R(126)=load(ctx,sp-0xA0);
    return 1;
}

// WKC_NATIVE_RENDER_VERIFY=1: run each installed routine's compiled code and
// its C++ on the same state and compare local store, the link, the stack
// pointer and the callee-saved registers. The compiled run's DMA writes are
// hidden from the per-transfer hook, so job replay still checks the C++ run.
extern "C" void (*g_spu_put_trace_hook)(uint32_t,uint32_t,int,uint32_t,const uint8_t*);
namespace {
struct VerifyStats {
    unsigned long long calls=0,mismatched=0;
    ~VerifyStats() {if(calls)std::fprintf(stderr,"[native-render-verify] calls=%llu mismatched=%llu\n",calls,mismatched);}
} verify_stats;
}
extern "C" int wkc_native_render_verify_enabled(void) {
    static const bool on=std::getenv("WKC_NATIVE_RENDER_VERIFY")!=nullptr;
    return on;
}
// Returns what `native` returned; on 1 the context holds the C++ result.
extern "C" int wkc_native_render_verify(spu_context* ctx,void (*reference)(spu_context*),
                                        int (*native)(spu_context*),unsigned entry) {
    static thread_local std::vector<std::uint8_t> before,expected;
    static thread_local spu_context ctx_before,ctx_expected;
    before.assign(ctx->ls,ctx->ls+SPU_LS_SIZE);ctx_before=*ctx;
    const std::uint32_t link=R(0)._u32[0];
    auto* hook=g_spu_put_trace_hook;g_spu_put_trace_hook=nullptr;
    reference(ctx);spu_drain_call(ctx,link);
    g_spu_put_trace_hook=hook;
    expected.assign(ctx->ls,ctx->ls+SPU_LS_SIZE);ctx_expected=*ctx;
    std::memcpy(ctx->ls,before.data(),SPU_LS_SIZE);*ctx=ctx_before;
    if(!native(ctx))return 0;
    ++verify_stats.calls;
    unsigned bad=0;
    for(std::uint32_t a=0;a<SPU_LS_SIZE;++a)
        if(ctx->ls[a]!=expected[a]&&bad++<4)
            std::fprintf(stderr,"[native-render-verify] %05X: LS %05X C++ %02X compiled %02X\n",entry,a,ctx->ls[a],expected[a]);
    for(int r=0;r<128;++r) {
        if(r>1&&r<80)continue;
        if(std::memcmp(&ctx->gpr[r],&ctx_expected.gpr[r],16)!=0&&bad++<8)
            std::fprintf(stderr,"[native-render-verify] %05X: r%d C++ %08X.%08X compiled %08X.%08X\n",entry,r,
                ctx->gpr[r]._u32[0],ctx->gpr[r]._u32[1],ctx_expected.gpr[r]._u32[0],ctx_expected.gpr[r]._u32[1]);
    }
    if(bad)++verify_stats.mismatched;
    return 1;
}
