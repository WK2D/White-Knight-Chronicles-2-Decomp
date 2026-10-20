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
int wkc2_test_native_skinning_stream();
static CyclicAffine cyclic(const float matrix[3][4]) {
    return {Float4{matrix[0][0],matrix[1][1],matrix[2][2],matrix[0][3]},
            Float4{matrix[0][1],matrix[1][2],matrix[2][0],matrix[1][3]},
            Float4{matrix[0][2],matrix[1][0],matrix[2][1],matrix[2][3]}};
}

int wkc2_test_native_skinning() {
    assert(wkc2_test_native_skinning_stream()==0);
    const NormalBatch sample_normals={Float4{3,4,0,7},Float4{0,0,2,-3},Float4{1,2,2,0},Float4{0,0,0,1}};
    const auto lengths=normal_length_stages(sample_normals);
    assert((lengths.length_squared==Float4{25,4,9,0}));
    const auto scaled=scale_skin_normals(sample_normals,Float4{.2f,.5f,1.f/3,0});
    for(unsigned vertex=0;vertex<4;++vertex)assert(scaled.normals[vertex][3]==sample_normals[vertex][3]);
    assert(scaled.normals[1][2]==1 && scaled.normals[3][0]==0);
    {
        auto normal_ctx=std::make_unique<spu_context>(),snapshot=std::make_unique<spu_context>();
        spu_context_init(normal_ctx.get(),0);normal_ctx->image_id=19;normal_ctx->pc=0xD500;
        const unsigned regs[]={97,89,106,75,76,105};
        const unsigned masks[][4]={
            {0xffffffffu,0xffffffffu,0,0},{0x18191a1bu,0x1c1d1e1fu,0x00010203u,0x04050607u},
            {0x00010203u,0x00010203u,0x00010203u,0x10111213u},
            {0x04050607u,0x04050607u,0x04050607u,0x14151617u},
            {0x08090a0bu,0x08090a0bu,0x08090a0bu,0x18191a1bu},
            {0x0c0d0e0fu,0x0c0d0e0fu,0x0c0d0e0fu,0x1c1d1e1fu}};
        for(unsigned i=0;i<6;++i)std::memcpy(&normal_ctx->gpr[regs[i]],masks[i],16);
        normal_ctx->gpr[66]=spu_splat_u32(0x3f800000u);
        const Float4 normal{0,1,0,1};
        for(unsigned reg:{94u,92u,93u,91u,17u,15u,12u,11u})std::memcpy(&normal_ctx->gpr[reg],normal.data(),16);
        const Float4 packed79{1,1,0,0},packed78{0,0,1,1},zero{};
        std::memcpy(&normal_ctx->gpr[79],packed79.data(),16);std::memcpy(&normal_ctx->gpr[78],packed78.data(),16);
        std::memcpy(&normal_ctx->gpr[88],zero.data(),16);std::memcpy(&normal_ctx->gpr[87],zero.data(),16);
        normal_ctx->gpr[84]=normal_ctx->gpr[90]=spu_splat_u32(0x3f800000u);
        normal_ctx->gpr[91]._u32[2]=0x7ffd520au;normal_ctx->gpr[90]._u32[3]=0xffcb3000u;
        normal_ctx->gpr[86]._u32[0]=0x2000;normal_ctx->gpr[85]._u32[0]=0x2100;
        const int mode=g_spu_float_mode;g_spu_float_mode=SPU_FLOAT_HOST;
        std::memcpy(snapshot.get(),normal_ctx.get(),sizeof *normal_ctx);
        assert(wkc_native_finish_normals(normal_ctx.get()));
        assert(normal_ctx->gpr[114]._u32[2]==0x7ffd520au);
        assert(spu_ls_read128(normal_ctx.get(),0x2030)._u32[2]==0x7ffd520au);
        assert(normal_ctx->pc==0xD500); // wrapper owns dispatch
        for(unsigned reg:{97u,89u,106u,75u,76u,105u,66u,79u,78u,88u,87u}) {
            std::memcpy(normal_ctx.get(),snapshot.get(),sizeof *normal_ctx);
            normal_ctx->gpr[reg]._u32[0]^=1;
            auto untouched=std::make_unique<spu_context>();std::memcpy(untouched.get(),normal_ctx.get(),sizeof *normal_ctx);
            assert(!wkc_native_finish_normals(normal_ctx.get()));
            assert(!std::memcmp(normal_ctx.get(),untouched.get(),sizeof *normal_ctx));
        }
        g_spu_float_mode=mode;
    }
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
        NormalBatch batch;Float4 inverse;
        for(unsigned vertex=0;vertex<4;++vertex) {
            inverse[vertex]=float(random()%1024)/128;
            for(float& lane:batch[vertex])lane=float(int(random()%8193)-4096)/64;
        }
        const auto stages=normal_length_stages(batch);
        const auto outputs=scale_skin_normals(batch,inverse);
        for(unsigned vertex=0;vertex<4;++vertex) {
            const float x=batch[vertex][0],y=batch[vertex][1],z=batch[vertex][2];
            const float first=x*x,second=std::fma(z,z,first),last=std::fma(y,y,second);
            assert(!std::memcmp(&stages.x_squared[vertex],&first,4));
            assert(!std::memcmp(&stages.zx_squared[vertex],&second,4));
            assert(!std::memcmp(&stages.length_squared[vertex],&last,4));
            for(unsigned lane=0;lane<4;++lane) {
                const float expected=batch[vertex][lane]*(lane==3?1:inverse[vertex]);
                assert(!std::memcmp(&outputs.normals[vertex][lane],&expected,4));
            }
        }
#ifdef WKC_NATIVE_SKINNING_HOST_FMA
        const auto vector_stages=normal_length_stages<HostFmaMath>(batch);
        const auto vector_outputs=scale_skin_normals<HostFmaMath>(batch,inverse);
        assert(!std::memcmp(&stages,&vector_stages,sizeof stages));
        assert(!std::memcmp(&outputs,&vector_outputs,sizeof outputs));
#endif
    }
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
        const Float4 normal{finite(),finite(),finite(),finite()};
        const auto geometry=prepare_skin_geometry(scalar.blended,vertex.position,normal);
        assert(!std::memcmp(&stages,&geometry.positions,sizeof stages));
        const auto cofactor_stages=cofactor_skin_stages(scalar.blended);
        assert(!std::memcmp(&cofactor_stages,&geometry.normal_matrix,sizeof cofactor_stages));
        for(unsigned lane=0;lane<4;++lane) {
            volatile float product=normal[lane]*cofactor_stages.cofactors[0][lane];
            assert(!std::memcmp((const void*)&product,&geometry.first_normal_product[lane],4));
        }
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
        auto cofactor=cofactor_skin_stages(scalar.blended);
        auto vector_cofactor=cofactor_skin_stages<HostFmaMath>(scalar.blended);
        assert(!std::memcmp(&cofactor,&vector_cofactor,sizeof cofactor));
        const auto vector_geometry=prepare_skin_geometry<HostFmaMath>(scalar.blended,vertex.position,normal);
        assert(!std::memcmp(&geometry,&vector_geometry,sizeof geometry));
