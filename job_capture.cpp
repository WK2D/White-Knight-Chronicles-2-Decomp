#include "job_capture.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
extern std::uint8_t* vm_base;
extern void (*g_spu_get_trace_hook)(std::uint32_t,std::uint32_t,int,std::uint32_t,const std::uint8_t*);
extern void (*g_spu_put_trace_hook)(std::uint32_t,std::uint32_t,int,std::uint32_t,const std::uint8_t*);
int spu_workload_dispatch_job(const std::uint8_t* image,std::uint32_t image_size,std::uint32_t job_ea,std::uint32_t job_desc_size);
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
    // The SDK stages the descriptor's input and cache lists with plain copies,
    // not traced DMA: record those sources as 'S' events.
    auto be16=[](const std::uint8_t* q){return std::uint32_t(q[0])<<8|q[1];};
    auto be32=[](const std::uint8_t* q){return std::uint32_t(q[0])<<24|std::uint32_t(q[1])<<16|std::uint32_t(q[2])<<8|q[3];};
    const std::uint8_t* d=vm_base+job_ea;
    const std::uint32_t n_dma=be16(d+0x0A)/8,n_cache=std::min<std::uint32_t>(be32(d+0x24)/8,4);
    for(std::uint32_t i=0;i<n_dma+n_cache&&0x30+8*(i+1)<=size;++i) {
        const std::uint8_t* e=d+0x30+8*i;
        const std::uint32_t bytes=i<n_dma?(be32(e)&0x7FFFu):be32(e),ea=be32(e+4);
        if(bytes&&ea&&bytes<=0x40000u)event('S',ea,bytes,0,vm_base+ea);
    }
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

namespace {
struct Event {char kind;std::uint32_t ea,size,pc;std::vector<std::uint8_t> bytes;};
std::vector<Event> expected_puts;std::size_t put_index=0;unsigned put_mismatches=0;bool replay_logging=true;
void replay_put(std::uint32_t ea,std::uint32_t size,int,std::uint32_t pc,const std::uint8_t* bytes) {
    const std::size_t i=put_index++;
    if(i>=expected_puts.size()||expected_puts[i].ea!=ea||expected_puts[i].size!=size||
       std::memcmp(expected_puts[i].bytes.data(),bytes,size)) {
        if(replay_logging&&put_mismatches<4)std::fprintf(stderr,"[job-replay] PUT %zu differs: ea=%08X size=%u pc=%05X\n",i,ea,size,pc);
        ++put_mismatches;
    }
}
// Rebuild one capture's inputs in guest memory, dispatch the job through the
// normal SDK path `runs` times and compare every DMA PUT with the capture.
// Adds the job time to total_us; returns the number of runs that differed.
unsigned replay_one(const char* path,unsigned runs,double& total_us) {
    FILE* f=std::fopen(path,"rb");if(!f){std::fprintf(stderr,"[job-replay] cannot open %s\n",path);return runs;}
    std::vector<std::uint8_t> d;{std::uint8_t b[65536];std::size_t n;while((n=std::fread(b,1,sizeof b,f))>0)d.insert(d.end(),b,b+n);}std::fclose(f);
    auto u32=[&](std::size_t at){std::uint32_t v;std::memcpy(&v,d.data()+at,4);return v;};
    if(d.size()<20||std::memcmp(d.data(),"WKCJ",4)){std::fprintf(stderr,"[job-replay] %s is not a capture\n",path);return runs;}
    const std::uint32_t job_ea=u32(12),desc_size=u32(16);
    std::vector<std::uint8_t> descriptor(d.begin()+20,d.begin()+20+desc_size);
    std::vector<Event> reads;expected_puts.clear();
    for(std::size_t at=20+desc_size;at+13<=d.size();) {
        Event e{char(d[at]),u32(at+1),u32(at+5),u32(at+9),{}};
        e.bytes.assign(d.begin()+at+13,d.begin()+at+13+e.size);at+=13+e.size;
        (e.kind=='P'?expected_puts:reads).push_back(std::move(e));
    }
    auto prepare=[&]{
        for(auto it=reads.rbegin();it!=reads.rend();++it)std::memcpy(vm_base+it->ea,it->bytes.data(),it->size); // earliest read wins
        std::memcpy(vm_base+job_ea,descriptor.data(),desc_size);
    };
    const std::uint8_t* h=vm_base+job_ea;prepare();
    auto be32=[](const std::uint8_t* q){return std::uint32_t(q[0])<<24|std::uint32_t(q[1])<<16|std::uint32_t(q[2])<<8|q[3];};
    const std::uint32_t binary=be32(h+4)&~7u,binary_size=((be32(h+8)>>16)&0xFFFFu)<<4;
    auto previous=g_spu_put_trace_hook;g_spu_put_trace_hook=replay_put;
    unsigned bad_runs=0;
    for(unsigned run=0;run<runs;++run) {
        prepare();put_index=0;put_mismatches=0;replay_logging=run==0;
        const auto t0=std::chrono::steady_clock::now();
        spu_workload_dispatch_job(vm_base+binary,binary_size,job_ea,desc_size);
        total_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count();
        if(put_mismatches||put_index!=expected_puts.size())++bad_runs;
        if(run==0&&(put_mismatches||put_index!=expected_puts.size()))
            std::printf("[job-replay] %s: reads=%zu puts expected=%zu produced=%zu mismatched=%u\n",
                path,reads.size(),expected_puts.size(),put_index,put_mismatches);
    }
    g_spu_put_trace_hook=previous;
    return bad_runs;
}
}
// WKC_JOB_REPLAY=<capture file or directory of job-*.bin>, optional
// WKC_JOB_REPLAY_RUNS=<n>: replay each capture n times and report mismatches and
// the mean job time. Returns -1 when not requested, else a process exit code
// (0 = every run of every job matched).
int replay_from_env() {
    const char* path=std::getenv("WKC_JOB_REPLAY");
    if(!path||!*path)return -1;
    const char* count=std::getenv("WKC_JOB_REPLAY_RUNS");
    const unsigned runs=std::max(1u,unsigned(std::strtoul(count?count:"1",nullptr,10)));
    std::vector<std::string> files;
    std::error_code error;
    if(std::filesystem::is_directory(path,error)) {
        for(const auto& entry:std::filesystem::directory_iterator(path,error))
            if(entry.path().extension()==".bin")files.push_back(entry.path().string());
        std::sort(files.begin(),files.end());
    } else files.push_back(path);
    double total_us=0;unsigned bad_runs=0,bad_jobs=0;
    for(const auto& file:files) {
        const unsigned bad=replay_one(file.c_str(),runs,total_us);
        bad_runs+=bad;bad_jobs+=bad!=0;
    }
    const double jobs=double(files.size())*runs;
    std::printf("[job-replay] jobs=%zu runs=%u bad jobs=%u bad runs=%u mean=%.1f us, %.2f ms per pass\n",
        files.size(),runs,bad_jobs,bad_runs,jobs?total_us/jobs:0.0,total_us/runs/1000.0);
    return bad_runs||files.empty()?1:0;
}
}

