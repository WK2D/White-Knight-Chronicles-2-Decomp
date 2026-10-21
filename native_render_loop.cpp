// Image 20's display-list interpreter loop in C++ (LS 0x1968 -> 0xFF0/0xFC0 ->
// 0x1308 -> fetch 0xC540). See native_render_commands.cpp for the command
// handlers and NATIVE_CODE.md for the structure.
//
// Each pass: check that more than 1 KB of command-output space is free (the
// rare flush runs the compiled 0xC40), record the finished command's id in the
// frame at SP+0x1E70, fetch the next command through the 2 KB ring buffer
// (refilling it with blocking DMA and starting the command's payload reads),
// then dispatch through the 37-entry table at 0x137C.
//
// The fetch and the space check work on local-store bytes with plain integers.
// They make the same local-store writes and DMA requests, in the same order,
// as the compiled code, and leave every register the command handlers can
// read (those 0x1308 sets after the fetch, which share the handler's SPU
// function) with its compiled value. Registers those calls set and that die
// at their return are not reproduced. The rare output flush follows the
// compiled instructions one for one and runs the compiled 0xC40.
//
// The loop runs only when the runtime's per-transfer hooks are idle (the
// direct-jump gate) and no LS debug watch is armed; otherwise it declines
// before any effect and the compiled loop runs. Jobs never enable SPU
// interrupts, and the other gates are fixed configuration, so one check per
// pass is enough.
#include "spu_context.h"
#include "spu_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
u128 spu_rdch(spu_context* ctx,uint32_t channel);
void spu_wrch(spu_context* ctx,uint32_t channel,u128 value);
void spu_halt(spu_context* ctx);
typedef void (*wkc_spu_fn)(spu_context*);
wkc_spu_fn spu_lookup(uint32_t addr,int image_id);
}

