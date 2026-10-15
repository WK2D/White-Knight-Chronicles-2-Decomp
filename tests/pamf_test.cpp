/* Synthetic PAMF headers only; no game data is included. Exercise registered
 * PPU handlers, guest endianness and sizes rather than host-layout shortcuts. */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <array>
#include <cassert>
#include <cstring>
#include <map>
#include <cstdio>

static std::array<uint8_t,32768> memory;
extern "C" { uint8_t* vm_base=memory.data(); void wkc2_register_pamf(void); }
static std::map<uint32_t,void(*)(ppu_context*)> handlers;
extern "C" void ps3_hle_register_ctx(uint32_t id,const char*,void(*fn)(ppu_context*)) { handlers[id]=fn; }
static void w16(uint32_t a,uint16_t v) { memory[a]=uint8_t(v>>8); memory[a+1]=uint8_t(v); }
static void w32(uint32_t a,uint32_t v) { w16(a,uint16_t(v>>16)); w16(a+2,uint16_t(v)); }
static uint16_t r16(uint32_t a) { return uint16_t(memory[a])<<8|memory[a+1]; }
static uint32_t r32(uint32_t a) { return uint32_t(r16(a))<<16|r16(a+2); }
static uint64_t r64(uint32_t a) { return uint64_t(r32(a))<<32|r32(a+4); }
static uint32_t call(const char* name,uint64_t a,uint64_t b=0,uint64_t d=0,uint64_t e=0) {
    ppu_context c{}; c.gpr[3]=a; c.gpr[4]=b; c.gpr[5]=d; c.gpr[6]=e;
    handlers.at(ps3_compute_nid(name))(&c); return uint32_t(c.gpr[3]);
}
int main(int argc,char** argv) {
    constexpr uint32_t header=0x1000,reader=0x2000,copy=0x2200,out=0x3000;
    wkc2_register_pamf();
    assert(handlers.size()==9);
    assert(handlers.count(0x28B4E2C1) && handlers.count(0x44F5C9E3) && handlers.count(0xCA8181C1));
    w32(header,0x50414D46); w32(header+4,0x30303431);
    w32(header+8,1); w32(header+12,0x200001); // More than 4 GiB of declared mux data.
    w32(header+0x50,0xC4); w16(header+0x56,1); w32(header+0x58,12345);
    w32(header+0x66,90000); w32(header+0x6A,3);
    memory[header+0x6F]=1; memory[header+0x81]=1; memory[header+0x87]=3;
    const uint32_t m2v=header+0x88,avc=m2v+48,lpcm=avc+48;
    memory[m2v]=2; memory[m2v+4]=0xE2;
    memory[m2v+16]=0x44; memory[m2v+18]=0xC4; memory[m2v+19]=1;
    memory[m2v+25]=80; memory[m2v+27]=45;
    w16(m2v+28,1280); w16(m2v+30,720); memory[m2v+36]=0xB0;
    memory[m2v+37]=1; memory[m2v+38]=6; memory[m2v+39]=7;
    memory[avc]=0x1B; memory[avc+4]=0xE0;
    memory[avc+16]=100; memory[avc+17]=41; memory[avc+18]=0xC4; memory[avc+19]=255;
    w16(avc+20,4); w16(avc+22,3); memory[avc+25]=120; memory[avc+27]=68;
    w16(avc+28,2); w16(avc+30,4); w16(avc+32,6); w16(avc+34,8);
    memory[avc+40]=0xF3; memory[avc+41]=12;
    memory[lpcm]=0x80; memory[lpcm+4]=0xBD; memory[lpcm+5]=0x43;
    memory[lpcm+18]=6; memory[lpcm+19]=1; memory[lpcm+20]=0xC0;
    const uint64_t total=2048+uint64_t(0x200001)*2048;
    assert(call("cellPamfGetHeaderSize",header,0,out)==0 && r64(out)==2048);
    assert(call("cellPamfGetStreamOffsetAndSize",header,total,out,out+8)==0);
    assert(r64(out)==2048 && r64(out+8)==uint64_t(0x200001)*2048);
    assert(call("cellPamfGetStreamOffsetAndSize",header,total-1,out,out+8)==0x80610502);
    assert(call("cellPamfGetStreamOffsetAndSize",header,total+1,out,out+8)==0x80610502);
    memory[reader-1]=0xA5; memory[reader+128]=0x5A;
    assert(call("cellPamfReaderInitialize",reader,header,0,0)==0);
    assert(memory[reader-1]==0xA5 && memory[reader+128]==0x5A);
    assert(r64(reader)==2048 && r64(reader+8)==uint64_t(0x200001)*2048);
    assert(r32(reader+36)==header && r32(reader+52)==m2v);
    assert(r16(reader+22)==41);
    assert(call("cellPamfReaderInitialize",copy,header,total,2)==0); // Full 64-bit argument retained.
    assert(call("cellPamfReaderGetPresentationStartTime",reader,out)==0 && r64(out)==(uint64_t(1)<<32)+12345);
    assert(call("cellPamfReaderGetNumberOfSpecificStreams",reader,20)==2);
    assert(call("cellPamfReaderGetNumberOfSpecificStreams",reader,21)==1);
    assert(call("cellPamfReaderGetNumberOfSpecificStreams",reader,3)==1);
    assert(call("cellPamfReaderGetStreamTypeAndChannel",reader,out,out+1)==0);
    assert(memory[out]==1 && memory[out+1]==2);
    std::memset(memory.data()+out,0xA5,64);
    assert(call("cellPamfReaderGetStreamInfo",reader,out,23)==0x80610503);
    assert(memory[out]==0xA5);
    assert(call("cellPamfReaderGetStreamInfo",reader,out,24)==0);
    assert(memory[out]==3 && memory[out+1]==1 && memory[out+2]==1 && memory[out+3]==4);
    assert(r16(out+10)==1280 && r16(out+12)==720 && r16(out+14)==1280 && r16(out+16)==720);
    assert(memory[out+18]==5 && memory[out+19]==1 && memory[out+20]==1 && memory[out+24]==0xA5);
    assert(call("cellPamfReaderSetStreamWithTypeAndIndex",reader,3,0)==2);
    assert(call("cellPamfReaderGetStreamTypeAndChannel",reader,out,out+1)==0 && memory[out]==3 && memory[out+1]==3);
    assert(call("cellPamfReaderGetStreamInfo",reader,out,8)==0);
    assert(r32(out)==1 && memory[out+4]==6 && r16(out+6)==3);
    // A copied reader must retain its independent selection without a host side table.
    std::memcpy(memory.data()+copy,memory.data()+reader,128);
    assert(call("cellPamfReaderSetStreamWithTypeAndIndex",reader,0,0)==1);
    assert(call("cellPamfReaderGetStreamInfo",reader,out,32)==0);
    assert(memory[out]==100 && memory[out+1]==41 && memory[out+4]==3);
    assert(r16(out+6)==4 && r16(out+8)==3 && r16(out+10)==1920 && r16(out+12)==1088);
    assert(r16(out+14)==2 && r16(out+20)==8 && memory[out+27]==1 && memory[out+30]==3);
    assert(call("cellPamfReaderGetStreamTypeAndChannel",copy,out,out+1)==0 && memory[out]==3);
    assert(call("cellPamfReaderSetStreamWithTypeAndIndex",reader,4,0)==0x80610501);
    assert(r32(reader+32)==1); // Failed selection leaves the current stream intact.
    assert(call("cellPamfStreamTypeToEsFilterId",3,3,out)==0);
    assert(r32(out)==0xBD && r32(out+4)==0x43 && r32(out+8)==0 && r32(out+12)==0);
    assert(call("cellPamfStreamTypeToEsFilterId",0,2,out)==0 && r32(out)==0xE2 && r32(out+8)==1);
    assert(call("cellPamfStreamTypeToEsFilterId",3,16,out)==0x80610503);
    assert(call("cellPamfReaderInitialize",0,header,0)==0x80610503);
    w32(header+4,0x30303432);
    assert(call("cellPamfReaderInitialize",reader,header,0)==0x80610505);
    w32(header+4,0x30303431); memory[header+0x87]=4;
    assert(call("cellPamfReaderInitialize",reader,header,0)==0x80610502);
    memory[header+0x87]=3; w32(header+0x50,0x10000);
    assert(call("cellPamfReaderInitialize",reader,header,0)==0x80610502);
    // Optional local fixture mode reads only a private header, never distributes it.
    if (argc==2) {
        FILE* f=std::fopen(argv[1],"rb"); assert(f);
        assert(std::fread(memory.data()+header,1,2048,f)==2048); std::fclose(f);
        assert(call("cellPamfReaderInitialize",reader,header,0,1)==0);
        assert(call("cellPamfReaderSetStreamWithTypeAndIndex",reader,20,0)<256);
        assert(call("cellPamfReaderGetStreamInfo",reader,out,32)==0);
        std::printf("Private video header: %u x %u\n",r16(out+10),r16(out+12));
    }
    std::puts("PAMF guest ABI checks passed");
}
