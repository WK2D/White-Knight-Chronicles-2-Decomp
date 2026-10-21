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
#include "spu_context.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
std::uint8_t* quad(spu_context* ctx,std::uint32_t address) {
    return ctx->ls+(address&SPU_LS_MASK&~15u);
}
// Bytes `address`..`address+3` as the SPU sees them after rotating the quad
// that holds `quad_address` (rotqby): they wrap within that quad.
std::uint32_t rotated_word(spu_context* ctx,std::uint32_t quad_address,std::uint32_t address) {
    const std::uint8_t* q=quad(ctx,quad_address);std::uint32_t value=0;
    for(unsigned k=0;k<4;++k)value=value<<8|q[(address+k)&15];
    return value;
}
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
    const std::uint32_t header=rotated_word(ctx,cmd,cmd); // read before the saves, as compiled
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
        const std::uint32_t slot=rotated_word(ctx,cmd+8,cmd+8);
        std::uint32_t source=cmd+16,target=state+720u+2064u+(slot<<4);
        std::uint32_t copied=0;
        do {
            ++copied;
            std::memcpy(quad(ctx,target),quad(ctx,source),16);
            source+=16;target+=16;
        } while(((rotated_word(ctx,cmd+6,cmd+20)&0xFFFFu)>>4)>copied);
    }
    quad(ctx,state+720u+0x800u)[(sp-80u)&15u]=1;
    ctx->gpr[0]=spu_ls_read128(ctx,sp+0x10);
    ctx->gpr[80]=spu_ls_read128(ctx,sp-0x10);
    ctx->gpr[81]=spu_ls_read128(ctx,sp-0x20);
    ctx->gpr[82]=spu_ls_read128(ctx,sp-0x30);
    return 1;
}
