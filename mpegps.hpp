/* Incremental MPEG-2 program-stream and access-unit parsing. No game bytes or
 * emulator implementation are included. Format/API facts were checked against
 * ISO MPEG-2 packet syntax, FFmpeg's format interface and cellDmux ABI records.
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <stdexcept>
#include <vector>

namespace wkc2::mpeg {
constexpr uint64_t no_time=UINT64_MAX;
inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0])<<8|p[1]; }
struct Packet {
    uint8_t id=0;
    uint64_t pts=no_time,dts=no_time,user=0;
    bool end=false;
    std::vector<uint8_t> data;
};
inline uint64_t timestamp(const uint8_t* p, unsigned prefix) {
    if ((p[0]>>4)!=prefix || !(p[0]&p[2]&p[4]&1))
        throw std::runtime_error("invalid PES timestamp markers");
    return uint64_t((p[0]>>1)&7)<<30 | uint64_t(p[1])<<22 |
        uint64_t(p[2]>>1)<<15 | uint64_t(p[3])<<7 | p[4]>>1;
}
class ProgramStream {
    struct Tag { size_t offset; uint64_t user; };
    std::vector<uint8_t> bytes;
    std::deque<Tag> tags;
    size_t head=0;
    uint64_t user=0;
public:
    void reset() { bytes.clear(); tags.clear(); head=0; user=0; }
    void append(std::span<const uint8_t> input,uint64_t value=0) {
        if (head) {
            bytes.erase(bytes.begin(),bytes.begin()+head);
            while (!tags.empty() && tags.front().offset<head) { user=tags.front().user; tags.pop_front(); }
            for(auto& t:tags) t.offset-=head;
            head=0;
        }
        if(bytes.size()+input.size()>16*1024*1024) throw std::runtime_error("oversized program-stream input");
        tags.push_back({bytes.size(),value}); bytes.insert(bytes.end(),input.begin(),input.end());
    }
    bool next(Packet& out) {
        for (;;) {
            size_t remaining=bytes.size()-head;
            if (remaining<4) return false;
            auto* p=bytes.data()+head;
            if (p[0] || p[1] || p[2]!=1) throw std::runtime_error("invalid program-stream start code");
            unsigned id=p[3]; size_t size=0;
            if (id==0xB9) size=4;
            else if (id==0xBA) {
                if (remaining<14) return false;
                if((p[4]&0xC0)!=0x40) throw std::runtime_error("unsupported MPEG pack header");
                size=14+(p[13]&7);
            } else {
                if (remaining<6) return false;
                size=6+be16(p+4);
                if (size==6 && id>=0xE0 && id<=0xEF) {
                    size=0;
                    for(size_t i=6;i+4<=remaining;++i)
                        if(!p[i] && !p[i+1] && p[i+2]==1 && p[i+3]>=0xB9) { size=i; break; }
                    if(!size) return false;
                }
            }
            if (remaining<size) return false;
            while (!tags.empty() && tags.front().offset<=head) { user=tags.front().user; tags.pop_front(); }
            out={}; out.id=uint8_t(id); out.user=user;
            if (id==0xB9) out.end=true;
            else if(id==0xBD || (id>=0xC0 && id<=0xEF)) {
                if(size<9 || (p[6]&0xC0)!=0x80 || (p[6]&0x30))
                    throw std::runtime_error("invalid or scrambled PES header");
                size_t start=9+p[8]; unsigned flags=p[7]>>6;
                if(start>size || flags==1 || (flags==2 && p[8]<5) || (flags==3 && p[8]<10))
                    throw std::runtime_error("invalid PES optional header");
                if(flags>=2) out.pts=timestamp(p+9,flags==3 ? 3 : 2);
                if(flags==3) out.dts=timestamp(p+14,1);
                out.data.assign(p+start,p+size);
            }
            head+=size;
            if(out.end || !out.data.empty()) return true;
        }
    }
    size_t pending() const { return bytes.size()-head; }
};

struct AccessUnit {
    std::vector<uint8_t> data;
    uint64_t pts=no_time,dts=no_time,user=0;
    bool rap=false;
    std::array<uint8_t,3> specific{};
};
enum class Codec { mpeg2,avc,lpcm,ac3 };
class Assembler {
    struct Mark { uint64_t position,pts,dts,user; };
    Codec codec;
    size_t pcm_bytes;
    std::vector<uint8_t> bytes;
    std::deque<Mark> marks;
    uint64_t origin=0,last_user=0;
    size_t scan=0,prefix=SIZE_MAX,picture=SIZE_MAX;
    std::array<uint8_t,3> pcm_info{};
    bool audio_started=false;
    AccessUnit take(size_t n,size_t first_picture=0) {
        AccessUnit out; out.user=last_user;
        while(!marks.empty() && marks.front().position<=origin+first_picture) {
            auto t=marks.front(); marks.pop_front(); out.user=last_user=t.user;
            if(t.pts!=no_time) out.pts=t.pts;
            if(t.dts!=no_time) out.dts=t.dts;
        }
        out.data.assign(bytes.begin(),bytes.begin()+n); out.specific=pcm_info;
        if(codec==Codec::mpeg2 && first_picture+6<=n) out.rap=((bytes[first_picture+5]>>3)&7)==1;
        bytes.erase(bytes.begin(),bytes.begin()+n); origin+=n;
        return out;
    }
    size_t ac3_size() const {
        if(bytes.size()<5) return 0;
        if(bytes[0]!=0x0B || bytes[1]!=0x77) throw std::runtime_error("invalid AC3 sync word");
        unsigned rate=bytes[4]>>6,code=bytes[4]&63;
        constexpr unsigned kbps[]={32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640};
        if(rate==3 || code>=38) throw std::runtime_error("invalid AC3 frame size");
        unsigned bitrate=kbps[code/2];
        if(rate==0) return bitrate*4;
        if(rate==2) return bitrate*6;
        return 2*((bitrate*320/147)+(code&1));
    }
public:
    explicit Assembler(Codec kind=Codec::mpeg2,size_t pcm_size=960):codec(kind),pcm_bytes(pcm_size) {}
    void reset() { bytes.clear(); marks.clear(); origin=last_user=0; scan=0; prefix=picture=SIZE_MAX; audio_started=false; }
    std::vector<AccessUnit> feed(const Packet& packet) {
        auto payload=std::span(packet.data);
        if(codec==Codec::lpcm || codec==Codec::ac3) {
            if(payload.size()<4) throw std::runtime_error("short private audio header");
            size_t skip=4;
            if(!audio_started) {
                // The optional initial byte count is only present at discontinuity.
                skip+=((unsigned(payload[2])<<8)|payload[3]) & (codec==Codec::lpcm ? 0x7FF : 0xFFFF);
                audio_started=true;
            }
            if(skip>payload.size()) throw std::runtime_error("invalid private audio offset");
            if(codec==Codec::lpcm) pcm_info={uint8_t(payload[1]>>4),uint8_t(payload[1]&15),uint8_t(payload[2]>>6)};
            payload=payload.subspan(skip);
        }
        marks.push_back({origin+bytes.size(),packet.pts,packet.dts,packet.user});
        bytes.insert(bytes.end(),payload.begin(),payload.end());
        if(bytes.size()>4*1024*1024) throw std::runtime_error("oversized access unit");
        std::vector<AccessUnit> out;
        if(codec==Codec::lpcm || codec==Codec::ac3) {
            for (;;) {
                size_t n=codec==Codec::lpcm ? pcm_bytes : ac3_size();
                if(!n || bytes.size()<n) break;
                out.push_back(take(n));
            }
            return out;
        }
        while(scan+4<=bytes.size()) {
            if(bytes[scan] || bytes[scan+1] || bytes[scan+2]!=1) { ++scan; continue; }
            unsigned code=bytes[scan+3];
            bool is_picture=codec==Codec::mpeg2 ? code==0 : (code&31)==9;
            bool is_prefix=codec==Codec::mpeg2 && (code==0xB3 || code==0xB8);
            if(is_prefix && picture!=SIZE_MAX && prefix==SIZE_MAX) prefix=scan;
            if(is_picture) {
                if(picture!=SIZE_MAX) {
                    size_t boundary=prefix==SIZE_MAX ? scan : prefix;
                    out.push_back(take(boundary,picture));
                    scan-=boundary;
                }
                picture=scan; prefix=SIZE_MAX;
            }
            if(codec==Codec::mpeg2 && code==0xB7 && picture!=SIZE_MAX) {
                out.push_back(take(scan+4,picture)); scan=0; picture=prefix=SIZE_MAX; continue;
            }
            scan+=4;
        }
        return out;
    }
    std::vector<AccessUnit> flush() {
        std::vector<AccessUnit> out;
        if(!bytes.empty()) {
            if(codec==Codec::ac3) throw std::runtime_error("truncated AC3 access unit");
            if(codec==Codec::lpcm || picture!=SIZE_MAX) out.push_back(take(bytes.size(),picture==SIZE_MAX ? 0 : picture));
            else throw std::runtime_error("video stream has no picture delimiter");
        }
        reset(); return out;
    }
};
} // namespace wkc2::mpeg
