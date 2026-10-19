#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <d3dcompiler.h>
#include "rsx_fragment_inputs.h"
#include "rsx_fp_decompiler.h"

static void word(unsigned char* out,unsigned value) {
    unsigned swapped=(value<<16)|(value>>16);
    out[0]=(unsigned char)(swapped>>24);out[1]=(unsigned char)(swapped>>16);
    out[2]=(unsigned char)(swapped>>8);out[3]=(unsigned char)swapped;
}
int main(void) {
    static rsx_dispatch state;
    static char literal[65536],buffered[65536],legacy[65536];
    unsigned cases=0;
    for(unsigned input=0;input<8;++input)for(unsigned perspective=0;perspective<2;++perspective)
    for(unsigned enabled=0;enabled<2;++enabled) {
        memset(&state,0,sizeof state);
        /* Upper bits must not create additional coordinate overrides. */
        state.regs[(0x0b40+input*4)/4]=0xfffffffeu|enabled;
        unsigned mask=rsx_fragment_tc2d_mask(&state);
        assert(mask==(enabled<<input));
        unsigned char program[16]={0};
        word(program,0x01001e01u|((4+input)<<13));
        word(program+4,1u|(1u<<11)|(2u<<13)|(3u<<15)|(7u<<18));
        word(program+12,perspective?0x80000000u:0);
        unsigned count=999;
        assert(rsx_fp_decompile_requests_ex(program,16,0x40,0,0,mask,literal,sizeof literal)>0);
        assert(rsx_fp_decompile_buffered_requests_ex(program,16,0x40,0,0,mask,buffered,sizeof buffered,&count)>0);
        assert(count==0);
        if(enabled) {
            char expected[100];
            snprintf(expected,sizeof expected,perspective?
                "float4(input.tc%u.xy * input.position.w,0.0,1.0)":
                "float4(input.tc%u.xy,0.0,rcp(input.position.w))",input);
            assert(strstr(literal,expected) && strstr(buffered,expected));
        } else {
            assert(rsx_fp_decompile_shadow_ex(program,16,0x40,0,0,legacy,sizeof legacy)>0);
            assert(!strcmp(literal,legacy));
        }
        for(unsigned mode=0;mode<2;++mode) {
            const char* source=mode?buffered:literal;
            ID3DBlob *bytecode=NULL,*errors=NULL;
            HRESULT hr=D3DCompile(source,strlen(source),"synthetic_tc2d",NULL,NULL,"main","ps_5_1",0,0,&bytecode,&errors);
            if(FAILED(hr) && errors)fprintf(stderr,"%s\n",(char*)errors->lpVtbl->GetBufferPointer(errors));
            assert(SUCCEEDED(hr) && bytecode);
            if(errors)errors->lpVtbl->Release(errors);
            bytecode->lpVtbl->Release(bytecode);++cases;
        }
        assert(rsx_fp_decompile_requests_ex(program,16,0x40,0,0,256,literal,sizeof literal)<0);
        assert(rsx_fp_decompile_buffered_requests_ex(program,16,0x40,0,0,256,buffered,sizeof buffered,&count)<0);
    }
    assert(cases==64);
    puts("64 DirectX shader compilations: TC0..7, both constant modes, perspective, override, legacy zero-mask and invalid-mask controls pass");
    return 0;
}
