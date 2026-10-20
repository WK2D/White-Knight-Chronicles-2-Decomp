#include "native_skinning_bridge.h"
#include "native_skinning_stream.h"
#include "spu_helpers.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
using namespace wkc::geometry;
Float4 unpack(u128 v){Float4 out;std::memcpy(out.data(),&v,16);return out;}
u128 pack(const Float4& v){u128 out;std::memcpy(&out,v.data(),16);return out;}
unsigned be16(const unsigned char* p){return unsigned(p[0])*256+p[1];}
unsigned be32(const unsigned char* p){return unsigned(p[0])*0x1000000+unsigned(p[1])*65536+unsigned(p[2])*256+p[3];}
struct RuntimeMath {
    static Float4 multiply(const Float4& a,const Float4& b){return unpack(spu_fm(pack(a),pack(b)));}
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c){return unpack(spu_fma(pack(a),pack(b),pack(c)));}
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c){return unpack(spu_fms(pack(a),pack(b),pack(c)));}
};
struct NormalMath:RuntimeMath {
    static Float4 multiply(const Float4& a,const Float4& b) {
        return g_spu_float_mode==SPU_FLOAT_HOST?OrderedHostCofactorMath<RuntimeMath>::multiply(a,b):RuntimeMath::multiply(a,b);
    }
};
struct CofactorMath:NormalMath {
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        return g_spu_float_mode==SPU_FLOAT_HOST?OrderedHostCofactorMath<RuntimeMath>::multiply_subtract(a,b,c):RuntimeMath::multiply_subtract(a,b,c);
    }
};
// Temporary adapter for the recovered call boundary. The stream algorithm
// remains independent of this register/LS interface and generated reference.
struct StreamViews {
    spu_context* ctx;
    unsigned addresses[3],packed,palette,frame;
    PackedSkinInfluences read_influences(unsigned vertex) {
        PackedSkinInfluences out;
        for(unsigned i=0;i<4;++i){const unsigned p=packed+vertex*8+i*2;out.weights[i]=ctx->ls[p];out.joints[i]=ctx->ls[p+1];}
        return out;
    }
    Float4 read_position(unsigned vertex){return unpack(spu_ls_read128(ctx,addresses[0]+vertex*16));}
    Float4 read_direction(unsigned stream,unsigned vertex){return unpack(spu_ls_read128(ctx,addresses[stream+1]+vertex*16));}
    CyclicAffine read_palette(unsigned joint) {
        CyclicAffine out;
        for(unsigned phase=0;phase<3;++phase)out[phase]=unpack(spu_ls_read128(ctx,palette+joint*48+phase*16));
        return out;
    }
    void stage_read_ahead(unsigned next_vertex) {
        NormalBatch next;
        for(unsigned lane=0;lane<4;++lane)next[lane]=read_position(next_vertex+lane);
        for(unsigned lane=0;lane<4;++lane){const auto& p=next[lane];spu_ls_write128(ctx,frame+0x90-lane*16,pack(Float4{p[1],p[2],p[0],p[3]}));}
        for(unsigned lane=0;lane<4;++lane){const auto& p=next[lane];spu_ls_write128(ctx,frame+0x50-lane*16,pack(Float4{p[2],p[0],p[1],p[3]}));}
    }
    void write_position(unsigned vertex,const Float4& value) {
        spu_ls_write128(ctx,addresses[0]+vertex*16,pack(value));
        if(vertex%4==3)stage_read_ahead(vertex+1);
    }
    void write_direction(unsigned stream,unsigned vertex,const Float4& value){spu_ls_write128(ctx,addresses[stream+1]+vertex*16,pack(value));}
};
bool range(unsigned a,unsigned size){return a<=SPU_LS_SIZE&&size<=SPU_LS_SIZE-a;}
bool overlap(unsigned a,unsigned n,unsigned b,unsigned m){return a<b+m&&b<a+n;}
std::unique_ptr<spu_context> clone(const spu_context* source) {
    auto out=std::make_unique<spu_context>();std::memcpy(out.get(),source,sizeof *out);
    std::memcpy(out->ls_store,source->ls,SPU_LS_SIZE);out->ls=out->ls_store;return out;
}
}

