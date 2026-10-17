/* Synthetic NV0039 transfers through the production FIFO walker. No game data. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cellGcmSys.c"
#include <assert.h>
#include <stdlib.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
static unsigned reservation_breaks, spu_notifications, reads, writes, mirrors, legacy, methods;
static int read_fails, reserved;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t ea) { (void)ea; ++reservation_breaks; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return reserved; }
void spu_coh_notify_write(uint32_t a) { (void)a; ++spu_notifications; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
ps3_guest_caller_fn g_ps3_guest_caller;
int rsx_draw_engine_enabled(void) { return 1; }
int rsx_draw_engine_guest_buffer_read(u32 l, u32 o, u32 b) { (void)l; (void)o; (void)b; ++reads; return !read_fails; }
void rsx_draw_engine_guest_buffer_wrote(u32 l, u32 o, u32 b) { (void)l; (void)o; (void)b; ++writes; }
void rsx_draw_engine_guest_read(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
void rsx_draw_engine_guest_wrote(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
void rsx_draw_engine_guest_access(u32 l, u32 o, u32 b) { (void)l; (void)o; (void)b; }
int rsx_draw_engine_resolve_read(u32 sl, u32 so, u32 sp, u32 ds, u32 dt, u32 dl, u32 d, u32 dp, u32 w, u32 h)
{ (void)sl; (void)so; (void)sp; (void)ds; (void)dt; (void)dl; (void)d; (void)dp; (void)w; (void)h; abort(); }
int rsx_live_draw_enabled(void) { return 1; }
int rsx_live_draw_blit(u32 sl, u32 sa, u32 sp, u32 dl, u32 da, u32 dp, u32 w, u32 h)
{ (void)sl; (void)sa; (void)sp; (void)dl; (void)da; (void)dp; (void)w; (void)h; abort(); }
void rsx_live_draw_note_resolve(u32 sl, u32 sa, u32 dl, u32 da) { (void)sl; (void)sa; (void)dl; (void)da; abort(); }
int rsx_live_draw_resolve_blit(u32 sl, u32 sa, u32 sp, u32 ds, u32 dt, u32 dl, u32 da, u32 dp,
                               u32 x, u32 y, u32 w, u32 h)
{ (void)sl; (void)sa; (void)sp; (void)ds; (void)dt; (void)dl; (void)da; (void)dp;
  (void)x; (void)y; (void)w; (void)h; abort(); }
/* The rest of the walker is linked but never reached from these blits. */
PPU_THREAD_LOCAL ppu_context* g_active_ctx;
u32 g_rsx_last_reference;
void (*g_spu_put_hook)(u32 ea, u32 size);
void ppu_dump_bctrl_ring(uint32_t n, const char* tag) { (void)n; (void)tag; abort(); }
void ppu_dump_guest_stack(ppu_context* ctx, const char* tag) { (void)ctx; (void)tag; abort(); }
void rsx_draw_engine_method(u32 method, u32 arg) { (void)method; (void)arg; ++mirrors; }
void rsx_draw_engine_note_flip(u32 buffer_id) { (void)buffer_id; abort(); }
void rsx_draw_engine_set_display_buffer(u32 b, u32 l, u32 o, u32 p, u32 w, u32 h)
{ (void)b; (void)l; (void)o; (void)p; (void)w; (void)h; abort(); }
void rsx_live_draw_method(u32 method, u32 arg) { (void)method; (void)arg; ++legacy; }
void rsx_live_draw_set_display_buffer(u32 b, u32 l, u32 o, u32 p, u32 w, u32 h)
{ (void)b; (void)l; (void)o; (void)p; (void)w; (void)h; abort(); }
int rsx_process_method(rsx_state* state, u32 method, u32 data) { (void)state; (void)method; (void)data; ++methods; return 0; }
void rsx_raise_user_cmd(u32 arg) { (void)arg; abort(); }
void rsx_state_init(rsx_state* state) { memset(state, 0, sizeof(*state)); }


enum { LOCAL=0xFEED0000u, MAIN=0xFEED0001u, REPORT=0x66626660u,
       SRC=0x10000u, DST=0x20000u, FIFO=0x400000u };
