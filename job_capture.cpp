#include "job_capture.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
extern std::uint8_t* vm_base;
extern void (*g_spu_get_trace_hook)(std::uint32_t,std::uint32_t,int,std::uint32_t,const std::uint8_t*);
}

namespace wkc::job_capture {
namespace {
std::string directory;
int image_id=-1;
unsigned limit=64;
std::atomic<unsigned> started{0};
std::atomic<bool> armed{false};
struct Record {std::vector<std::uint8_t> bytes;unsigned index=0;bool active=false;};
thread_local Record current;

void put32(std::vector<std::uint8_t>& out,std::uint32_t value) {
    for(int i=0;i<4;++i)out.push_back(std::uint8_t(value>>(8*i)));
}
void event(char kind,std::uint32_t ea,std::uint32_t size,std::uint32_t pc,const std::uint8_t* bytes) {
    auto& out=current.bytes;
    out.push_back(std::uint8_t(kind));put32(out,ea);put32(out,size);put32(out,pc);
    out.insert(out.end(),bytes,bytes+size);
}
void get_hook(std::uint32_t ea,std::uint32_t size,int image,std::uint32_t pc,const std::uint8_t* bytes) {
    get(ea,size,image,pc,bytes);
}
}

void configure() {
    const char* dir=std::getenv("WKC_JOB_CAPTURE");
    const char* image=std::getenv("WKC_JOB_CAPTURE_IMAGE");
    if(!dir||!*dir||!image||!*image)return;
    directory=dir;image_id=int(std::strtol(image,nullptr,10));
    if(const char* count=std::getenv("WKC_JOB_CAPTURE_COUNT"))limit=unsigned(std::strtoul(count,nullptr,10));
    g_spu_get_trace_hook=get_hook;
}
bool enabled() {return image_id>=0;}

void begin(int image,std::uint32_t job_ea,std::uint32_t descriptor_size) {
    if(image!=image_id||current.active||!vm_base)return;
    // Recording starts once <directory>/arm exists, so a route can choose the scene.
    if(!armed.load(std::memory_order_relaxed)) {
        std::error_code error;
        if(!std::filesystem::exists(std::filesystem::path(directory)/"arm",error))return;
        armed=true;
    }
    const unsigned index=started.fetch_add(1);
    if(index>=limit)return;
    const std::uint32_t size=descriptor_size?descriptor_size:256u;
    current.bytes.clear();current.index=index;current.active=true;
    const char magic[4]={'W','K','C','J'};
    current.bytes.insert(current.bytes.end(),magic,magic+4);
    put32(current.bytes,1);put32(current.bytes,std::uint32_t(image));
    put32(current.bytes,job_ea);put32(current.bytes,size);
    current.bytes.insert(current.bytes.end(),vm_base+job_ea,vm_base+job_ea+size);
}
void end(int image) {
    if(image!=image_id||!current.active)return;
    current.active=false;
    char name[32];std::snprintf(name,sizeof name,"/job-%05u.bin",current.index);
    if(FILE* f=std::fopen((directory+name).c_str(),"wb")) {
        std::fwrite(current.bytes.data(),1,current.bytes.size(),f);std::fclose(f);
    }
    current.bytes.clear();
}
void get(std::uint32_t ea,std::uint32_t size,int image,std::uint32_t pc,const std::uint8_t* bytes) {
    if(current.active&&image==image_id)event('G',ea,size,pc,bytes);
}
void put(std::uint32_t ea,std::uint32_t size,int image,std::uint32_t pc,const std::uint8_t* bytes) {
    if(current.active&&image==image_id)event('P',ea,size,pc,bytes);
}
}