extern "C" int wkc_native_skin_stream(spu_context* ctx) {
    static const bool enabled=[] {const char* value=std::getenv("WKC_NATIVE_SKIN_STREAM");return !value||std::strcmp(value,"0");}();
    if(!enabled||!ctx||!ctx->ls||ctx->image_id!=19||ctx->policy_mode||ctx->int_enable||!ctx->host_depth)return 0;
    constexpr unsigned frame_bytes=2224;
    const unsigned metadata=ctx->gpr[3]._u32[0],attributes=ctx->gpr[4]._u32[0],streams=ctx->gpr[5]._u32[0];
    const unsigned packed=ctx->gpr[6]._u32[0],palette=ctx->gpr[7]._u32[0],sp=ctx->gpr[1]._u32[0];
    if((metadata|attributes|packed|palette|sp)&15||!range(metadata,16)||!range(attributes,16)||sp<frame_bytes||sp>SPU_LS_SIZE||!ctx->gpr[8]._u32[0]||streams<2||streams>3)return 0;
    const unsigned vertices=be16(ctx->ls+metadata+8),frame=sp-frame_bytes;
    if(!vertices||vertices%4||!range(packed,vertices*8))return 0;
    StreamViews io{ctx,{},packed,palette,frame};
    if(overlap(packed,vertices*8,frame,frame_bytes)||overlap(metadata,16,frame,frame_bytes)||overlap(attributes,16,frame,frame_bytes))return 0;
    for(unsigned stream=0;stream<streams;++stream) {
        const unsigned address=io.addresses[stream]=be32(ctx->ls+attributes+stream*4);
        if(address&15||!range(address,vertices*16)||overlap(address,vertices*16,frame,frame_bytes)||overlap(address,vertices*16,packed,vertices*8)||overlap(address,vertices*16,metadata,16)||overlap(address,vertices*16,attributes,16))return 0;
        for(unsigned prior=0;prior<stream;++prior)if(overlap(address,vertices*16,io.addresses[prior],vertices*16))return 0;
    }
    // All declines above occur before writes. Mirror the saved frame because
    // live palettes may alias it, including beyond their declared joint count.
    const auto original_sp=ctx->gpr[1];
    for(unsigned reg=3;reg<128;++reg)spu_ls_write128(ctx,sp-(reg-2)*16,ctx->gpr[reg]);
    spu_ls_write128(ctx,frame,original_sp);
    spu_ls_write128(ctx,frame+0xd0,ctx->gpr[7]);
    io.stage_read_ahead(0);
    const auto delta=spu_sf(spu_ila(0xC988),spu_link(0xC988));
    spu_ls_write128(ctx,frame+0xa0,spu_a(spu_ila(streams==2?0xD270:0xD374),delta));
    spu_ls_write128(ctx,frame+0xb0,spu_ai(ctx->gpr[7],32));
    spu_ls_write128(ctx,frame+0xc0,spu_ai(ctx->gpr[7],16));
    SkinningStreamSummary summary{};
    const auto reciprocal=[](const Float4& lengths){return unpack(spu_fi(pack(lengths),spu_frsqest(pack(lengths))));};
    // Probe the existing conversion policy once per routine, rather than
    // reproducing a compiler-specific unsigned-to-double lowering per vertex.
    const u128 unsigned_zero{};
    const auto converted_zero=spu_cuflt_mode(&unsigned_zero,155);
    const SkinningStreamPolicy arithmetic{{converted_zero._f32[0]},unpack(spu_fi(unsigned_zero,unsigned_zero))};
    skin_geometry_stream<RuntimeMath,NormalMath,CofactorMath>(io,{vertices,streams-1},reciprocal,&summary,arithmetic);
    ctx->gpr[2]=pack(streams==2?summary.final_direction_lengths[0].length_squared:summary.final_direction_lengths[0].x_squared);
    for(unsigned reg=3;reg<128;++reg)ctx->gpr[reg]=spu_ls_read128(ctx,sp-(reg-2)*16);
    // Preserve trampoline bookkeeping; this is neither elapsed time nor FPS.
    ctx->steps+=streams==2?4*(vertices/4)+1:3*(vertices/4)+2;
    ctx->pc=ctx->gpr[0]._u32[0];return 1;
}

extern "C" int wkc_native_skin_stream_checked(spu_context* ctx,void (*reference)(spu_context*)) {
    static const bool verify=std::getenv("WKC_NATIVE_SKIN_STREAM_VERIFY")!=nullptr;
    static const bool trace=std::getenv("WKC_NATIVE_SKINNING_TRACE")!=nullptr;
    static std::atomic<unsigned long long> calls{0},accepted{0},declines{0},comparisons{0};
    const auto count=(verify||trace)?calls.fetch_add(1,std::memory_order_relaxed)+1:0;
    const bool compare=verify&&(count<=32||count%256==0);
    auto before=compare&&ctx&&ctx->ls?clone(ctx):nullptr;
    if(!wkc_native_skin_stream(ctx)) {
        if(verify||trace)declines.fetch_add(1,std::memory_order_relaxed);
        return 0;
    }
    const auto native_count=(verify||trace)?accepted.fetch_add(1,std::memory_order_relaxed)+1:0;
    if(before) {
        auto actual=clone(ctx);
        const auto pending=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;
        spu_drain_call(actual.get(),actual->gpr[0]._u32[0]);
        g_spu_trampoline_fn=nullptr;
        reference(before.get());spu_drain_call(before.get(),before->gpr[0]._u32[0]);
        const bool unexpected_transfer=g_spu_trampoline_fn!=nullptr;
        g_spu_trampoline_fn=pending;
        actual->ls=before->ls;
        if(unexpected_transfer||std::memcmp(actual.get(),before.get(),sizeof *actual)) {
            unsigned offset=0;while(offset<sizeof *actual&&((unsigned char*)actual.get())[offset]==((unsigned char*)before.get())[offset])++offset;
            std::fprintf(stderr,"[native-skin-stream] full-state mismatch call=%llu offset=%u\n",count,offset);std::abort();
        }
        comparisons.fetch_add(1,std::memory_order_relaxed);
    }
    if((verify||trace)&&(count<=4||!(count&(count-1))))
        std::fprintf(stderr,"[native-skin-stream] calls=%llu native=%llu declined=%llu verified=%llu\n",count,native_count,declines.load(std::memory_order_relaxed),comparisons.load(std::memory_order_relaxed));
    return 1;
}
