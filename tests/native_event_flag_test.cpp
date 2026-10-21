// Synthetic checks of the image-21 event-flag update rules (no game data).
#include "../native_event_flag.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace wkc::event_flag;

int main() {
    std::uint8_t line[128];
    // Any-to-any, OR-mode PPU waiter on bit 1 in wait slot 2, automatic clear.
    std::memset(line,0,sizeof line);
    line[0x0E]=3;line[0x0C]=5;store16(line+0x04,0x0002);line[0x06]=0x20;
    SetResult r=apply_set(line,0x0002);
    assert(r.wake_ppu&&r.port==5&&!r.woken_tasks&&!r.rejected);
    assert(load16(line)==0&&load16(line+0x04)==0&&line[0x07]==1&&load16(line+0x34)==0x0002);
    // AND-mode PPU waiter stays blocked until every wanted bit is set; manual clear keeps events.
    std::memset(line,0,sizeof line);
    line[0x0E]=1;line[0x0F]=1;store16(line+0x04,0x0003);line[0x06]=0x01;
    r=apply_set(line,0x0001);
    assert(!r.wake_ppu&&load16(line)==0x0001&&load16(line+0x04)==0x0003);
    r=apply_set(line,0x0002);
    assert(r.wake_ppu&&load16(line)==0x0003);
    // Spu-to-spu: OR-mode task in slot 0 is released; its events are consumed.
    std::memset(line,0,sizeof line);
    line[0x0E]=0;store16(line+0x08,0x8000);store16(line+0x10,0x00F0);
    r=apply_set(line,0x0010);
    assert(r.woken_tasks==0x8000&&!r.wake_ppu&&load16(line+0x02)==0x8000&&load16(line+0x30)==0x0010&&load16(line)==0);
    // A slot already pending is not released again.
    r=apply_set(line,0x0010);
    assert(!r.woken_tasks&&load16(line)==0x0010);
    // PPU-to-SPU flags and unknown directions are rejected without changes.
    std::memset(line,0,sizeof line);line[0x0E]=2;
    r=apply_set(line,0xFFFF);
    assert(r.rejected&&load16(line)==0);
    std::puts("native event flag: PPU/task release, AND/OR modes, clear modes and rejection");
    return 0;
}
