// Optional bounded observation of native SPURS jobs. All captured game data is private.
#include "spu_context.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

extern "C" {
extern void (*g_spu_job_trace_hook)(const spu_context*,uint32_t,uint32_t,int);
extern void (*g_spu_put_trace_hook)(uint32_t,uint32_t,int,uint32_t,const uint8_t*);
}

namespace {
std::string request_file, capture_dir;
std::mutex claim_mutex;
unsigned last_request=0, remaining=0, selected_image=0, total=0;
constexpr unsigned max_jobs=128, max_request_jobs=32, max_put_records=512;
constexpr uint32_t max_put_bytes=1024*1024;
void (*previous_put)(uint32_t,uint32_t,int,uint32_t,const uint8_t*)=nullptr;
struct Capture {
    const spu_context* ctx=nullptr;
    std::string stem;
    FILE* puts=nullptr;
    FILE* payload=nullptr;
    unsigned records=0, omitted=0, nested=0, nested_jobs=0;
    uint32_t bytes=0, io_errors=0;
};
thread_local Capture current;

void write_blob(const std::string& name,const void* bytes,size_t count) {
    FILE* f=std::fopen(name.c_str(),"wb");
    if (!f) { ++current.io_errors; return; }
    if (std::fwrite(bytes,1,count,f)!=count) ++current.io_errors;
    if (std::fclose(f)) ++current.io_errors;
}
void state(const spu_context* ctx,const char* phase) {
    write_blob(current.stem+"."+phase+".ls.bin",ctx->ls,SPU_LS_SIZE);
    write_blob(current.stem+"."+phase+".registers.bin",ctx->gpr,sizeof ctx->gpr);
    FILE* f=std::fopen((current.stem+"."+phase+".txt").c_str(),"wb");
    if (!f) { ++current.io_errors; return; }
    if (std::fprintf(f,"image=%d pc=%05X status=%X stop=%X r0=%08X r1=%08X r3=%08X r4=%08X\n",
        ctx->image_id,ctx->pc,ctx->status,ctx->stop_code,ctx->gpr[0]._u32[0],
        ctx->gpr[1]._u32[0],ctx->gpr[3]._u32[0],ctx->gpr[4]._u32[0])<0) ++current.io_errors;
    if (std::fclose(f)) ++current.io_errors;
}
void observe(const spu_context* ctx,uint32_t job,uint32_t descriptor_size,int after) {
    if (after) {
        if (!current.ctx) return;
        if (current.ctx!=ctx) { if(current.nested) --current.nested; return; }
        state(ctx,"after");
        if (current.puts) {
            if (std::fprintf(current.puts,"# records=%u bytes=%u omitted=%u nested_jobs=%u io_errors=%u\n",
                current.records,current.bytes,current.omitted,current.nested_jobs,current.io_errors)<0) ++current.io_errors;
            if (std::fclose(current.puts)) ++current.io_errors;
        }
        if (current.payload && std::fclose(current.payload)) ++current.io_errors;
        std::fprintf(stderr,"[job-capture] complete image=%d job=%08X records=%u bytes=%u omitted=%u nested=%u errors=%u\n",
            ctx->image_id,job,current.records,current.bytes,current.omitted,current.nested_jobs,current.io_errors);
        current=Capture{};
        return;
    }
    if (current.ctx) { ++current.nested; ++current.nested_jobs; return; }
    unsigned ordinal=0, generation=0;
    {
        std::lock_guard lock(claim_mutex);
        if (total>=max_jobs) return;
        FILE* f=std::fopen(request_file.c_str(),"rb");
        if (f) {
            unsigned request=0,count=0,image=0;
            if (std::fscanf(f,"%u %u %u",&request,&count,&image)==3 && request && request>last_request && count && count<=max_request_jobs) {
                last_request=request; remaining=count; selected_image=image;
            }
            std::fclose(f);
        }
        if (!remaining || unsigned(ctx->image_id)!=selected_image) return;
        --remaining; ordinal=++total; generation=last_request;
    }
    current.ctx=ctx;
    char leaf[128];
    std::snprintf(leaf,sizeof leaf,"/request_%u_job_%03u_image_%d",generation,ordinal,ctx->image_id);
    current.stem=capture_dir+leaf;
    current.puts=std::fopen((current.stem+".puts.txt").c_str(),"wb");
    current.payload=std::fopen((current.stem+".puts.bin").c_str(),"wb");
    if (!current.puts || !current.payload) ++current.io_errors;
    if (current.puts && std::fprintf(current.puts,"# job=%08X descriptor_size=%u\n# pc ea size stored offset\n",job,descriptor_size)<0) ++current.io_errors;
    state(ctx,"before");
}
void put(uint32_t ea,uint32_t size,int image,uint32_t pc,const uint8_t* bytes) {
    if (previous_put) previous_put(ea,size,image,pc,bytes);
    if (!current.ctx || current.nested) return;
    if (image!=current.ctx->image_id) { ++current.omitted; return; }
    if (current.records>=max_put_records) { ++current.omitted; return; }
    ++current.records;
    uint32_t stored=size;
    if (stored>max_put_bytes-current.bytes) stored=max_put_bytes-current.bytes;
    if (!current.payload || !bytes) stored=0;
    if (current.puts && std::fprintf(current.puts,"%05X %08X %u %u %u\n",pc,ea,size,stored,current.bytes)<0) ++current.io_errors;
    if (stored && std::fwrite(bytes,1,stored,current.payload)!=stored) ++current.io_errors;
    current.bytes+=stored;
    if (stored!=size) ++current.omitted;
}
}

extern "C" void wkc2_init_spu_job_capture(void) {
    const char* request=std::getenv("WKC2_JOB_CAPTURE_FILE");
    const char* directory=std::getenv("WKC2_JOB_CAPTURE_DIR");
    if (!request || !*request || !directory || !*directory) return;
    std::error_code error;
    std::filesystem::create_directories(directory,error);
    if (error) { std::fprintf(stderr,"[job-capture] could not create private output directory\n"); return; }
    request_file=request; capture_dir=directory;
    previous_put=g_spu_put_trace_hook;
    g_spu_put_trace_hook=put;
    g_spu_job_trace_hook=observe;
    std::fprintf(stderr,"[job-capture] enabled; request: increasing_id count image; max128 jobs, 32/request, 512 PUTs and 1MiB/job\n");
}
