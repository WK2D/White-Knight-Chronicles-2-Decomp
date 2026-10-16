#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "../osk.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>
#include <tuple>
#include <vector>

static std::array<uint8_t,65536> memory;
extern "C" { uint8_t* vm_base=memory.data();void wkc2_register_osk(void); }
using Handler=void(*)(ppu_context*);
static std::map<uint32_t,Handler> handlers;
static std::vector<std::tuple<int,uint32_t,uint32_t>> queued,delivered;
static uint64_t generation=0;
static uint32_t ui_limit=0;
static std::u16string ui_prompt,ui_initial;
static bool ui_available=true;
static unsigned closed=0;
extern "C" void ps3_hle_register_ctx(uint32_t nid,const char*,Handler handler) { handlers[nid]=handler; }
extern "C" void cellSysutilQueueEvent(int slot,uint32_t status,uint32_t value) { queued.emplace_back(slot,status,value); }
extern "C" int32_t cellSysutilCheckCallback(void) {
    delivered.insert(delivered.end(),queued.begin(),queued.end());queued.clear();return 0;
}
bool wkc2_osk_ui_open(uint64_t id,const std::u16string& prompt,const std::u16string& initial,uint32_t limit,bool) {
    generation=id;ui_prompt=prompt;ui_initial=initial;ui_limit=limit;return ui_available;
}
void wkc2_osk_ui_close(uint64_t) { ++closed; }
void wkc2_osk_ui_clear(uint64_t) {}
void wkc2_osk_ui_show(uint64_t,bool) {}
static void w32(uint32_t a,uint32_t v) { for(unsigned i=0;i<4;++i) memory[a+i]=uint8_t(v>>(24-8*i)); }
static uint32_t r32(uint32_t a) { return uint32_t(memory[a])<<24|uint32_t(memory[a+1])<<16|uint32_t(memory[a+2])<<8|memory[a+3]; }
static uint16_t r16(uint32_t a) { return uint16_t(memory[a])<<8|memory[a+1]; }
static void text(uint32_t a,const std::u16string& value) {
    for(auto c:value) { memory[a++]=uint8_t(c>>8);memory[a++]=uint8_t(c); } memory[a++]=0;memory[a]=0;
}
static uint32_t call(const char* name,std::initializer_list<uint32_t> args={}) {
    ppu_context context{};unsigned i=3;for(auto a:args) context.gpr[i++]=a;
    handlers.at(ps3_compute_nid(name))(&context);return uint32_t(context.gpr[3]);
}
static void poll() { assert(call("cellSysutilCheckCallback")==0); }
static void status(size_t first,uint32_t code,uint32_t value=0) {
    assert(delivered.size()>=first+4);
    for(int i=0;i<4;++i) assert(delivered[first+i]==std::make_tuple(i,code,value));
}
int main(int argc,char** argv) {
    assert(argc==2);
    wkc2_register_osk();assert(handlers.size()==7 && handlers.count(0x7FCFC915));
    constexpr uint32_t params=0x100,input=0x200,output=0x300,buffer=0x500;
    w32(input,0x800);w32(input+4,0xC00);w32(input+8,8);
    text(0x800,u"Character name");text(0xC00,u"Initial");w32(params+16,3);
    w32(output+4,3);w32(output+8,buffer);
    assert(call("cellOskDialogUnloadAsync",{output})==0x8002B302);
    assert(call("cellOskDialogLoadAsync",{0,0,input})==0x8002B504);
    w32(input+8,513);assert(call("cellOskDialogLoadAsync",{0,params,input})==0x8002B504);
    w32(input+8,8);
    // No invented Init dependency; pointer layout is 12 guest bytes, not host pointers.
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0);
    assert(ui_prompt==u"Character name" && ui_initial==u"Initial" && ui_limit==8);
    uint64_t old=generation;
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0x8002B105);
    assert(delivered.empty());poll();status(0,0x502);assert(delivered.size()==4);
    std::memset(memory.data()+buffer,0xA5,32);
    assert(call("cellOskDialogGetInputText",{output})==0 && r32(output)==3);
    assert(r32(output+4)==3 && memory[buffer]==0xA5);
    assert(!wkc2_osk_complete(old,0,u"A B") && !wkc2_osk_complete(old,0,u"A\nB"));
    assert(!wkc2_osk_complete(old,0,std::u16string(1,char16_t(0xD800))));
    std::thread producer([&] { assert(wkc2_osk_complete(old,0,u"A\u00E9\U0001F600BCDEFG")); });producer.join();
    assert(!wkc2_osk_complete(old,1,{}));assert(delivered.size()==4);
    poll();status(4,0x503);assert(closed==1);
    // Capacity is an input count excluding the terminator, and is preserved.
    // Three units would split the emoji pair, so copy only A + accented e.
    assert(call("cellOskDialogUnloadAsync",{output})==0 && r32(output)==0 && r32(output+4)==3);
    assert(r16(buffer)=='A' && r16(buffer+2)==0xE9 && r16(buffer+4)==0 && memory[buffer+6]==0xA5);
    poll();status(8,0x504);assert(!wkc2_osk_complete(old,0,u"stale"));
    assert(call("cellOskDialogUnloadAsync",{output})==0x8002B302);
    // Cancellation leaves the caller's string untouched.
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0);poll();
    assert(!wkc2_osk_complete(old,0,u"stale"));
    assert(wkc2_osk_complete(generation,1,{}));poll();
    std::memset(memory.data()+buffer,0x5A,32);
    assert(call("cellOskDialogUnloadAsync",{output})==0 && r32(output)==1 && memory[buffer]==0x5A);poll();
    // Abort invalidates pending UI completion but still requires Unload.
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0);poll();
    assert(call("cellOskDialogAbort")==0);assert(!wkc2_osk_complete(generation,0,u"late"));poll();
    assert(call("cellOskDialogUnloadAsync",{output})==0 && r32(output)==2);poll();
    assert(call("cellOskDialogAbort")==0x8002B302);
    // Empty acceptance clears stale caller text, including capacity zero.
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0);poll();
    assert(wkc2_osk_complete(generation,0,{}));poll();w32(output+4,0);
    assert(call("cellOskDialogUnloadAsync",{output})==0 && r16(buffer)==0 && r32(output+4)==0);poll();
    // A failed platform launch releases the lifetime without publishing LOADED.
    ui_available=false;size_t count=delivered.size();
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0x8002B105);poll();assert(delivered.size()==count);
    ui_available=true;
    // The optional scripted input uses the same completion path as the UI.