static u32 put=0x1000;
static void emit(u32 sub, u32 method, u32 data) {
    vm_write32(FIFO+put, (1u<<18)|(sub<<13)|method); put+=4;
    vm_write32(FIFO+put,data); put+=4;
}
static void drain(void) {
    vm_write32(GCM_CONTROL_GUEST_ADDR,put);
    cellGcm_rsx_process_fifo();
    assert(s_fifo_getoff==put && vm_read32(GCM_CONTROL_GUEST_ADDR+4)==put);
}
static void transfer(u32 sd,u32 src,u32 dd,u32 dst,int32_t sp,int32_t dp,u32 cols,u32 rows,u32 fmt,u32 notify) {
    emit(1,0x184,sd);emit(1,0x188,dd);emit(1,0x30c,src);emit(1,0x310,dst);
    emit(1,0x314,(u32)sp);emit(1,0x318,(u32)dp);emit(1,0x31c,cols);
    emit(1,0x320,rows);emit(1,0x324,fmt);emit(1,0x328,notify);drain();
}
static u8* local(u32 off) { return vm_base+s_config.localAddress+off; }
static void pattern(u8* p,u32 n) { for(u32 i=0;i<n;i++)p[i]=(u8)(17+i*13); }
static void check_copy(u32 sd,u32 src,u32 dd,u32 dst,int32_t sp,int32_t dp,u32 cols,u32 rows,u32 fmt) {
    u8 expected[4096]; assert((u64)cols*rows<=sizeof expected);
    const u32 si=fmt&255,di=(fmt>>8)&255;
    for(u32 y=0;y<rows;y++)for(u32 x=0;x<cols;x++)
        expected[y*cols+x]=nv0039_read(sd,(u32)((int64_t)src+(int64_t)sp*y+x*si));
    const u64 before=s_nv0039_copies;
    transfer(sd,src,dd,dst,sp,dp,cols,rows,fmt,0);
    assert(s_nv0039_copies==before+1);
    /* Destination rows in these cases are disjoint, except dp==0: then the
     * last source row must win, without inventing a default row pitch. */
    const u32 first=dp==0 ? rows-1 : 0;
    for(u32 y=first;y<rows;y++)for(u32 x=0;x<cols;x++)
        assert(vm_read8(nv0039_ea(dd,(u32)((int64_t)dst+(int64_t)dp*y+x*di)))==expected[y*cols+x]);
}
int main(int argc,char** argv) {
    ppu_vm_size=16u<<20;vm_base=calloc(1,ppu_vm_size);assert(vm_base);
    s_config.localAddress=8u<<20;s_config.localSize=4u<<20;
    memset(s_ea_address_table,0xff,sizeof s_ea_address_table);
    s_ea_address_table[0]=4;  /* FIFO IO 0 maps to EA 4 MiB */
    s_ea_address_table[6]=2;  /* overlaps the LOCAL number space, but maps to MAIN */
    s_ea_address_table[32]=3;s_ea_address_table[33]=1; /* deliberately nonadjacent */
    ppu_hle_inject_base=6u<<20;s_gcm_syscall_mode=1;s_fifo_getoff=put;
    pattern(vm_base+(2u<<20),0x20000);pattern(local(SRC),0x1000);
    if(argc==2) {
        assert(!strcmp(argv[1],"3d"));
        transfer(MAIN,0x600680,LOCAL,DST,0,0,12,1,0x101,0);
        assert(!s_nv0039_copies && methods==10 && mirrors==10 && legacy==10);
        assert(local(DST)[0]==0);
        puts("Subchannel 1 remains 3D when the title has not selected NV0039");
        free(vm_base);return 0;
    }
    /* A draw-state method on either side must survive; no copy method may
     * leak to the classic decoder, shared renderer, or legacy renderer. */
    emit(0,0x310,1);drain();
    check_copy(MAIN,0x600680,LOCAL,DST,0,0,12,1,0x101);
    check_copy(MAIN,0x600830,LOCAL,DST+32,0,0,16,1,0x101);
    emit(0,0x310,0);drain();
    assert(methods==2 && mirrors==2 && legacy==2);
    assert(reads==4 && writes==2);
    check_copy(LOCAL,SRC,MAIN,0x610000,11,13,7,3,0x101);
    check_copy(LOCAL,SRC,LOCAL,DST,0,13,7,3,0x101);
    check_copy(LOCAL,SRC,LOCAL,DST,11,0,7,3,0x101);
    check_copy(LOCAL,SRC+32,LOCAL,DST,-16,16,12,3,0x101);
    check_copy(LOCAL,SRC,LOCAL,DST+32,16,-16,12,3,0x101);
    /* Copying an interleaved byte channel preserves all destination holes. */
    memset(local(DST),0xa5,128);
    check_copy(LOCAL,SRC,LOCAL,DST,32,48,9,2,0x402);
    for(u32 y=0;y<2;y++)for(u32 x=0;x<36;x++)if(x%4)assert(local(DST)[y*48+x]==0xa5);
    /* Read all source rows before overwriting any overlapping row. */
    pattern(local(SRC),128);
    check_copy(LOCAL,SRC,LOCAL,SRC+8,16,16,12,3,0x101);
    pattern(local(SRC),128);
    check_copy(LOCAL,SRC+8,LOCAL,SRC,16,16,12,3,0x101);
    for(u32 x=0;x<12;x++)vm_base[nv0039_ea(MAIN,0x020ffffa+x)]=(u8)(0xe0+x);
    check_copy(MAIN,0x020ffffa,LOCAL,DST,0,0,12,1,0x101);
    for(u32 x=0;x<12;x++)assert(local(DST)[x]==(u8)(0xe0+x));
    s_report_data[2].timestamp=0x0102030405060708ull;s_report_data[2].value=0x11223344;
    transfer(REPORT,32,MAIN,0x610000,0,0,16,1,0x101,0);
    const u8 report[16]={1,2,3,4,5,6,7,8,0x11,0x22,0x33,0x44,0,0,0,0};
    assert(!memcmp(vm_base+(2u<<20)+0x10000,report,16));
    const u64 good=s_nv0039_copies;
    memset(local(DST),0xa5,128);
    memset(local(s_config.localSize-8),0xa5,8);
    transfer(MAIN,0x700000,LOCAL,DST,0,0,12,1,0x101,0); /* unmapped MAIN */
    transfer(LOCAL,SRC,LOCAL,s_config.localSize-8,0,16,4,2,0x101,0); /* bad second row */
    transfer(LOCAL,SRC,LOCAL,DST,0,0,12,1,0x100,0); /* zero stride */
    transfer(LOCAL,SRC,LOCAL,DST,0,0,12,1,0x101,1); /* notification not implemented */
    transfer(0x1234,SRC,LOCAL,DST,0,0,12,1,0x101,0);
    transfer(LOCAL,0xfffffff8,LOCAL,DST,0,0,16,1,0x101,0); /* no wrap to low RAM */
    transfer(LOCAL,SRC,LOCAL,DST,-0x20000,0,4,2,0x101,0); /* negative start */
    transfer(LOCAL,SRC,LOCAL,DST,0,0,0x4000001,1,0x101,0); /* resource bound */
    transfer(LOCAL,SRC,LOCAL,DST,0,0,12,0,0x101,0); /* zero rows = no copy */
    read_fails=1;transfer(LOCAL,SRC,LOCAL,DST,0,0,12,1,0x101,0);read_fails=0;
    assert(s_nv0039_copies==good && s_nv0039_rejected==9);
    for(u32 i=0;i<128;i++)assert(local(DST)[i]==0xa5);
    for(u32 i=0;i<8;i++)assert(local(s_config.localSize-8)[i]==0xa5);
    g_resv_store_active=reserved=1;reservation_breaks=spu_notifications=0;
    check_copy(LOCAL,SRC,LOCAL,DST,0,0,16,1,0x101);
    assert(reservation_breaks>=16 && spu_notifications>=16);
    assert(s_nv0039_verify_errors==0);
    assert(methods==2 && mirrors==2 && legacy==2);
    puts("NV0039 FIFO copies, routing, mappings, overlap, strides, coherence and bounds passed");
    free(vm_base);return 0;
}
