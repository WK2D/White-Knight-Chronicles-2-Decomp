/* CPU-only regression: compile the production depth helper
 * through the production HLSL compiler, and validate descriptor compatibility.
 * town-depth-hlsl.inc is generated from s_zconv_hlsl in the SDK source. */
#include "rsx_shader_spirv.h"
#include "depth-shader-fixture.inc"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static bool check_bindings(const char* hlsl,int stage,const std::vector<u32>& expected)
{
    u32 words[16384],count=0;
    char log[4096]={0};
    if(rsx_hlsl_to_spirv(hlsl,stage,words,16384,&count,log,sizeof log)){
        std::fprintf(stderr,"Shader translation failed: %s\n",log);return false;
    }
    std::vector<u32> bindings;
    for(u32 i=5;i<count;){
        const u32 n=words[i]>>16,op=words[i]&65535u;
        if(!n || n>count-i){std::fputs("Invalid SPIR-V instruction\n",stderr);return false;}
        if(op==71 && n==4 && words[i+2]==33)bindings.push_back(words[i+3]);
        if(op==71 && n==4 && words[i+2]==34 && words[i+3]!=0){
            std::fputs("Unexpected descriptor set\n",stderr);return false;
        }
        i+=n;
    }
    if(bindings.size()!=expected.size()){
        std::fprintf(stderr,"Expected %zu bindings, got %zu\n",expected.size(),bindings.size());return false;
    }
    for(u32 value:expected){
        unsigned hits=0;for(u32 got:bindings)hits+=got==value;
        if(hits!=1){std::fprintf(stderr,"Expected one shader binding %u, got %u\n",value,hits);return false;}
    }
    return true;
}

int main()
{
    const std::string depth="#define ZCONV_STORE float4(r,g,b,a)\n"+std::string(s_zconv_hlsl);
    if(!check_bindings(depth.c_str(),RSX_SHADER_STAGE_COMPUTE,{0,1}))return 1;
    if(!check_bindings(s_fxaa_hlsl,RSX_SHADER_STAGE_COMPUTE,{0,1,2}))return 3;
    const char* fragment=
        "Texture2D<float4> tx : register(t0);\n"
        "SamplerState sm : register(s0);\n"
        "float4 main(float2 uv : TEXCOORD0) : SV_Target { return tx.Sample(sm,uv); }\n";
    if(!check_bindings(fragment,RSX_SHADER_STAGE_FRAGMENT,{RSX_SPIRV_TEXTURE_BINDING,RSX_SPIRV_SAMPLER_BINDING}))return 2;
    std::puts("Depth compute bindings match its descriptor layout; graphics shifts remain intact.");
    return 0;
}
