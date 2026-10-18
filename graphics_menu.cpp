/* Native additions to the verified BLUS30784 System submenu.
 * Widget creation, fonts, input and transitions remain the game's own. */
#include "ppu_recomp.h"
#include "graphics_settings.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
extern "C" int64_t sys_memory_allocate(ppu_context*);
extern "C" uint8_t* vm_base;
extern void func_00048E54(ppu_context*);
extern void func_0004EB10(ppu_context*);
extern void func_0065D0B4(ppu_context*);
extern void func_0004DFB8(ppu_context*);
extern void func_0004FE84(ppu_context*);
extern void func_0065F9A0(ppu_context*);
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);

namespace {
constexpr uint32_t marker = 0x574B4700;
enum class Page { system, graphics };
Page page = Page::system;
uint32_t owner = 0, labels = 0;
std::array<uint8_t,192> system_rows{};
uint32_t system_count = 0;
int restore_cursor = -1;
constexpr wkc::GraphicsOption options[] = {
    wkc::GraphicsOption::renderer, wkc::GraphicsOption::window_mode,
    wkc::GraphicsOption::output_resolution, wkc::GraphicsOption::vsync,
    wkc::GraphicsOption::frame_limit, wkc::GraphicsOption::anisotropy,
    wkc::GraphicsOption::anti_aliasing, wkc::GraphicsOption::world_distance,
    wkc::GraphicsOption::npc_distance, wkc::GraphicsOption::distance_fog,
    wkc::GraphicsOption::fps_counter
};
constexpr unsigned visible_rows=8, option_count=sizeof(options)/sizeof(options[0]);
constexpr unsigned item_count=option_count+1, max_first=item_count-visible_rows;
unsigned first=0;
int last_cursor=-1;
int highlight_from=-1;
bool pending(wkc::GraphicsOption option) {
    return option==wkc::GraphicsOption::world_distance ||
        option==wkc::GraphicsOption::npc_distance || option==wkc::GraphicsOption::distance_fog;
}
bool available(wkc::GraphicsOption option) {
    return !pending(option) && (!wkc::graphics_active_renderer() ||
        option==wkc::GraphicsOption::renderer || option==wkc::GraphicsOption::frame_limit);
}
uint32_t call(ppu_context* source, void (*function)(ppu_context*),
              uint32_t a=0,uint32_t b=0,uint32_t c=0,uint32_t d=0,uint32_t e=0) {
    ppu_context nested=*source;
    nested.gpr[3]=a; nested.gpr[4]=b; nested.gpr[5]=c; nested.gpr[6]=d; nested.gpr[7]=e;
    // Tail calls belong to this nested context; preserve any outer pending call.
    const auto outer=g_trampoline_fn;g_trampoline_fn=nullptr;
    function(&nested);
    while(g_trampoline_fn) { const auto next=g_trampoline_fn;g_trampoline_fn=nullptr;next(&nested); }
    g_trampoline_fn=outer;
    return uint32_t(nested.gpr[3]);
}
bool pool(ppu_context* source) {
    if (labels) return true;
    const uint32_t output=uint32_t(source->gpr[1])-32;
    ppu_context nested=*source;
    nested.gpr[3]=65536; nested.gpr[4]=0x200; nested.gpr[5]=output;
    if (sys_memory_allocate(&nested) != 0) return false;
    labels=vm_read32(output);
    return labels != 0;
}
uint32_t text(ppu_context* source,unsigned slot,const std::string& value) {
    if (!pool(source) || slot>=32 || value.size()>=240) return 0;
    const uint32_t address=labels+slot*256;
    std::memcpy(vm_base+address,value.c_str(),value.size()+1);
    return address;
}
void widget_text(ppu_context* ctx,uint32_t render,uint32_t widget,uint32_t string) {
    const uint32_t resource=vm_read32(render+0x194);
    const uint32_t block=call(ctx,func_0004EB10,render,resource,widget,0);
    if (!block || !string) return;
    vm_write32(block+8,string);
    vm_write32(block+0x14,(vm_read32(block+0x14)&7)|0x80001000u);
    vm_write32(block+0xc,0xdcd7d2ffu);
}
void footer(ppu_context* ctx,uint32_t render,const std::string& value) {
    const auto pointer=text(ctx,17,value);
    widget_text(ctx,render,0x7e,pointer);
    widget_text(ctx,render,call(ctx,func_00048E54,render,0x7e,100),pointer);
}
void heading(ppu_context* ctx,uint32_t render) {
    const auto resource=vm_read32(render+0x194);
    call(ctx,func_0004DFB8,render,resource,0x8e,1,1);
    ppu_context alpha=*ctx;alpha.gpr[3]=render;alpha.gpr[4]=resource;
    alpha.gpr[5]=0x8e;alpha.fpr[1]=1.0;func_0004FE84(&alpha);
    const auto title="Graphics Settings ("+std::to_string(first+1)+"-"+
        std::to_string(first+visible_rows)+"/"+std::to_string(item_count)+")";
    widget_text(ctx,render,0x90,text(ctx,16,title));
}
void rows(uint32_t object) {
    const unsigned count=visible_rows;
    uint32_t placeholder=0;
    std::memcpy(&placeholder,system_rows.data(),4);
    for(unsigned i=0;i<8;++i) {
        const uint32_t row=object+0x208+i*24;
        std::memset(vm_base+row,0,24);
        // Existing text key is a safe placeholder until the widget hook runs.
        std::memcpy(vm_base+row,&placeholder,4);
        vm_write32(row+4,marker+first+i);
        // The original input handler accepts only rows with an action.
        // Keep this nonzero; the finish hook handles our marker before dispatch.
        std::memcpy(vm_base+row+12,system_rows.data()+12,4);
    }
    vm_write32(object+0x1d0,count);
}
}
extern "C" void wkc2_graphics_menu_rows(ppu_context* ctx,uint32_t object) {
    if (vm_read32(object+0x1cc)!=3) { if (object==owner) page=Page::system; return; }
    const uint32_t count=vm_read32(object+0x1d0);
    if (page!=Page::system && object==owner) { rows(object); return; }
    page=Page::system; owner=object;
    if (!count || count>=8 || !pool(ctx)) return;
    system_count=count;
    std::memcpy(system_rows.data(),vm_base+object+0x208,192);
    const uint32_t row=object+0x208+count*24;
    std::memcpy(vm_base+row,vm_base+object+0x208,24);
    vm_write32(row+4,marker);
    vm_write32(row+8,0); vm_write32(row+16,0); vm_write32(row+20,0);
    vm_write32(object+0x1d0,count+1);
    std::fprintf(stderr,"[graphics-menu] added System entry, original rows=%u\n",count);
}
extern "C" void wkc2_graphics_menu_labels(ppu_context* ctx,uint32_t object,uint32_t render) {
    if (!render || object!=owner || vm_read32(object+0x1cc)!=3 || !pool(ctx)) return;
    if (restore_cursor>=0) { vm_write16(object+0x102,uint16_t(restore_cursor));restore_cursor=-1; }
    const unsigned cursor=vm_read16(object+0x102);
    const uint32_t base=call(ctx,func_00048E54,render,0xd1,200);
    if (page==Page::system) {
        widget_text(ctx,render,base+system_count,text(ctx,0,"Graphics Settings"));
        if (cursor==system_count) footer(ctx,render,"Change graphics settings for this PC.");
        return;
    }
    const auto settings=wkc::graphics_snapshot();
    for(unsigned i=0;i<visible_rows;++i) {
        const unsigned logical=first+i;
        std::string label="Back";
        if(logical<option_count) {
            const auto option=options[logical];
            label=std::string(wkc::graphics_option_label(option))+": ";
            label+=pending(option) ? "Pending" :
                wkc::graphics_choice_label(option,wkc::graphics_value(settings,option));
            if(option==wkc::GraphicsOption::renderer && settings.renderer!=wkc::graphics_active_renderer()) label+=" *";
        }
        widget_text(ctx,render,base+i,text(ctx,i+1,label));
    }
    heading(ctx,render);
    if(highlight_from>=0 && cursor<visible_rows) {
        call(ctx,func_0065F9A0,object,render,unsigned(highlight_from),0,0);
        call(ctx,func_0065F9A0,object,render,cursor,1,0);
        highlight_from=-1;
    }
    const unsigned logical=first+cursor;
    const char* help="Up/Down: Scroll. X: Change setting. Circle: Back.";
    if(logical==option_count) help="X: Return to System. Circle: Back.";
    else if(logical<option_count) {
        const auto option=options[logical];
        if(pending(option)) help="Draw distance and fog controls are not active yet. Circle: Back.";
        else if(option==wkc::GraphicsOption::renderer) help="Restart required. DirectX 12 is experimental and may render incorrectly.";
        else if(!available(option)) help="This option currently applies to Vulkan. Switch API and restart.";
        else if(option==wkc::GraphicsOption::frame_limit) help="X: Change game frame-rate limit. Higher rates need more performance.";
        else if(option==wkc::GraphicsOption::anti_aliasing) help="X: Off / FXAA. FXAA smooths the completed picture, including menus.";
        else if(option==wkc::GraphicsOption::fps_counter) help="X: Show or hide the completed-frame FPS counter. Circle: Back.";
    }
    footer(ctx,render,help);
    last_cursor=int(cursor);
}
// Run after the game's row update and before it positions the selection hand.
// Native input still provides repeat timing, sounds and physical row movement.
extern "C" void wkc2_graphics_menu_scroll(ppu_context*,uint32_t object) {
    if(page!=Page::graphics || object!=owner || vm_read32(object+0x1cc)!=3 || restore_cursor>=0) return;
    unsigned cursor=vm_read16(object+0x102);
    const unsigned native_cursor=cursor;
    if(cursor>=visible_rows) return;
    if(last_cursor==int(visible_rows-1) && cursor==0) {
        if(first<max_first) { ++first;cursor=visible_rows-1; }
        else first=0; // Back -> first item.
    } else if(last_cursor==0 && cursor==visible_rows-1) {
        if(first>0) { --first;cursor=0; }
        else first=max_first; // First item -> Back.
    }
    if(cursor!=native_cursor) highlight_from=int(native_cursor);
    vm_write16(object+0x102,uint16_t(cursor));
    rows(object);
    last_cursor=int(cursor);
}

