/* Project-local cellDmux bridge. Guest ABI records are byte encoded, callbacks
 * use guest OPDs, and input is actually parsed into codec access units.
 * The worker retains each AU in the supplied guest resource until ReleaseAu.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/guest_call.h"
#include "mpegps.hpp"
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register_ctx(uint32_t,const char*,void (*)(ppu_context*));
}
namespace {
using namespace wkc2::mpeg;
constexpr uint32_t arg_error=0x80610201,seq_error=0x80610202;
constexpr uint32_t busy_error=0x80610203,empty_error=0x80610204,fatal_error=0x80610205;
bool range(uint32_t a,uint64_t n) { return a && n<= (uint64_t(1)<<32)-a; }
uint32_t r32(uint32_t a) { return uint32_t(vm_base[a])<<24|uint32_t(vm_base[a+1])<<16|uint32_t(vm_base[a+2])<<8|vm_base[a+3]; }
void w32(uint32_t a,uint32_t v) { for(unsigned i=0;i<4;++i) vm_base[a+i]=uint8_t(v>>(24-i*8)); }
void w64(uint32_t a,uint64_t v) { w32(a,uint32_t(v>>32)); w32(a+4,uint32_t(v)); }
uint32_t arg(ppu_context* c,unsigned n) { return uint32_t(c->gpr[3+n]); }
void result(ppu_context* c,uint32_t v) { c->gpr[3]=uint64_t(int64_t(int32_t(v))); }
struct Allocation { uint32_t address,size; bool acquired=false; };
struct Elementary {
    uint32_t handle=0,size=0,capacity=0,callback=0,user=0,major=0,minor=0;
    bool enabled=true,flushing=false;
    Codec codec=Codec::mpeg2;
    Assembler assembler;
    std::map<uint32_t,uint32_t> free;
    std::deque<Allocation> units;
    uint64_t published=0;
};
struct Command {
    uint32_t flush_es=0;
    bool discontinuity=false;
    uint64_t user=0;
    std::vector<uint8_t> input;
};
struct Demuxer {
    uint32_t handle=0,callback=0,user=0;
    std::mutex mutex;
    std::condition_variable changed;
    bool closing=false,busy=false,failed=false;
    std::deque<Command> commands;
    std::map<uint32_t,std::shared_ptr<Elementary>> streams;
    std::thread worker;
    ~Demuxer() { if(worker.joinable()) { {std::lock_guard l(mutex); closing=true;} changed.notify_all(); worker.join(); } }
};
std::mutex directory_mutex;
std::map<uint32_t,std::shared_ptr<Demuxer>> directory;
using EsEntry=std::pair<std::shared_ptr<Demuxer>,std::shared_ptr<Elementary>>;
std::map<uint32_t,EsEntry> es_directory;
std::shared_ptr<Demuxer> find_dmux(uint32_t handle) {
    std::lock_guard l(directory_mutex); auto i=directory.find(handle);
    return i==directory.end() ? nullptr : i->second;
}
EsEntry find_es(uint32_t handle) {
    std::lock_guard l(directory_mutex); auto i=es_directory.find(handle);
    return i==es_directory.end() ? EsEntry{} : i->second;
}
void message(const std::shared_ptr<Demuxer>& d,uint32_t type,uint64_t info=0) {
    uint32_t m=d->handle+0xC0; std::memset(vm_base+m,0,16); w32(m,type); w64(m+8,info);
    ps3_invoke_guest(d->callback,d->handle,m,d->user,0,0,0,0,0);
}
void es_message(const std::shared_ptr<Demuxer>& d,const std::shared_ptr<Elementary>& es,uint32_t type) {
    uint32_t m=es->handle+0x50; std::memset(vm_base+m,0,16); w32(m,type);
    ps3_invoke_guest(es->callback,d->handle,es->handle,m,es->user,0,0,0,0);
}
void release_block(Elementary& es,uint32_t address,uint32_t size) {
    uint32_t off=address-es.handle;
    auto next=es.free.lower_bound(off);
    if(next!=es.free.begin()) { auto prev=std::prev(next); if(uint64_t(prev->first)+prev->second==off) { off=prev->first; size+=prev->second; es.free.erase(prev); } }
    next=es.free.lower_bound(off);
    if(next!=es.free.end() && uint64_t(off)+size==next->first) { size+=next->second; es.free.erase(next); }
    es.free.emplace(off,size);
}
bool publish(const std::shared_ptr<Demuxer>& d,const std::shared_ptr<Elementary>& es,const AccessUnit& au) {
    uint64_t want=(au.data.size()+128+127)&~uint64_t(127);
    if(want>es->capacity) throw std::runtime_error("access unit exceeds guest ES resource");
    std::unique_lock l(d->mutex);
    auto block=es->free.end();
    d->changed.wait(l,[&] {
        block=std::find_if(es->free.begin(),es->free.end(),[&](auto& b){return b.second>=want;});
        return d->closing || !es->enabled || (block!=es->free.end() && es->units.size()<256);
    });
    if(d->closing || !es->enabled) return false;
    uint32_t off=block->first,available=block->second,address=es->handle+off;
    es->free.erase(block);
    if(available>want) es->free.emplace(off+uint32_t(want),available-uint32_t(want));
    std::memset(vm_base+address,0,128);
    w32(address,address+128); w32(address+4,uint32_t(au.data.size()));
    w32(address+8,uint32_t(au.data.size())); vm_base[address+12]=uint8_t(au.rap);
    w64(address+16,au.user); w64(address+24,au.pts); w64(address+32,au.dts);
    if(es->codec==Codec::lpcm) std::memcpy(vm_base+address+40,au.specific.data(),3);
    std::memcpy(vm_base+address+128,au.data.data(),au.data.size());
    es->units.push_back({address,uint32_t(want)});
    uint64_t n=++es->published;
    l.unlock();
    if(n<=3 || n%256==0) std::printf("[wkc2-dmux] ES %08X AU #%llu: %zu bytes pts=%llu\n",es->handle,(unsigned long long)n,au.data.size(),(unsigned long long)au.pts);
    es_message(d,es,0); return true;
}
void run(const std::shared_ptr<Demuxer>& d) {
    ProgramStream parser;
    for (;;) {
        Command command;
        std::vector<std::shared_ptr<Elementary>> streams;
        { std::unique_lock l(d->mutex); d->changed.wait(l,[&]{return d->closing || !d->commands.empty();});
          if(d->closing) return;
          command=std::move(d->commands.front()); d->commands.pop_front();
          for(auto& [id,s]:d->streams) streams.push_back(s);
        }
        try {
            if(command.flush_es) {
                for(auto& s:streams) if(s->handle==command.flush_es) {
                    for(auto& au:s->assembler.flush()) if(!publish(d,s,au)) return;
                    {std::lock_guard l(d->mutex); s->flushing=false;}
                    es_message(d,s,1);
                }
                continue;
            }
            if(command.discontinuity) { parser.reset(); for(auto& s:streams) s->assembler.reset(); }
            parser.append(command.input,command.user); Packet packet;
            while(parser.next(packet)) {
                if(packet.end) { message(d,2,command.user); continue; }
                for(auto& s:streams) {
                    if(s->major!=packet.id || (packet.id==0xBD && (packet.data.empty() || packet.data[0]!=s->minor))) continue;
                    for(auto& au:s->assembler.feed(packet)) if(!publish(d,s,au)) return;
                }
            }
            {std::lock_guard l(d->mutex); d->busy=false;}
            message(d,0,command.user);
        } catch(const std::exception& e) {
            {std::lock_guard l(d->mutex); d->busy=false; d->failed=true;}
            std::fprintf(stderr,"[wkc2-dmux] parsing failed: %s\n",e.what()); message(d,1,fatal_error);
        }
    }
}
void open_dmux(ppu_context* c) {
    uint32_t type=arg(c,0),res=arg(c,1),cb=arg(c,2),out=arg(c,3);
    if(!range(type,12) || r32(type)!=1 || !range(res,24) || !range(cb,8) || !r32(cb) || !range(out,4)) {result(c,arg_error); return;}
    uint32_t base=r32(res),size=r32(res+4);
    if(!range(base,size) || size<256 || (base&15)) {result(c,arg_error); return;}
    auto d=std::make_shared<Demuxer>(); d->handle=base; d->callback=r32(cb); d->user=r32(cb+4);
    {std::lock_guard l(directory_mutex); if(directory.count(base) || directory.size()>=8) {result(c,busy_error); return;} directory.emplace(base,d);}
    std::memset(vm_base+base,0,256); w32(base,base); w32(base+4,256); w32(out,base);
    d->worker=std::thread([d]{run(d);});
    std::printf("[wkc2-dmux] open %08X resource=%u callback=%08X\n",base,size,d->callback); result(c,0);
}
void stop(const std::shared_ptr<Demuxer>& d) {
    {std::lock_guard l(d->mutex); d->closing=true;} d->changed.notify_all();
    if(d->worker.joinable()) {
        if(d->worker.get_id()==std::this_thread::get_id()) d->worker.detach();
        else d->worker.join();
    }
}
void close_dmux(ppu_context* c) {
    auto d=find_dmux(arg(c,0)); if(!d) {result(c,arg_error); return;}
    if(d->worker.get_id()==std::this_thread::get_id()) {result(c,seq_error); return;}
    stop(d);
    {std::lock_guard l(directory_mutex); for(auto i=es_directory.begin();i!=es_directory.end();) {if(i->second.first==d) i=es_directory.erase(i); else ++i;} directory.erase(d->handle);}
    result(c,0);
}
struct EsConfig {Codec codec; size_t pcm=960; uint32_t resource=0;};
bool configure(uint32_t filter,uint32_t specific,EsConfig& config) {
    if(!range(filter,16) || r32(filter+12)) return false;
    uint32_t major=r32(filter),minor=r32(filter+4),supp=r32(filter+8);
    if(major>=0xE0 && major<=0xEF && !minor && supp<=1) {
        config.codec=supp ? Codec::avc : Codec::mpeg2;
        uint32_t max_unit=supp ? 0xCC000 : 0x12A800;
        config.resource=max_unit*4+0x1000;
    } else if(major==0xBD && minor>=0x40 && minor<=0x4F && !supp) {
        config.codec=Codec::lpcm;
        if(!range(specific,12)) return false;
        uint32_t rate=r32(specific),channels=r32(specific+4),bits=r32(specific+8);
        if(rate!=48000 || (channels!=1 && channels!=2 && channels!=6 && channels!=8) || (bits!=16 && bits!=24)) return false;
        config.pcm=uint64_t(rate)*(channels+(channels&1))*(bits/8)/200;
        config.resource=uint32_t(((config.pcm+255)&~size_t(127))*256+256);
    } else if(major==0xBD && minor>=0x30 && minor<=0x3F && !supp) {
        config.codec=Codec::ac3; config.resource=(3840+128)*64+256;
    } else return false;
    return true;
}
void query_attr(ppu_context* c) {
    uint32_t type=arg(c,0),out=arg(c,1);
    if(!range(type,12) || r32(type)!=1 || !range(out,12)) {result(c,arg_error); return;}
    // Work-memory sizing for this implementation, not an acknowledgement stub.
    w32(out,4096); w32(out+4,0x10000); w32(out+8,0); result(c,0);
}
void query_es_attr(ppu_context* c) {
    uint32_t type=arg(c,0),out=arg(c,3); EsConfig config;
    if(!range(type,12) || r32(type)!=1 || !range(out,4) || !configure(arg(c,1),arg(c,2),config)) {result(c,arg_error); return;}
    w32(out,config.resource); result(c,0);
}
void enable_es(ppu_context* c) {
    auto d=find_dmux(arg(c,0)); uint32_t filter=arg(c,1),res=arg(c,2),cb=arg(c,3),specific=arg(c,4),out=arg(c,5);
    if(!d || !range(filter,16) || !range(res,8) || !range(cb,8) || !r32(cb) || !range(out,4)) {result(c,arg_error); return;}
    uint32_t base=r32(res),size=r32(res+4),major=r32(filter),minor=r32(filter+4),supp=r32(filter+8);
    std::printf("[wkc2-dmux] ES request base=%08X size=%u filter=%X/%X/%X specific=%08X callback=%08X\n",base,size,major,minor,supp,specific,r32(cb));
    EsConfig config;
    if(!configure(filter,specific,config)) {result(c,arg_error); return;}
    if(!range(base,size) || size<512 || (base&15)) {result(c,arg_error); return;}
    auto es=std::make_shared<Elementary>(); es->handle=base; es->size=size;
    es->callback=r32(cb); es->user=r32(cb+4); es->major=major; es->minor=minor; es->codec=config.codec; es->assembler=Assembler(config.codec,config.pcm);
    uint32_t arena=uint32_t((uint64_t(base)+255)&~uint64_t(127))-base;
    es->capacity=(size-arena)&~127u;
    es->free.emplace(arena,es->capacity);
    {std::lock_guard l(d->mutex); if(d->busy || d->closing || d->streams.size()>=16) {result(c,seq_error); return;}
     for(auto& [id,s]:d->streams) if(s->major==major && s->minor==minor) {result(c,arg_error); return;}
     std::lock_guard directory_lock(directory_mutex);
     if(es_directory.count(base)) {result(c,arg_error); return;}
     d->streams.emplace(base,es); es_directory.emplace(base,EsEntry{d,es});}
    std::memset(vm_base+base,0,128); w32(out,base);
    std::printf("[wkc2-dmux] enable ES %08X major=%X minor=%X resource=%u pcm-unit=%zu\n",base,major,minor,size,config.pcm); result(c,0);
}
void disable_es(ppu_context* c) {
    auto [d,s]=find_es(arg(c,0)); if(!d) {result(c,arg_error); return;}
    {std::lock_guard l(d->mutex); if(d->busy || s->flushing) {result(c,seq_error); return;} s->enabled=false; d->streams.erase(s->handle);}
    d->changed.notify_all(); {std::lock_guard l(directory_mutex); es_directory.erase(s->handle);} result(c,0);
}
void set_stream(ppu_context* c) {
    auto d=find_dmux(arg(c,0)); uint32_t address=arg(c,1),size=arg(c,2);
    if(!d || !size || size>16*1024*1024 || !range(address,size)) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex);
    if(d->failed || d->closing) {result(c,seq_error); return;}
    if(d->busy || !d->commands.empty()) {result(c,busy_error); return;}
    Command command; command.discontinuity=arg(c,3)!=0; command.user=c->gpr[7];
    command.input.assign(vm_base+address,vm_base+address+size);
    d->busy=true; d->commands.push_back(std::move(command)); d->changed.notify_one(); result(c,0);
}
void get_au(ppu_context* c,bool peek) {
    auto [d,s]=find_es(arg(c,0)); uint32_t out=arg(c,1),specific=arg(c,2);
    if(!d || (out && !range(out,4)) || (specific && !range(specific,4))) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex);
    auto i=std::find_if(s->units.begin(),s->units.end(),[](const auto& a){return !a.acquired;});
    if(i==s->units.end()) {result(c,empty_error); return;}
    if(out) w32(out,i->address);
    if(specific) w32(specific,s->codec==Codec::lpcm ? i->address+40 : 0);
    if(!peek) i->acquired=true;
    result(c,0);
}
void get(ppu_context* c) {get_au(c,false);}
void peek(ppu_context* c) {get_au(c,true);}
void release(ppu_context* c) {
    auto [d,s]=find_es(arg(c,0)); if(!d) {result(c,arg_error); return;}
    {std::lock_guard l(d->mutex); if(s->units.empty()) {result(c,seq_error); return;}
     auto a=s->units.front(); s->units.pop_front(); release_block(*s,a.address,a.size);}
    d->changed.notify_one(); result(c,0);
}
void flush(ppu_context* c) {
    auto [d,s]=find_es(arg(c,0)); if(!d) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex);
    if(d->busy || d->closing || s->flushing) {result(c,seq_error); return;}
    s->flushing=true; Command command; command.flush_es=s->handle; d->commands.push_back(std::move(command)); d->changed.notify_one(); result(c,0);
}
} // namespace
extern "C" void wkc2_shutdown_dmux(void) {
    std::vector<std::shared_ptr<Demuxer>> all;
    {std::lock_guard l(directory_mutex); for(auto& [id,d]:directory) all.push_back(d);}
    for(auto& d:all) stop(d);
    {std::lock_guard l(directory_mutex); es_directory.clear(); directory.clear();}
}
extern "C" void wkc2_dump_dmux(void) {
    std::vector<std::shared_ptr<Demuxer>> all;
    {std::lock_guard l(directory_mutex); for(auto& [h,d]:directory) all.push_back(d);}
    for(auto& d:all) {
        std::lock_guard l(d->mutex);
        std::fprintf(stderr,"[dmux-state] handle=%08X busy=%d commands=%zu failed=%d\n",d->handle,d->busy,d->commands.size(),d->failed);
        for(auto& [h,s]:d->streams) {
            unsigned acquired=0; uint64_t bytes=0;
            for(auto& u:s->units) acquired+=u.acquired;
            for(auto& [a,n]:s->free) bytes+=n;
            std::fprintf(stderr,"[dmux-state] ES=%08X published=%llu pending=%zu acquired=%u free=%llu\n",h,(unsigned long long)s->published,s->units.size(),acquired,(unsigned long long)bytes);
        }
    }
}
extern "C" void wkc2_register_dmux(void) {
    struct Binding {const char* name; void (*fn)(ppu_context*);};
    const Binding bindings[]={
        {"cellDmuxQueryAttr",query_attr},{"cellDmuxQueryEsAttr",query_es_attr},
        {"cellDmuxOpen",open_dmux},{"cellDmuxClose",close_dmux},
        {"cellDmuxEnableEs",enable_es},{"cellDmuxDisableEs",disable_es},
        {"cellDmuxSetStream",set_stream},{"cellDmuxGetAu",get},
        {"cellDmuxPeekAu",peek},{"cellDmuxReleaseAu",release},{"cellDmuxFlushEs",flush},
    };
    for(auto& b:bindings) ps3_hle_register_ctx(ps3_compute_nid(b.name),b.name,b.fn);
}
