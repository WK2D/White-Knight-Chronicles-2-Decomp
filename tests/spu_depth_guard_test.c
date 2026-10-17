/* Exact SDK recursion guard; synthetic host threads, no game data. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "win32_compat.h"
#include <assert.h>
#include <stdarg.h>
#include <setjmp.h>
#include "spu_context.h"
static SPU_THREAD_LOCAL char text[4096];
static SPU_THREAD_LOCAL unsigned used;
static SPU_THREAD_LOCAL jmp_buf halt_env;
static SPU_THREAD_LOCAL unsigned halts;
static int capture(FILE* ignored,const char* fmt,...) {
    (void)ignored;va_list args;va_start(args,fmt);
    int n=vsnprintf(text+used,sizeof(text)-used,fmt,args);va_end(args);
    assert(n>=0&&(unsigned)n<sizeof(text)-used);used+=(unsigned)n;return n;
}
static int flush(FILE* ignored) {(void)ignored;return 0;}
void spu_halt(spu_context* ctx) {(void)ctx;++halts;longjmp(halt_env,1);}
#define fprintf capture
#define fflush flush
#include "spu_depth_guard_fixture.inc"
#undef fprintf
#undef fflush
typedef struct {unsigned id,limit;HANDLE turn[2];char output[4096];unsigned count;} worker;
static DWORD WINAPI run(LPVOID argument) {
    worker* w=argument;
    spu_context* ctx=calloc(1,sizeof(*ctx));assert(ctx);ctx->image_id=(int)w->id+1;
    /* Events interleave two simultaneously alive workers deterministically,
     * avoiding a data race even in the old negative control while exposing
     * its globally mixed history and process-wide suppression. */
    for(unsigned k=0;k<(w->limit?32:0);++k) {
        assert(WaitForSingleObject(w->turn[w->id],5000)==WAIT_OBJECT_0);
        ctx->pc=0x1000u*(w->id+1)+k*4;
        ctx->host_depth=w->limit-1;
        if(setjmp(halt_env)) {assert(0&&"below threshold must not halt");}
        spu_depth_guard(ctx);
        assert(!halts);assert(SetEvent(w->turn[1-w->id]));
    }
    assert(WaitForSingleObject(w->turn[w->id],5000)==WAIT_OBJECT_0);
    ctx->pc=0x1000u*(w->id+1)+0x80;ctx->host_depth=w->limit;
    if(!setjmp(halt_env)) {spu_depth_guard(ctx);assert(0&&"threshold must halt");}
    assert(halts==1);
    /* Reporting is bounded per worker; halting must never be suppressed. */
    if(!setjmp(halt_env)) {spu_depth_guard(ctx);assert(0&&"repeat must halt");}
    assert(halts==2);strcpy(w->output,text);w->count=halts;
    assert(SetEvent(w->turn[1-w->id]));free(ctx);return 0;
}
int main(int argc,char** argv) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif
    assert(argc==2);unsigned limit=(unsigned)strtoul(argv[1],0,0);
    HANDLE turns[2]={CreateEventA(NULL,FALSE,TRUE,NULL),CreateEventA(NULL,FALSE,FALSE,NULL)};
    assert(turns[0]&&turns[1]);worker state[2]={0};HANDLE threads[2];
    for(unsigned i=0;i<2;i++) {
        state[i].id=i;state[i].limit=limit;state[i].turn[0]=turns[0];state[i].turn[1]=turns[1];
        threads[i]=CreateThread(NULL,0,run,&state[i],0,NULL);assert(threads[i]);
    }
    for(unsigned i=0;i<2;i++) {
        assert(WaitForSingleObject(threads[i],5000)==WAIT_OBJECT_0);CloseHandle(threads[i]);
        assert(state[i].count==2);
        char image[32];snprintf(image,sizeof image,"[spu-depth] img=%u",i+1);
        assert(strstr(state[i].output,image));
        assert(!strstr(strstr(state[i].output,image)+strlen(image),"[spu-depth] img="));
        const char* history=strstr(state[i].output,"(oldest first):");assert(history);
        history+=strlen("(oldest first):");unsigned entries=0;
        while(*history&&*history!='\n') {
            char* end;unsigned long pc=strtoul(history,&end,16);assert(end!=history);
            assert(pc>=0x1000u*(i+1)&&pc<0x1000u*(i+1)+0x100);++entries;history=end;
        }
        assert(entries==(limit?24:1));
        printf("Worker%u: threshold%u, two real halts, one report, %u private history entries\n",i,limit,entries);
    }
    CloseHandle(turns[0]);CloseHandle(turns[1]);return 0;
}
