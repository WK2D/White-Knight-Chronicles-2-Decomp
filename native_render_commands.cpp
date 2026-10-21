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
#include <deque>
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
// Copy `quads` 16-byte rows from `source` to the output cursor, one word at
// a time, advancing the cursor by 16 after each row (as 0xA8E0 and 0x53C8 do).
void emit_rows(spu_context* ctx,std::uint32_t cursor,std::uint32_t to,std::uint32_t source,std::uint32_t quads) {
    for(;quads;--quads,source+=16) {
        for(unsigned k=0;k<16;k+=4)put_word(ctx,to+k,word(ctx,source+k));
        to=word(ctx,cursor)+16;put_word(ctx,cursor,to);
    }
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
                emit_rows(ctx,cursor,out+8,source,4);
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

// 0x53C8, called from the state flush when the constant table that command 25
// fills is dirty: upload table constants. The upload list is read like
// 0xA8E0's (count at +20, first quad at +22, 4-byte entries from +32); an
// entry of kind k > 4 uploads the run of table rows starting at row k - 4
// (state + 0xAE0 + 16 * (k - 4)) to its constant slot. A byte table at LS
// 0xD4F0 gives each run's length in rows. Rows go out eight at a time with
// SET_TRANSFORM_CONSTANT_LOAD (33 words), then the remainder in one shorter
// load. A nonempty list first makes sure 1 KB of output space is free.
extern "C" int wkc_native_render_table_constants(spu_context* ctx) {
    if(debug_watch_armed())return 0;
    const std::uint32_t sp=R(1)._u32[0],prog=R(4)._u32[0],state=R(3)._u32[0];
    store(ctx,sp-0x70,R(126));
    R(126)=spu_sf(spu_ila(0x53D8),spu_link(0x53D8));  // position-independent base: 0
    store(ctx,sp-0x10,R(80));store(ctx,sp-0x20,R(81));R(81)=R(5);
    store(ctx,sp-0x30,R(82));store(ctx,sp-0x40,R(83));R(83)=spu_il(20);
    store(ctx,sp-0x50,R(84));R(84)=R(4);store(ctx,sp-0x60,R(85));R(85)=R(3);
    store(ctx,sp-0x90,R(1));store(ctx,sp+0x10,R(0));
    R(1)=spu_ai(R(1),-144);R(82)=spu_ai(R(84),34);
    R(80)=spu_a(R(81),spu_il(4124));                  // 0x9B0: the output cursor block
    const std::uint32_t cursor=R(80)._u32[0]+8;
    if(word(ctx,prog+20,prog+34)&0xFFFFu) {
        R(4)=spu_il(1024);R(3)=R(81);
        call_compiled(ctx,0xFF0,0x5470);
        std::uint32_t count=word(ctx,prog+20,prog+34);
        const std::uint32_t first=word(ctx,prog+22,prog+36)&0xFFFFu;
        const std::uint32_t table=0xD4F0u+R(126)._u32[0];
        if(count&0xFFFFu) {
            std::uint32_t entry=prog+32+(first<<4);
            for(std::uint32_t i=0;;) {
                const std::uint32_t kind=word(ctx,entry,entry+14)&0xFFFFu;
                if(kind>4) {
                    const std::uint32_t row=kind-4,slot=word(ctx,entry+2,entry+16)&0xFFFFu;
                    const std::uint32_t words=std::uint32_t(quad(ctx,table+row)[(table+row)&15])<<2;
                    const std::uint32_t chunks=words>>5,rest=words&31;
                    std::uint32_t source=state+2784+(row<<4);
                    for(std::uint32_t c=0;c<chunks;++c,source+=128) {
                        const std::uint32_t out=word(ctx,cursor);
                        put_word(ctx,out,0x00841EFCu);put_word(ctx,out+4,slot+8*c);
                        for(unsigned k=0;k<64;k+=4)put_word(ctx,out+8+k,word(ctx,source+k));
                        const std::uint32_t half=word(ctx,cursor)+72;
                        for(unsigned k=0;k<64;k+=4)put_word(ctx,half+k,word(ctx,source+64+k));
                        put_word(ctx,cursor,word(ctx,cursor)+136);
                    }
                    if(rest) {
                        const std::uint32_t out=word(ctx,cursor);
                        put_word(ctx,out,(rest+1)<<18|0x1EFCu);put_word(ctx,out+4,slot+8*chunks);
                        put_word(ctx,cursor,out+8);
                        emit_rows(ctx,cursor,out+8,source,rest>>2);
                    }
                    count=word(ctx,prog+20,prog+34);
                }
                if(!((count&0xFFFFu)>++i))break;
                entry+=4;
            }
        }
    }
    const std::uint32_t frame=sp-144;
    R(0)=load(ctx,frame+0xA0);R(1)=spu_ai(R(1),144);
    R(80)=load(ctx,sp-0x10);R(81)=load(ctx,sp-0x20);R(82)=load(ctx,sp-0x30);R(83)=load(ctx,sp-0x40);
    R(84)=load(ctx,sp-0x50);R(85)=load(ctx,sp-0x60);R(126)=load(ctx,sp-0x70);
    return 1;
}

// 0xAD50 (r3 = state, r4 = job frame): flush dirty render state, called before
// each draw command. It waits for DMA tag 7, then, with S = state + 720:
// - byte S+0xA60 set: transform constants (0xA8E0), then clear it;
// - byte S+0x800 set (command 25's table): table constants (0x53C8), clear it;
// - word S+0x33C8 set: clear it and emit one word, (word S+0x17B4) | 2;
// - word S+0x33CC set: clear it and emit the shader block at T = S+0x2FC0.
// The shader block copies, for each of the word-at-T+24 entries at T+28 (8
// bytes: row, count, then count halfword offsets), row `row` of S with its
// halfwords swapped into S+0x1BC0+offset; uploads the fragment program
// (compiled 0x7B0) and stores its address at T; then emits methods 0x1FD8 (1),
// SET_SHADER_PROGRAM (address | 1), 0x1FF4 (word T+4), a texture-coordinate
// control (0xB40 + 4i) for each bit i of the halfword at T+8 with bits from
// the halfwords at T+10 and T+12, and SET_SHADER_CONTROL from T+16..T+20.
// Each output word goes to the cursor at frame+4124+8, which advances.
extern "C" int wkc_native_render_state_flush(spu_context* ctx) {
    if(debug_watch_armed())return 0;
    const std::uint32_t sp=R(1)._u32[0];
    R(5)=spu_il(720);
    store(ctx,sp-0x30,R(82));R(82)=spu_il(128);
    store(ctx,sp-0x10,R(80));store(ctx,sp-0x20,R(81));R(81)=R(3);
    store(ctx,sp-0x40,R(83));R(83)=spu_il(2);
    store(ctx,sp-0x60,R(85));R(85)=spu_a(R(81),R(5));
    store(ctx,sp-0xA0,R(89));R(89)=R(4);
    store(ctx,sp+0x10,R(0));store(ctx,sp-0x50,R(84));store(ctx,sp-0x70,R(86));store(ctx,sp-0x80,R(87));
    store(ctx,sp-0x90,R(88));store(ctx,sp-0xB0,R(126));store(ctx,sp-0xE0,R(1));
    R(1)=spu_ai(R(1),-224);
    const std::uint32_t frame=sp-224,S=R(85)._u32[0];
    auto wait_tag7=[&] {   // tag mask 1 << 7, wait for all
        spu_wrch(ctx,MFC_WrTagMask,R(82));spu_wrch(ctx,MFC_WrTagUpdate,R(83));R(3)=spu_rdch(ctx,MFC_RdTagStat);
    };
    // A dirty byte is quad byte 0; r80 keeps the compiled test value, whose
    // low byte is what the clear writes (at the stack pointer's byte slot).
    auto dirty=[&](std::uint32_t address) {
        R(80)=spu_xsbh(spu_ceqbi(spu_rotqbyi(load(ctx,address),13),0));
        return (R(80)._u32[0]&0xFFFFu)==0;
    };
    auto clear=[&](std::uint32_t address) {quad(ctx,address)[frame&15]=std::uint8_t(R(80)._u32[0]);};
    wait_tag7();
    if(dirty(S+0xA60)) {
        R(3)=spu_a(R(85),spu_il(11200));R(5)=R(89);R(4)=R(3);R(3)=R(81);   // 0xC3C8 is an identity stub
        call_compiled(ctx,0xA8E0,0xB434);
        clear(S+0xA60);
    }
    if(dirty(S+0x800)) {
        R(3)=spu_a(R(85),spu_il(11200));R(5)=R(89);R(4)=R(3);R(3)=R(81);
        call_compiled(ctx,0x53C8,0xB3FC);
        clear(S+0x800);
    }
    R(80)=spu_il(13256);
    if(word(ctx,S+13256,8)!=0) {
        const std::uint32_t cursor=R(89)._u32[0]+4124+8;
        put_word(ctx,(S+13256)&~15u|((frame+13256)&12u),0);
        wait_tag7();
        const std::uint32_t out=word(ctx,cursor);
        put_word(ctx,out,word(ctx,S+0x17B0,4)|2);
        put_word(ctx,cursor,out+4);
    }
    R(80)=spu_il(13260);
    if(word(ctx,S+13260,12)!=0) {
        R(88)=spu_a(R(89),spu_il(4124));                // 0x9B0
        const std::uint32_t cursor=R(88)._u32[0]+8;
        put_word(ctx,(S+13260)&~15u|((frame+13260)&12u),0);
        R(86)=spu_a(R(85),spu_il(12224));
        const std::uint32_t T=R(86)._u32[0];
        wait_tag7();
        std::uint32_t entries=word(ctx,T+24);
        if(entries) {
            R(87)=u128{};R(82)=spu_ai(R(86),28);R(84)=spu_a(R(85),spu_il(7104));
            for(;;) {
                const std::uint32_t e=R(82)._u32[0];
                const u128 head=load(ctx,e);
                R(83)=spu_andi(spu_rotqby(head,spu_ai(R(82),13)),255);
                R(81)=spu_ai(R(82),14);
                const std::uint32_t row=R(83)._u32[0]<<4;
                const std::uint32_t w[4]={word(ctx,S+row,row),word(ctx,S+row+4),word(ctx,S+row+8),word(ctx,S+row+12)};
                u128 swapped=load(ctx,frame+0x20);
                for(unsigned k=0;k<4;++k)swapped=with_word(swapped,frame+4*k,w[k]<<16|w[k]>>16);
                store(ctx,frame+0x20,swapped);
                if(byte_of(head,e+1)!=0) {
                    R(81)=spu_il(0);R(83)=spu_ila(0xFFFF);R(80)=spu_ai(R(82),2);
                    do {
                        const std::uint32_t at=R(80)._u32[0];
                        const std::uint32_t target=R(84)._u32[0]+(word(ctx,at,at+14)&0xFFFFu),source=frame+32;
                        R(81)=spu_ai(R(81),1);R(80)=spu_ai(R(80),2);
                        if((target|source)&15) {   // memcpy 0xCE90; aligned 16 bytes is one quad
                            R(3)=spu_il(0);R(3)._u32[0]=target;R(4)=spu_ai(R(1),32);R(5)=spu_il(16);
                            call_compiled(ctx,0xCE90,0xAF70);
                        } else std::memcpy(quad(ctx,target),quad(ctx,source),16);
                    } while(byte_of(load(ctx,e),e+1)>R(81)._u32[0]);
                    entries=word(ctx,T+24);
                }
                R(87)=spu_ai(R(87),1);
                if(!(entries>R(87)._u32[0]))break;
                R(82)=spu_ai(R(82),8);
            }
        }
        R(3)=R(89);R(87)=spu_iohl(spu_ilhu(4),0x8E4);R(4)=spu_a(R(85),spu_il(7104));
        R(83)=spu_il(6);R(80)=spu_ai(R(86),18);R(81)=spu_il(7);R(5)=load(ctx,S+13248);
        call_compiled(ctx,0x7B0,0xAFE8);                // fragment program upload; r3 = its address
        std::uint32_t out=word(ctx,cursor);
        put_word(ctx,out,0x00041FD8u);put_word(ctx,out+4,1);put_word(ctx,cursor,out+8);
        put_word(ctx,T,R(3)._u32[0]);
        put_word(ctx,out+8,0x000408E4u);put_word(ctx,out+12,(word(ctx,T)&0x1FFFFFFFu)|1);
        put_word(ctx,cursor,out+16);
        auto bytes=[&](std::uint32_t a) {
            return ls_byte(ctx,a)<<24|ls_byte(ctx,a+1)<<16|ls_byte(ctx,a+2)<<8|ls_byte(ctx,a+3);
        };
        put_word(ctx,out+16,0x00041FF4u);put_word(ctx,out+20,bytes(T+4));
        out+=24;put_word(ctx,cursor,out);
        std::uint32_t enabled=ls_byte(ctx,T+8)<<8|ls_byte(ctx,T+9),bit_b=ls_byte(ctx,T+10)<<8|ls_byte(ctx,T+11);
        std::uint32_t bit_c=ls_byte(ctx,T+12)<<8|ls_byte(ctx,T+13);
        if(enabled) {
            for(std::uint32_t method=2880;enabled;enabled>>=1,bit_b>>=1,bit_c>>=1,method+=4) {
                if(!(enabled&1))continue;
                put_word(ctx,out,0x40000u|method);put_word(ctx,out+4,(bit_b&1)|(bit_c&1)<<4);
                out+=8;
            }
            put_word(ctx,cursor,out);
        }
        const std::uint32_t mode=ls_byte(ctx,T+20);
        put_word(ctx,out,0x00041D60u);
        put_word(ctx,out+4,(mode>1?mode<<24:0x02000000u)|bytes(T+16));
        put_word(ctx,cursor,out+8);
    }
    R(0)=load(ctx,frame+0xF0);R(1)=spu_ai(R(1),224);
    R(80)=load(ctx,sp-0x10);R(81)=load(ctx,sp-0x20);R(82)=load(ctx,sp-0x30);R(83)=load(ctx,sp-0x40);
    R(84)=load(ctx,sp-0x50);R(85)=load(ctx,sp-0x60);R(86)=load(ctx,sp-0x70);R(87)=load(ctx,sp-0x80);
    R(88)=load(ctx,sp-0x90);R(89)=load(ctx,sp-0xA0);R(126)=load(ctx,sp-0xB0);
    return 1;
}

namespace {
u128 word_reg(std::uint32_t v) {u128 r{};r._u32[0]=v;return r;}   // channel writes use the preferred word
void dma(spu_context* ctx,std::uint32_t lsa,std::uint32_t ea,std::uint32_t size,std::uint32_t tag,std::uint32_t command) {
    spu_wrch(ctx,MFC_LSA,word_reg(lsa));spu_wrch(ctx,MFC_EAH,word_reg(0));spu_wrch(ctx,MFC_EAL,word_reg(ea));
    spu_wrch(ctx,MFC_Size,word_reg(size));spu_wrch(ctx,MFC_TagID,word_reg(tag));spu_wrch(ctx,MFC_Cmd,word_reg(command));
}
void wait_tags(spu_context* ctx,std::uint32_t mask) {
    spu_wrch(ctx,MFC_WrTagMask,word_reg(mask));spu_wrch(ctx,MFC_WrTagUpdate,word_reg(2));(void)spu_rdch(ctx,MFC_RdTagStat);
}
// `heqi` on a DMA address that is not 16-byte aligned stops the SPU.
bool misaligned(spu_context* ctx,std::uint32_t ea,unsigned pc) {
    if(!(ea&15))return false;
    std::fprintf(stderr,"[spu] HALT-ASSERT heqi pc=0x%05X img=%d\n",pc,ctx->image_id);
    ctx->status=SPU_STATUS_STOPPED_BY_HALT;spu_halt(ctx);
    return true;
}
// Sixteen bytes stored at any address: the SPU merges them into the two
// quads they touch (read before the cursor update, written high quad first).
struct Spill {std::uint32_t at;u128 low,high;};
Spill spill_begin(spu_context* ctx,std::uint32_t at) {return {at,load(ctx,at),load(ctx,at+16)};}
void spill_end(spu_context* ctx,const Spill& s,const u128& v) {
    const unsigned m=s.at&15;
    std::uint8_t low[16],high[16],bytes[16];
    for(unsigned k=0;k<16;++k){low[k]=std::uint8_t(byte_of(s.low,k));high[k]=std::uint8_t(byte_of(s.high,k));bytes[k]=std::uint8_t(byte_of(v,k));}
    for(unsigned k=0;k<m;++k)high[k]=bytes[16-m+k];
    for(unsigned k=m;k<16;++k)low[k]=bytes[k-m];
    std::memcpy(quad(ctx,s.at+16),high,16);std::memcpy(quad(ctx,s.at),low,16);
}
}

// 0x9D58 (r3 = state, r4 = command, r5 = job frame): build the draws of
// command 12, after the state flush. With S = state + 720 it first refreshes
// three constant blocks whose dirty bytes sit at S+0xA74..0xA76, uploading
// each through the compiled 0x1000 (which returns its address): the three
// rows at S+0xB80 (to S+0xA68), the bone matrices at S+0xBB0 (count at
// S+0xA64, address to S+0xA6C) and, after forming M3 * M2 into S+0xA90, the
// 128 bytes at S+0xA80 (address to S+0xA70). It notes the vertex-attribute
// mask (halfword at S+0x2BD0) and, when state+136 is set, a shader option.
// Then it walks the command's draw records: word +16 counts them, +20 is
// the first record's address (256 bytes each) and +24 the address of 32-byte
// attribute lists; records are fetched three-deep with DMA tag 5. For each
// record it emits SET_VERTEX_DATA_ARRAY_FORMAT and _OFFSET for every listed
// attribute (offsets from word +28), reserves its index data in the command
// stream (compiled 0xE50) when it has any, fills in the state words, the
// base vertex offset and the attribute mask, and writes the record back
// (tag 11). Finally it runs the compiled helpers 0x7C80, 0x2228 and 0x1F8.
extern "C" int wkc_native_render_draw(spu_context* ctx) {
    if(debug_watch_armed())return 0;
    const std::uint32_t sp=R(1)._u32[0];
    for(int r=80;r<=117;++r)store(ctx,sp-0x10*std::uint32_t(r-79),R(r));
    store(ctx,sp-0x270,R(126));store(ctx,sp+0x10,R(0));store(ctx,sp-0x670,R(1));
    R(82)=R(4);R(86)=spu_a(R(3),spu_il(720));R(105)=R(3);R(90)=R(5);R(106)=spu_il(0);
    R(1)=spu_a(R(1),spu_il(-1648));
    const std::uint32_t frame=sp-1648,S=R(86)._u32[0],state=R(105)._u32[0],cmd=R(82)._u32[0];
    wait_tags(ctx,512);
    R(92)=spu_andi(spu_ai(R(1),159),-128);
    // Each flag test keeps its compiled register; its low byte is what the
    // clear writes, at the stack pointer's byte slot plus the flag's offset.
    auto flag_clear=[&](const u128& q,unsigned offset,const u128& test) {
        return spu_shufb(test,q,spu_cbd(R(1),int(offset)));
    };
    R(80)=spu_xsbh(spu_ceqbi(spu_rotqbyi(load(ctx,S+0xA70),2),0));
    if((R(80)._u32[0]&0xFFFFu)==0) {                  // S+0xA75: the three rows at S+0xB80
        R(3)=R(5);R(5)=spu_il(48);R(4)=spu_a(R(86),spu_il(2944));
        call_compiled(ctx,0x1000,0xA648);
        const u128 q60=load(ctx,S+0xA60),q70=load(ctx,S+0xA70);
        store(ctx,S+0xA60,with_word(q60,frame+8,R(3)._u32[0]));
        store(ctx,S+0xA70,flag_clear(q70,5,R(80)));
    }
    if(byte_of(load(ctx,S+0xA70),6)!=0) {             // S+0xA76: bone matrices
        const std::uint32_t bones=word(ctx,S+0xA60,4);
        std::uint32_t address=0;
        if(bones) {
            R(3)=R(90);R(4)=spu_a(R(86),spu_il(2992));R(5)=spu_il(std::int32_t(48*bones));
            call_compiled(ctx,0x1000,0xA8C8);
            address=R(3)._u32[0];
        }
        store(ctx,S+0xA60,with_word(load(ctx,S+0xA60),frame+12,address));
        store(ctx,S+0xA70,flag_clear(load(ctx,S+0xA70),6,spu_il(0)));
    }
    R(83)=spu_xsbh(spu_ceqbi(spu_rotqbyi(load(ctx,S+0xA70),1),0));
    if((R(83)._u32[0]&0xFFFFu)==0) {                  // S+0xA74: M3 * M2, then the 128-byte block
        R(101)=spu_il(2704);R(100)=spu_il(2816);R(99)=spu_il(2880);
        R(81)=spu_a(R(86),R(101));R(98)=spu_a(R(86),R(100));
        const u128 m3base=spu_a(R(86),R(99));
        R(94)=spu_ila(0x10203);R(91)=spu_iohl(spu_ilhu(0x405),0x607);
        const u128 lane2=spu_iohl(spu_ilhu(0x809),0xA0B),lane3=spu_iohl(spu_ilhu(0xC0D),0xE0F);
        const std::uint32_t m2=R(98)._u32[0],m3=m3base._u32[0];
        R(85)=load(ctx,m2);R(84)=load(ctx,m2+16);
        const u128 b2=load(ctx,m2+32),b3=load(ctx,m2+48);
        const u128 rows[4]={load(ctx,m3),load(ctx,m3+16),load(ctx,m3+32),load(ctx,m3+48)};
        // The compiler kept some of the splatted lanes in callee-saved registers.
        R(97)=spu_shufb(rows[3],rows[3],R(94));R(96)=spu_shufb(rows[3],rows[3],R(91));
        R(95)=spu_shufb(rows[0],rows[0],R(94));R(93)=spu_shufb(rows[0],rows[0],R(91));
        R(89)=spu_shufb(rows[1],rows[1],R(94));R(88)=spu_shufb(rows[1],rows[1],R(91));
        R(87)=spu_shufb(rows[2],rows[2],R(94));R(80)=spu_shufb(rows[2],rows[2],R(91));
        const u128 b[4]={R(85),R(84),b2,b3};
        for(int j=0;j<4;++j) {
            const u128 m=rows[j];
            const u128 odd=spu_fma(b[3],spu_shufb(m,m,lane3),spu_fm(b[1],spu_shufb(m,m,R(91))));
            const u128 even=spu_fma(b[2],spu_shufb(m,m,lane2),spu_fm(b[0],spu_shufb(m,m,R(94))));
            store(ctx,R(81)._u32[0]+16*j,spu_fa(even,odd));
        }
        R(3)=R(90);R(4)=spu_a(R(86),spu_il(2688));R(5)=spu_il(128);
        call_compiled(ctx,0x1000,0xA794);
        const u128 q70=with_word(load(ctx,S+0xA70),frame,R(3)._u32[0]);
        store(ctx,S+0xA70,flag_clear(q70,4,R(83)));
    }
    // Attribute mask and shader option.
    R(109)=spu_il(11200);R(104)=spu_chd(R(1),2);R(108)=spu_il(136);
    R(102)=spu_ai(R(105),136);R(83)=load(ctx,state+136);
    R(107)=load(ctx,S+11216);R(116)=spu_rotqby(R(83),R(102));
    R(103)=spu_rotqby(R(107),spu_a(R(86),R(109)));R(106)=spu_shufb(R(103),R(106),R(104));
    if(R(116)._u32[0]!=0) {
        R(81)=spu_chd(R(1),0);
        R(106)=spu_shufb(spu_ori(spu_rotmi(R(106),-16),1),R(106),R(81));
        R(5)=spu_il(0);R(4)=spu_a(R(90),spu_il(4124));R(3)=spu_ai(R(105),120);
        call_compiled(ctx,0x2228,0xA838);
        const u128 option=spu_rotqby(load(ctx,state+200),spu_ai(R(105),200));
        const u128 mode=spu_rotqby(load(ctx,state+152),spu_ai(R(105),152));
        R(111)=spu_shufb(spu_ori(spu_rotmi(R(106),-16),2),R(106),R(81));
        R(117)=spu_sfi(spu_ceqi(option,0),0);
        R(115)=spu_selb(R(117),spu_xori(R(117),1),spu_ceqi(mode,4));
        R(114)=spu_ceqbi(R(115),0);R(113)=spu_xsbh(R(114));R(112)=spu_ceqhi(R(113),0);
        R(110)=spu_fsm(R(112));R(106)=spu_selb(R(106),R(111),R(110));
    }
    R(117)=spu_a(R(90),spu_il(4124));                // 0x9B0: the output cursor block
    R(84)=spu_ai(R(82),16);
    R(3)=R(105);R(4)=R(117);
    call_compiled(ctx,0x7C80,0x9F10);
    const std::uint32_t cursor=R(117)._u32[0]+8;
    {
        const u128 first=spu_rotqby(load(ctx,cmd+20),spu_ai(R(82),20));
        R(87)=first;
        const std::uint32_t flags=word(ctx,cmd+28);
        R(114)=spu_and(spu_rotqby(load(ctx,cmd+28),spu_ai(R(82),28)),spu_iohl(spu_ilhu(0x7FFF),0xFFFF));
        if(misaligned(ctx,first._u32[0],0x9F60))return 1;
        dma(ctx,R(92)._u32[0],first._u32[0],256,5,64);
        const std::uint32_t lists=word(ctx,cmd+24);
        if(lists) {
            if(misaligned(ctx,lists,0xA7E4))return 1;
            dma(ctx,frame+32+896,lists,32,5,64);
        }
        if(!word(ctx,cmd+16))goto finish;
        R(95)=spu_iohl(spu_ilhu(0xAAAA),0xAAAB);R(96)=spu_iohl(spu_ilhu(0xAAAB),0xAAAA);
        R(108)=spu_il(256);R(113)=spu_il(5);R(112)=spu_il(64);R(82)=spu_il(896);
        R(91)=u128{};R(94)=u128{};R(111)=spu_il(std::int32_t(flags>>31<<31));
        R(104)=spu_and(R(106),spu_ila(0xFFFF));R(115)=spu_a(spu_ai(R(1),32),spu_il(896));
        R(93)=spu_il(32);R(109)=spu_il(2);
    }
    {
        const std::uint32_t buffers=R(92)._u32[0],lists=R(115)._u32[0];
        std::uint32_t slot=0,ea=R(87)._u32[0];
        for(;;) {
            const std::uint32_t next=(slot+1)%3,next_ea=ea+256;
            R(88)=spu_il(std::int32_t(next_ea));
            wait_tags(ctx,32);
            R(83)=spu_il(std::int32_t(next));
            R(81)=spu_a(R(92),spu_il(std::int32_t(slot<<8)));
            const std::uint32_t record=R(81)._u32[0];
            if(misaligned(ctx,next_ea,0xA1E0))return 1;
            dma(ctx,buffers+(next<<8),next_ea,256,5,64);
            const u128 list_q=spu_rotqby(load(ctx,cmd+24),spu_ai(R(84),8));
            if(list_q._u32[0]) {
                R(85)=spu_shlqbyi(spu_rotqmbyi(spu_a(spu_ai(list_q,32),R(94)),-4),4);
                if(misaligned(ctx,R(85)._u32[0],0xA23C))return 1;
                dma(ctx,lists+(next<<5),R(85)._u32[0],32,5,64);
                if(word(ctx,cmd+24)) {
                    const std::uint32_t list=lists+(slot<<5);
                    R(82)=spu_a(R(115),spu_il(std::int32_t(slot<<5)));
                    if(word(ctx,list,list+14)&0xFFFFu) {
                        R(85)=spu_il(0);
                        for(std::uint32_t at=list;;at+=4) {
                            const std::uint32_t out_block=R(90)._u32[0]+4124;
                            const std::uint32_t index=ls_byte(ctx,at+12),mask=R(104)._u32[0];
                            const std::uint32_t format=0x40000u|(5952+4*index),offset=0x40000u|(5760+4*index);
                            u128 v{};
                            v=with_word(v,frame,format);
                            if((index&63)<32&&(mask>>(index&63)&1)) {   // enabled (rotma counts mod 64)
                                const std::uint32_t layout=byte_of(load(ctx,list),list+3)<<8|ls_byte(ctx,at+13)<<4|ls_byte(ctx,at+14);
                                const std::uint32_t base=R(114)._u32[0]+word(ctx,list+4)+ls_byte(ctx,at+15);
                                v=with_word(v,frame+4,layout);v=with_word(v,frame+8,offset);
                                v=with_word(v,frame+12,base|R(111)._u32[0]);
                            } else {                          // disabled: format only
                                v=with_word(v,frame+4,ls_byte(ctx,at+14));v=with_word(v,frame+8,offset);
                            }
                            const std::uint32_t out=word(ctx,out_block+8);
                            const Spill spill=spill_begin(ctx,out);
                            put_word(ctx,out_block+8,out+16);
                            spill_end(ctx,spill,v);
                            R(85)=spu_ai(R(85),1);
                            if(!((word(ctx,list,list+14)&0xFFFFu)>R(85)._u32[0]))break;
                        }
                    }
                }
            }
            // Index data, state words, base vertex offset and attribute mask.
            R(80)=load(ctx,record+0xD0);R(82)=spu_rotqby(R(80),R(81));
            if(R(82)._u32[1]) {
                R(3)=R(90);R(4)=spu_shlqbyi(R(82),4);
                call_compiled(ctx,0xE50,0xA4B8);
                R(97)=spu_rotqmbyi(R(3),-4);R(98)=spu_cdd(R(81),0);R(89)=load(ctx,record+0xD0);
                R(85)=spu_shufb(R(97),R(89),R(98));
                store(ctx,record+0xD0,R(85));
            }
            {
                const u128 q70=load(ctx,S+0xA70),q60=load(ctx,S+0xA60);
                const u128 high_80=spu_andbi(spu_fsmbi(0x1010),-128),high_30=spu_andbi(spu_fsmbi(0x1010),48);
                store(ctx,record+0xA0,spu_shufb(spu_or(spu_rotqmbyi(q70,-4),high_80),load(ctx,record+0xA0),spu_cdd(R(81),0)));
                store(ctx,record+168,spu_shufb(spu_or(spu_rotqmbyi(spu_rotqbyi(q60,8),-4),high_30),load(ctx,record+168),spu_cdd(R(81),8)));
            }
            R(80)=spu_ai(R(81),72);
            {
                const std::uint32_t base=word(ctx,S+0xA60,12);
                if(base==0||word(ctx,record+72)+word(ctx,record+80)==0) {
                    store(ctx,record+72,spu_shufb(spu_il(0),load(ctx,record+72),spu_cdd(R(81),8)));
                    store(ctx,record+0x50,spu_shufb(spu_il(0),load(ctx,record+0x50),spu_cdd(R(81),0)));
                } else {
                    put_word(ctx,record+76,word(ctx,record+76)+base);
                    put_word(ctx,record+84,word(ctx,record+84)+word(ctx,S+0xA60,12));
                }
            }
            store(ctx,record+216,spu_shufb(R(106),load(ctx,record+216),spu_cdd(R(81),8)));
            wait_tags(ctx,2048);
            if(misaligned(ctx,ea,0xA114))return 1;
            dma(ctx,record,ea,256,11,32);
            R(91)=spu_ai(R(91),1);R(94)=spu_ai(R(94),32);
            if(!(word(ctx,cmd+16)>R(91)._u32[0]))break;
            ea=next_ea;slot=next;
        }
    }
finish:
    wait_tags(ctx,32);wait_tags(ctx,2048);
    R(3)=R(105);R(4)=R(117);
    call_compiled(ctx,0x7C80,0xA554);
    if(R(116)._u32[0]) {
        R(5)=R(116);R(4)=spu_a(R(90),spu_il(4124));R(3)=spu_ai(R(105),120);
        call_compiled(ctx,0x2228,0xA570);
    }
    R(3)=R(90);
    call_compiled(ctx,0x1F8,0xA578);
    R(0)=load(ctx,frame+0x680);R(1)=spu_a(R(1),spu_il(1648));
    for(int r=80;r<=117;++r)R(r)=load(ctx,sp-0x10*std::uint32_t(r-79));
    R(126)=load(ctx,sp-0x270);
    return 1;
}

// WKC_NATIVE_RENDER_VERIFY=1: run each installed routine's compiled code and
// its C++ on the same state and compare local store, the link, the stack
// pointer and the callee-saved registers. The C++ rerun's DMA writes are
// hidden from the per-transfer hook (a tag wait may complete transfers the
// compiled run already reported); a plain replay checks the C++ transfers.
// Main memory the compiled run read is put back before the rerun, newest
// read first, so a routine that reads, changes and writes back a record sees
// the same input twice.
extern "C" void (*g_spu_put_trace_hook)(uint32_t,uint32_t,int,uint32_t,const uint8_t*);
extern "C" void (*g_spu_get_trace_hook)(uint32_t,uint32_t,int,uint32_t,const uint8_t*);
extern "C" uint8_t* vm_base;
namespace {
struct GetRecord {std::uint32_t ea;std::vector<std::uint8_t> bytes;};
thread_local std::vector<std::vector<GetRecord>*> recorders;   // levels in their compiled run
void (*previous_get_hook)(uint32_t,uint32_t,int,uint32_t,const uint8_t*)=nullptr;
void record_get(uint32_t ea,uint32_t size,int image,uint32_t pc,const uint8_t* bytes) {
    for(auto* r:recorders)r->push_back({ea,std::vector<std::uint8_t>(bytes,bytes+size)});
    if(previous_get_hook)previous_get_hook(ea,size,image,pc,bytes);
}
struct VerifyStats {
    unsigned long long calls=0,mismatched=0;
    ~VerifyStats() {if(calls)std::fprintf(stderr,"[native-render-verify] calls=%llu mismatched=%llu\n",calls,mismatched);}
} verify_stats;
}
extern "C" int wkc_native_render_verify_enabled(void) {
    static const bool on=std::getenv("WKC_NATIVE_RENDER_VERIFY")!=nullptr;
    return on;
}
// Returns 1 when the context holds the C++ result, 2 when the C++ declined and
// it holds the compiled result (either way the routine has returned).
extern "C" int wkc_native_render_verify(spu_context* ctx,void (*reference)(spu_context*),
                                        int (*native)(spu_context*),unsigned entry) {
    // Routines nest (the state flush calls the constant uploads), so each
    // level keeps its own snapshots.
    struct Level {std::vector<std::uint8_t> before,expected;spu_context ctx_before,ctx_expected;std::vector<GetRecord> gets;};
    static thread_local std::deque<Level> levels;   // grows without moving outer levels
    static thread_local unsigned depth=0;
    if(levels.size()<=depth)levels.resize(depth+1);
    struct Nest {Nest(){++depth;}~Nest(){--depth;}} nest;
    Level& level=levels[depth-1];
    auto& before=level.before;auto& expected=level.expected;
    auto& ctx_before=level.ctx_before;auto& ctx_expected=level.ctx_expected;
    before.assign(ctx->ls,ctx->ls+SPU_LS_SIZE);ctx_before=*ctx;
    const std::uint32_t link=R(0)._u32[0];
    if(g_spu_get_trace_hook!=record_get){previous_get_hook=g_spu_get_trace_hook;g_spu_get_trace_hook=record_get;}
    level.gets.clear();recorders.push_back(&level.gets);
    reference(ctx);spu_drain_call(ctx,link);
    recorders.pop_back();
    expected.assign(ctx->ls,ctx->ls+SPU_LS_SIZE);ctx_expected=*ctx;
    std::memcpy(ctx->ls,before.data(),SPU_LS_SIZE);*ctx=ctx_before;
    for(auto g=level.gets.rbegin();g!=level.gets.rend();++g)std::memcpy(vm_base+g->ea,g->bytes.data(),g->bytes.size());
    auto* hook=g_spu_put_trace_hook;g_spu_put_trace_hook=nullptr;
    const int ran=native(ctx);
    if(!ran) {reference(ctx);spu_drain_call(ctx,link);}   // declined: the compiled result, unreported again
    g_spu_put_trace_hook=hook;
    if(!ran)return 2;
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