#endif
    }

#ifdef WKC_NATIVE_SKINNING_HOST_FMA
    // Wide finite bit patterns cover subnormals, overflow and cancellation,
    // including nonfinite intermediates produced from finite inputs. Compare
    // every output bit; do not mask NaN payload or signed-zero differences.
    for(unsigned trial=0;trial<50000;++trial) {
        auto wide=[&] {unsigned bits=random();if((bits&0x7f800000u)==0x7f800000u)bits^=0x00800000u;float v;std::memcpy(&v,&bits,4);return v;};
        for(auto& value:vertex.position)value=wide();
        for(auto& value:vertex.weights)value=wide();
        for(auto& matrix:vertex.influences)for(auto& phase:matrix)for(auto& value:phase)value=wide();
        const auto scalar=blend_skin_matrix(vertex.weights,vertex.influences);
        const auto vector=blend_skin_matrix<HostFmaMath>(vertex.weights,vertex.influences);
        assert(!std::memcmp(&scalar,&vector,sizeof scalar));
        const Float4 normal{wide(),wide(),wide(),wide()};
        const auto geometry=prepare_skin_geometry(vertex.influences[0],vertex.position,normal);
        const auto vector_geometry=prepare_skin_geometry<HostFmaMath>(vertex.influences[0],vertex.position,normal);
        assert(!std::memcmp(&geometry,&vector_geometry,sizeof geometry));
    }
