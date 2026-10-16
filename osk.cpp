// Guest OSK ABI and asynchronous lifetime. Platform windows stay outside this
// file so a future Apple Silicon backend can share the same protocol.
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "osk.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
void cellSysutilQueueEvent(int, uint32_t, uint32_t);
int32_t cellSysutilCheckCallback(void);
}
namespace {
constexpr uint32_t param_error=0x8002B504, busy=0x8002B105, not_open=0x8002B302;
enum class Phase { unloaded, open, finished };
struct State {
    Phase phase=Phase::unloaded;
    uint64_t generation=0;
    uint32_t limit=0, prohibit=0, result=0;
    std::u16string text;
    bool completion_pending=false;
    bool separate=false,has_input=false;
    uint32_t continuous=0;
};
std::mutex mutex;
State state;
std::deque<std::pair<uint32_t,uint32_t>> events;
uint64_t next_generation=0;
bool separate_option=false;
uint32_t continuous_option=0;
bool range(uint32_t address, uint64_t bytes) {
    return address && bytes<=0x100000000ull-address;
}
uint32_t read32(uint32_t a) {
    return uint32_t(vm_base[a])<<24|uint32_t(vm_base[a+1])<<16|
           uint32_t(vm_base[a+2])<<8|vm_base[a+3];
}
uint16_t read16(uint32_t a) { return uint16_t(vm_base[a])<<8|vm_base[a+1]; }
void write32(uint32_t a,uint32_t v) {
    for(unsigned i=0;i<4;++i) vm_base[a+i]=uint8_t(v>>(24-8*i));
}
void write16(uint32_t a,uint16_t v) { vm_base[a]=uint8_t(v>>8);vm_base[a+1]=uint8_t(v); }
uint32_t arg(ppu_context* c,unsigned i) { return uint32_t(c->gpr[3+i]); }
void result(ppu_context* c,uint32_t v) { c->gpr[3]=uint64_t(int64_t(int32_t(v))); }
bool guest_text(uint32_t a,uint32_t limit,std::u16string& text) {
    if(!range(a,2ull*(limit+1))) return false;
    text.clear();
    for(uint32_t i=0;i<limit;++i) {
        char16_t unit=read16(a+2*i); if(!unit) break; text.push_back(unit);
    }
    return true;
}
bool valid_text(const std::u16string& text,uint32_t prohibit) {
    for(size_t i=0;i<text.size();++i) {
        uint16_t c=text[i];
        if(!c || ((prohibit&1) && c==' ') || ((prohibit&2) && (c=='\r'||c=='\n'))) return false;
        if(c>=0xD800 && c<=0xDBFF) {
            if(++i==text.size() || text[i]<0xDC00 || text[i]>0xDFFF) return false;
        } else if(c>=0xDC00 && c<=0xDFFF) return false;
    }
    return true;
}
std::u16string limited(std::u16string text,uint32_t limit) {
    if(text.size()>limit) text.resize(limit);
    if(!text.empty() && text.back()>=0xD800 && text.back()<=0xDBFF) text.pop_back();
    return text;
}
void event(uint32_t code,uint32_t value=0) { events.emplace_back(code,value); }
void load(ppu_context* c) {
    uint32_t params=arg(c,1),input=arg(c,2);
    if(!range(params,20)||!range(input,12)) { result(c,param_error);return; }
    uint32_t limit=read32(input+8);
    std::u16string prompt,initial;
    if(limit>512 || !guest_text(read32(input),512,prompt) ||
       !guest_text(read32(input+4),std::min(limit,511u),initial)) {
        result(c,param_error);return;
    }
    uint64_t generation;uint32_t mode;
    {
        std::lock_guard lock(mutex);
        if(state.phase!=Phase::unloaded) { result(c,busy);return; }
        state=State{};state.phase=Phase::open;
        state.generation=generation=++next_generation;
        state.limit=std::min(limit,511u);state.prohibit=read32(params+16);
        state.separate=separate_option;state.continuous=continuous_option;
        mode=state.continuous;
        state.text=limited(initial,state.limit);
    }
    if(!wkc2_osk_ui_open(generation,prompt,initial,std::min(limit,511u),mode!=2)) {
        std::lock_guard lock(mutex);state=State{};result(c,busy);return;
    }
    {
        std::lock_guard lock(mutex);
        if(state.separate) event(0x508,mode==2 ? 0 : 1);
        event(0x502);
    }
    std::fprintf(stderr,"[wkc2-osk] loaded generation=%llu limit=%u prohibit=%X continuous=%u\n",
        (unsigned long long)generation,limit,read32(params+16),mode);
    result(c,0);
}
void copy_result(ppu_context* c,bool unload) {
    uint32_t output=arg(c,0);
    if(!range(output,12)) { result(c,param_error);return; }
    int32_t capacity=int32_t(read32(output+4));uint32_t text=read32(output+8);
    if(capacity<0 || (text && !range(text,2ull*(std::min(uint32_t(capacity),511u)+1)))) {
        result(c,param_error);return;
    }
    uint64_t generation=0,clear_generation=0;
    {
        std::lock_guard lock(mutex);
        if(state.phase==Phase::unloaded) { result(c,not_open);return; }
        // In normal (non-continuous) mode GetInputText has no committed text.
        // Unload returns the accepted text. numCharsResultString is input capacity.
        bool continuous=state.separate && state.continuous!=0;
        uint32_t status=unload ? state.result : continuous && state.has_input && !state.text.empty() ? 0 : 3;
        write32(output,status);
        if(text && (status==0 || (unload && status==3))) {
            auto value=limited(state.text,std::min(uint32_t(capacity),511u));
            for(uint32_t i=0;i<value.size();++i) write16(text+2*i,value[i]);
            write16(text+2*uint32_t(value.size()),0);
            std::fprintf(stderr,"[wkc2-osk] %s text units=%u capacity=%d\n",
                unload ? "unload" : "get",unsigned(value.size()),capacity);
        }
        if(!unload && continuous) {
            state.text.clear();state.has_input=false;clear_generation=state.generation;
        }
        if(unload) {
            generation=state.generation;state=State{};event(0x504);
            separate_option=false;continuous_option=0;
            std::fprintf(stderr,"[wkc2-osk] unloaded generation=%llu result=%u\n",
                (unsigned long long)generation,status);
        }
    }
    if(generation) wkc2_osk_ui_close(generation);
    if(clear_generation) wkc2_osk_ui_clear(clear_generation);
    result(c,0);
}
void unload(ppu_context* c) { copy_result(c,true); }
void get_text(ppu_context* c) { copy_result(c,false); }
void abort_dialog(ppu_context* c) {
    uint64_t generation;
    {
        std::lock_guard lock(mutex);
        if(state.phase==Phase::unloaded) { result(c,not_open);return; }
        generation=state.generation;state.phase=Phase::finished;
        state.result=2;state.completion_pending=false;event(0x503);
    }
    wkc2_osk_ui_close(generation);result(c,0);
}
void get_size(ppu_context* c) {
    uint32_t w=arg(c,0),h=arg(c,1);
    if(!range(w,2)||!range(h,2)) { result(c,param_error);return; }
    write16(w,arg(c,2)>=4 ? 0 : 1);write16(h,1);result(c,0);
}
void separate(ppu_context* c) {
    uint32_t a=arg(c,0);
    if(!range(a,28) || read32(a)>3 || read32(a+4)>0xFF || !range(read32(a+16),12) || read32(a+24)) {
        result(c,param_error);return;
    }
    uint64_t generation=0;bool visible=read32(a)!=2;
    { std::lock_guard lock(mutex);separate_option=true;continuous_option=read32(a);
      if(state.phase!=Phase::unloaded) {
          state.separate=true;state.continuous=continuous_option;
          generation=state.generation;event(0x508,visible ? 1 : 0);
      }
    }
    if(generation) wkc2_osk_ui_show(generation,visible);
    result(c,0);
}
bool utf8(const char* bytes,std::u16string& out) {
    auto p=reinterpret_cast<const unsigned char*>(bytes);
    while(*p) {
        uint32_t cp=*p++;unsigned more=0;uint32_t minimum=0;
        if(cp>=0xC2&&cp<=0xDF) { cp&=31;more=1;minimum=0x80; }
        else if(cp>=0xE0&&cp<=0xEF) { cp&=15;more=2;minimum=0x800; }
        else if(cp>=0xF0&&cp<=0xF4) { cp&=7;more=3;minimum=0x10000; }
        else if(cp>=0x80) return false;
        for(unsigned i=0;i<more;++i) { if((*p&0xC0)!=0x80) return false;cp=(cp<<6)|(*p++&63); }
        if(cp<minimum || cp>0x10FFFF || (cp>=0xD800&&cp<=0xDFFF)) return false;
        if(cp<0x10000) out.push_back(char16_t(cp));
        else { cp-=0x10000;out.push_back(char16_t(0xD800+(cp>>10)));out.push_back(char16_t(0xDC00+(cp&1023))); }
    }
    return true;
}
void file_input() {
    const char* path=std::getenv("WKC2_OSK_INPUT_FILE");if(!path||!*path) return;
    uint64_t generation;
    { std::lock_guard lock(mutex);if(state.phase!=Phase::open) return;generation=state.generation; }
    FILE* f=std::fopen(path,"rb");if(!f) return;
    char line[4096];size_t n=std::fread(line,1,sizeof(line)-1,f);bool oversized=std::fgetc(f)!=EOF;
    std::fclose(f);if(!n) return;oversized=oversized||std::memchr(line,0,n)!=nullptr;line[n]=0;
    f=std::fopen(path,"wb");if(f) std::fclose(f);
    while(n && (line[n-1]=='\r'||line[n-1]=='\n')) line[--n]=0;
    std::u16string text;
    bool ok=!oversized && std::string(line)=="cancel" ? wkc2_osk_complete(generation,1,{}) :
        !oversized && std::string(line).rfind("submit ",0)==0 && utf8(line+7,text) &&
        wkc2_osk_complete(generation,0,text);
    std::fprintf(stderr,"[wkc2-osk] input request %s\n",ok ? "accepted" : "rejected");
}
void check_callbacks(ppu_context* c) {
    file_input();
    std::deque<std::pair<uint32_t,uint32_t>> batch;
    uint64_t generation=0,hide_generation=0;
    {
        std::lock_guard lock(mutex);
        if(state.phase==Phase::open && state.completion_pending) {
            state.completion_pending=false;
            if(state.separate && state.continuous!=0) {
                state.has_input=state.result!=1;event(state.result==1 ? 0x506 : 0x505);
                if(state.continuous==2) { hide_generation=state.generation;event(0x508,0); }
            } else {
                state.phase=Phase::finished;generation=state.generation;event(0x503);
            }
        }
        batch.swap(events);
    }
    if(generation) wkc2_osk_ui_close(generation);
    if(hide_generation) wkc2_osk_ui_show(hide_generation,false);
    // Only the callback-polling thread touches the SDK's event ring.
    for(auto [status,value]:batch)
        for(int slot=0;slot<4;++slot) cellSysutilQueueEvent(slot,status,value);
    result(c,uint32_t(cellSysutilCheckCallback()));
}
}
bool wkc2_osk_complete(uint64_t generation,uint32_t status,const std::u16string& text) {
    std::lock_guard lock(mutex);
    if(state.phase!=Phase::open || state.generation!=generation || state.completion_pending || status>1) return false;
    if(status==0 && !valid_text(text,state.prohibit)) return false;
    state.result=status==0 && state.separate && text.empty() ? 3 : status;
    state.text=status==0 ? limited(text,state.limit) : std::u16string{};
    state.completion_pending=true;return true;
}
#if !defined(_WIN32) && !defined(WKC2_OSK_TEST_BACKEND)
bool wkc2_osk_ui_open(uint64_t,const std::u16string&,const std::u16string&,uint32_t,bool) { return false; }
void wkc2_osk_ui_close(uint64_t) {}
void wkc2_osk_ui_clear(uint64_t) {}
void wkc2_osk_ui_show(uint64_t,bool) {}
#endif
extern "C" void wkc2_register_osk(void) {
    struct Binding { const char* name;void (*fn)(ppu_context*); };
    const Binding bindings[]={
        {"cellOskDialogLoadAsync",load},{"cellOskDialogUnloadAsync",unload},
        {"cellOskDialogGetInputText",get_text},{"cellOskDialogAbort",abort_dialog},
        {"cellOskDialogGetSize",get_size},{"cellOskDialogSetSeparateWindowOption",separate},
        {"cellSysutilCheckCallback",check_callbacks}};
    for(const auto& b:bindings) ps3_hle_register_ctx(ps3_compute_nid(b.name),b.name,b.fn);
}
