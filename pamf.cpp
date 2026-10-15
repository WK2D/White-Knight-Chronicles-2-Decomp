/* PAMF guest ABI used by WKC2. All persistent reader state is held in the
 * 128-byte guest object, so guest memcpy and independent readers remain valid.
 * Header offsets and public ABI were checked against real disc headers and
 * RPCS3's cellPamf interface definitions; no emulator implementation is included.
 */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t* vm_base;
void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
}

namespace {
constexpr uint32_t missing=0x80610501, invalid_header=0x80610502;
constexpr uint32_t invalid_arg=0x80610503, unknown_type=0x80610504;
constexpr uint32_t unsupported_version=0x80610505;
constexpr uint32_t descriptor_size=48, stream_table=0x88, reader_size=128;

bool range(uint32_t a, uint64_t n) { return a && n <= (uint64_t(1)<<32)-a; }
uint16_t r16(uint32_t a) { return uint16_t(vm_base[a])<<8 | vm_base[a+1]; }
uint32_t r32(uint32_t a) { return uint32_t(r16(a))<<16 | r16(a+2); }
void w16(uint32_t a,uint16_t v) { vm_base[a]=uint8_t(v>>8); vm_base[a+1]=uint8_t(v); }
void w32(uint32_t a,uint32_t v) { w16(a,uint16_t(v>>16)); w16(a+2,uint16_t(v)); }
void w64(uint32_t a,uint64_t v) { w32(a,uint32_t(v>>32)); w32(a+4,uint32_t(v)); }
uint32_t arg(ppu_context* c,unsigned i) { return uint32_t(c->gpr[3+i]); }
void result(ppu_context* c,uint32_t v) { c->gpr[3]=uint64_t(int64_t(int32_t(v))); }

struct Header {
    uint32_t base=0, count=0, version=0;
    uint64_t header_bytes=0, data_bytes=0;
};

uint32_t sizes(uint32_t a,uint64_t file_bytes,Header& h) {
    if (!range(a,16)) return invalid_arg;
    if (r32(a)!=0x50414D46) return unknown_type;
    h.version=r32(a+4);
    if (h.version!=0x30303430 && h.version!=0x30303431) return unsupported_version;
    h.header_bytes=uint64_t(r32(a+8))*2048;
    h.data_bytes=uint64_t(r32(a+12))*2048;
    if (h.header_bytes<stream_table || !range(a,h.header_bytes)) return invalid_header;
    if (file_bytes && file_bytes!=h.header_bytes+h.data_bytes) return invalid_header;
    h.base=a;
    return 0;
}

uint32_t parse(uint32_t a,uint64_t file_bytes,Header& h) {
    uint32_t rc=sizes(a,file_bytes,h);
    if (rc) return rc;
    // PAMF has one grouping period and one group. Reject incompatible topology.
    h.count=vm_base[a+0x87];
    if (vm_base[a+0x6F]!=1 || vm_base[a+0x81]!=1 || !h.count ||
        h.count!=r32(a+0x6A) || stream_table+uint64_t(h.count)*descriptor_size>h.header_bytes)
        return invalid_header;
    uint64_t sequence_end=0x54ull+r32(a+0x50);
    if (sequence_end<stream_table+uint64_t(h.count)*descriptor_size || sequence_end>h.header_bytes)
        return invalid_header;
    return 0;
}

uint32_t read_reader(uint32_t reader,Header& h,uint32_t& index) {
    if (!range(reader,reader_size)) return invalid_arg;
    uint32_t rc=parse(r32(reader+36),0,h);
    if (rc) return rc;
    index=r32(reader+32);
    return index<h.count ? 0 : invalid_arg;
}

int stream_type(uint32_t d) {
    switch(vm_base[d]) {
    case 0x1B:return 0; case 0x02:return 1; case 0xDC:return 2;
    case 0x80:return 3; case 0x81:return 4; case 0xDD:return 5;
    default:return -1;
    }
}
bool same_type(int actual,uint32_t requested) {
    if (requested==20) return actual==0 || actual==1;
    if (requested==21) return actual>=2 && actual<=4;
    return actual>=0 && uint32_t(actual)==requested;
}
uint8_t channel(uint32_t d) {
    return (vm_base[d+4]==0xBD ? vm_base[d+5] : vm_base[d+4]) & 15;
}
void select(uint32_t reader,const Header& h,uint32_t index) {
    w32(reader+32,index); w32(reader+52,h.base+stream_table+index*descriptor_size);
}

void initialize(ppu_context* c) {
    uint32_t reader=arg(c,0),base=arg(c,1),attributes=arg(c,3);
    if (!range(reader,reader_size)) { result(c,invalid_arg); return; }
    Header h;
    uint32_t rc=parse(base,c->gpr[5],h); // file size is one full 64-bit PPC argument.
    if (rc) { result(c,rc); return; }
    std::memset(vm_base+reader,0,reader_size);
    w64(reader,h.header_bytes); w64(reader+8,h.data_bytes); w32(reader+16,attributes);
    w16(reader+22,uint16_t(h.version==0x30303431 ? 41 : 40));
    w32(reader+36,base); w32(reader+40,base+0x50);
    w32(reader+44,base+0x70); w32(reader+48,base+0x82);
    select(reader,h,0);
    std::printf("[wkc2-pamf] reader initialized: %u streams, header=%llu data=%llu\n",
        h.count,(unsigned long long)h.header_bytes,(unsigned long long)h.data_bytes);
    result(c,0);
}

void get_header_size(ppu_context* c) {
    Header h;
    uint32_t out=arg(c,2),rc=sizes(arg(c,0),c->gpr[4],h);
    if (!rc && !range(out,8)) rc=invalid_arg;
    if (!rc) w64(out,h.header_bytes);
    result(c,rc);
}
void get_offset_size(ppu_context* c) {
    Header h;
    uint32_t off=arg(c,2),size=arg(c,3),rc=sizes(arg(c,0),c->gpr[4],h);
    if (!rc && ((off && !range(off,8)) || (size && !range(size,8)))) rc=invalid_arg;
    if (!rc) { if (off) w64(off,h.header_bytes); if (size) w64(size,h.data_bytes); }
    result(c,rc);
}
void timestamp(ppu_context* c) {
    Header h; uint32_t index=0,out=arg(c,1),rc=read_reader(arg(c,0),h,index);
    if (!rc && !range(out,8)) rc=invalid_arg;
    if (!rc) { w32(out,r16(h.base+0x56)); w32(out+4,r32(h.base+0x58)); }
    result(c,rc);
}
void count_type(ppu_context* c) {
    Header h; uint32_t index=0,count=0,rc=read_reader(arg(c,0),h,index);
    if (!rc) for (uint32_t i=0;i<h.count;++i)
        count+=same_type(stream_type(h.base+stream_table+i*descriptor_size),arg(c,1));
    result(c,rc ? rc : count);
}
void set_type_index(ppu_context* c) {
    Header h; uint32_t index=0,reader=arg(c,0),rc=read_reader(reader,h,index);
    if (rc) { result(c,rc); return; }
    uint32_t ordinal=0;
    for (uint32_t i=0;i<h.count;++i) if (same_type(stream_type(h.base+stream_table+i*descriptor_size),arg(c,1))) {
        if (ordinal++==arg(c,2)) { select(reader,h,i); result(c,i); return; }
    }
    result(c,missing);
}
void type_channel(ppu_context* c) {
    Header h; uint32_t index=0,rc=read_reader(arg(c,0),h,index);
    if (!rc) {
        uint32_t d=h.base+stream_table+index*descriptor_size;
        int type=stream_type(d);
        if (type<0) rc=unknown_type;
        else { if (arg(c,1)) vm_base[arg(c,1)]=uint8_t(type); if (arg(c,2)) vm_base[arg(c,2)]=channel(d); }
    }
    result(c,rc);
}
void filter_id(ppu_context* c) {
    uint32_t type=arg(c,0),ch=arg(c,1),out=arg(c,2);
    if (!range(out,16) || ch>=16 || type>5) { result(c,invalid_arg); return; }
    uint32_t major=0xBD,minor=0;
    if (type<=1) major=0xE0+ch;
    else if (type==2) minor=ch;
    else if (type==3) minor=0x40+ch;
    else if (type==4) minor=0x30+ch;
    else minor=0x20+ch;
    w32(out,major); w32(out+4,minor); w32(out+8,type==0); w32(out+12,0);
    result(c,0);
}

void stream_info(ppu_context* c) {
    Header h; uint32_t index=0,rc=read_reader(arg(c,0),h,index);
    if (rc) { result(c,rc); return; }
    uint32_t d=h.base+stream_table+index*descriptor_size,s=d+16,out=arg(c,1);
    int type=stream_type(d);
    uint32_t bytes=type==0 ? 32 : type==1 ? 24 : (type>=2 && type<=4 ? 8 : 0);
    if (!bytes || arg(c,2)<bytes || !range(out,bytes)) { result(c,invalid_arg); return; }
    std::memset(vm_base+out,0,bytes);
    if (type>=2) {
        w32(out,vm_base[s+3]&15); vm_base[out+4]=vm_base[s+2]&15;
        if (type==3) w16(out+6,vm_base[s+4]>>6);
    } else {
        bool avc=type==0;
        auto* p=vm_base+out;
        if (avc) { p[0]=vm_base[s]; p[1]=vm_base[s+1]; }
        else p[0]=vm_base[s]==0x44 ? 3 : vm_base[s]==0x48 ? 1 : 255;
        unsigned flags=avc ? 2 : 1;
        p[flags]=(vm_base[s+2]>>7)&1; p[flags+1]=(vm_base[s+2]>>6)&1;
        p[flags+2]=uint8_t((vm_base[s+2]&15)-(avc ? 1 : 0));
        p[flags+3]=vm_base[s+3];
        if (vm_base[s+3]==255) { w16(out+6,r16(s+4)); w16(out+8,r16(s+6)); }
        w16(out+10,uint16_t(vm_base[s+9])*16); w16(out+12,uint16_t(vm_base[s+11])*16);
        if (avc) for(unsigned i=0;i<4;++i) w16(out+14+i*2,r16(s+12+i*2));
        else { w16(out+14,r16(s+12)); w16(out+16,r16(s+14)); }
        unsigned colour=avc ? 22 : 18;
        if (p[flags+1]) {
            p[colour]=vm_base[s+20]>>5; p[colour+1]=(vm_base[s+20]>>4)&1;
            for(unsigned i=0;i<3;++i) p[colour+2+i]=vm_base[s+21+i];
        }
        if (avc) {
            p[27]=(vm_base[s+24]>>7)&1; p[28]=(vm_base[s+24]>>6)&1;
            p[29]=(vm_base[s+24]>>4)&3; p[30]=vm_base[s+24]&3; p[31]=vm_base[s+25];
        }
    }
    result(c,0);
}
} // namespace

extern "C" void wkc2_register_pamf(void) {
    struct Binding { const char* name; void (*fn)(ppu_context*); };
    const Binding bindings[]={
        {"cellPamfReaderInitialize",initialize}, {"cellPamfGetHeaderSize",get_header_size},
        {"cellPamfGetStreamOffsetAndSize",get_offset_size},
        {"cellPamfReaderGetPresentationStartTime",timestamp},
        {"cellPamfReaderGetNumberOfSpecificStreams",count_type},
        {"cellPamfReaderSetStreamWithTypeAndIndex",set_type_index},
        {"cellPamfReaderGetStreamTypeAndChannel",type_channel},
        {"cellPamfReaderGetStreamInfo",stream_info}, {"cellPamfStreamTypeToEsFilterId",filter_id},
    };
    for (const auto& b:bindings) ps3_hle_register_ctx(ps3_compute_nid(b.name),b.name,b.fn);
}
