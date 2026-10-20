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
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        return unpack(spu_fms(pack(a),pack(b),pack(c)));
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

extern "C" int wkc_native_transform_geometry(spu_context* ctx) {
    if(!ctx || !ctx->ls || ctx->image_id!=19)return 0;
    // These control words establish the affine xyz/w layout. Do not guess
    // semantics for a different packing program, even if its values look alike.
    const unsigned xyz_mask[]={0xffffffffu,0xffffffffu,0xffffffffu,0};
    const unsigned translation_mask[]={0x1c1d1e1fu,0x04050607u,0x08090a0bu,0x80808080u};
    const unsigned transpose_mask[]={0x80808080u,0x0c0d0e0fu,0x1c1d1e1fu,0x80808080u};
    const unsigned xyz_copy[]={0x00010203u,0x04050607u,0x08090a0bu,0x80808080u};
    const auto ones=spu_splat_u32(0x3f800000u);
    if(std::memcmp(&ctx->gpr[66],&ones,16) ||
       std::memcmp(&ctx->gpr[73],&xyz_mask,16) ||
       std::memcmp(&ctx->gpr[122],&translation_mask,16) ||
       std::memcmp(&ctx->gpr[123],&transpose_mask,16) ||
       std::memcmp(&ctx->gpr[30],xyz_copy,16))return 0;
    const unsigned matrices[4][3]={{45,50,41},{23,49,32},{24,36,15},{42,13,14}};
    const unsigned positions[]={82,81,80,83};
    const unsigned second_coordinates[]={10,31,22,21};
    const unsigned third_coordinates[]={6,46,48,61};
    PositionStages result[4];
    for(unsigned vertex=0;vertex<4;++vertex) {
        const auto position=unpack(ctx->gpr[positions[vertex]]);
        const Float4 second{position[1],position[2],position[0],position[3]};
        const Float4 third{position[2],position[0],position[1],position[3]};
        if(std::memcmp(second.data(),&ctx->gpr[second_coordinates[vertex]],16) ||
           std::memcmp(third.data(),&ctx->gpr[third_coordinates[vertex]],16))return 0;
        CyclicAffine matrix;
        for(unsigned phase=0;phase<3;++phase)matrix[phase]=unpack(ctx->gpr[matrices[vertex][phase]]);
        result[vertex]=transform_skin_position<RuntimeMath>(matrix,position);
    }
    // Decode all four inputs before changing overlapping input/output GPRs.
    ctx->gpr[57]=pack(result[0].position);ctx->gpr[56]=pack(result[1].position);
    ctx->gpr[83]=pack(result[2].position);ctx->gpr[81]=pack(result[3].position);
    ctx->gpr[60]=pack(result[3].second); // still live at the compiled block exit
    static const bool trace=std::getenv("WKC_NATIVE_SKINNING_TRACE")!=nullptr;
    if(trace) {
        static std::atomic<unsigned long long> calls{0};
        const auto count=calls.fetch_add(1,std::memory_order_relaxed)+1;
        if(count<=4 || !(count&(count-1)))std::fprintf(stderr,"[native-skinning] transformed_blocks=%llu\n",count);
    }
    return 1;
}

extern "C" int wkc_native_cofactor_geometry(const spu_context* ctx,void* output,unsigned bytes) {
    if(!ctx || !ctx->ls || ctx->image_id!=19 || !output || bytes<384)return 0;
    const unsigned xyz[]={0xffffffffu,0xffffffffu,0xffffffffu,0};
    const unsigned rotate_left[]={0x04050607u,0x08090a0bu,0x00010203u,0x0c0d0e0fu};
    const unsigned rotate_right[]={0x08090a0bu,0x00010203u,0x04050607u,0x0c0d0e0fu};
    const unsigned xyz_copy[]={0x00010203u,0x04050607u,0x08090a0bu,0x80808080u};
    const auto ones=spu_splat_u32(0x3f800000u);
    if(std::memcmp(&ctx->gpr[66],&ones,16) || std::memcmp(&ctx->gpr[73],xyz,16) ||
       std::memcmp(&ctx->gpr[71],rotate_left,16) || std::memcmp(&ctx->gpr[65],rotate_right,16) ||
       std::memcmp(&ctx->gpr[30],xyz_copy,16))return 0;
    const unsigned registers[4][3]={{45,50,41},{23,49,32},{24,36,15},{42,13,14}};
    CyclicAffine result[8];
    for(unsigned vertex=0;vertex<4;++vertex) {
        CyclicAffine matrix;
        for(unsigned phase=0;phase<3;++phase)matrix[phase]=unpack(ctx->gpr[registers[vertex][phase]]);
        const auto stages=cofactor_skin_stages<RuntimeMath>(matrix);
        result[vertex]=stages.cofactors;result[vertex+4]=stages.products;
    }
    static_assert(sizeof result==384,"Packed cofactor matrices and products");
    std::memcpy(output,result,sizeof result);
    static const bool trace=std::getenv("WKC_NATIVE_SKINNING_TRACE")!=nullptr;
    if(trace) {
        static std::atomic<unsigned long long> calls{0};
        const auto count=calls.fetch_add(1,std::memory_order_relaxed)+1;
        if(count<=4 || !(count&(count-1)))std::fprintf(stderr,"[native-skinning] cofactor_blocks=%llu\n",count);
    }
    return 1;
}

