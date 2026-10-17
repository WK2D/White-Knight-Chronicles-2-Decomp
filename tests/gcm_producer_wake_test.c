/* Synthetic producer handoffs through the production FIFO walker. No game data. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <stdint.h>
static void test_after_arm(uint32_t ea);
#define GCM_PRODUCER_TEST_AFTER_ARM(ea) test_after_arm(ea)
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


enum { FIFO_EA=0x400000u, PARK=0x1000u, NEXT=0x1100u,
       LABEL_INDEX=0x48u, COMPLETED=0x12345678u };
static HANDLE producer_ready,producer_done;
static atomic_int race_after_arm;
static int profile_on;

static u32 label_ea(void) { return GCM_LABEL_GUEST_BASE+LABEL_INDEX*GCM_LABEL_STRIDE; }
static void after_put(u32 ea,u32 size) { gcm_note_spu_put(ea,size); }
/* Production's ordinary DMA hook runs after the exact copy and reservation
 * notification. This synthetic completed PUT copies a big-endian command and
 * invokes that same production notification; no guest code/assets are used. */
static void complete_jump_put(void) {
    const u32 jump=0x20000000u|NEXT;
    const u8 bytes[4]={(u8)(jump>>24),(u8)(jump>>16),(u8)(jump>>8),(u8)jump};
    memcpy(vm_base+FIFO_EA+PARK,bytes,sizeof bytes);
    after_put(FIFO_EA+PARK,sizeof bytes);
}
static DWORD WINAPI producer(LPVOID ignored) {
    (void)ignored;
    assert(WaitForSingleObject(producer_ready,5000)==WAIT_OBJECT_0);
    complete_jump_put();
    assert(SetEvent(producer_done));
    return 0;
}
static void test_after_arm(uint32_t ea) {
    assert(ea==FIFO_EA+PARK);
    if(atomic_exchange(&race_after_arm,0)) {
        assert(SetEvent(producer_ready));
        assert(WaitForSingleObject(producer_done,5000)==WAIT_OBJECT_0);
    }
}
static DWORD WINAPI initialize_event(LPVOID result) {
    *(HANDLE*)result=(HANDLE)cellGcm_fifo_kick_event();return 0;
}
static void clear_event(void) {
    HANDLE event=(HANDLE)cellGcm_fifo_kick_event();
    if(event) while(WaitForSingleObject(event,0)==WAIT_OBJECT_0) {}
}
static void setup_fifo(void) {
    cellGcm_syscall_set_fifo(NEXT+16,PARK);
    gcm_forget_spu_blocks();
    clear_event();
    vm_write32(FIFO_EA+PARK,0x20000000u|PARK);
    vm_write32(FIFO_EA+NEXT,(1u<<18)|0x64u);
    vm_write32(FIFO_EA+NEXT+4,LABEL_INDEX*GCM_LABEL_STRIDE);
    vm_write32(FIFO_EA+NEXT+8,(1u<<18)|0x6cu);
    vm_write32(FIFO_EA+NEXT+12,COMPLETED);
    vm_write32(label_ea(),0);
}
static void parked(void) {
    cellGcm_rsx_process_fifo();
    assert(s_fifo_getoff==PARK);
    assert(vm_read32(GCM_CONTROL_GUEST_ADDR+4)==PARK);
    assert(vm_read32(label_ea())==0);
}
static void drained(void) {
    cellGcm_rsx_process_fifo();
    assert(s_fifo_getoff==NEXT+16);
    assert(vm_read32(GCM_CONTROL_GUEST_ADDR+4)==NEXT+16);
    assert(vm_read32(label_ea())==COMPLETED);
    assert(atomic_load(&s_gcm_producer_park_descriptor)==0);
}
static void check_no_wake(u32 ea,u32 size) {
    HANDLE event=(HANDLE)cellGcm_fifo_kick_event();
    const unsigned long long count=atomic_load(&g_gcm_producer_overlap_wake_count);
    after_put(ea,size);
    assert(WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    assert(atomic_load(&g_gcm_producer_overlap_wake_count)==count);
}
static void check_wake(u32 ea,u32 size) {
    HANDLE event=(HANDLE)cellGcm_fifo_kick_event();
    const unsigned long long count=atomic_load(&g_gcm_producer_overlap_wake_count);
    after_put(ea,size);
    assert(WaitForSingleObject(event,1000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(event,0)==WAIT_TIMEOUT); /* auto-reset */
    assert(atomic_load(&g_gcm_producer_overlap_wake_count)==count+(profile_on?1:0));
}
int main(int argc,char** argv) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif
    const int disabled=argc>1&&!strcmp(argv[1],"disabled");
    profile_on=getenv("GCM_PRODUCER_PROFILE")!=NULL;
    ppu_vm_size=16u<<20;vm_base=calloc(1,ppu_vm_size);assert(vm_base);
    s_config.localAddress=8u<<20;s_config.localSize=4u<<20;
    memset(s_ea_address_table,0xff,sizeof s_ea_address_table);
    s_ea_address_table[0]=FIFO_EA>>20;
    s_io_mappings[0].ea=FIFO_EA;s_io_mappings[0].io=0;
    s_io_mappings[0].size=1u<<20;s_io_mappings[0].active=1;
    s_io_mapping_count=1;ppu_hle_inject_base=6u<<20;s_gcm_syscall_mode=1;

    /* All simultaneous first accessors get one initialized auto-reset event,
     * or all get NULL when the existing launch option disables it. */
    HANDLE threads[12],events[12]={0};
    for(unsigned i=0;i<12;i++) {
        threads[i]=CreateThread(NULL,0,initialize_event,&events[i],0,NULL);assert(threads[i]);
    }
    for(unsigned i=0;i<12;i++) {
        assert(WaitForSingleObject(threads[i],5000)==WAIT_OBJECT_0);
        assert(CloseHandle(threads[i]));assert(events[i]==events[0]);
    }
    assert((events[0]==NULL)==disabled);
    setup_fifo();parked();
    if(disabled) {
        assert(!atomic_load(&s_gcm_producer_park_descriptor));
        cellGcm_fifo_kick_wait(1);
        complete_jump_put();drained();
        assert(!atomic_load(&g_gcm_producer_park_count));
        assert(!atomic_load(&g_gcm_producer_overlap_wake_count));
        assert(!atomic_load(&g_gcm_producer_retry_count));
        assert(!atomic_load(&g_gcm_producer_unchanged_overlap_count));
        puts("Disabled kick preserves FIFO park, bounded-wait fallback and real RELEASE");
        free(vm_base);return 0;
    }

    /* Neighboring, empty, unmapped and wrapped-address DMA cannot wake us. */
    assert(atomic_load(&s_gcm_producer_park_descriptor)==
           ((u64)(0x20000000u|PARK)<<32 | (FIFO_EA+PARK)));
    check_no_wake(FIFO_EA+PARK-4,4);
    check_no_wake(FIFO_EA+PARK+4,4);
    check_no_wake(FIFO_EA+PARK,0);
    check_no_wake(0x10000,4);
    check_no_wake(0xfffffff0u,0x40);
    /* Completed overlap that preserves the self-JUMP must not wake. */
    const unsigned long long unchanged_before=atomic_load(&g_gcm_producer_unchanged_overlap_count);
    check_no_wake(FIFO_EA+PARK-1,2);
    check_no_wake(FIFO_EA+PARK+3,1);
    check_no_wake(FIFO_EA+PARK,4);
    assert(atomic_load(&g_gcm_producer_unchanged_overlap_count)==unchanged_before+(profile_on?3:0));
    assert(vm_read32(label_ea())==0&&s_fifo_getoff==PARK);

    /* A changed first or last byte still wakes, even for partial overlaps.
     * Notification itself consumes nothing and writes no completion label. */
    vm_write8(FIFO_EA+PARK,0x21);
    check_wake(FIFO_EA+PARK-1,2);
    assert(vm_read32(label_ea())==0&&s_fifo_getoff==PARK);
    setup_fifo();parked();
    vm_write8(FIFO_EA+PARK+3,1);
    check_wake(FIFO_EA+PARK+3,1);
    assert(atomic_load(&g_gcm_producer_unchanged_overlap_count)==unchanged_before+(profile_on?3:0));
    assert(vm_read32(label_ea())==0&&s_fifo_getoff==PARK);

    /* An unchanged coalesced PUT stays quiet; an actual replacement COPY
     * through that same early-return path must signal and then drain. */
    setup_fifo();parked();
    after_put(FIFO_EA+PARK-16,16);
    check_no_wake(FIFO_EA+PARK,4);
    assert(s_spu_blk_w==1);
    assert(atomic_load(&g_gcm_producer_unchanged_overlap_count)==unchanged_before+(profile_on?4:0));
    gcm_forget_spu_blocks();parked();
    after_put(FIFO_EA+PARK-16,16);
    const unsigned long long coalesced_before=atomic_load(&g_gcm_producer_overlap_wake_count);
    complete_jump_put();
    assert(WaitForSingleObject(events[0],1000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(events[0],0)==WAIT_TIMEOUT);
    assert(atomic_load(&g_gcm_producer_overlap_wake_count)==coalesced_before+(profile_on?1:0));
    assert(s_spu_blk_w==1&&s_fifo_getoff==PARK&&vm_read32(label_ea())==0);
    drained();
    assert(atomic_load(&g_gcm_producer_unchanged_overlap_count)==unchanged_before+(profile_on?4:0));
    setup_fifo();parked();

    /* A real producer thread patches the command after the walk parked.
     * Before another drain, GET/label remain unchanged despite the wake. */
    producer_ready=CreateEventA(NULL,FALSE,FALSE,NULL);assert(producer_ready);
    producer_done=CreateEventA(NULL,FALSE,FALSE,NULL);assert(producer_done);
    HANDLE worker=CreateThread(NULL,0,producer,NULL,0,NULL);assert(worker);
    assert(SetEvent(producer_ready));
    assert(WaitForSingleObject(events[0],5000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(producer_done,5000)==WAIT_OBJECT_0);
    assert(s_fifo_getoff==PARK&&vm_read32(label_ea())==0);
    drained();assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);

    /* A completed write immediately before registration must be detected by
     * the arm's reread even though the notification had no waiter to wake. */
    setup_fifo();
    const u32 original_word=vm_read32(FIFO_EA+PARK);
    complete_jump_put();
    assert(WaitForSingleObject(events[0],0)==WAIT_TIMEOUT);
    assert(gcm_producer_arm(FIFO_EA+PARK,original_word)==0);
    assert(!atomic_load(&s_gcm_producer_park_descriptor));drained();

    /* Force producer completion between arm and reread inside the actual
     * production FIFO walk. It must retry now rather than wait for a future
     * DMA that may never arrive. */
    setup_fifo();atomic_store(&race_after_arm,1);
    const unsigned long long retries=atomic_load(&g_gcm_producer_retry_count);
    worker=CreateThread(NULL,0,producer,NULL,0,NULL);assert(worker);
    drained();
    assert(atomic_load(&g_gcm_producer_retry_count)==retries+(profile_on?1:0));
    assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);clear_event();

    /* A FIFO reset and termination must not retain an obsolete DMA watcher. */
    setup_fifo();parked();cellGcm_syscall_set_fifo(NEXT+16,NEXT+16);
    assert(!atomic_load(&s_gcm_producer_park_descriptor));
    check_no_wake(FIFO_EA+PARK,4);
    setup_fifo();parked();cellGcmTerminate();
    assert(!atomic_load(&s_gcm_producer_park_descriptor));
    check_no_wake(FIFO_EA+PARK,4);
    if(!profile_on) {
        assert(!atomic_load(&g_gcm_producer_park_count));
        assert(!atomic_load(&g_gcm_producer_overlap_wake_count));
        assert(!atomic_load(&g_gcm_producer_retry_count));
        assert(!atomic_load(&g_gcm_producer_unchanged_overlap_count));
    }
    printf("Producer FIFO wake: %llu parks, %llu overlap wakes, %llu immediate retries\n",
        atomic_load(&g_gcm_producer_park_count),atomic_load(&g_gcm_producer_overlap_wake_count),
        atomic_load(&g_gcm_producer_retry_count));
    printf("Unchanged producer overlaps: %llu\n",
        atomic_load(&g_gcm_producer_unchanged_overlap_count));
    CloseHandle(producer_ready);CloseHandle(producer_done);free(vm_base);return 0;
}
