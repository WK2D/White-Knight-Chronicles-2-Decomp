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
void rsx_draw_engine_note_flip(u32 buffer_id) { (void)buffer_id; }
void rsx_draw_engine_set_display_buffer(u32 b, u32 l, u32 o, u32 p, u32 w, u32 h)
{ (void)b; (void)l; (void)o; (void)p; (void)w; (void)h; abort(); }
void rsx_live_draw_method(u32 method, u32 arg) { (void)method; (void)arg; ++legacy; }
void rsx_live_draw_set_display_buffer(u32 b, u32 l, u32 o, u32 p, u32 w, u32 h)
{ (void)b; (void)l; (void)o; (void)p; (void)w; (void)h; }
int rsx_process_method(rsx_state* state, u32 method, u32 data) { (void)state; (void)method; (void)data; ++methods; return 0; }
static unsigned lle_user_calls;
static u32 lle_user_arg;
void rsx_raise_user_cmd(u32 arg) { ++lle_user_calls; lle_user_arg=arg; }
void rsx_state_init(rsx_state* state) { memset(state, 0, sizeof(*state)); }



static void test_after_arm(uint32_t ea) { (void)ea; }
enum { FIFO_EA=0x400000, LABEL=0x49 };
static unsigned flips, blanks, since_flip, user_calls;
static u32 user_arg;
static void guest(uint32_t opd, uint64_t a, uint64_t b, uint64_t c,
                  uint64_t d, uint64_t e, uint64_t f, uint64_t g0, uint64_t h) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g0;(void)h;
    if(opd==0x1000) { ++flips; since_flip=0; }
    else if(opd==0x1020) {
        ++blanks;
        if(++since_flip>=2) vm_write32(GCM_LABEL_GUEST_BASE+LABEL*16,0);
    } else if(opd==0x1040) { ++user_calls; user_arg=(u32)a; }
    else abort();
}
static void tick(void) { cellGcmTickVBlank();cellGcmTickFlip();ppu_gcm_pump(); }
static void fifo(u32 bytes,u32 get) {
    cellGcm_syscall_set_fifo(bytes,get);
    gcm_forget_spu_blocks();
}
static void marker(u32 offset) { vm_write32(FIFO_EA+offset,GCM_FLIP_MARKER); }
int main(void) {
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _putenv_s("GCM_FLIPCB_ONTICK","1");_putenv_s("GCM_SEMA_ACQUIRE","1");
    ppu_vm_size=16u<<20;vm_base=calloc(1,ppu_vm_size);assert(vm_base);
    memset(s_ea_address_table,0xff,sizeof s_ea_address_table);
    s_ea_address_table[0]=FIFO_EA>>20;
    s_io_mappings[0].ea=FIFO_EA;s_io_mappings[0].io=0;s_io_mappings[0].size=1u<<20;s_io_mappings[0].active=1;
    s_io_mapping_count=1;ppu_hle_inject_base=6u<<20;s_gcm_syscall_mode=1;
    s_display_buffer_set[0]=1;
    g_ps3_guest_caller=guest;
    s_flip_handler_opd=0x1000;s_vblank_handler_opd=0x1020;
    /* A reset is a polling-state change, not a request or display interrupt. */
    cellGcmResetFlipStatus();
    tick();tick();assert(flips==0&&blanks==2);
    assert(cellGcmGetFlipStatus()==CELL_GCM_FLIP_STATUS_WAITING);
    /* Queued but unconsumed work must not complete on a timer tick. */
    marker(0x100);fifo(0x104,0x100);
    tick();assert(flips==0);
    cellGcm_rsx_process_fifo();assert(cellGcm_take_flip_pending()==1);
    cellGcmResetFlipStatus(); /* must not erase the real outstanding request */
    tick();assert(flips==1&&cellGcmGetFlipStatus()==CELL_GCM_FLIP_STATUS_DONE);
    tick();tick();assert(flips==1);
    /* Immediate HLE request also completes exactly once. */
    assert(cellGcmSetFlipCommand(0)==CELL_OK);assert(flips==1);
    tick();assert(flips==2);tick();assert(flips==2);
    assert(cellGcm_take_flip_pending()==1);
    /* Real ACQUIRE before FLIP: reset status cannot manufacture a completion
     * that resets the guest's two-vblank counter and prevents label release. */
    since_flip=0;vm_write32(GCM_LABEL_GUEST_BASE+LABEL*16,1);
    vm_write32(FIFO_EA+0x200,(1u<<18)|0x64u);vm_write32(FIFO_EA+0x204,LABEL*16);
    vm_write32(FIFO_EA+0x208,(1u<<18)|0x68u);vm_write32(FIFO_EA+0x20c,0);
    marker(0x210);fifo(0x214,0x200);
    cellGcmResetFlipStatus();cellGcm_rsx_process_fifo();assert(s_fifo_getoff==0x208);
    tick();cellGcm_rsx_process_fifo();assert(s_fifo_getoff==0x208&&flips==2);
    tick();cellGcm_rsx_process_fifo();assert(s_fifo_getoff==0x214&&flips==2);
    assert(cellGcm_take_flip_pending()==1);tick();assert(flips==3);
    tick();assert(flips==3);
    /* Combined legacy tick uses the same eligibility, including resets. */
    cellGcmResetFlipStatus();cellGcm_request_tick();ppu_gcm_pump();assert(flips==3);
    assert(cellGcmSetFlipCommand(0)==CELL_OK);
    cellGcm_request_tick();ppu_gcm_pump();assert(flips==4);
    cellGcm_request_tick();ppu_gcm_pump();assert(flips==4);
    /* Real user-interrupt FIFO words must reach the HLE callback, including
     * cause zero. Merely testing QueueUserCommand bypasses the faulty walker. */
    cellGcmSetUserHandler((CellGcmUserHandler)(uintptr_t)0x1040);
    vm_write32(FIFO_EA+0x300,(1u<<18)|0xEB00u);
    vm_write32(FIFO_EA+0x304,0);
    fifo(0x308,0x300);cellGcm_rsx_process_fifo();
    assert(user_calls==0&&lle_user_calls==0); /* deferred, one delivery path */
    ppu_gcm_pump();assert(user_calls==1&&user_arg==0);
    ppu_gcm_pump();assert(user_calls==1);
    vm_write32(FIFO_EA+0x310,(1u<<18)|0xEB04u);
    vm_write32(FIFO_EA+0x314,0x1234);
    fifo(0x318,0x310);cellGcm_rsx_process_fifo();
    assert(user_calls==1&&lle_user_calls==0);
    ppu_gcm_pump();assert(user_calls==2&&user_arg==0x1234);
    cellGcmSetUserHandler(NULL);
    fifo(0x308,0x300);cellGcm_rsx_process_fifo();
    assert(lle_user_calls==1&&lle_user_arg==0);
    ppu_gcm_pump();assert(user_calls==2);
    cellGcmTerminate();cellGcmTickFlip();ppu_gcm_pump();assert(flips==4);
    free(vm_base);puts("Real flip completion and two-vblank acquire passed");return 0;
}
