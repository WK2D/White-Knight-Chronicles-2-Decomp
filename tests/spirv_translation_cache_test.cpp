#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_shader_spirv.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <thread>
#include <vector>
static rsx_spirv_cache_statistics stats() { rsx_spirv_cache_statistics s{}; rsx_hlsl_to_spirv_cache_statistics(&s); return s; }
int main(int argc,char** argv) {
    const bool enabled=argc>1 && argv[1][0]=='1';

    const char* source="float4 main(float4 position : POSITION) : SV_Position { return position; }";
    uint32_t original[16384],cached[16384],n=0,m=0; char log[2048]={};
    assert(!rsx_hlsl_to_spirv(source,RSX_SHADER_STAGE_VERTEX,original,16384,&n,log,sizeof log));
    assert(n>5 && original[0]==0x07230203);
    for(int i=0;i<8;++i) {
        assert(!rsx_hlsl_to_spirv(source,RSX_SHADER_STAGE_VERTEX,cached,16384,&m,log,sizeof log));
        assert(m==n && !memcmp(original,cached,n*4));
    }
    assert(stats().hits==(enabled?8:0));
    assert(stats().compilations==(enabled?1:9));
    uint64_t before=stats().entries;
    m=99; cached[0]=0x76543210;
    assert(rsx_hlsl_to_spirv(source,RSX_SHADER_STAGE_VERTEX,cached,1,&m,log,sizeof log)==-1);
    assert(m==0 && cached[0]==0x76543210 && strstr(log,"SPIR-V output too large"));
    const char* bad="this is not valid HLSL";
    for(int i=0;i<2;++i) {
        m=99; assert(rsx_hlsl_to_spirv(bad,RSX_SHADER_STAGE_VERTEX,cached,16384,&m,log,sizeof log)==-1);
        assert(m==0 && stats().entries==before);
    }
    const char* fs="float4 main(float4 p:SV_Position):SV_Target { return float4(0.2,0.3,0.4,1); }";
    assert(!rsx_hlsl_to_spirv(fs,RSX_SHADER_STAGE_FRAGMENT,cached,16384,&m,log,sizeof log));
    const char* cs="RWTexture2D<float4> output : register(u0); [numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) { output[id.xy]=float4(1,0,0,1); }";
    assert(!rsx_hlsl_to_spirv(cs,RSX_SHADER_STAGE_COMPUTE,cached,16384,&m,log,sizeof log));
    std::vector<std::thread> threads;
    for(int i=0;i<4;++i) threads.emplace_back([&] {
        uint32_t out[16384],len=0; char message[256];
        assert(!rsx_hlsl_to_spirv(source,RSX_SHADER_STAGE_VERTEX,out,16384,&len,message,sizeof message));
        assert(len==n && !memcmp(out,original,n*4));
    });
    for(auto& thread:threads)thread.join();
    // Repeat both other stages; they must also preserve exact compiled words.
    for (auto pair : {std::pair<const char*,int>{fs,RSX_SHADER_STAGE_FRAGMENT},{cs,RSX_SHADER_STAGE_COMPUTE}}) {
        uint32_t reference[16384],count=0;
        assert(!rsx_hlsl_to_spirv(pair.first,pair.second,reference,16384,&count,log,sizeof log));
        assert(!rsx_hlsl_to_spirv(pair.first,pair.second,cached,16384,&m,log,sizeof log));
        assert(m==count && !memcmp(cached,reference,count*4));
    }
    printf("Cache %s: exact output, errors, capacity, stages and concurrent calls pass. Compiles=%llu hits=%llu\n",enabled?"on":"off",stats().compilations,stats().hits);
}
