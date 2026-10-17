/* NV3089 scaled-image blits through the FIFO walker's own 2D-engine methods:
 * IMAGE_IN source point decoding (12.4, centre or corner origin), the 1:1
 * row copy and the per-pixel copy agreeing, scaling, and the source formats
 * the walker accepts. No renderer is attached. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cellGcmSys.c"
#include <assert.h>
#include <stdlib.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t ea) { (void)ea; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_coh_notify_write(uint32_t a) { (void)a; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
ps3_guest_caller_fn g_ps3_guest_caller;
int rsx_draw_engine_enabled(void) { return 0; }
int rsx_draw_engine_guest_buffer_read(u32 l, u32 o, u32 b) { (void)l; (void)o; (void)b; return 1; }
void rsx_draw_engine_guest_buffer_wrote(u32 l, u32 o, u32 b) { (void)l; (void)o; (void)b; }
void rsx_draw_engine_guest_read(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
void rsx_draw_engine_guest_wrote(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
void rsx_draw_engine_guest_access(u32 l, u32 o, u32 b) { (void)l; (void)o; (void)b; }
int rsx_draw_engine_resolve_read(u32 sl, u32 so, u32 sp, u32 ds, u32 dt, u32 dl, u32 d, u32 dp, u32 w, u32 h)
{ (void)sl; (void)so; (void)sp; (void)ds; (void)dt; (void)dl; (void)d; (void)dp; (void)w; (void)h; abort(); }
int rsx_live_draw_enabled(void) { return 0; }
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
void rsx_draw_engine_method(u32 method, u32 arg) { (void)method; (void)arg; abort(); }
void rsx_draw_engine_note_flip(u32 buffer_id) { (void)buffer_id; abort(); }
void rsx_draw_engine_set_display_buffer(u32 b, u32 l, u32 o, u32 p, u32 w, u32 h)
{ (void)b; (void)l; (void)o; (void)p; (void)w; (void)h; abort(); }
void rsx_live_draw_method(u32 method, u32 arg) { (void)method; (void)arg; abort(); }
void rsx_live_draw_set_display_buffer(u32 b, u32 l, u32 o, u32 p, u32 w, u32 h)
{ (void)b; (void)l; (void)o; (void)p; (void)w; (void)h; abort(); }
int rsx_process_method(rsx_state* state, u32 method, u32 data) { (void)state; (void)method; (void)data; abort(); }
void rsx_raise_user_cmd(u32 arg) { (void)arg; abort(); }
void rsx_state_init(rsx_state* state) { (void)state; abort(); }

enum { SRC = 0x10000, SRC_PITCH = 256, DST = 0x40000, DST_PITCH = 128, CENTER = 1, CORNER = 2 };
static const u32 ONE = 0x100000u;   /* 1.0 in DS_DX / DT_DY */

/* Local memory, so offsets are guest addresses before cellGcmInit. */
static void destination(u32 surface_format)
{
    gcm_2d_method(3, 0x0188, 0xFEED0000u);
    gcm_2d_method(3, 0x0300, surface_format);
    gcm_2d_method(3, 0x0304, ((u32)DST_PITCH << 16) | SRC_PITCH);
    gcm_2d_method(3, 0x030C, DST);
}

static void blit(u32 format, u32 origin, u32 w, u32 h, u32 ds, u32 dt, u32 point)
{
    memset(vm_base + DST, 0, 64 * DST_PITCH);
    gcm_2d_method(6, 0x0184, 0xFEED0000u);
    gcm_2d_method(6, 0x0300, format);
    gcm_2d_method(6, 0x0304, 3);                     /* SRCCOPY */
    gcm_2d_method(6, 0x0308, 0);
    gcm_2d_method(6, 0x030C, (h << 16) | w);         /* clip */
    gcm_2d_method(6, 0x0310, 0);
    gcm_2d_method(6, 0x0314, (h << 16) | w);         /* out */
    gcm_2d_method(6, 0x0318, ds);
    gcm_2d_method(6, 0x031C, dt);
    gcm_2d_method(6, 0x0400, (32u << 16) | 64u);
    gcm_2d_method(6, 0x0404, (origin << 16) | SRC_PITCH);
    gcm_2d_method(6, 0x0408, SRC);
    gcm_2d_method(6, 0x040C, point);                 /* IMAGE_IN: starts the copy */
}

