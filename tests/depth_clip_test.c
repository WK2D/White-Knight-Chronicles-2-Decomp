#include "rsx_commands.h"
#include "rsx_raster_state.h"
#include <assert.h>
#include <stdio.h>

/* No memory or milestone side effects belong to these synthetic draws. */
uint32_t ppu_hle_inject_base;
void vm_write32(uint32_t address,uint32_t value) { (void)address;(void)value;assert(0); }
void ps3_ms(const char* key) { (void)key; }
static rsx_state state;
static unsigned keys[4],draws;
static void draw_arrays(void* opaque,u32 primitive,u32 first,u32 count) {
    (void)opaque;(void)primitive;(void)first;
    assert(count==3 && draws<4);
    keys[draws++]=rsx_raster_key(state.cull_face_enable,state.cull_face,
                               state.front_face,state.zmin_max_control);
}
static void draw(void) {
    assert(!rsx_process_method(&state,NV4097_SET_BEGIN_END,5));
    assert(!rsx_process_method(&state,NV4097_DRAW_ARRAYS,2u<<24));
    assert(!rsx_process_method(&state,NV4097_SET_BEGIN_END,0));
}
int main(void) {
    rsx_backend backend={0};backend.draw_arrays=draw_arrays;
    rsx_state_init(&state);rsx_set_backend(&backend);
    assert(state.zmin_max_control==1);
    draw();
    state.depth_dirty=0;
    assert(!rsx_process_method(&state,NV4097_SET_ZMIN_MAX_CONTROL,0));
    assert(state.depth_dirty);
    draw();
    assert(!rsx_process_method(&state,NV4097_SET_ZMIN_MAX_CONTROL,1));
    draw();
    assert(!rsx_process_method(&state,NV4097_SET_ZMIN_MAX_CONTROL,0x11));
    draw();
    assert(draws==4 && keys[0]==8 && keys[1]==0 && keys[2]==8 && keys[3]==0);
    /* Later register writes must not alter the retained pipeline selections. */
    assert(keys[0]!=keys[1] && keys[0]==keys[2]);
    assert(rsx_raster_depth_clip(2) && !rsx_raster_depth_clip(0x21));
    assert((rsx_raster_key(1,0x404,0x901,1)&7)==7);
    assert((rsx_raster_key(1,0x405,0x900,0)&7)==1);
    rsx_set_backend(NULL);
    puts("Guest clipping reset, command transitions and deferred raster keys passed");
    return 0;
}