namespace {
#define R(n) ctx->gpr[n]
inline uint32_t P(const u128& v) {return v._u32[0];}
inline u128 LS(spu_context* ctx,uint32_t a) {return spu_ls_read128(ctx,a);}
inline void ST(spu_context* ctx,uint32_t a,u128 v) {spu_ls_write128(ctx,a,v);}

bool native_loop_enabled() {
    static const bool enabled=[]{
        const char* v=std::getenv("WKC_NATIVE_RENDER_COMMANDS");return !v||std::strcmp(v,"0")!=0;}();
    return enabled;
}

wkc_spu_fn compiled(spu_context* ctx,uint32_t address) {
    // Fixed code addresses of this image; resolved once per address.
    struct Slot {uint32_t address;wkc_spu_fn fn;};
    static thread_local Slot slots[16];
    for(auto& s:slots) {
        if(s.address==address&&s.fn)return s.fn;
        if(!s.fn){s.address=address;s.fn=spu_lookup(address,ctx->image_id);return s.fn;}
    }
    return spu_lookup(address,ctx->image_id);
}

// One compiled-style hop inside a nested call: run the target here if the
// direct-jump gate allows it, otherwise leave it to the enclosing drain.
// Returns true when the caller should keep going inline.
bool hop(spu_context* ctx,uint32_t pc) {
    ctx->pc=pc;
    if(spu_direct_jump_ok(ctx)){ctx->steps++;return true;}
    g_spu_trampoline_fn=compiled(ctx,pc);
    return false;
}

void halt_assert(spu_context* ctx,unsigned pc) {
    std::fprintf(stderr,"[spu] HALT-ASSERT heqi pc=0x%05X img=%d\n",pc,ctx->image_id);
    ctx->status=SPU_STATUS_STOPPED_BY_HALT;spu_halt(ctx);
}

// --- Fast path on local-store bytes -------------------------------------
// Local store holds guest big-endian bytes; a 16-byte access uses the quad
// holding its address, and a word read by rotating that quad wraps inside it.
std::uint8_t* quad_at(spu_context* ctx,uint32_t address) {return ctx->ls+(address&SPU_LS_MASK&~15u);}
uint32_t be32(const std::uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint32_t word_at(spu_context* ctx,uint32_t quad_address,uint32_t rotate) {
    const std::uint8_t* q=quad_at(ctx,quad_address);uint32_t v=0;
    for(unsigned k=0;k<4;++k)v=v<<8|q[(rotate+k)&15];
    return v;
}
void put_word(std::uint8_t* q,unsigned offset,uint32_t v) {
    q[offset]=std::uint8_t(v>>24);q[offset+1]=std::uint8_t(v>>16);q[offset+2]=std::uint8_t(v>>8);q[offset+3]=std::uint8_t(v);
}
u128 word_reg(uint32_t v) {u128 r{};r._u32[0]=v;return r;} // channel writes use the preferred word
void dma(spu_context* ctx,uint32_t lsa,uint32_t ea,uint32_t size,uint32_t tag,uint32_t command) {
    spu_wrch(ctx,MFC_LSA,word_reg(lsa));spu_wrch(ctx,MFC_EAH,word_reg(0));spu_wrch(ctx,MFC_EAL,word_reg(ea));
    spu_wrch(ctx,MFC_Size,word_reg(size));spu_wrch(ctx,MFC_TagID,word_reg(tag));spu_wrch(ctx,MFC_Cmd,word_reg(command));
}

// The fetch call at 0xC540 on bytes. Ring state: words 1-3 of the quad at
// ring+0x950 are the list position, list end and ring read index (quads); word
// 0 at ring+0x960 is the number of quads in the ring. Leaves r3 as compiled;
// the routine's other registers are caller-saved and dead at its return.
void fetch_fast(spu_context* ctx) {
    const u128 ring_q=R(3);const uint32_t ring=P(ring_q),state=P(R(4)),sp=P(R(1));
    const std::uint8_t* q950=quad_at(ctx,ring+0x950);
    const uint32_t position=be32(q950+4),end=be32(q950+8);
    if(!(end>position)){R(3)=u128{};ctx->pc=P(R(0));return;}      // end of list: return 0
    const uint32_t remaining=end-position;
    const u128 index_q=spu_rotqbyi(spu_ls_read128(ctx,ring+0x950),12);
    uint32_t index=be32(q950+12),avail=be32(quad_at(ctx,ring+0x960))-index;
    bool refilled=false;
    // c850 (ring empty) / c768 (command doesn't fit): GET up to 2 KB of the
    // list from the current position into the ring, wait, restart the index.
    auto refill=[&](unsigned halt_pc) {
        const uint32_t amount=remaining>2048u?2048u:remaining;
        put_word(quad_at(ctx,ring+0x960),sp&12u,amount);
        if((ring&15)||(position&15)||amount>16384u||(amount&15)){halt_assert(ctx,halt_pc);return false;}
        dma(ctx,ring,position,amount,4,64);
        spu_wrch(ctx,MFC_WrTagMask,word_reg(16));spu_wrch(ctx,MFC_WrTagUpdate,word_reg(2));
        (void)spu_rdch(ctx,MFC_RdTagStat);
        std::uint8_t* q960=quad_at(ctx,ring+0x960);
        avail=be32(q960)>>4;
        put_word(quad_at(ctx,ring+0x950),(sp+12)&12u,0);
        put_word(q960,sp&12u,avail);
        index=0;refilled=true;
        return true;
    };
    if(avail==0&&!refill(0x0C8C0))return;
    uint32_t command=ring+(index<<4);
    uint32_t header=word_at(ctx,command,command);                       // c578
    if((((header&0xFFFFu)+16u)>>4)>avail) {
        if(!refill(0x0C7D8))return;
        command=ring;header=be32(quad_at(ctx,ring));                   // c768 -> c5a8
    }
    {   // c5a8: advance the ring index and list position past this command.
        const uint32_t size=header&0xFFFFu;
        std::uint8_t* q=quad_at(ctx,ring+0x950);
        put_word(q,(sp+12)&12u,(size>>4)+index+1);
        put_word(q,(sp+4)&12u,position+16+size);
    }
    // Start the payload reads the command names (tag 5, no wait).
    const uint32_t first=word_at(ctx,command+4,command+18)&0xFFFFu;
    if(first) {
        const uint32_t ea=word_at(ctx,command+8,command+8),lsa=state+16;
        if(first>16384u||(first&15)||(lsa&15)||(ea&15)){halt_assert(ctx,0x0C728);return;}
        dma(ctx,lsa,ea,first,5,65);
    }
    const uint32_t second=word_at(ctx,command+6,command+20)&0xFFFFu;
    if(second) {
        const uint32_t ea=word_at(ctx,command+12,command+12),lsa=state+2064;
        if(second>16384u||(second&15)||(lsa&15)||(ea&15)){halt_assert(ctx,0x0C684);return;}
        dma(ctx,lsa,ea,second,5,65);
    }
    R(3)=refilled?ring_q:spu_a(ring_q,spu_shli(index_q,4));
    ctx->pc=P(R(0)); // SPU_RET with a live host frame
}

// The command's handler, resolved once per table entry instead of through the
// runtime's indirect-branch lookup on every command. The table lives in the
// job's local store, so each cached entry is keyed by the address it read.
// For a lifted target below the taskset task region (every entry of this
// table is) the runtime's lookup only masks the pc, clears the resident task
// and resolves in the job's image; the loop's gate has already excluded
// policy mode, resident overlays and armed stack resets. Anything else (a
// pending interrupt return, a resident code span over the target, a target
// in the task region or with no lifted function) keeps the indirect branch.
wkc_spu_fn handler(spu_context* ctx,uint32_t index,uint32_t target) {
    struct Slot {uint32_t target;int32_t image;wkc_spu_fn fn;};
    static thread_local Slot slots[37];
    target&=SPU_LS_MASK;ctx->pc=target;
    if(!target||target>=0x3000u||ctx->irq_saved)return spu_indirect_branch;
    for(const auto& code:ctx->resident_code)
        if(code.image_id&&target>=code.lsa&&target-code.lsa<code.size)return spu_indirect_branch;
    Slot& s=slots[index];
    if(!(s.fn&&s.target==target&&s.image==ctx->image_id)) {
        const wkc_spu_fn fn=spu_lookup(target,ctx->image_id);
        if(!fn)return spu_indirect_branch;
        s={target,ctx->image_id,fn};
    }
    ctx->resident_task=0;
    return s.fn;
}

// A lifted `brsl`: link, nested host frame, body, then drain to the link.
template<class Body> void call(spu_context* ctx,uint32_t link,Body body) {
    R(0)=spu_link(link);
    const int32_t saved=ctx->image_id;ctx->host_depth++;
    body();
    spu_drain_call(ctx,link);
    ctx->host_depth--;spu_img_restore(ctx,saved);
}
}

// Runs the interpreter loop from 0x1968 (from_return=1, after a command) or
// 0x1308 (from_return=0). Returns the next function with ctx->pc set: the
// dispatcher for the next command's handler, or the compiled exit/error path.
// nullptr means "declined": nothing has run, use the compiled reference.
namespace {
// WKC_NATIVE_RENDER_STATS=1: count native passes and declines, printed at exit.
struct Stats {
    unsigned long long passes=0,declined=0;bool on=false;
    Stats() {on=std::getenv("WKC_NATIVE_RENDER_STATS")!=nullptr;}
    ~Stats() {if(on)std::fprintf(stderr,"[native-render-loop] passes=%llu declined=%llu\n",passes,declined);}
} stats;
}

extern "C" wkc_spu_fn wkc_native_render_loop(spu_context* ctx,int from_return) {
    if(!native_loop_enabled()||g_spu_ls_watch_n!=0||g_spu_smc_watch!=0||g_spu_ls_probe!=0||
       ctx->policy_mode||ctx->irq_frame||ctx->resident_ovl||!spu_direct_jump_ok(ctx)) {
        if(stats.on)++stats.declined;
        return nullptr;
    }
    if(stats.on)++stats.passes;
    if(from_return) {
        // 0x1968: make sure there is output space (0xFF0 -> 0xFC0, flush at 0xC40).
        R(47)=spu_il(3648);R(5)=spu_ai(R(1),32);R(4)=spu_il(1024);R(3)=spu_a(R(5),R(47));
        const uint32_t buffer=P(R(3))+4116;
        if(word_at(ctx,buffer+12,buffer+12)-word_at(ctx,buffer+0x10,buffer)>1024u) {
            // Space left: 0xFF0/0xFC0 return at once. Their registers are
            // caller-saved and dead here; replay the call's frame effects.
            R(0)=spu_link(0x1980);ctx->pc=0x1980;ctx->drain_ret_pc=0;spu_img_restore(ctx,ctx->image_id);
        } else call(ctx,0x1980,[&]{
            // A direct host call, as compiled: no gate on entry, only on its jumps.
            R(2)=spu_il(4116);R(5)=spu_fsmbi(0x0);R(3)=spu_a(R(3),R(2));
            if(!hop(ctx,0xFC0))return;
            R(13)=spu_il(12);R(9)=spu_ai(R(3),12);R(11)=LS(ctx,P(R(3))+0x10);R(10)=LS(ctx,P(R(3))+P(R(13)));
            R(8)=spu_rotqby(R(11),R(3));R(7)=spu_rotqby(R(10),R(9));R(6)=spu_sf(R(8),R(7));
            R(2)=spu_clgt(R(6),R(4));R(4)=spu_shlqbyi(R(5),0);
            if(P(R(2))!=0){ctx->pc=P(R(0));return;}
            ctx->pc=0xC40;
            if(spu_direct_jump_ok(ctx)){ctx->steps++;compiled(ctx,0xC40)(ctx);}
            else g_spu_trampoline_fn=compiled(ctx,0xC40);
        });
        // Record the finished command's id word in the frame at SP+0x1E70.
        // (r81 is reloaded with the next command before anything reads it.)
        const uint32_t done=P(R(81)),sp=P(R(1));
        put_word(quad_at(ctx,sp+0x1E70),sp&12u,word_at(ctx,done,done+14)&0xFFFFu);
        ctx->steps++;
    }
    // 0x1308: fetch the next command and dispatch it.
    R(50)=spu_il(3648);R(51)=spu_ai(R(1),32);R(48)=spu_il(1232);R(83)=spu_a(R(51),R(50));
    R(49)=spu_ai(R(1),32);R(4)=spu_ori(R(83),0);R(3)=spu_a(R(49),R(48));
    call(ctx,0x1328,[&]{fetch_fast(ctx);}); // direct host call, as compiled
    R(81)=spu_ori(R(3),0);
    if(P(R(3))==0){ctx->pc=0x1430;return compiled(ctx,0x1430);}
    R(46)=spu_ai(R(81),14);R(45)=LS(ctx,P(R(81))+0x0);R(43)=spu_ila(0xFFFF);R(42)=spu_il(4095);
    R(47)=spu_ila(0x137C);R(37)=spu_a(R(47),R(126));R(44)=spu_rotqby(R(45),R(46));R(6)=spu_and(R(44),R(43));
    R(41)=spu_and(R(6),R(42));R(8)=spu_ai(R(41),-1);R(40)=spu_shli(R(8),2);R(35)=spu_clgti(R(8),36);
    R(39)=spu_a(R(40),R(37));R(38)=LS(ctx,P(R(37))+P(R(40)));R(36)=spu_rotqby(R(38),R(39));R(2)=spu_a(R(36),R(37));
    if(P(R(35))!=0){ctx->pc=0x1410;return compiled(ctx,0x1410);}
    ctx->pc=P(R(2));
    return handler(ctx,P(R(8)),P(R(2)));
}
