/* Asynchronous guest video decoder. Input command capacity and decoded output
 * capacity are independent. A full output queue delays input completion until
 * the consumer makes space; it never creates a busy result with no future work
 * capable of notifying the guest. Pixel decoding uses the SDK FFmpeg backend.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/guest_call.h"
#include "vdec_ffmpeg.h"
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
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
constexpr uint32_t arg_error=0x80610101,seq_error=0x80610102,busy=0x80610103,empty=0x80610104,fatal=0x80610180;
constexpr uint32_t slot_count=32,stride=0x400,work_size=0x10000;
constexpr uint64_t invalid_pts=~uint64_t(0);
bool range(uint32_t a,uint64_t n) {return a && n<=(uint64_t(1)<<32)-a;}
uint32_t r32(uint32_t a) {return uint32_t(vm_base[a])<<24|uint32_t(vm_base[a+1])<<16|uint32_t(vm_base[a+2])<<8|vm_base[a+3];}
uint64_t r64(uint32_t a) {return uint64_t(r32(a))<<32|r32(a+4);}
void w32(uint32_t a,uint32_t v) {for(unsigned i=0;i<4;++i) vm_base[a+i]=uint8_t(v>>(24-i*8));}
void w64(uint32_t a,uint64_t v) {w32(a,uint32_t(v>>32));w32(a+4,uint32_t(v));}
void w16(uint32_t a,uint32_t v) {vm_base[a]=uint8_t(v>>8);vm_base[a+1]=uint8_t(v);}
uint32_t arg(ppu_context* c,unsigned n) {return uint32_t(c->gpr[3+n]);}
void result(ppu_context* c,uint32_t v) {c->gpr[3]=uint64_t(int64_t(int32_t(v)));}
struct Au {uint32_t address=0,size=0; uint64_t pts=invalid_pts,dts=invalid_pts,user=0;};
struct Command {bool end=false; uint32_t token=0; Au info; std::vector<uint8_t> data;};
struct Picture {
    VdecFfPicture decoded{};
    Au au;
    uint32_t slot=0;
    uint64_t pts=invalid_pts;
    ~Picture() {std::free(decoded.yuv);}
};
struct Decoder {
    uint32_t handle=0,codec=0,callback=0,user=0;
    std::mutex mutex;
    std::condition_variable changed;
    bool closing=false,active=false,accepting=false,failed=false;
    unsigned pending=0,next_slot=0;
    uint32_t token=0;
    uint64_t next_pts=0,remainder=0,pictures=0;
    bool clock_valid=false;
    std::array<Au,64> aus;
    std::array<bool,slot_count> used{};
    std::deque<Command> commands;
    std::deque<std::shared_ptr<Picture>> output;
    VdecFf* ff=nullptr;
    std::thread worker;
    uint32_t address(unsigned slot) const {return handle+0x100+slot*stride;}
    ~Decoder() {if(worker.joinable()) {{std::lock_guard l(mutex);closing=true;} changed.notify_all();worker.join();} if(ff)vdec_ff_close(ff);}
};
std::mutex directory_mutex;
std::map<uint32_t,std::shared_ptr<Decoder>> directory;
std::shared_ptr<Decoder> find(uint32_t h) {std::lock_guard l(directory_mutex);auto i=directory.find(h);return i==directory.end()?nullptr:i->second;}
void notify(Decoder& d,uint32_t type,uint32_t data=0) {ps3_invoke_guest(d.callback,d.handle,type,uint64_t(int64_t(int32_t(data))),d.user,0,0,0,0);}
void emit(void* context,const VdecFfPicture* decoded) {
    auto& d=*static_cast<Decoder*>(context);
    auto p=std::make_shared<Picture>();p->decoded=*decoded;p->au=d.aus[decoded->token%64];p->pts=p->au.pts;
    if(p->pts!=invalid_pts) {d.clock_valid=true;d.next_pts=p->pts;d.remainder=0;}
    if(d.clock_valid && decoded->rate_num && decoded->rate_den) {
        p->pts=d.next_pts;
        uint64_t divisor=uint64_t(decoded->rate_num)*2;
        uint64_t ticks=uint64_t(90000)*decoded->rate_den*(2+decoded->repeat_pict)+d.remainder;
        d.next_pts=(d.next_pts+ticks/divisor)&0x1FFFFFFFFull;d.remainder=ticks%divisor;
    }
    std::unique_lock l(d.mutex);
    d.changed.wait(l,[&]{return d.closing || d.output.size()<slot_count;});
    if(d.closing)return;
    while(d.used[d.next_slot])d.next_slot=(d.next_slot+1)%slot_count;
    p->slot=d.next_slot;d.used[p->slot]=true;d.next_slot=(d.next_slot+1)%slot_count;
    d.output.push_back(p);++d.pictures;l.unlock();
    notify(d,1);
}
void run(const std::shared_ptr<Decoder>& d) {
    for(;;) {
        Command c;
        {std::unique_lock l(d->mutex);d->changed.wait(l,[&]{return d->closing || (!d->commands.empty() && d->output.size()<16);});
         if(d->closing)return;c=std::move(d->commands.front());d->commands.pop_front();}
        if(c.end) {
            vdec_ff_drain(d->ff,emit,d.get());
            {std::lock_guard l(d->mutex);d->active=false;}
            notify(*d,2);continue;
        }
        d->aus[c.token%64]=c.info;
        int status=vdec_ff_decode(d->ff,c.data.data(),uint32_t(c.data.size()),c.token,emit,d.get());
        {std::lock_guard l(d->mutex);--d->pending;if(status<0)d->failed=true;}
        notify(*d,0,status<0?fatal:0);
        if(status<0)notify(*d,3,fatal);
    }
}
void query(ppu_context* c) {
    uint32_t type=arg(c,0),out=arg(c,1);
    if(!range(type,8)||r32(type)>1||!range(out,16)){result(c,arg_error);return;}
    std::memset(vm_base+out,0,16);w32(out,work_size);vm_base[out+4]=4;w32(out+8,0x10000);result(c,0);
}
void open(ppu_context* c) {
    uint32_t type=arg(c,0),res=arg(c,1),cb=arg(c,2),out=arg(c,3);
    // CellVdecResource begins memAddr,memSize,ppuPriority,ppuStackSize.
    if(!range(type,8)||r32(type)>1||!range(res,24)||r32(res+4)<work_size||!range(r32(res),r32(res+4))||
       !range(cb,8)||!r32(cb)||!range(out,4)){result(c,arg_error);return;}
    auto d=std::make_shared<Decoder>();d->handle=r32(res);d->codec=r32(type);d->callback=r32(cb);d->user=r32(cb+4);
    d->ff=vdec_ff_open(d->codec);if(!d->ff){result(c,fatal);return;}
    {std::lock_guard l(directory_mutex);if(directory.count(d->handle)){result(c,busy);return;}
     directory.emplace(d->handle,d);d->worker=std::thread([d]{run(d);});}
    w32(out,d->handle);std::printf("[wkc2-vdec] open handle=%08X codec=%u\n",d->handle,d->codec);result(c,0);
}
void close(ppu_context* c) {
    auto d=find(arg(c,0));if(!d){result(c,arg_error);return;}
    if(d->worker.get_id()==std::this_thread::get_id()){result(c,seq_error);return;}
    {std::lock_guard l(d->mutex);d->closing=true;}d->changed.notify_all();d->worker.join();
    {std::lock_guard l(directory_mutex);directory.erase(d->handle);}result(c,0);
}
void start(ppu_context* c) {
    auto d=find(arg(c,0));if(!d){result(c,arg_error);return;}
    std::lock_guard l(d->mutex);
    if(d->active||!d->output.empty()||d->pending||!d->commands.empty()){result(c,seq_error);return;}
    vdec_ff_reset(d->ff);d->clock_valid=false;d->failed=false;d->remainder=0;d->active=d->accepting=true;result(c,0);
}
void end(ppu_context* c) {
    auto d=find(arg(c,0));if(!d){result(c,arg_error);return;}
    {std::lock_guard l(d->mutex);if(!d->accepting){result(c,seq_error);return;}
     d->accepting=false;Command cmd;cmd.end=true;d->commands.push_back(std::move(cmd));}d->changed.notify_one();result(c,0);
}
void decode(ppu_context* c) {
    auto d=find(arg(c,0));uint32_t au=arg(c,2);
    if(!d||arg(c,1)!=0||!range(au,32)){result(c,arg_error);return;}
    std::lock_guard l(d->mutex);
    if(!d->accepting||d->failed){result(c,seq_error);return;}
    if(d->pending>=4){result(c,busy);return;}
    Command cmd;cmd.info={r32(au),r32(au+4),r64(au+8),r64(au+16),r64(au+24)};
    if(!cmd.info.size||cmd.info.size>16*1024*1024||!range(cmd.info.address,cmd.info.size)){result(c,arg_error);return;}
    cmd.data.assign(vm_base+cmd.info.address,vm_base+cmd.info.address+cmd.info.size);cmd.token=++d->token;
    d->commands.push_back(std::move(cmd));++d->pending;d->changed.notify_one();result(c,0);
}
void metadata(Decoder& d,const Picture& p) {
    auto& f=p.decoded;uint32_t item=d.address(p.slot),info=item+0x80;
    std::memset(vm_base+item,0,stride);w32(item,d.codec);w32(item+4,p.au.address);w32(item+8,p.au.size);vm_base[item+12]=1;
    w64(item+16,p.pts);w64(item+24,invalid_pts);w64(item+32,p.au.dts);w64(item+40,invalid_pts);w64(item+48,p.au.user);
    w32(item+72,info);w16(info,f.width);w16(info+2,f.height);
    if(d.codec==0) {
        constexpr uint32_t num[]={0,24000,24,25,30000,30,50,60000,60},den[]={1,1001,1,1,1001,1,1,1001,1};
        for(unsigned n=1;n<=8;++n)if(uint64_t(f.rate_num)*den[n]==uint64_t(num[n])*f.rate_den)vm_base[info+5]=uint8_t(n);
        vm_base[info+6]=!f.interlaced;vm_base[info+8]=5;vm_base[info+9]=f.primaries!=2||f.transfer!=2||f.matrix!=2;
        vm_base[info+10]=uint8_t(f.primaries);vm_base[info+11]=uint8_t(f.transfer);vm_base[info+12]=uint8_t(f.matrix);
        vm_base[info+18]=uint8_t(f.pict_type);vm_base[info+20]=3;vm_base[info+22]=uint8_t(f.top_first);
        vm_base[info+23]=f.repeat_pict!=0;vm_base[info+24]=!f.interlaced;
    } else {vm_base[info+4]=uint8_t(f.pict_type>=1&&f.pict_type<=3?f.pict_type-1:3);vm_base[info+5]=3;vm_base[info+6]=uint8_t(f.key);}
}
void get_item(ppu_context* c) {
    auto d=find(arg(c,0));uint32_t out=arg(c,1);if(!d||!range(out,4)){result(c,arg_error);return;}
    std::lock_guard l(d->mutex);if(d->output.empty()){result(c,empty);return;}
    auto& p=*d->output.front();metadata(*d,p);w32(out,d->address(p.slot));result(c,0);
}
void get_picture(ppu_context* c) {
    auto d=find(arg(c,0));uint32_t format=arg(c,1),out=arg(c,2);
    if(!d||!range(format,9)||r32(format)>3||r32(format+4)>1){result(c,arg_error);return;}
    std::shared_ptr<Picture> p;
    {std::lock_guard l(d->mutex);if(d->output.empty()){result(c,empty);return;}p=d->output.front();
     uint64_t size=uint64_t(p->decoded.width)*p->decoded.height;
     size=r32(format)==3?size*3/2:size*(r32(format)==2?2:4);
     if(out&&!range(out,size)){result(c,arg_error);return;}
     d->output.pop_front();d->used[p->slot]=false;}
    d->changed.notify_one();
    if(out)vdec_store_picture(out,p->decoded.yuv,p->decoded.width,p->decoded.height,r32(format),r32(format+4),vm_base[format+8]);
    result(c,0);
}
}
extern "C" void wkc2_shutdown_vdec() {
    std::vector<std::shared_ptr<Decoder>> all;
    {std::lock_guard l(directory_mutex);for(auto& [h,d]:directory)all.push_back(d);directory.clear();}
    for(auto& d:all){{std::lock_guard l(d->mutex);d->closing=true;}d->changed.notify_all();}
    for(auto& d:all)if(d->worker.joinable())d->worker.join();
}
extern "C" void wkc2_dump_vdec() {
    std::vector<std::shared_ptr<Decoder>> all;
    {std::lock_guard l(directory_mutex);for(auto& [h,d]:directory)all.push_back(d);}
    for(auto& d:all){std::lock_guard l(d->mutex);std::fprintf(stderr,"[vdec-state] handle=%08X pending=%u commands=%zu output=%zu pictures=%llu failed=%d\n",d->handle,d->pending,d->commands.size(),d->output.size(),(unsigned long long)d->pictures,d->failed);}
}
extern "C" void wkc2_register_vdec() {
    struct Entry{const char* name;void(*handler)(ppu_context*);};
    for(auto& e:{Entry{"cellVdecQueryAttr",query},Entry{"cellVdecOpen",open},Entry{"cellVdecClose",close},
        Entry{"cellVdecStartSeq",start},Entry{"cellVdecEndSeq",end},Entry{"cellVdecDecodeAu",decode},
        Entry{"cellVdecGetPicItem",get_item},Entry{"cellVdecGetPicture",get_picture}})
        ps3_hle_register_ctx(ps3_compute_nid(e.name),e.name,e.handler);
}