#endif

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
    const unsigned copy_mask[]={0x00010203u,0x04050607u,0x08090a0bu,0x80808080u};
    std::memcpy(&ctx->gpr[30],copy_mask,16);
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
    ctx->gpr[30]._u32[0]^=1;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(!wkc_native_transform_geometry(ctx.get()));assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    ctx->gpr[30]._u32[0]^=1;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(wkc_native_transform_geometry(ctx.get()));
    for(unsigned reg:{57u,56u,83u,81u,60u}) {
        assert(!std::memcmp(&ctx->gpr[reg],p.data(),16));
        before->gpr[reg]=ctx->gpr[reg];
    }
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    unsigned char cofactor_output[384],unchanged[384];
    std::memset(cofactor_output,0xa5,sizeof cofactor_output);
    std::memcpy(unchanged,cofactor_output,sizeof unchanged);
    assert(!wkc_native_cofactor_geometry(nullptr,cofactor_output,sizeof cofactor_output));
    assert(!wkc_native_cofactor_geometry(ctx.get(),cofactor_output,383));
    assert(!wkc_native_cofactor_geometry(ctx.get(),nullptr,384));
    assert(!wkc_native_cofactor_geometry(ctx.get(),cofactor_output,384));
    assert(!std::memcmp(cofactor_output,unchanged,384));
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    const unsigned left[]={0x04050607u,0x08090a0bu,0x00010203u,0x0c0d0e0fu};
    const unsigned right[]={0x08090a0bu,0x00010203u,0x04050607u,0x0c0d0e0fu};
    const unsigned xyz_copy[]={0x00010203u,0x04050607u,0x08090a0bu,0x80808080u};
    std::memcpy(&ctx->gpr[71],left,16);std::memcpy(&ctx->gpr[65],right,16);std::memcpy(&ctx->gpr[30],xyz_copy,16);
    std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(wkc_native_cofactor_geometry(ctx.get(),cofactor_output,384));
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    const float identity_cofactor[12][4]={{1,1,1,1},{0,0,0,0},{0,0,0,0},
        {1,1,1,1},{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0},
        {1,1,1,1},{0,0,0,0},{0,0,0,0}};
    assert(!std::memcmp(cofactor_output,identity_cofactor,192));
    for(unsigned i=192;i<384;++i)assert(cofactor_output[i]==0);

    // A complete post-blend batch uses synthetic identity matrices and distinct
    // read-ahead records. Check LS output/stack order and continuation inputs.
    spu_context_init(ctx.get(),0);ctx->image_id=19;ctx->pc=0xce08;
    ctx->gpr[66]=spu_splat_u32(0x3f800000u);
    std::memcpy(&ctx->gpr[73],xyz,16);std::memcpy(&ctx->gpr[122],translation,16);
    std::memcpy(&ctx->gpr[123],transpose,16);std::memcpy(&ctx->gpr[71],left,16);
    std::memcpy(&ctx->gpr[65],right,16);std::memcpy(&ctx->gpr[30],xyz_copy,16);
    const unsigned normals[]={69,68,67,70};
    const Float4 n{.25f,.5f,.75f,0};
    for(unsigned v=0;v<4;++v) {
        std::memcpy(&ctx->gpr[positions[v]],p.data(),16);
        std::memcpy(&ctx->gpr[second[v]],q.data(),16);std::memcpy(&ctx->gpr[third[v]],r.data(),16);
        std::memcpy(&ctx->gpr[matrix[v][0]],diagonal.data(),16);
        ctx->gpr[matrix[v][1]]=u128();ctx->gpr[matrix[v][2]]=u128();
        std::memcpy(&ctx->gpr[normals[v]],n.data(),16);
    }
    ctx->gpr[1]=spu_splat_u32(0x3000);ctx->gpr[55]=spu_splat_u32(0x2000);
    ctx->gpr[64]=spu_splat_u32(0x2040);ctx->gpr[86]=spu_splat_u32(0x400);
    ctx->gpr[109]=spu_splat_u32(5);ctx->gpr[90]=spu_splat_u32(0x3f800000u);
    const Float4 scale{.25f,.25f,.25f,.25f};std::memcpy(&ctx->gpr[107],scale.data(),16);
    const unsigned next_weights[]={54,53,52,34};
    for(unsigned v=0;v<4;++v)ctx->gpr[next_weights[v]]=spu_splat_u32(v+1);
    u128 next_input[4];
    for(unsigned v=0;v<4;++v) {
        for(unsigned lane=0;lane<4;++lane)next_input[v]._u32[lane]=0x1000+v*16+lane;
        spu_ls_write128(ctx.get(),0x2040+v*16,next_input[v]);
    }
    const auto address=spu_splat_u32(0x1234);spu_ls_write128(ctx.get(),0x30a0,address);
    ctx->gpr[65]._u32[3]^=1;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(!wkc_native_finish_geometry(nullptr));assert(!wkc_native_finish_geometry(ctx.get()));
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    ctx->gpr[65]._u32[3]^=1;
    ctx->gpr[61]._u32[0]^=1;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(!wkc_native_finish_geometry(ctx.get()));assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    ctx->gpr[61]._u32[0]^=1;
    std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    assert(wkc_native_finish_geometry(ctx.get()));
    for(unsigned v=0;v<4;++v) {
        const auto stored=spu_ls_read128(ctx.get(),0x2000+v*16);
        assert(!std::memcmp(&stored,p.data(),16));
        assert(!std::memcmp(&ctx->gpr[positions[v]],&next_input[v],16));
        const unsigned partial[]={17,4,11,12},scaled[]={74,54,53,52},converted_regs[]={48,47,62,61};
        assert(!std::memcmp(&ctx->gpr[partial[v]],n.data(),16));
        for(unsigned lane=0;lane<4;++lane) {
            assert(ctx->gpr[scaled[v]]._f32[lane]==float(v+1)*.25f);
            assert(ctx->gpr[converted_regs[v]]._f32[lane]==float(v+1));
        }
        const unsigned left_offsets[]={0x90,0x80,0x70,0x60},right_offsets[]={0x50,0x40,0x30,0x20};
        const auto stored_left=spu_ls_read128(ctx.get(),0x3000+left_offsets[v]);
        const auto stored_right=spu_ls_read128(ctx.get(),0x3000+right_offsets[v]);
        for(unsigned lane=0;lane<4;++lane) {
            assert(stored_left._u32[lane]==next_input[v]._u32[lane==3?3:(lane+1)%3]);
            assert(stored_right._u32[lane]==next_input[v]._u32[lane==3?3:(lane+2)%3]);
        }
    }
    assert(ctx->gpr[15]._u32[0]==0x1234 && ctx->gpr[13]._u32[0]==0x440 && ctx->gpr[34]._u32[0]==4);
    assert(ctx->pc==0xce08); // caller owns continuation dispatch
    std::memcpy(ctx.get(),before.get(),sizeof *ctx);
    const Float4 off_diagonal{2,3,5,7},rotated_normal_row{3,5,2,0};
    std::memcpy(&ctx->gpr[14],off_diagonal.data(),16);
    assert(wkc_native_finish_geometry(ctx.get()));
    assert(!std::memcmp(&ctx->gpr[14],rotated_normal_row.data(),16));
    std::memcpy(ctx.get(),before.get(),sizeof *ctx);
    ctx->gpr[69]._u32[0]=0x7fc12345;std::memcpy(before.get(),ctx.get(),sizeof *ctx);
    const int saved_mode=g_spu_float_mode;g_spu_float_mode=0;
    assert(!wkc_native_finish_geometry(ctx.get())); // host NaN payload compatibility
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    g_spu_float_mode=2;
    assert(!wkc_native_finish_geometry(ctx.get())); // audit returns host results
    assert(!std::memcmp(before.get(),ctx.get(),sizeof *ctx));
    g_spu_float_mode=1;
    assert(wkc_native_finish_geometry(ctx.get())); // extended SPU floats accept the same bits
    g_spu_float_mode=saved_mode;
    std::printf("Native skinning: independent affine oracle, 32000 finite arithmetic cases, %u x86 FMA cases, and bridge state guards pass.\n",simd_cases);
    return 0;
}
