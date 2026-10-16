#include "rsx_index_assembly.h"
#include "rsx_commands.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

extern "C" {
uint32_t ppu_hle_inject_base;
void vm_write32(uint32_t, uint32_t) { assert(false); }
void ps3_ms(const char*) {}
}

using Indices = std::vector<uint32_t>;
static void append(rsx_index_assembly& cursor, Indices& out,
        std::initializer_list<uint32_t> input, bool fan=false,
        bool enabled=true, uint32_t restart=65535) {
    for (auto index : input) {
        uint32_t triangle[3];
        if (rsx_index_assembly_push(&cursor,index,fan,enabled,restart,triangle))
            out.insert(out.end(),triangle,triangle+3);
    }
}
static rsx_state state;
int main() {
    rsx_state_init(&state);
    assert(state.restart_index_enable==0);
    assert(rsx_process_method(&state,0x1DB0,65535)==0);
    assert(rsx_process_method(&state,0x1DAC,1)==0);
    assert(state.restart_index_enable==1 && state.restart_index==65535);

    rsx_index_assembly cursor{};
    Indices out;
    append(cursor,out,{0,1}); // a packet boundary must not end the strip
    assert(out.empty());
    append(cursor,out,{2,3,4});
    assert((out==Indices{0,1,2,2,1,3,2,3,4}));

    rsx_index_assembly_reset(&cursor);out.clear();
    append(cursor,out,{65535,0,1,2,3,65535});
    append(cursor,out,{65535,8});
    append(cursor,out,{9,10,11,65535,20,65535});
    assert((out==Indices{0,1,2,2,1,3,8,9,10,10,9,11}));
    for (auto index : out) assert(index!=65535);

    rsx_index_assembly_reset(&cursor);out.clear();
    append(cursor,out,{0,1,1,2,3},false,false);
    // Degenerate triangles still advance strip parity.
    assert((out==Indices{0,1,1,1,1,2,1,2,3}));

    rsx_index_assembly_reset(&cursor);out.clear();
    append(cursor,out,{0,65535,2},false,false);
    assert((out==Indices{0,65535,2}));
    rsx_index_assembly_reset(&cursor);out.clear();
    append(cursor,out,{0,65535,2},false,true,0xffffffffu);
    assert((out==Indices{0,65535,2})); // no u16 truncation of the marker

    rsx_index_assembly_reset(&cursor);out.clear();
    append(cursor,out,{0,1,2,3,0xffffffffu,8,9},true,true,0xffffffffu);
    append(cursor,out,{10,11},true,true,0xffffffffu);
    assert((out==Indices{0,1,2,0,2,3,8,9,10,8,10,11}));

    rsx_index_assembly_reset(&cursor);out.clear();
    append(cursor,out,{0,1,2,7,8,9},false,true,7);
    assert((out==Indices{0,1,2}));
    rsx_index_assembly_reset(&cursor);
    append(cursor,out,{30,31,32}); // a new BEGIN starts a fresh primitive
    assert((out==Indices{0,1,2,30,31,32}));
    assert(rsx_process_method(&state,0x1DAC,0)==0);
    assert(state.restart_index_enable==0 && state.restart_index==65535);
    std::cout << "Indexed strip/fan winding, restart and packet continuity passed\n";
}