extern "C" int wkc_native_finish_geometry(spu_context* ctx) {
    if(!ctx || !ctx->ls || ctx->image_id!=19)return 0;
    if(g_spu_float_mode<0)spu_float_mode_init();
    const unsigned xyz[]={0xffffffffu,0xffffffffu,0xffffffffu,0};
    const unsigned translation[]={0x1c1d1e1fu,0x04050607u,0x08090a0bu,0x80808080u};
    const unsigned transpose[]={0x80808080u,0x0c0d0e0fu,0x1c1d1e1fu,0x80808080u};
    const unsigned left[]={0x04050607u,0x08090a0bu,0x00010203u,0x0c0d0e0fu};
    const unsigned right[]={0x08090a0bu,0x00010203u,0x04050607u,0x0c0d0e0fu};
    const unsigned copy[]={0x00010203u,0x04050607u,0x08090a0bu,0x80808080u};
    const auto ones=spu_splat_u32(0x3f800000u);
    if(std::memcmp(&ctx->gpr[66],&ones,16) || std::memcmp(&ctx->gpr[73],xyz,16) ||
       std::memcmp(&ctx->gpr[122],translation,16) || std::memcmp(&ctx->gpr[123],transpose,16) ||
       std::memcmp(&ctx->gpr[71],left,16) || std::memcmp(&ctx->gpr[65],right,16) ||
       std::memcmp(&ctx->gpr[30],copy,16))return 0;
    const unsigned matrix_regs[4][3]={{45,50,41},{23,49,32},{24,36,15},{42,13,14}};
    const unsigned position_regs[]={82,81,80,83};
    const unsigned second_regs[]={10,31,22,21},third_regs[]={6,46,48,61};
    const unsigned normal_regs[]={69,68,67,70};
    // Validate every vertex before any architectural write or LS output.
    Float4 positions[4];
    for(unsigned vertex=0;vertex<4;++vertex) {
        positions[vertex]=unpack(ctx->gpr[position_regs[vertex]]);
        const auto& p=positions[vertex];
        const Float4 second{p[1],p[2],p[0],p[3]},third{p[2],p[0],p[1],p[3]};
        if(std::memcmp(second.data(),&ctx->gpr[second_regs[vertex]],16) ||
           std::memcmp(third.data(),&ctx->gpr[third_regs[vertex]],16))return 0;
    }
    CyclicAffine matrices[4];Float4 normals[4];
    for(unsigned vertex=0;vertex<4;++vertex) {
        for(unsigned phase=0;phase<3;++phase)matrices[vertex][phase]=unpack(ctx->gpr[matrix_regs[vertex][phase]]);
        normals[vertex]=unpack(ctx->gpr[normal_regs[vertex]]);
        // Host IEEE arithmetic does not define which NaN payload survives an
        // operand exchange. The retained compiled C and this C++ may make
        // different legal choices. Preserve the existing host-mode boundary
        // for nonfinite inputs; exact SPU mode accepts all bit patterns.
        if(g_spu_float_mode!=SPU_FLOAT_EXACT) {
            for(const auto& phase:matrices[vertex])for(float value:phase)if(!std::isfinite(value))return 0;
            for(float value:positions[vertex])if(!std::isfinite(value))return 0;
            for(float value:normals[vertex])if(!std::isfinite(value))return 0;
        }
    }
    GeometryStages stages[4];
    for(unsigned vertex=0;vertex<4;++vertex)
        stages[vertex]=prepare_skin_geometry<RuntimeMath>(matrices[vertex],positions[vertex],normals[vertex]);
    const auto stack=ctx->gpr[1]._u32[0],output=ctx->gpr[55]._u32[0],next=ctx->gpr[64]._u32[0];
    u128 converted[4],weights[4];
    const unsigned weight_regs[]={54,53,52,34};
    for(unsigned vertex=0;vertex<4;++vertex) {
        converted[vertex]=spu_cuflt(ctx->gpr[weight_regs[vertex]],155);
        weights[vertex]=spu_fm(converted[vertex],ctx->gpr[107]);
    }
    const auto estimate=spu_fi(ctx->gpr[84],ctx->gpr[90]);
    const auto next_cursor=spu_ai(ctx->gpr[86],64),next_count=spu_ai(ctx->gpr[109],-1);
    const auto packed_a=spu_selb(ctx->gpr[88],ctx->gpr[87],ctx->gpr[97]);
    const auto packed_b=spu_selb(ctx->gpr[79],ctx->gpr[78],ctx->gpr[97]);

    // All positions are written before reloading the return address and next
    // input batch. Output may overlap either source or the saved stack frame.
    for(unsigned vertex=0;vertex<4;++vertex)
        spu_ls_write128(ctx,output+vertex*16,pack(stages[vertex].positions.position));
    const auto return_address=spu_ls_read128(ctx,stack+0xa0);
    u128 next_positions[4],next_left[4],next_right[4];
    for(unsigned vertex=0;vertex<4;++vertex) {
        next_positions[vertex]=spu_ls_read128(ctx,next+vertex*16);
        next_left[vertex]=spu_shufb(next_positions[vertex],next_positions[vertex],ctx->gpr[71]);
        next_right[vertex]=spu_shufb(next_positions[vertex],next_positions[vertex],ctx->gpr[65]);
    }
    // Preserve the original write sequence even for wrapped/overlapping LS.
    const unsigned stack_offsets[]={0x90,0x70,0x20,0x60,0x80,0x50,0x40,0x30};
    const u128 stack_values[]={next_left[0],next_left[2],next_right[3],next_left[3],
        next_left[1],next_right[0],next_right[1],next_right[2]};
    for(unsigned i=0;i<8;++i)spu_ls_write128(ctx,stack+stack_offsets[i],stack_values[i]);

    // Export the verified block ABI, including scratch registers used by its
    // compiled continuation. The arithmetic above has no register identities.
    const unsigned cf_regs[4][3]={{39,27,41},{40,28,42},{44,30,45},{43,32,46}};
    const unsigned product_regs[4][3]={{50,9,3},{25,8,4},{29,7,5},{31,74,2}};
    for(unsigned vertex=0;vertex<4;++vertex)for(unsigned phase=0;phase<3;++phase) {
        ctx->gpr[cf_regs[vertex][phase]]=pack(stages[vertex].normal_matrix.cofactors[phase]);
        ctx->gpr[product_regs[vertex][phase]]=pack(stages[vertex].normal_matrix.products[phase]);
    }
    const unsigned partial_regs[]={17,4,11,12};
    for(unsigned vertex=0;vertex<4;++vertex)ctx->gpr[partial_regs[vertex]]=pack(stages[vertex].first_normal_product);
    ctx->gpr[3]=packed_b;ctx->gpr[29]=packed_a;ctx->gpr[31]=estimate;
    ctx->gpr[13]=next_cursor;ctx->gpr[15]=return_address;ctx->gpr[34]=next_count;
    ctx->gpr[6]=pack(stages[3].right[0]);ctx->gpr[10]=pack(stages[3].left[0]);
    ctx->gpr[14]=pack(stages[3].left[2]);ctx->gpr[19]=pack(stages[1].right[1]);
    ctx->gpr[20]=pack(stages[0].right[1]);ctx->gpr[21]=pack(stages[1].left[2]);
    ctx->gpr[22]=pack(stages[0].left[2]);ctx->gpr[26]=pack(stages[3].right[2]);
    ctx->gpr[36]=pack(stages[3].left[1]);ctx->gpr[37]=pack(stages[1].left[1]);
    ctx->gpr[38]=pack(stages[0].left[1]);ctx->gpr[51]=pack(stages[0].left[0]);
    ctx->gpr[58]=pack(stages[3].homogeneous[0]);ctx->gpr[59]=pack(stages[2].homogeneous[2]);
    ctx->gpr[16]=next_left[3];ctx->gpr[18]=next_right[2];
    ctx->gpr[23]=next_right[1];ctx->gpr[24]=next_right[0];ctx->gpr[49]=next_left[1];
    ctx->gpr[56]=pack(stages[1].positions.position);ctx->gpr[57]=pack(stages[0].positions.position);
    ctx->gpr[60]=pack(stages[3].positions.second);
    const unsigned converted_regs[]={48,47,62,61},scaled_regs[]={74,54,53,52};
    for(unsigned vertex=0;vertex<4;++vertex) {
        ctx->gpr[converted_regs[vertex]]=converted[vertex];ctx->gpr[scaled_regs[vertex]]=weights[vertex];
        ctx->gpr[position_regs[vertex]]=next_positions[vertex];
    }
    static const bool trace=std::getenv("WKC_NATIVE_SKINNING_TRACE")!=nullptr;
    if(trace) {
        static std::atomic<unsigned long long> calls{0};
        const auto count=calls.fetch_add(1,std::memory_order_relaxed)+1;
        if(count<=4 || !(count&(count-1)))std::fprintf(stderr,"[native-skinning] completed_blocks=%llu\n",count);
    }
    return 1;
}
