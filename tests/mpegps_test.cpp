#include "../mpegps.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
using namespace wkc2::mpeg;
using Bytes=std::vector<uint8_t>;
static void join(Bytes& a,const Bytes& b) { a.insert(a.end(),b.begin(),b.end()); }
static Bytes time_bytes(uint64_t t,unsigned prefix=2) {
    return {uint8_t(prefix<<4|((t>>30)&7)<<1|1),uint8_t(t>>22),
        uint8_t(((t>>15)&127)<<1|1),uint8_t(t>>7),uint8_t((t&127)<<1|1)};
}
static Bytes pes(uint8_t id,const Bytes& data,uint64_t pts=no_time) {
    auto t=pts==no_time ? Bytes{} : time_bytes(pts);
    unsigned n=3+unsigned(t.size()+data.size());
    Bytes out={0,0,1,id,uint8_t(n>>8),uint8_t(n),0x80,uint8_t(t.empty() ? 0 : 0x80),uint8_t(t.size())};
    join(out,t); join(out,data); return out;
}
static std::vector<AccessUnit> parse(const Bytes& input,size_t chunk,Codec kind,size_t pcm=960) {
    ProgramStream ps; Assembler stream(kind,pcm); std::vector<AccessUnit> out;
    for(size_t pos=0;pos<input.size();pos+=chunk) {
        ps.append(std::span(input).subspan(pos,std::min(chunk,input.size()-pos)),0x123456789ABCDEF0ull);
        Packet p;
        while(ps.next(p)) if(!p.end) {
            auto units=stream.feed(p);
            for(auto& u:units) out.push_back(std::move(u));
        }
    }
    assert(ps.pending()==0);
    for(auto& u:stream.flush()) out.push_back(std::move(u));
    return out;
}
int main(int argc,char** argv) {
    Bytes first={0,0,1,0xB3,0x50,0x02,0xD0,0x14,0,0,1,0xB8,0,0,0,0,
        0,0,1,0,0,8,0,0,1,1,0xA7,0xE8};
    Bytes second={0,0,1,0,0,16,0,0,1,1,0x17,0x88};
    Bytes third={0,0,1,0xB3,1,2,3,4,0,0,1,0,0,8,0,0,1,1,0x11,0x33,0,0,1,0xB7};
    Bytes elementary=first; join(elementary,second); join(elementary,third);
    Bytes input=pes(0xE0,Bytes(elementary.begin(),elementary.begin()+18),0x100000004ull);
    join(input,pes(0xE0,Bytes(elementary.begin()+18,elementary.begin()+first.size()+second.size())));
    join(input,pes(0xE0,third,99999)); join(input,{0,0,1,0xB9});
    for(size_t chunk=1;chunk<100;++chunk) {
        auto units=parse(input,chunk,Codec::mpeg2);
        assert(units.size()==3 && units[0].data==first && units[1].data==second && units[2].data==third);
        assert(units[0].pts==0x100000004ull && units[1].pts==no_time && units[2].pts==99999);
        assert(units[0].rap && !units[1].rap && units[2].rap);
        assert(units[2].user==0x123456789ABCDEF0ull);
    }
    Bytes audio(1920); for(size_t i=0;i<audio.size();++i) audio[i]=uint8_t(i*37);
    Bytes a={0x40,0x31,0x40,0}; join(a,Bytes(audio.begin(),audio.begin()+1101));
    input=pes(0xBD,a,90000); a={0x40,0x31,0x40,0}; join(a,Bytes(audio.begin()+1101,audio.end()));
    join(input,pes(0xBD,a,90450));
    for(size_t chunk: {size_t(1),size_t(3),size_t(2048)}) {
        auto units=parse(input,chunk,Codec::lpcm);
        assert(units.size()==2 && units[0].data.size()==960 && units[1].data.size()==960);
        Bytes combined=units[0].data; join(combined,units[1].data); assert(combined==audio);
        assert(units[0].specific[0]==3 && units[0].specific[1]==1 && units[0].specific[2]==1);
    }
    Bytes ac3(128); ac3[0]=0x0B; ac3[1]=0x77; ac3[4]=0;
    a={0x30,0,0,0}; join(a,ac3); join(a,ac3);
    auto ac3_units=parse(pes(0xBD,a),7,Codec::ac3);
    assert(ac3_units.size()==2 && ac3_units[0].data==ac3 && ac3_units[1].data==ac3);
    ProgramStream bad; Packet p; input=pes(0xE0,first,1); input[9]&=0xFE;
    bad.append(input); bool rejected=false;
    try { bad.next(p); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected); bad.reset(); bad.append(Bytes{0,0,1}); assert(!bad.next(p) && bad.pending()==3);
    // Optional private integration fixture: export assembled units for independent decoding.
    if(argc>=2) {
        std::ifstream f(argv[1],std::ios::binary); assert(f);
        Bytes file((std::istreambuf_iterator<char>(f)),{}); assert(file.size()>2048);
        size_t header=(uint32_t(file[8])<<24|uint32_t(file[9])<<16|uint32_t(file[10])<<8|file[11])*2048ull;
        assert(header<file.size());
        bool has_audio=file[0x87]>1,ac3_audio=has_audio && file[0xB8]==0x81;
        size_t pcm_size=960;
        if(has_audio && !ac3_audio) {
            unsigned channels=file[0xCA]&15,bits=file[0xCC]>>6;
            assert((file[0xCB]&15)==1 && (bits==1 || bits==3));
            pcm_size=48000*(channels+(channels&1))*(bits==1 ? 2 : 3)/200;
        }
        ProgramStream ps; Assembler video(Codec::mpeg2),pcm(ac3_audio ? Codec::ac3 : Codec::lpcm,pcm_size);
        std::ofstream es,raw,sizes;
        if(argc==3) { es.open(std::string(argv[2])+".m2v",std::ios::binary); raw.open(std::string(argv[2])+(ac3_audio ? ".ac3" : ".pcm"),std::ios::binary); sizes.open(std::string(argv[2])+".sizes"); }
        size_t videos=0,audios=0,packets=0,video_bytes=0,audio_bytes=0;
        auto emit=[&](std::vector<AccessUnit> units,bool is_video) {
            for(auto& u:units) {
                if(is_video) { ++videos; video_bytes+=u.data.size(); if(es) es.write((const char*)u.data.data(),u.data.size()); if(sizes) sizes<<u.data.size()<<' '<<u.pts<<' '<<u.dts<<'\n'; }
                else { ++audios; audio_bytes+=u.data.size(); if(raw) raw.write((const char*)u.data.data(),u.data.size()); }
            }
        };
        for(size_t pos=header;pos<file.size();pos+=997) {
            ps.append(std::span(file).subspan(pos,std::min(size_t(997),file.size()-pos)));
            while(ps.next(p)) {
                ++packets;
                if(p.id==0xE0) emit(video.feed(p),true);
                if(has_audio && p.id==0xBD && !p.data.empty() && p.data[0]==(ac3_audio ? 0x30 : 0x40)) emit(pcm.feed(p),false);
            }
        }
        assert(ps.pending()==0); emit(video.flush(),true); emit(pcm.flush(),false);
        std::printf("Private stream: %zu packets, %zu video units / %zu bytes, %zu audio units / %zu bytes\n",packets,videos,video_bytes,audios,audio_bytes);
    }
    std::puts("MPEG-PS split-boundary and access-unit checks passed");
}
