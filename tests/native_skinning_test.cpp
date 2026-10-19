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
#ifdef WKC_NATIVE_SKINNING_HOST_FMA
        auto vector=skin_vertex<HostFmaMath>(vertex);
        assert(std::memcmp(&scalar,&vector,sizeof scalar)==0);++simd_cases;
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
    std::printf("Native skinning: independent affine oracle, 32000 finite arithmetic cases, %u x86 FMA cases, and bridge state guards pass.\n",simd_cases);
    return 0;
}
