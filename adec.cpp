/* Guest cellAdec bridge for PAMF LPCM and AC-3. Each accepted AU owns an output
 * slot until GetPcm consumes it. Codec records and floating samples are encoded
 * in guest byte order; callbacks execute through guest procedure descriptors.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include "ps3emu/guest_call.h"
#include "ffmpeg_runtime.h"
#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <cmath>
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
constexpr uint32_t work_size=0xD0000,slot_stride=0xC100,slot_count=16;
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
    bool integer=false;
    float scale=1.0f,compression=1.0f;
    unsigned mode=0,lfe=0,compression_mode=2;
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
    return f.payload%frame==0 && uint64_t(f.payload/frame)*f.channels*4<=0xC000;
}
bool ac3_format(uint32_t a,Format& f) {
    if(!range(a,48)) return false;
    unsigned word=vm_base[a],mode=vm_base[a+1],lfe=vm_base[a+2];
    float cut=std::bit_cast<float>(r32(a+4)),boost=std::bit_cast<float>(r32(a+8));
    f.integer=word==0; f.mode=mode; f.lfe=lfe; f.compression_mode=vm_base[a+12];
    f.channels=vm_base[a+13]; f.scale=std::bit_cast<float>(r32(a+20)); f.compression=cut;
    // The title uses mono, stereo or full 5.1 with automatic stereo handling.
    // Reject unsupported custom DRC/stereo/karaoke modes explicitly.
    if(word>1 || (mode!=1 && mode!=2 && mode!=7) || lfe>1 ||
       f.channels!=(mode==7?5+lfe:mode) || (mode!=7 && lfe) ||
       !std::isfinite(cut) || cut<0 || cut>6 || cut!=boost ||
       !std::isfinite(f.scale) || f.scale<0 || f.scale>1 ||
       (f.compression_mode!=2 && f.compression_mode!=3) ||
       vm_base[a+14]!=0 || vm_base[a+15]!=0 || vm_base[a+16]!=3) return false;
    std::array<bool,6> used{};
    for(unsigned n=0;n<f.channels;++n) {
        unsigned channel=r32(a+24+n*4);
        if(channel>=6 || used[channel]) return false;
        used[channel]=true; f.order[n]=channel;
    }
    f.hz=48000; return true;
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
    uint32_t handle=0,callback=0,user=0,codec=1;
    Format fmt;
    std::mutex mutex;
    std::condition_variable changed;
    bool closing=false,accepting=false,active=false,failed=false;
    std::array<bool,slot_count> used{};
    unsigned next_slot=0;
    uint64_t anchor=invalid_pts,samples=0,decoded=0;
    std::deque<Command> commands;
    std::deque<Output> output;
    std::thread worker;
    const PS3FFmpeg* ff=nullptr;
    AVCodecContext* context=nullptr;
    AVPacket* packet=nullptr;
    AVFrame* frame=nullptr;
    uint32_t address(unsigned slot) const {return handle+0x100+slot*slot_stride;}
    ~Decoder() {
        if(worker.joinable()) {{std::lock_guard l(mutex); closing=true;} changed.notify_all(); worker.join();}
        if(context) ff->avcodec_free_context(&context);
        if(packet) ff->av_packet_free(&packet);
        if(frame) ff->av_frame_free(&frame);
    }
};
std::mutex directory_mutex;
std::map<uint32_t,std::shared_ptr<Decoder>> directory;
std::shared_ptr<Decoder> find(uint32_t h) {
    std::lock_guard l(directory_mutex); auto i=directory.find(h); return i==directory.end() ? nullptr : i->second;
}
void notify(const std::shared_ptr<Decoder>& d,uint32_t type,uint32_t data=0) {
    ps3_invoke_guest(d->callback,d->handle,type,uint64_t(int64_t(int32_t(data))),d->user,0,0,0,0);
}
bool open_ac3(Decoder& d,const Format& f) {
    if(d.context) d.ff->avcodec_free_context(&d.context);
    const AVCodec* codec=d.ff->avcodec_find_decoder(AV_CODEC_ID_AC3);
    if(!codec || !(d.context=d.ff->avcodec_alloc_context3(codec))) return false;
    auto option=[&](const char* key,const char* value) {return d.ff->av_opt_set(d.context->priv_data,key,value,0)>=0;};
    char drc[32]; std::snprintf(drc,sizeof(drc),"%.9g",double(f.compression));
    if(!option("cons_noisegen","1") || !option("drc_scale",drc) ||
       !option("heavy_compr",f.compression_mode==3?"1":"0") || !option("target_level","0") ||
       (f.mode!=7 && !option("downmix",f.mode==1?"mono":"stereo"))) return false;
    return d.ff->avcodec_open2(d.context,codec,nullptr)>=0;
}
unsigned ac3_index(const AVFrame& frame,unsigned label) {
    // cellAdec labels L,C,R,SL,SR,LFE. FFmpeg native masks enumerate L,R,C,LFE,...
    static const unsigned bits[]={AV_CHAN_FRONT_LEFT,AV_CHAN_FRONT_CENTER,AV_CHAN_FRONT_RIGHT,AV_CHAN_SIDE_LEFT,AV_CHAN_SIDE_RIGHT,AV_CHAN_LOW_FREQUENCY};
    if(frame.ch_layout.order!=AV_CHANNEL_ORDER_NATIVE) return 8;
    unsigned bit=bits[label]; uint64_t mask=frame.ch_layout.u.mask;
    if(!(mask&(uint64_t(1)<<bit))) {
        if(label==3) bit=AV_CHAN_BACK_LEFT;
        else if(label==4) bit=AV_CHAN_BACK_RIGHT;
    }
    if(!(mask&(uint64_t(1)<<bit))) return 8;
    return std::popcount(mask&((uint64_t(1)<<bit)-1));
}
bool decode_ac3(Decoder& d,Command& c,uint32_t pcm,uint32_t& frames,uint32_t& size,uint32_t& hz) {
    size_t input_size=c.data.size(); c.data.resize(input_size+AV_INPUT_BUFFER_PADDING_SIZE,0);
    d.packet->data=c.data.data();d.packet->size=int(input_size);d.packet->pts=AV_NOPTS_VALUE;
    int rc=d.ff->avcodec_send_packet(d.context,d.packet);d.packet->data=nullptr;d.packet->size=0;
    if(rc<0) return false;
    rc=d.ff->avcodec_receive_frame(d.context,d.frame);if(rc<0)return false;
    auto& frame=*d.frame; const auto& f=d.fmt;
    bool valid=frame.format==AV_SAMPLE_FMT_FLTP && frame.nb_samples==1536 && frame.ch_layout.nb_channels==f.channels && frame.sample_rate>0;
    frames=uint32_t(frame.nb_samples);hz=uint32_t(frame.sample_rate);size=frames*f.channels*(f.integer?2:4);
    if(size>0xC000) valid=false;
    std::array<unsigned,6> indices{};
    for(unsigned ch=0;ch<f.channels && valid;++ch) {indices[ch]=ac3_index(frame,f.order[ch]);if(indices[ch]>=f.channels)valid=false;}
    if(valid) for(unsigned n=0;n<frames;++n)for(unsigned ch=0;ch<f.channels;++ch) {
        float value;std::memcpy(&value,frame.extended_data[indices[ch]]+n*4,4);value*=f.scale;
        if(!std::isfinite(value)) {valid=false;break;}
        if(f.integer) {
            int v=int(std::lrint(std::clamp(value,-1.0f,1.0f)*32768.0f));v=std::clamp(v,-32768,32767);
            vm_base[pcm+(n*f.channels+ch)*2]=uint8_t(uint16_t(v)>>8);vm_base[pcm+(n*f.channels+ch)*2+1]=uint8_t(v);
        } else w32(pcm+(n*f.channels+ch)*4,std::bit_cast<uint32_t>(value));
    }
    d.ff->av_frame_unref(d.frame);
    // A single complete AC-3 access unit produces exactly one audio frame.
    if(d.ff->avcodec_receive_frame(d.context,d.frame)>=0) {d.ff->av_frame_unref(d.frame);valid=false;}
    return valid;
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
        uint32_t hz=f.hz;
        if(d->codec==2) {
            if(!decode_ac3(*d,c,pcm,frames,size,hz)) {
                {std::lock_guard l(d->mutex);d->used[c.slot]=false;d->failed=true;}
                notify(d,0,c.au_address);notify(d,2,fatal);continue;
            }
        } else for(uint32_t n=0;n<frames;++n) for(uint32_t ch=0;ch<f.channels;++ch) {
            const uint8_t* s=c.data.data()+(n*source_channels+f.order[ch])*word;
            uint32_t raw=uint32_t(s[0])<<24|uint32_t(s[1])<<16;
            if(word==3) raw|=uint32_t(s[2])<<8;
            float value=float(std::bit_cast<int32_t>(raw))*(1.0f/2147483648.0f);
            w32(pcm+(n*f.channels+ch)*4,std::bit_cast<uint32_t>(value));
        }
        uint64_t pts=0; for(unsigned n=8;n<16;++n) pts=pts<<8|c.info[n];
        if(pts!=invalid_pts) {d->anchor=pts; d->samples=0;}
        if(d->anchor!=invalid_pts) pts=(d->anchor+d->samples*90000/hz)&0x1FFFFFFFFull;
        d->samples+=frames;
        std::memset(vm_base+item,0,0x100);
        w32(item,c.slot); w32(item+8,pcm); w32(item+12,size); w32(item+16,item+0x40);
        std::memcpy(vm_base+item+24,c.info.data(),24); w64(item+32,pts);
        if(d->codec==1) {w32(item+0x40,f.assignment); w32(item+0x44,f.rate_code); w32(item+0x48,size);}
        else {
            uint32_t bsi=item+0x40;
            w32(bsi,2);w32(bsi+12,uint32_t(d->decoded+1));w32(bsi+16,uint32_t(d->context->bit_rate));
            w32(bsi+20,size);w32(bsi+24,uint32_t(c.data.size()-AV_INPUT_BUFFER_PADDING_SIZE)*8);
            vm_base[bsi+33]=1;vm_base[bsi+34]=uint8_t(f.channels);vm_base[bsi+35]=c.data[4]>>6;
            vm_base[bsi+36]=f.integer?0:1;vm_base[bsi+37]=uint8_t(f.mode);vm_base[bsi+38]=f.channels==1;
            vm_base[bsi+48]=0;vm_base[bsi+49]=c.data[4]&63;vm_base[bsi+50]=c.data[6]>>5;
            vm_base[bsi+58]=c.data[5]&7;vm_base[bsi+59]=c.data[5]>>3;
        }
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
    if(!range(type,4) || (r32(type)!=1 && r32(type)!=2) || !range(out,12)) {result(c,arg_error); return;}
    w32(out,work_size); w32(out+4,0x10000); w32(out+8,0); result(c,0);
}
void open(ppu_context* c) {
    uint32_t type=arg(c,0),res=arg(c,1),cb=arg(c,2),out=arg(c,3);
    if(!range(type,4) || (r32(type)!=1 && r32(type)!=2) || !range(res,20) || !range(cb,8) || !r32(cb) ||
       !range(out,4) || r32(res)<work_size || !range(r32(res+4),r32(res)) || (r32(res+4)&127)) {result(c,arg_error); return;}
    auto d=std::make_shared<Decoder>(); d->handle=r32(res+4); d->callback=r32(cb); d->user=r32(cb+4);d->codec=r32(type);
    if(d->codec==2) {
        if(!(d->ff=ps3_ffmpeg_get()) || !(d->packet=d->ff->av_packet_alloc()) || !(d->frame=d->ff->av_frame_alloc())) {result(c,fatal);return;}
    }
    {std::lock_guard l(directory_mutex); if(directory.count(d->handle)) {result(c,busy); return;}
     directory.emplace(d->handle,d); d->worker=std::thread([d]{run(d);});}
    w32(out,d->handle); std::printf("[wkc2-adec] open codec=%u handle=%08X\n",d->codec,d->handle); result(c,0);
}
void close(ppu_context* c) {
    auto d=find(arg(c,0)); if(!d) {result(c,arg_error); return;}
    if(d->worker.get_id()==std::this_thread::get_id()) {result(c,seq); return;}
    {std::lock_guard l(d->mutex); d->closing=true;} d->changed.notify_all(); d->worker.join();
    {std::lock_guard l(directory_mutex); directory.erase(d->handle);} result(c,0);
}
void start(ppu_context* c) {
    auto d=find(arg(c,0)); Format f;
    if(!d || !(d->codec==1 ? format(arg(c,1),f) : ac3_format(arg(c,1),f))) {result(c,arg_error); return;}
    std::lock_guard l(d->mutex);
    if(d->active || !d->output.empty() || !d->commands.empty()) {result(c,seq); return;}
    if(d->codec==2 && !open_ac3(*d,f)) {result(c,fatal);return;}
    d->fmt=f; d->anchor=invalid_pts; d->samples=0; d->active=d->accepting=true;d->failed=false;
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
    if(!d->accepting || d->failed) {result(c,seq); return;}
    uint32_t address=r32(au),size=r32(au+4);
    if(!range(address,size) || (d->codec==1 ? size!=d->fmt.payload : size<7 || size>3840 || vm_base[address]!=0x0B || vm_base[address+1]!=0x77 || (vm_base[address+5]>>3)>10)) {result(c,arg_error); return;}
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
