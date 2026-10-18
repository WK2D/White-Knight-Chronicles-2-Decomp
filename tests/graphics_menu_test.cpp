/* Production menu hooks with synthetic guest objects; no copyrighted data. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ppu_recomp.h"
#include "graphics_settings.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
extern "C" uint8_t* vm_base;
uint8_t* vm_base=nullptr;
extern "C" void wkc2_graphics_menu_rows(ppu_context*,uint32_t);
extern "C" void wkc2_graphics_menu_labels(ppu_context*,uint32_t,uint32_t);
extern "C" int wkc2_graphics_menu_finish(ppu_context*);
extern "C" void wkc2_graphics_panel_begin(uint32_t);
extern "C" void wkc2_graphics_panel_end(uint32_t);
extern "C" void wkc2_graphics_panel_selection(uint32_t);
extern "C" int wkc2_graphics_panel_owns(uint32_t);
extern "C" void wkc2_graphics_panel_labels(ppu_context*,uint32_t,uint32_t);
extern "C" int wkc2_graphics_panel_input(ppu_context*);
extern "C" uint16_t vm_read16(uint64_t a) { assert(a+2<=0x100000);return uint16_t(vm_base[a]<<8|vm_base[a+1]); }
extern "C" uint32_t vm_read32(uint64_t a) { return uint32_t(vm_read16(a))<<16|vm_read16(a+2); }
extern "C" void vm_write16(uint64_t a,uint16_t v) { assert(a+2<=0x100000);vm_base[a]=uint8_t(v>>8);vm_base[a+1]=uint8_t(v); }
extern "C" void vm_write32(uint64_t a,uint32_t v) { vm_write16(a,uint16_t(v>>16));vm_write16(a+2,uint16_t(v)); }
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*)=nullptr;
extern "C" int64_t sys_memory_allocate(ppu_context* c) { vm_write32(uint32_t(c->gpr[5]),0x50000); return 0; }
void func_00048E54(ppu_context* c) {
    c->gpr[3]=c->gpr[4]==0xd1?100:c->gpr[4]==0x236?200:c->gpr[4]==0x238?300:400;
}
static void widget_tail(void* raw) { auto* c=static_cast<ppu_context*>(raw);c->gpr[3]=0x70000+uint32_t(c->gpr[5])*32; }
void func_0004EB10(ppu_context*) { g_trampoline_fn=widget_tail; }
static void outer_tail(void*) { assert(false && "Outer tail call must stay on its own context"); }
int main() {
    auto memory=std::make_unique<uint8_t[]>(0x100000);vm_base=memory.get();
    ppu_context ctx{};ctx.gpr[1]=0x90000;
    const uint32_t obj=0x10000,panel=0x18000,render=0x30000,input=0x40000;
    const auto path=std::filesystem::temp_directory_path()/"wkc2-menu-test.ini";
    std::filesystem::remove(path);wkc::graphics_initialize(path);
    vm_write32(render+0x194,1);vm_write32(obj+0x1cc,3);vm_write32(obj+0x1d0,4);
    vm_write32(obj+0x208,0x123);vm_write32(obj+0x208+12,0x456);vm_write32(obj+0x208+16,0x789);
    wkc2_graphics_menu_rows(&ctx,obj);
    const auto row=obj+0x208+4*24;
    assert(vm_read32(obj+0x1d0)==5 && vm_read32(row+4)==0x574b4700);
    assert(vm_read32(row+12)==0x456 && vm_read32(row+16)==0x789); // Real panel action retained.
    g_trampoline_fn=outer_tail;wkc2_graphics_menu_labels(&ctx,obj,render);
    assert(g_trampoline_fn==outer_tail);g_trampoline_fn=nullptr;
    auto label=[&](unsigned widget){return reinterpret_cast<char*>(vm_base+vm_read32(0x70000+widget*32+8));};
    assert(std::strcmp(label(104),"Graphics Settings")==0);
    auto dispatch=[&](int selected) {
        ctx.gpr[3]=obj;vm_write16(obj+0x200,uint16_t(selected));
        assert(wkc2_graphics_menu_finish(&ctx)==0); // Native dispatcher always opens the actual panel.
        wkc2_graphics_panel_begin(panel);
    };
    dispatch(0);assert(!wkc2_graphics_panel_owns(panel)); // Original Game Settings unaffected.
    dispatch(4);assert(wkc2_graphics_panel_owns(panel));
    wkc2_graphics_panel_selection(panel);assert(vm_read16(panel+0x66)==12);
    wkc2_graphics_panel_labels(&ctx,panel,render);
    assert(std::strcmp(label(200),"Graphics API")==0 && std::strcmp(label(300),"Vulkan")==0);
    assert(std::strcmp(label(0x90),"Graphics Settings")==0);
    assert(std::strcmp(label(211),"Back")==0 && std::strcmp(label(311),"")==0);
    auto press=[&](unsigned selected,uint32_t buttons,uint32_t directions) {
        vm_write16(panel+0x32,uint16_t(selected));vm_write32(input+4,buttons);vm_write32(input+0xc,directions);
        ctx.gpr[3]=panel;ctx.gpr[5]=render;ctx.gpr[7]=input;
        return wkc2_graphics_panel_input(&ctx);
    };
    assert(press(6,2,0)==1 && wkc::graphics_snapshot().anti_aliasing==1); // X advances.
    assert(press(6,0,0x4000)==1 && wkc::graphics_snapshot().anti_aliasing==0); // Left reverses.
    assert(press(6,0,0x8000)==1 && wkc::graphics_snapshot().anti_aliasing==1);
    assert(press(0,2,0)==1 && wkc::graphics_snapshot().renderer==1 && wkc::graphics_active_renderer()==0);
    wkc2_graphics_panel_labels(&ctx,panel,render);assert(std::strstr(label(300)," *"));
    const auto before=wkc::graphics_snapshot();assert(press(7,2,0)==1 && wkc::graphics_snapshot()==before);
    assert(press(10,2,0)==1 && wkc::graphics_snapshot().fps_counter==1);
    assert(press(0,0,0x1000)==0 && press(0,0,0x2000)==0); // Up/Down remain native.
    assert(press(0,4,0)==0); // Circle remains native.
    assert(press(11,2,0)==0 && vm_read32(input+4)==4); // Back uses the same exit animation.
    wkc::GraphicsSettings saved;assert(wkc::graphics_load(path,saved) && saved.fps_counter==1);
    assert(press(99,2,0)==1 && wkc::graphics_snapshot()==saved); // No out-of-range dispatch.
    wkc2_graphics_panel_end(panel);assert(!wkc2_graphics_panel_owns(panel));
    dispatch(0);assert(!wkc2_graphics_panel_owns(panel));
    std::filesystem::remove(path);
    std::cout<<"Native full-panel routing, labels, input, persistence and original-panel isolation pass.\n";
}
