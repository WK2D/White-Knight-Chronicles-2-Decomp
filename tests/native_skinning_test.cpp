#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_skinning.h"
#include "native_skinning_simd.h"
#include "native_skinning_bridge.h"
#include "spu_helpers.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>

using namespace wkc::geometry;
static CyclicAffine cyclic(const float matrix[3][4]) {
    return {Float4{matrix[0][0],matrix[1][1],matrix[2][2],matrix[0][3]},
            Float4{matrix[0][1],matrix[1][2],matrix[2][0],matrix[1][3]},
            Float4{matrix[0][2],matrix[1][0],matrix[2][1],matrix[2][3]}};
}

int wkc2_test_native_skinning() {
    // Independent ordinary-matrix oracle with exact small integer results.
    SkinVertex vertex{};vertex.position={2,3,5,1};vertex.weights={.125f,.25f,.5f,.125f};
    float expected[3]={0,0,0};
    for(unsigned influence=0;influence<4;++influence) {
        float matrix[3][4];
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
            matrix[row][col]=float(1+influence*3+row*5+col);
        vertex.influences[influence]=cyclic(matrix);
        for(unsigned row=0;row<3;++row)
            expected[row]+=vertex.weights[influence]*(matrix[row][0]*2+matrix[row][1]*3+matrix[row][2]*5+matrix[row][3]);
    }
    auto result=skin_vertex(vertex);
    for(unsigned row=0;row<3;++row)assert(result.position[row]==expected[row]);
    assert(result.position[3]==1);
    vertex.weights={2,-1,0,0};
    auto unnormalized=blend_skin_matrix(vertex.weights,vertex.influences);
    for(unsigned phase=0;phase<3;++phase)for(unsigned lane=0;lane<4;++lane)
        assert(unnormalized[phase][lane]==2*vertex.influences[0][phase][lane]-vertex.influences[1][phase][lane]);
    // Independent row-major signed-minor oracle, including singular matrices.
    for(unsigned trial=0;trial<64;++trial) {
        float m[3][4],cofactor[3][4]{};
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
            m[row][col]=float(int((trial+row*7+col*3)%13)-6);
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col) {
            unsigned rows[2],cols[2],nr=0,nc=0;
            for(unsigned i=0;i<3;++i){if(i!=row)rows[nr++]=i;if(i!=col)cols[nc++]=i;}
            float minor=m[rows[0]][cols[0]]*m[rows[1]][cols[1]]-m[rows[0]][cols[1]]*m[rows[1]][cols[0]];
            cofactor[row][col]=((row+col)&1)?-minor:minor;
        }
        const auto expected_cofactor=cyclic(cofactor);
        const auto actual_cofactor=cofactor_skin_matrix(cyclic(m));
        for(unsigned phase=0;phase<3;++phase)for(unsigned lane=0;lane<3;++lane)
            assert(actual_cofactor[phase][lane]==expected_cofactor[phase][lane]);
        assert(actual_cofactor[0][3]==1 && actual_cofactor[1][3]==0 && actual_cofactor[2][3]==0);
    }

    std::mt19937 random(0xCE08);unsigned simd_cases=0;
    for(unsigned trial=0;trial<32000;++trial) {
        auto finite=[&] {return float(int(random()%8193)-4096)/64;};
        for(auto& value:vertex.position)value=finite();
        for(auto& value:vertex.weights)value=finite();
        for(auto& matrix:vertex.influences)for(auto& phase:matrix)for(auto& value:phase)value=finite();
        auto scalar=skin_vertex(vertex);
        // Independent lane equations verify the first product rounds before
        // the three fused accumulations; weights are deliberately unnormalized.
        for(unsigned phase=0;phase<3;++phase)for(unsigned lane=0;lane<4;++lane) {
            volatile float first=vertex.weights[0]*vertex.influences[0][phase][lane];
            float value=first;
            for(unsigned influence=1;influence<4;++influence)
                value=std::fma(vertex.weights[influence],vertex.influences[influence][phase][lane],value);
            assert(std::memcmp(&value,&scalar.blended[phase][lane],4)==0);
        }
        const auto stages=transform_skin_position(scalar.blended,vertex.position);
        for(unsigned lane=0;lane<4;++lane) {
            float value=std::fma(vertex.position[lane],lane==3?1.f:scalar.blended[0][lane],
                                 lane==3?0.f:scalar.blended[lane][3]);
            assert(!std::memcmp(&value,&stages.first[lane],4));
            value=std::fma(vertex.position[lane==3?3:(lane+1)%3],lane==3?0.f:scalar.blended[1][lane],value);
            assert(!std::memcmp(&value,&stages.second[lane],4));
            value=std::fma(vertex.position[lane==3?3:(lane+2)%3],lane==3?0.f:scalar.blended[2][lane],value);
            assert(!std::memcmp(&value,&scalar.position[lane],4));
        }
#ifdef WKC_NATIVE_SKINNING_HOST_FMA
        auto vector=skin_vertex<HostFmaMath>(vertex);
        assert(std::memcmp(&scalar,&vector,sizeof scalar)==0);++simd_cases;
        auto cofactor=cofactor_skin_matrix(scalar.blended);
        auto vector_cofactor=cofactor_skin_matrix<HostFmaMath>(scalar.blended);
        assert(!std::memcmp(&cofactor,&vector_cofactor,sizeof cofactor));
#endif
    }

    auto ctx=std::make_unique<spu_context>();spu_context_init(ctx.get(),0);ctx->image_id=19;
    auto before=std::make_unique<spu_context>();
    // Unknown mask/image/null inputs must reject without changing any state.
    std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(!wkc_native_blend_geometry(nullptr));
    assert(!wkc_native_blend_geometry(ctx.get()));
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    const unsigned masks[]={121,112,111,118};
    for(unsigned lane=0;lane<4;++lane)ctx->gpr[masks[lane]]=spu_splat_u32(0x00010203u+lane*0x04040404u);
    ctx->image_id=18;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(!wkc_native_blend_geometry(ctx.get()));assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    ctx->image_id=19;
    // All zero indices/weights/palette are valid. Prefix preserves local store,
    // PC/status and channel/MFC state; only the specified GPRs may change.
    ctx->gpr[64]=spu_splat_u32(0x100);ctx->pc=0xCE08;ctx->status=SPU_STATUS_RUNNING;
    std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(wkc_native_blend_geometry(ctx.get())==1);
    assert(ctx->pc==before->pc && ctx->status==before->status);
    assert(!std::memcmp(ctx->ls,before->ls,SPU_LS_SIZE));
    assert(!std::memcmp(&ctx->mfc,&before->mfc,sizeof ctx->mfc));
    const unsigned written[]={6,10,13,14,15,21,22,23,24,30,31,32,33,34,35,36,41,42,43,45,46,48,49,50,52,53,54,55,61,63,64,72};
    for(unsigned reg=0;reg<128;++reg) {
        bool allowed=false;for(auto id:written)allowed|=reg==id;
        if(!allowed)assert(!std::memcmp(&ctx->gpr[reg],&before->gpr[reg],16));
    }
    std::memcpy(before->gpr,ctx->gpr,sizeof ctx->gpr);
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    assert(!wkc_native_transform_geometry(nullptr));
    assert(!wkc_native_transform_geometry(ctx.get()));
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    const unsigned xyz[]={0xffffffffu,0xffffffffu,0xffffffffu,0};
    const unsigned translation[]={0x1c1d1e1fu,0x04050607u,0x08090a0bu,0x80808080u};
    const unsigned transpose[]={0x80808080u,0x0c0d0e0fu,0x1c1d1e1fu,0x80808080u};
    ctx->gpr[66]=spu_splat_u32(0x3f800000u);
    std::memcpy(&ctx->gpr[73],xyz,16);std::memcpy(&ctx->gpr[122],translation,16);std::memcpy(&ctx->gpr[123],transpose,16);
    const unsigned positions[]={82,81,80,83},second[]={10,31,22,21},third[]={6,46,48,61};
    const unsigned matrix[4][3]={{45,50,41},{23,49,32},{24,36,15},{42,13,14}};
    const Float4 p{2,3,5,1},q{3,5,2,1},r{5,2,3,1},diagonal{1,1,1,0};
    for(unsigned v=0;v<4;++v) {
        std::memcpy(&ctx->gpr[positions[v]],p.data(),16);
        std::memcpy(&ctx->gpr[second[v]],q.data(),16);
        std::memcpy(&ctx->gpr[third[v]],r.data(),16);
        std::memcpy(&ctx->gpr[matrix[v][0]],diagonal.data(),16);
        ctx->gpr[matrix[v][1]]=u128();ctx->gpr[matrix[v][2]]=u128();
    }
    // An inconsistent late vertex must decline without partially writing the earlier ones.
    ctx->gpr[61]._u32[0]^=1;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(!wkc_native_transform_geometry(ctx.get()));assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    ctx->gpr[61]._u32[0]^=1;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(wkc_native_transform_geometry(ctx.get()));
    for(unsigned reg:{57u,56u,83u,81u,60u}) {
        assert(!std::memcmp(&ctx->gpr[reg],p.data(),16));
        before->gpr[reg]=ctx->gpr[reg];
    }
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    std::printf("Native skinning: independent affine oracle, 32000 finite arithmetic cases, %u x86 FMA cases, and bridge state guards pass.\n",simd_cases);
    return 0;
}
