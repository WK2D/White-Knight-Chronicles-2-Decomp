/* Exercise real menu hooks using synthetic guest objects and widget calls. */
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
extern "C" uint16_t vm_read16(uint64_t a) { if(a==0x13539e4 || a==0x13539e6)return 0;assert(a+2<=0x100000);return uint16_t(vm_base[a]<<8|vm_base[a+1]); }
extern "C" uint32_t vm_read32(uint64_t a) { return uint32_t(vm_read16(a))<<16|vm_read16(a+2); }
extern "C" void vm_write16(uint64_t a,uint16_t v) { assert(a+2<=0x100000);vm_base[a]=uint8_t(v>>8);vm_base[a+1]=uint8_t(v); }
extern "C" void vm_write32(uint64_t a,uint32_t v) { vm_write16(a,uint16_t(v>>16));vm_write16(a+2,uint16_t(v)); }
static unsigned opens,closes;
extern "C" int64_t sys_memory_allocate(ppu_context* c) { vm_write32(uint32_t(c->gpr[5]),0x50000); return 0; }
void func_00048E54(ppu_context* c) { c->gpr[3]=c->gpr[4]==0xd1 ? 100 : 300; }
void func_0004EB10(ppu_context* c) { c->gpr[3]=0x70000+uint32_t(c->gpr[5])*32; }
void func_0005A5F0(ppu_context* c) { c->gpr[3]=0x90; }
void func_0004DFB8(ppu_context*) {}
void func_0004E6B8(ppu_context*) {}
void func_0004FE84(ppu_context*) {}
void func_00053050(ppu_context*) {}
void func_0065D0B4(ppu_context* c) { assert(c->gpr[3]==0x10000 && c->gpr[4]==0x30000); ++closes; }
int main() {
    auto memory=std::make_unique<uint8_t[]>(0x100000); vm_base=memory.get();
    ppu_context ctx{}; ctx.gpr[1]=0x90000;
    const uint32_t obj=0x10000, render=0x30000;
    const auto path=std::filesystem::temp_directory_path()/"wkc2-menu-test.ini";
    std::filesystem::remove(path); wkc::graphics_initialize(path);
    vm_write32(obj+0x24,0x20000); vm_write32(render+0x194,1);
    vm_write32(obj+0x1cc,3); vm_write32(obj+0x1d0,4);
    vm_write32(obj+0x208,0x123); vm_write32(obj+0x208+12,0x456);
    uint8_t original[192];std::memcpy(original,vm_base+obj+0x208,192);
    wkc2_graphics_menu_rows(&ctx,obj);
    assert(vm_read32(obj+0x1d0)==5);
    const auto row=obj+0x208+4*24;
    assert(vm_read32(row+4)==0x574b4700 && vm_read32(row+12)!=0);
    wkc2_graphics_menu_labels(&ctx,obj,render);
    auto label=[&](unsigned i){return reinterpret_cast<char*>(vm_base+vm_read32(0x70000+(100+i)*32+8));};
    assert(std::strcmp(label(4),"Graphics Settings")==0);
    auto select=[&](int i) {
        vm_write16(obj+0x200,uint16_t(i)); ctx.gpr[3]=obj;ctx.gpr[5]=render;ctx.gpr[6]=0;
        return wkc2_graphics_menu_finish(&ctx);
    };
    assert(select(4)==1 && ctx.gpr[3]==0 && closes==1);
    wkc2_graphics_menu_rows(&ctx,obj); assert(vm_read32(obj+0x1d0)==8);
    for(unsigned i=0;i<8;i++) assert(vm_read32(obj+0x208+i*24+12)!=0);
    wkc2_graphics_menu_labels(&ctx,obj,render);
    assert(std::strstr(label(0),"Window Mode") && std::strcmp(label(4),"Anti-Aliasing: Off")==0);
    assert(select(4)==1 && wkc::graphics_snapshot().anti_aliasing==1);
    wkc2_graphics_menu_rows(&ctx,obj);
    assert(select(6)==1); // More Settings
    wkc2_graphics_menu_rows(&ctx,obj);wkc2_graphics_menu_labels(&ctx,obj,render);
    assert(vm_read32(obj+0x1d0)==4 && std::strstr(label(0),"Graphics API: Vulkan"));
    assert(std::strcmp(label(2),"FPS Counter: Off")==0);
    assert(select(2)==1 && wkc::graphics_snapshot().fps_counter==1);
    assert(select(0)==1 && wkc::graphics_snapshot().renderer==1);
    assert(wkc::graphics_active_renderer()==0); // No live renderer teardown.
    wkc2_graphics_menu_rows(&ctx,obj);wkc2_graphics_menu_labels(&ctx,obj,render);
    assert(std::strstr(label(0)," *"));
    assert(select(1)==1); // Draw Distance
    wkc2_graphics_menu_rows(&ctx,obj);wkc2_graphics_menu_labels(&ctx,obj,render);
    assert(std::strstr(label(0),"pending"));
    const auto distance=wkc::graphics_snapshot();assert(select(0)==1 && wkc::graphics_snapshot()==distance);
    assert(select(-1)==1);wkc2_graphics_menu_rows(&ctx,obj); // More
    assert(select(-1)==1);wkc2_graphics_menu_rows(&ctx,obj); // Graphics
    wkc::GraphicsSettings saved; assert(wkc::graphics_load(path,saved) && saved.fps_counter==1);
    const auto before=opens; assert(select(99)==1 && opens==before && ctx.gpr[3]==5);
    assert(select(-1)==1); // Circle returns to System, without changing preferences.
    vm_write32(obj+0x1d0,4);std::memcpy(vm_base+obj+0x208,original,192); wkc2_graphics_menu_rows(&ctx,obj);
    assert(vm_read32(obj+0x1d0)==5 && select(0)==0); // Original Game Settings still dispatches normally.
    std::filesystem::remove(path);
    std::cout<<"System entry, X acceptance, separate page, counter persistence and Back pass.\n";
}
