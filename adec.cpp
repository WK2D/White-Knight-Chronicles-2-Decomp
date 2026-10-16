/* Guest cellAdec bridge for PAMF LPCM. Each accepted AU owns a distinct output
 * slot until GetPcm consumes it. Codec records and floating samples are encoded
 * in guest byte order; callbacks execute through guest procedure descriptors.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/guest_call.h"
#include <array>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register_ctx(uint32_t,const char*,void (*)(ppu_context*));
}
namespace {
constexpr uint32_t fatal=0x80610001,seq=0x80610002,arg_error=0x80610003;
constexpr uint32_t busy=0x80610004,empty=0x80610005;
constexpr uint32_t work_size=0x90000,slot_stride=0x8100,slot_count=16;
constexpr uint64_t invalid_pts=~uint64_t(0);
bool range(uint32_t a,uint64_t n) {return a && n<=(uint64_t(1)<<32)-a;}
uint32_t r32(uint32_t a) {return uint32_t(vm_base[a])<<24|uint32_t(vm_base[a+1])<<16|uint32_t(vm_base[a+2])<<8|vm_base[a+3];}
uint64_t r64(uint32_t a) {return uint64_t(r32(a))<<32|r32(a+4);}
void w32(uint32_t a,uint32_t v) {for(unsigned i=0;i<4;++i) vm_base[a+i]=uint8_t(v>>(24-i*8));}
void w64(uint32_t a,uint64_t v) {w32(a,uint32_t(v>>32)); w32(a+4,uint32_t(v));}
uint32_t arg(ppu_context* c,unsigned n) {return uint32_t(c->gpr[3+n]);}
void result(ppu_context* c,uint32_t v) {c->gpr[3]=uint64_t(int64_t(int32_t(v)));}
struct Format {
    uint32_t assignment=0,rate_code=0,depth=0,payload=0,channels=0,hz=0;
    std::array<unsigned,8> order{};
};
bool format(uint32_t a,Format& f) {
    if(!range(a,16)) return false;
    f.assignment=r32(a); f.rate_code=r32(a+4); f.depth=r32(a+8); f.payload=r32(a+12);
    constexpr unsigned channels[]={0,1,0,2,3,3,4,4,5,6,7,8};
    constexpr unsigned rates[]={0,48000,16000,96000,192000,8000};
    if(f.assignment>=std::size(channels) || !(f.channels=channels[f.assignment]) ||
       f.rate_code>=std::size(rates) || !(f.hz=rates[f.rate_code]) || f.depth<1 || f.depth>3 || !f.payload) return false;
    f.order={0,1,2,3,4,5,6,7};
    // The firmware's output order is L,C,R for layouts containing a center,
    // except 7.1, which uses L,R,C,LFE,SL,SR,RL,RR.
    if(f.assignment==4 || f.assignment==6 || f.assignment==8 || f.assignment==9 || f.assignment==10)
        std::swap(f.order[1],f.order[2]);
    if(f.assignment==10) f.order={0,2,1,3,6,4,5,7};
    if(f.assignment==11) f.order={0,1,2,7,3,6,4,5};
    uint32_t frame=((f.channels+1)&~1u)*(f.depth==1 ? 2 : 3);
    return f.payload%frame==0 && uint64_t(f.payload/frame)*f.channels*4<=0x8000;
}
struct Command {
    bool end=false;
    unsigned slot=0;
    uint32_t au_address=0;
    std::array<uint8_t,24> info{};
    std::vector<uint8_t> data;
};
struct Output {unsigned slot; bool item_read=false; uint32_t size=0;};
struct Decoder {
    uint32_t handle=0,callback=0,user=0;
    Format fmt;
    std::mutex mutex;
    std::condition_variable changed;
    bool closing=false,accepting=false,active=false;
    std::array<bool,slot_count> used{};
    unsigned next_slot=0;
    uint64_t anchor=invalid_pts,samples=0,decoded=0;
    std::deque<Command> commands;
    std::deque<Output> output;
    std::thread worker;
    uint32_t address(unsigned slot) const {return handle+0x100+slot*slot_stride;}
    ~Decoder() {if(worker.joinable()) {{std::lock_guard l(mutex); closing=true;} changed.notify_all(); worker.join();}}
};
std::mutex directory_mutex;
std::map<uint32_t,std::shared_ptr<Decoder>> directory;
std::shared_ptr<Decoder> find(uint32_t h) {
    std::lock_guard l(directory_mutex); auto i=directory.find(h); return i==directory.end() ? nullptr : i->second;
}
void notify(const std::shared_ptr<Decoder>& d,uint32_t type,uint32_t data=0) {
    ps3_invoke_guest(d->callback,d->handle,type,uint64_t(int64_t(int32_t(data))),d->user,0,0,0,0);
}
void run(const std::shared_ptr<Decoder>& d) {
    for(;;) {
        Command c;
        {std::unique_lock l(d->mutex); d->changed.wait(l,[&]{return d->closing || !d->commands.empty();});
         if(d->closing) return;
         c=std::move(d->commands.front()); d->commands.pop_front();}
        if(c.end) {
            {std::lock_guard l(d->mutex); d->active=false;}
            notify(d,3); continue;
        }
        const Format& f=d->fmt;
        uint32_t word=f.depth==1 ? 2 : 3,source_channels=(f.channels+1)&~1u;
        uint32_t frames=uint32_t(c.data.size())/(word*source_channels);
        uint32_t size=frames*f.channels*4,item=d->address(c.slot),pcm=item+0x100;
        for(uint32_t n=0;n<frames;++n) for(uint32_t ch=0;ch<f.channels;++ch) {
            const uint8_t* s=c.data.data()+(n*source_channels+f.order[ch])*word;
            uint32_t raw=uint32_t(s[0])<<24|uint32_t(s[1])<<16;
            if(word==3) raw|=uint32_t(s[2])<<8;
            float value=float(std::bit_cast<int32_t>(raw))*(1.0f/2147483648.0f);
            w32(pcm+(n*f.channels+ch)*4,std::bit_cast<uint32_t>(value));
        }
        uint64_t pts=0; for(unsigned n=8;n<16;++n) pts=pts<<8|c.info[n];
        if(pts!=invalid_pts) {d->anchor=pts; d->samples=0;}
        if(d->anchor!=invalid_pts) pts=(d->anchor+d->samples*90000/f.hz)&0x1FFFFFFFFull;
        d->samples+=frames;
        std::memset(vm_base+item,0,0x100);
        w32(item,c.slot); w32(item+8,pcm); w32(item+12,size); w32(item+16,item+0x40);
        std::memcpy(vm_base+item+24,c.info.data(),24); w64(item+32,pts);
        w32(item+0x40,f.assignment); w32(item+0x44,f.rate_code); w32(item+0x48,size);
        {std::lock_guard l(d->mutex); if(d->closing) return; d->output.push_back({c.slot,false,size});}
        // Input bytes were copied on submission and are no longer referenced.
        // Publish metadata before either callback can wake a concurrent reader.
        notify(d,0,c.au_address); notify(d,1);
        if(++d->decoded<=3 || d->decoded%256==0)
            std::printf("[wkc2-adec] AU #%llu: %u samples/ch, %u PCM bytes pts=%llu\n",
                (unsigned long long)d->decoded,frames,size,(unsigned long long)pts);
    }
}
void query(ppu_context* c) {
    uint32_t type=arg(c,0),out=arg(c,1);
    if(!range(type,4) || r32(type)!=1 || !range(out,12)) {result(c,arg_error); return;}
    w32(out,work_size); w32(out+4,0x10000); w32(out+8,0); result(c,0);
}
void open(ppu_context* c) {
    uint32_t type=arg(c,0),res=arg(c,1),cb=arg(c,2),out=arg(c,3);
    if(!range(type,4) || r32(type)!=1 || !range(res,20) || !range(cb,8) || !r32(cb) ||
       !range(out,4) || r32(res)<work_size || !range(r32(res+4),r32(res)) || (r32(res+4)&127)) {result(c,arg_error); return;}
    auto d=std::make_shared<Decoder>(); d->handle=r32(res+4); d->callback=r32(cb); d->user=r32(cb+4);
    {std::lock_guard l(directory_mutex); if(directory.count(d->handle)) {result(c,busy); return;}
     directory.emplace(d->handle,d); d->worker=std::thread([d]{run(d);});}
    w32(out,d->handle); std::printf("[wkc2-adec] open LPCM handle=%08X\n",d->handle); result(c,0);
}
void close(ppu_context* c) {
    auto d=find(arg(c,0)); if(!d) {result(c,arg_error); return;}
    if(d->worker.get_id()==std::this_thread::get_id()) {result(c,seq); return;}
    {std::lock_guard l(d->mutex); d->closing=true;} d->changed.notify_all(); d->worker.join();
    {std::lock_guard l(directory_mutex); directory.erase(d->handle);} result(c,0);
}
void start(ppu_context* c) {
    auto d=find(arg(c,0)); Format f;
    if(!d || !format(arg(c,1),f)) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex);
    if(d->active || !d->output.empty() || !d->commands.empty()) {result(c,seq); return;}
    d->fmt=f; d->anchor=invalid_pts; d->samples=0; d->active=d->accepting=true;
    std::printf("[wkc2-adec] start: assignment=%u channels=%u rate=%u depth=%u payload=%u\n",f.assignment,f.channels,f.hz,f.depth,f.payload);
    result(c,0);
}
void end(ppu_context* c) {
    auto d=find(arg(c,0)); if(!d) {result(c,arg_error); return;}
    {std::lock_guard l(d->mutex); if(!d->accepting) {result(c,seq); return;}
     d->accepting=false; Command cmd; cmd.end=true; d->commands.push_back(std::move(cmd));}
    d->changed.notify_one(); result(c,0);
}
void decode(ppu_context* c) {
    auto d=find(arg(c,0)); uint32_t au=arg(c,1);
    if(!d || !range(au,24)) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex);
    if(!d->accepting) {result(c,seq); return;}
    uint32_t address=r32(au),size=r32(au+4);
    if(!range(address,size) || size!=d->fmt.payload) {result(c,arg_error); return;}
    unsigned slot=d->next_slot,n=0;
    for(;n<slot_count && d->used[slot];++n) slot=(slot+1)%slot_count;
    if(n==slot_count) {result(c,busy); return;}
    Command cmd; cmd.slot=slot; cmd.au_address=au;
    std::memcpy(cmd.info.data(),vm_base+au,24); cmd.data.assign(vm_base+address,vm_base+address+size);
    d->used[slot]=true; d->next_slot=(slot+1)%slot_count; d->commands.push_back(std::move(cmd));
    d->changed.notify_one(); result(c,0);
}
void get_item(ppu_context* c) {
    auto d=find(arg(c,0)); uint32_t out=arg(c,1);
    if(!d || !range(out,4)) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex);
    for(auto& o:d->output) if(!o.item_read) {o.item_read=true; w32(out,d->address(o.slot)); result(c,0); return;}
    result(c,empty);
}
void get_pcm(ppu_context* c) {
    auto d=find(arg(c,0)); uint32_t out=arg(c,1);
    if(!d) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex); if(d->output.empty()) {result(c,empty); return;}
    const auto& o=d->output.front();
    if(out && !range(out,o.size)) {result(c,arg_error); return;}
    if(out) std::memcpy(vm_base+out,vm_base+d->address(o.slot)+0x100,o.size);
    d->used[o.slot]=false; d->output.pop_front(); result(c,0);
}
}
extern "C" void wkc2_shutdown_adec() {
    std::vector<std::shared_ptr<Decoder>> decoders;
    {std::lock_guard l(directory_mutex); for(auto& [h,d]:directory) decoders.push_back(d); directory.clear();}
    for(auto& d:decoders) {{std::lock_guard l(d->mutex); d->closing=true;} d->changed.notify_all();}
    for(auto& d:decoders) if(d->worker.joinable()) d->worker.join();
}
extern "C" void wkc2_dump_adec() {
    std::vector<std::shared_ptr<Decoder>> all;
    {std::lock_guard l(directory_mutex); for(auto& [h,d]:directory) all.push_back(d);}
    for(auto& d:all) {
        std::lock_guard l(d->mutex); unsigned used=0,read=0;
        for(auto u:d->used) used+=u;
        for(auto& o:d->output) read+=o.item_read;
        std::fprintf(stderr,"[adec-state] handle=%08X active=%d accepting=%d commands=%zu output=%zu items_read=%u used=%u\n",d->handle,d->active,d->accepting,d->commands.size(),d->output.size(),read,used);
    }
}
extern "C" void wkc2_register_adec() {
    struct Entry {const char* name; void(*handler)(ppu_context*);};
    for(const auto& e: {Entry{"cellAdecQueryAttr",query},Entry{"cellAdecOpen",open},Entry{"cellAdecClose",close},
        Entry{"cellAdecStartSeq",start},Entry{"cellAdecEndSeq",end},Entry{"cellAdecDecodeAu",decode},
        Entry{"cellAdecGetPcm",get_pcm},Entry{"cellAdecGetPcmItem",get_item}})
        ps3_hle_register_ctx(ps3_compute_nid(e.name),e.name,e.handler);
}