static u32 point(u32 x16, u32 y16) { return (y16 << 16) | x16; }
static u32 src32(u32 x, u32 y) { return 0xFF000000u | (y << 8) | x; }

/* Each destination pixel came from source pixel (x0 + i * step, y0 + j * step). */
static void check32(u32 w, u32 h, u32 x0, u32 y0, u32 step)
{
    for (u32 j = 0; j < h; j++)
        for (u32 i = 0; i < w; i++)
            assert(vm_read32(DST + j * DST_PITCH + i * 4) == src32(x0 + i * step, y0 + j * step));
}

static void both_paths(u32 format, u32 origin, u32 w, u32 h, u32 p, u32 x0, u32 y0)
{
    g_ww_lo = g_ww_hi = 0;                           /* 1:1 row copy */
    blit(format, origin, w, h, ONE, ONE, p); check32(w, h, x0, y0, 1);
    g_ww_lo = 0xFFFFFFF0u; g_ww_hi = 0xFFFFFFFFu;    /* a write watch keeps the per-pixel copy */
    blit(format, origin, w, h, ONE, ONE, p); check32(w, h, x0, y0, 1);
    g_ww_lo = g_ww_hi = 0;
}

int main(void)
{
    ppu_vm_size = 0x100000;
    vm_base = calloc(1, ppu_vm_size);
    assert(vm_base);
    for (u32 y = 0; y < 32; y++)
        for (u32 x = 0; x < 64; x++) vm_write32(SRC + y * SRC_PITCH + x * 4, src32(x, y));
    destination(0xA);                                /* A8R8G8B8 surface */

    /* The usual centred unit copy, and an origin point with no offset. */
    both_paths(3, CENTER, 8, 4, point(8, 8), 0, 0);
    both_paths(3, CORNER, 8, 4, 0, 0, 0);
    /* An integer start, and a centred one: both name pixel (10, 3). A centre
     * point of exactly 10.0 is half-way into pixel 9. */
    both_paths(3, CORNER, 8, 4, point(10 * 16, 3 * 16), 10, 3);
    both_paths(3, CENTER, 8, 4, point(10 * 16 + 8, 3 * 16 + 8), 10, 3);
    both_paths(3, CENTER, 8, 4, point(10 * 16, 3 * 16), 9, 2);
    /* A 2:1 reduction steps two source pixels per destination pixel from the start. */
    blit(3, CORNER, 8, 4, 2 * ONE, 2 * ONE, point(4 * 16, 2 * 16)); check32(8, 4, 4, 2, 2);
    /* X8R8G8B8 is copied like A8R8G8B8 rather than skipped. */
    both_paths(4, CORNER, 8, 4, point(5 * 16, 1 * 16), 5, 1);

    /* R5G6B5 into a 16-bit surface copies two bytes per pixel. */
    for (u32 y = 0; y < 32; y++)
        for (u32 x = 0; x < 64; x++) vm_write16(SRC + y * SRC_PITCH + x * 2, (u16)((y << 8) | x));
    destination(4);
    blit(7, CORNER, 8, 4, ONE, ONE, point(6 * 16, 2 * 16));
    for (u32 j = 0; j < 4; j++)
        for (u32 i = 0; i < 8; i++)
            assert(vm_read16(DST + j * DST_PITCH + i * 2) == (u16)(((2 + j) << 8) | (6 + i)));

    free(vm_base);
    puts("NV3089 source points, unit and scaled copies and source formats passed");
    return 0;
}