#ifdef _WIN32
    _putenv_s("WKC2_OSK_INPUT_FILE",argv[1]);
#else
    setenv("WKC2_OSK_INPUT_FILE",argv[1],1);
#endif
    auto request=[&](const char* bytes,size_t size) {
        FILE* file=std::fopen(argv[1],"wb");assert(file);
        assert(std::fwrite(bytes,1,size,file)==size);std::fclose(file);
    };
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0);poll();
    count=delivered.size();
    const char invalid[]="submit \xC0\xAF";request(invalid,sizeof(invalid)-1);poll();
    assert(delivered.size()==count);
    const char embedded[]="submit ABC\0hidden";request(embedded,sizeof(embedded)-1);poll();
    assert(delivered.size()==count);
    const char valid[]="submit N\xC3\xA9\r\n";request(valid,sizeof(valid)-1);poll();
    status(count,0x503);w32(output+4,8);
    assert(call("cellOskDialogUnloadAsync",{output})==0 && r16(buffer)=='N' && r16(buffer+2)==0xE9 && r16(buffer+4)==0);poll();
    assert(call("cellOskDialogLoadAsync",{0,params,input})==0);poll();
    request("cancel",6);poll();
    assert(call("cellOskDialogUnloadAsync",{output})==0 && r32(output)==1);poll();
    std::remove(argv[1]);
#ifdef _WIN32
    _putenv_s("WKC2_OSK_INPUT_FILE","");
#else
    unsetenv("WKC2_OSK_INPUT_FILE");
#endif
    memory[0xA04]=0xA5;
    assert(call("cellOskDialogGetSize",{0xA00,0xA02,0})==0 && r16(0xA00)==1 && r16(0xA02)==1 && memory[0xA04]==0xA5);
    assert(call("cellOskDialogGetSize",{0xA00,0xA02,4})==0 && r16(0xA00)==0);
    assert(call("cellOskDialogSetSeparateWindowOption",{0})==0x8002B504);
    w32(0xA20,4);assert(call("cellOskDialogSetSeparateWindowOption",{0xA20})==0x8002B504);
    w32(0xA20+16,0xA40);
    for(uint32_t mode:{1u,2u,3u}) {
        w32(0xA20,mode);assert(call("cellOskDialogSetSeparateWindowOption",{0xA20})==0);
        count=delivered.size();
        assert(call("cellOskDialogLoadAsync",{0,params,input})==0);poll();
        status(count,0x508,mode==2 ? 0 : 1);status(count+4,0x502);
        count=delivered.size();assert(wkc2_osk_complete(generation,0,u"Native"));poll();
        status(count,0x505);
        assert(call("cellOskDialogGetInputText",{output})==0 && r32(output)==0 && r16(buffer)=='N' && r16(buffer+12)==0);
        assert(call("cellOskDialogGetInputText",{output})==0 && r32(output)==3);
        count=delivered.size();assert(wkc2_osk_complete(generation,1,{}));poll();status(count,0x506);
        assert(call("cellOskDialogAbort")==0);poll();
        assert(call("cellOskDialogUnloadAsync",{output})==0 && r32(output)==2);poll();
    }
    std::puts("OSK guest ABI, callbacks, Unicode limits, cancellation, abort, stale generations and scripted UTF-8 input passed");
}
