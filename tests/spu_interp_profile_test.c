#include "spu_interp.h"
#include "spu_interp_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The snippets issue no channel operations and nothing is lifted, so the
 * interpreter links without the full runtime, as in the SDK selftest. */
u128 spu_rdch(spu_context* c,uint32_t ch) {(void)c;(void)ch;u128 z;memset(&z,0,sizeof z);return z;}
void spu_wrch(spu_context* c,uint32_t ch,u128 v) {(void)c;(void)ch;(void)v;}
uint32_t spu_rchcnt(spu_context* c,uint32_t ch) {(void)c;(void)ch;return 1;}
int g_spu_ls_watch_n,g_spu_ls_probe,g_wws_read_probe,g_wws_code_probe,g_spu_smc_watch;
spu_lifted_fn spu_lifted_lookup(const spu_context* c,uint32_t a) {(void)c;(void)a;return 0;}
void spu_spurs_taskset_syscall(spu_context* c) {(void)c;}
void spu_ls_watch_slow(uint32_t l,int w,const uint8_t* p,uint32_t pc,uint32_t lr) {(void)l;(void)w;(void)p;(void)pc;(void)lr;}

/* il $2,5; il $3,7; stop -- and a different body at the same address. */
static const uint8_t three[]={0x40,0x80,0x02,0x82, 0x40,0x80,0x03,0x83, 0,0,0,0};
static const uint8_t two[]={0x40,0x80,0x02,0x82, 0,0,0,0};
static spu_context ctx;

#ifdef _WIN32
#include <windows.h>
static DWORD WINAPI initialize_profile(LPVOID unused) {
    (void)unused;return (DWORD)spu_interp_profile_enabled();
}
static void concurrent_init(int expected) {
    HANDLE threads[8];
    for(unsigned i=0;i<8;i++) {threads[i]=CreateThread(NULL,0,initialize_profile,NULL,0,NULL);assert(threads[i]);}
    for(unsigned i=0;i<8;i++) {
        DWORD result;assert(WaitForSingleObject(threads[i],5000)==WAIT_OBJECT_0);
        assert(GetExitCodeThread(threads[i],&result) && result==(DWORD)expected);CloseHandle(threads[i]);
    }
}
#else
#include <pthread.h>
static void* initialize_profile(void* unused) {
    (void)unused;return (void*)(uintptr_t)spu_interp_profile_enabled();
}
static void concurrent_init(int expected) {
    pthread_t threads[8];
    for(unsigned i=0;i<8;i++) assert(pthread_create(&threads[i],NULL,initialize_profile,NULL)==0);
    for(unsigned i=0;i<8;i++) {void* result;assert(pthread_join(threads[i],&result)==0);assert((intptr_t)result==expected);}
}
#endif

static void run(int image,uint32_t lsa,const uint8_t* code,size_t size) {
    memset(ctx.ls+lsa,0,64);memcpy(ctx.ls+lsa,code,size);
    ctx.image_id=image;
    spu_interp_run(&ctx,lsa);
    assert(ctx.status&SPU_STATUS_STOPPED_BY_STOP);
}
/* Sessions and steps of the row for kind/image/entry/fingerprint; -1 if absent. */
static int row(const char* path,const char* kind,int image,uint32_t entry,uint64_t fp,unsigned long long* steps,unsigned long long* max) {
    FILE* f=fopen(path,"rb");if(!f) return -1;
    char line[256];int found=-1;
    while(fgets(line,sizeof line,f)) {
        char k[16];int i;unsigned e;unsigned long long h,s,t,m;
        if(sscanf(line,"%15s\t%d\t0x%x\t%llx\t%llu\t%llu\t%llu",k,&i,&e,&h,&s,&t,&m)==7 &&
           !strcmp(k,kind) && i==image && e==entry && h==fp) {
            found=(int)s;*steps=t;*max=m;
        }
    }
    fclose(f);return found;
}

int main(int argc,char** argv) {
    assert(argc==3);
    const char* path=argv[1];int expect=atoi(argv[2]);
    remove(path);
    spu_context_init(&ctx,0);
    concurrent_init(expect);
    assert(spu_interp_profile_enabled()==expect);
    if(!expect) {
        run(0,0x100,three,sizeof three);spu_interp_profile_flush();
        FILE* f=fopen(path,"rb");assert(!f);
        puts("SPU interpreter profile stays off without SPU_INTERP_PROFILE");
        return 0;
    }
    unsigned long long steps=0,max=0;
    run(0,0x100,three,sizeof three);
    uint64_t a=spu_interp_fingerprint(ctx.ls,0x100);
    /* Written on first sight, before any flush: a killed probe keeps it. */
    assert(row(path,"interp",0,0x100,a,&steps,&max)==1 && steps==3 && max==3);
    run(0,0x100,three,sizeof three);run(0,0x100,three,sizeof three);
    run(0,0x100,two,sizeof two);
    uint64_t b=spu_interp_fingerprint(ctx.ls,0x100);
    assert(a!=b);
    run(4,0x200,three,sizeof three);
    /* The microstepper reports through the same table as a separate path;
     * a session that executed nothing is not fallback use. */
    spu_interp_profile_record(SPU_PROFILE_MICROSTEP,0,0x100,a,5);
    spu_interp_profile_record(SPU_PROFILE_MICROSTEP,0,0x300,a,0);
    spu_interp_profile_flush();
    assert(row(path,"interp",0,0x100,a,&steps,&max)==3 && steps==9 && max==3);
    assert(row(path,"interp",0,0x100,b,&steps,&max)==1 && steps==2 && max==2);
    assert(row(path,"interp",4,0x200,a,&steps,&max)==1 && steps==3);
    assert(row(path,"microstep",0,0x100,a,&steps,&max)==1 && steps==5);
    assert(row(path,"microstep",0,0x300,a,&steps,&max)==-1);
    FILE* f=fopen(path,"rb");char header[128];
    assert(f && fgets(header,sizeof header,f) && !strcmp(header,"# spu-interp-profile v1\n"));
    assert(fgets(header,sizeof header,f) && !strcmp(header,"# rows=4 sessions=6 steps=19 dropped_sessions=0\n"));
    fclose(f);remove(path);
    puts("SPU interpreter profile: per-entry sessions, steps, code identity, paths and early write passed");
    return 0;
}
