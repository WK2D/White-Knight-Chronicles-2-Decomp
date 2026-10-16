#include "rsx_vertex_fetch.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static u8 memory[64];u8* vm_base=memory;
u32 cellGcmResolveLocated(int local,u32 offset) { assert(local==1);return offset; }
u32 cellGcmResolveOffset(u32 offset) { return offset; }
static void put(u32 offset,u32 value) {
 memory[offset]=(u8)(value>>24);memory[offset+1]=(u8)(value>>16);
 memory[offset+2]=(u8)(value>>8);memory[offset+3]=(u8)value;
}
static void check(rsx_state* state,u32 vertex,float x,float y,float z) {
 float out[4];rsx_fetch_attrib(state,2,vertex,out);
 assert(fabsf(out[0]-x)<0.000001f);assert(fabsf(out[1]-y)<0.000001f);
 assert(fabsf(out[2]-z)<0.000001f);assert(out[3]==1.0f);
}
int main(void) {
 rsx_state state;memset(&state,0,sizeof state);
 state.vertex_data4f[2][3]=9.0f;
 state.vertex_attribs[2]=(rsx_vertex_attrib){.format=0x416,.offset=8,.stride=4,.size=1,.type=6,.enabled=1};
 put(8,0);check(&state,0,0,0,0);
 put(12,0x7fdffbffu);check(&state,1,32736.f/32767,32736.f/32767,32704.f/32767);
 put(16,0x80200400u);check(&state,2,-32768.f/32767,-32768.f/32767,-32768.f/32767);
 put(20,0xffffffffu);check(&state,3,-32.f/32767,-32.f/32767,-64.f/32767);
 // Distinct lanes, byte order, stride and format's nominal size must not lose Y/Z.
 put(24,0x400ff801u);check(&state,4,32.f/32767,16352.f/32767,16384.f/32767);
 // The shared fetch path still handles divisor selection and constant attributes.
 state.vertex_attribs[2].frequency=2;
 check(&state,2,32736.f/32767,32736.f/32767,32704.f/32767);
 state.frequency_divider_op=1u<<2;check(&state,2,0,0,0);
 state.vertex_attribs[2].enabled=0;
 float out[4];state.vertex_data4f[2][1]=0.75f;rsx_fetch_attrib(&state,2,0,out);
 assert(out[0]==0 && out[1]==0.75f && out[2]==0 && out[3]==9);
 puts("Packed CMP vertex normals, byte order, stride, frequency and constants passed");
 return 0;
}