extern "C" int wkc2_graphics_menu_finish(ppu_context* ctx) {
    const uint32_t object=uint32_t(ctx->gpr[3]);
    if(object!=owner || vm_read32(object+0x1cc)!=3) return 0;
    const int selected=int16_t(vm_read16(object+0x200));
    const uint32_t count=vm_read32(object+0x1d0);
    if (page!=Page::system && (count>8 || selected>=int(count))) {
        ctx->gpr[3]=5; return 1; // malformed selection: keep the page waiting
    }
    const bool native=selected>=0 && unsigned(selected)<count &&
        (vm_read32(object+0x208+selected*24+4)&0xffffff00u)==marker;
    if(page==Page::system && !native) return 0;
    if(page==Page::system) {
        page=Page::graphics;first=0;last_cursor=-1;restore_cursor=0;
    } else if(selected<0 || first+unsigned(selected)==option_count) {
        page=Page::system;restore_cursor=int(system_count);last_cursor=-1;
    } else {
        const auto option=options[first+unsigned(selected)];
        if(available(option) && !wkc::graphics_change(option,1))
            std::fprintf(stderr,"[graphics-menu] preference write failed\n");
        restore_cursor=selected;
    }
    if(page==Page::graphics) highlight_from=0;
    call(ctx,func_0004DFB8,uint32_t(ctx->gpr[5]),vm_read32(uint32_t(ctx->gpr[5])+0x194),0x8e,1,0);
    // Close the old widgets, then return the original create state. This
    // lets the game rebuild rows and enter with the correct previous state.
    call(ctx,func_0065D0B4,object,uint32_t(ctx->gpr[5]),vm_read16(object+0x1ea));
    vm_write16(object+0x1ea,0);
    ctx->gpr[3]=0;
    return 1;
}
