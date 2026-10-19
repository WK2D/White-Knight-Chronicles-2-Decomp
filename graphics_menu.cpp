/* Graphics Settings uses the game's full settings panel and native input/layout. */
#include "ppu_recomp.h"
#include "graphics_settings.h"
#include <cstdio>
#include <cstring>
#include <string>
extern "C" int64_t sys_memory_allocate(ppu_context*);
extern "C" uint8_t* vm_base;
extern void func_00048E54(ppu_context*);
extern void func_0004EB10(ppu_context*);
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);

namespace {
constexpr uint32_t marker=0x574B4700;
uint32_t owner=0, labels=0, system_count=0, panel=0;
bool requested=false;
constexpr wkc::GraphicsOption options[]={
    wkc::GraphicsOption::renderer, wkc::GraphicsOption::window_mode,
    wkc::GraphicsOption::output_resolution, wkc::GraphicsOption::vsync,
    wkc::GraphicsOption::frame_limit, wkc::GraphicsOption::anisotropy,
    wkc::GraphicsOption::anti_aliasing, wkc::GraphicsOption::world_distance,
    wkc::GraphicsOption::npc_distance, wkc::GraphicsOption::distance_fog,
    wkc::GraphicsOption::fps_counter
};
constexpr unsigned option_count=sizeof(options)/sizeof(options[0]);
constexpr unsigned item_count=option_count+1;
static_assert(item_count<=12,"The native panel has twelve visible rows");
bool pending(wkc::GraphicsOption o) {
    return o==wkc::GraphicsOption::world_distance || o==wkc::GraphicsOption::npc_distance || o==wkc::GraphicsOption::distance_fog;
}
bool available(wkc::GraphicsOption o) {
    return !pending(o) && (!wkc::graphics_active_renderer() ||
        (o!=wkc::GraphicsOption::anti_aliasing));
}
uint32_t call(ppu_context* source,void (*function)(ppu_context*),uint32_t a,uint32_t b,uint32_t c,uint32_t d=0) {
    ppu_context nested=*source;
    nested.gpr[3]=a;nested.gpr[4]=b;nested.gpr[5]=c;nested.gpr[6]=d;
    const auto outer=g_trampoline_fn;g_trampoline_fn=nullptr;
    function(&nested);
    while(g_trampoline_fn) { const auto next=g_trampoline_fn;g_trampoline_fn=nullptr;next(&nested); }
    g_trampoline_fn=outer;
    return uint32_t(nested.gpr[3]);
}
bool pool(ppu_context* source) {
    if(labels) return true;
    ppu_context nested=*source;
    const uint32_t output=uint32_t(source->gpr[1])-32;
    nested.gpr[3]=65536;nested.gpr[4]=0x200;nested.gpr[5]=output;
    if(sys_memory_allocate(&nested)!=0) return false;
    labels=vm_read32(output);return labels!=0;
}
uint32_t text(ppu_context* ctx,unsigned slot,const std::string& value) {
    if(!pool(ctx) || slot>=32 || value.size()>=240) return 0;
    const auto address=labels+slot*256;
    std::memcpy(vm_base+address,value.c_str(),value.size()+1);return address;
}
void widget_text(ppu_context* ctx,uint32_t render,uint32_t widget,uint32_t string) {
    const auto block=call(ctx,func_0004EB10,render,vm_read32(render+0x194),widget);
    if(!block || !string) return;
    vm_write32(block+8,string);
    vm_write32(block+0x14,(vm_read32(block+0x14)&7)|0x80001000u);
    vm_write32(block+0xc,0xdcd7d2ffu);
}
void footer(ppu_context* ctx,uint32_t render,const char* help) {
    const auto pointer=text(ctx,31,help);
    widget_text(ctx,render,0x7e,pointer);
    widget_text(ctx,render,call(ctx,func_00048E54,render,0x7e,100),pointer);
}
const char* help(unsigned selected) {
    if(selected>=option_count) return "X: Return to System. Circle: Back.";
    const auto option=options[selected];
    if(pending(option)) return "Draw distance and fog controls are not active yet. Circle: Back.";
    if(option==wkc::GraphicsOption::renderer) return "Restart required. DirectX 12 is experimental and may render incorrectly.";
    if(!available(option)) return "This option currently applies to Vulkan. Switch API and restart.";
    if(option==wkc::GraphicsOption::frame_limit) return "Left/Right: Change game frame-rate limit. Higher rates need more performance.";
    if(option==wkc::GraphicsOption::output_resolution) return "Left/Right: Change output window size. Internal rendering resolution is unchanged.";
    if(option==wkc::GraphicsOption::anti_aliasing) return "Left/Right: Off / FXAA. FXAA smooths the completed picture, including menus.";
    return "Up/Down: Select. Left/Right or X: Change setting. Circle: Back.";
}
}
extern "C" void wkc2_graphics_menu_rows(ppu_context* ctx,uint32_t object) {
    if(vm_read32(object+0x1cc)!=3) return;
    owner=object;system_count=vm_read32(object+0x1d0);
    if(!system_count || system_count>=8 || !pool(ctx)) return;
    const auto row=object+0x208+system_count*24;
    // Preserve Game Settings' real action and this-pointer adjustment so the
    // normal state machine opens and closes the full settings panel.
    std::memcpy(vm_base+row,vm_base+object+0x208,24);
    vm_write32(row+4,marker);vm_write32(row+20,0);
    vm_write32(object+0x1d0,system_count+1);
}
extern "C" void wkc2_graphics_menu_labels(ppu_context* ctx,uint32_t object,uint32_t render) {
    if(!render || object!=owner || vm_read32(object+0x1cc)!=3) return;
    const auto base=call(ctx,func_00048E54,render,0xd1,200);
    widget_text(ctx,render,base+system_count,text(ctx,0,"Graphics Settings"));
    if(vm_read16(object+0x102)==system_count) footer(ctx,render,"Change graphics settings for this PC.");
}
extern "C" void wkc2_graphics_menu_scroll(ppu_context*,uint32_t) {} // Retired narrow-list hook.
extern "C" int wkc2_graphics_menu_finish(ppu_context* ctx) {
    const auto object=uint32_t(ctx->gpr[3]);
    if(object!=owner || vm_read32(object+0x1cc)!=3) return 0;
    const int selected=int16_t(vm_read16(object+0x200));
    requested=selected>=0 && unsigned(selected)<vm_read32(object+0x1d0) &&
        vm_read32(object+0x208+unsigned(selected)*24+4)==marker;
    return 0; // The native action opens the panel.
}
extern "C" int wkc2_graphics_panel_owns(uint32_t object) { return object && object==panel; }
extern "C" void wkc2_graphics_panel_begin(uint32_t object) { panel=requested?object:0; }
extern "C" void wkc2_graphics_panel_end(uint32_t object) { if(object==panel) {panel=0;requested=false;} }
extern "C" void wkc2_graphics_panel_selection(uint32_t object) {
    if(object==panel) vm_write16(object+0x66,item_count);
}
extern "C" void wkc2_graphics_panel_labels(ppu_context* ctx,uint32_t object,uint32_t render) {
    if(!render || !wkc2_graphics_panel_owns(object)) return;
    const auto name_base=call(ctx,func_00048E54,render,0x236,0x834);
    const auto value_base=call(ctx,func_00048E54,render,0x238,0x834);
    const auto settings=wkc::graphics_snapshot();
    for(unsigned i=0;i<12;++i) {
        std::string name="",value="";
        if(i<option_count) {
            const auto option=options[i];name=wkc::graphics_option_label(option);
            value=pending(option)?"Pending":wkc::graphics_choice_label(option,wkc::graphics_value(settings,option));
            if(option==wkc::GraphicsOption::renderer && settings.renderer!=wkc::graphics_active_renderer()) value+=" *";
        } else if(i==option_count) name="Back";
        widget_text(ctx,render,name_base+i,text(ctx,1+i,name));
        widget_text(ctx,render,value_base+i,text(ctx,13+i,value));
    }
    widget_text(ctx,render,0x90,text(ctx,30,"Graphics Settings"));
    footer(ctx,render,help(vm_read16(object+0x32)));
}
// Return true only for consumed changes. The original handler retains native
// Up/Down repeat, row highlight, hand, sounds, scrollbar and exit animation.
extern "C" int wkc2_graphics_panel_input(ppu_context* ctx) {
    const auto object=uint32_t(ctx->gpr[3]), input=uint32_t(ctx->gpr[7]);
    if(!wkc2_graphics_panel_owns(object) || !input) return 0;
    const auto buttons=vm_read32(input+4),directions=vm_read32(input+0xc);
    const unsigned selected=vm_read16(object+0x32);
    if(buttons&4) return 0; // Circle follows the native close path.
    if(!(buttons&2) && !(directions&0xc000)) return 0;
    if(selected==option_count && (buttons&2)) {
        vm_write32(input+4,(buttons&~2u)|4u);return 0;
    }
    if(selected<option_count && available(options[selected]) &&
       !wkc::graphics_change(options[selected],directions&0x4000?-1:1))
        std::fprintf(stderr,"[graphics-menu] preference write failed\n");
    ctx->gpr[3]=1;return 1;
}
