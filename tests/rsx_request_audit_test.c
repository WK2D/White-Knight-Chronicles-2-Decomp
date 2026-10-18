#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_request_audit.h"
#include <assert.h>
#include <stdlib.h>
int main(int argc,char** argv)
{
    assert(argc==2);
    rsx_dispatch* r=(rsx_dispatch*)calloc(1,sizeof *r);assert(r);
    rsx_dispatch_init(r,NULL);
    rsx_request_audit a;assert(!rsx_ra_init(&a,argv[1],RSX_RA_CAP_FILL_OFFSET));
#define SET(m,v) rsx_dispatch_method(r,m,v)
    SET(0x1E98,0x01000000); /* Always, not conditional. */
    SET(0x1A04,0x30);SET(0x1A08,0x00F00000);SET(0x1A0C,4);
    rsx_ra_draw(&a,r,5,1,1);
    assert(!a.events[RSX_RA_TEXTURE_GAMMA] && !a.events[RSX_RA_TEXTURE_ALPHA_KILL]);
    SET(0x1A0C,0x80000004);rsx_ra_draw(&a,r,5,0,1);
    assert(!a.events[RSX_RA_TEXTURE_GAMMA]); /* Enabled but unused. */
    rsx_ra_draw(&a,r,5,1,1);
    assert(a.events[RSX_RA_TEXTURE_GAMMA]==1 && a.events[RSX_RA_TEXTURE_ALPHA_KILL]==1);
    assert(a.events[RSX_RA_TEXTURE_3D]==1 && !a.events[RSX_RA_CONDITIONAL]);
    SET(0x1E98,0x02000010);SET(0x0B40,1);SET(0x0A68,1);SET(0x0A7C,0xC2C80000);
    SET(0x0324,0x00010000);SET(0x0370,0x20); /* R only on A and B. */
    rsx_ra_draw(&a,r,5,0,2);
    assert(a.events[RSX_RA_CONDITIONAL]==1 && a.events[RSX_RA_TEXCOORD_2D]==1);
    assert(a.events[RSX_RA_FILL_OFFSET]==1);
    assert(!strcmp(rsx_ra_status(&a,RSX_RA_FILL_OFFSET),"implemented"));
    assert(!a.events[RSX_RA_MRT_MASK]);
    SET(0x0370,0);rsx_ra_draw(&a,r,5,0,2);assert(a.events[RSX_RA_MRT_MASK]==1);
    SET(0x0220,1);SET(0x0208,8);SET(0x1FEC,1);SET(0x1D60,0x40);
    rsx_ra_draw(&a,r,5,0,1);assert(!a.events[RSX_RA_FRAMEBUFFER_SRGB]);
    SET(0x1D60,0);
    for(unsigned fmt=11;fmt<=13;fmt++) {
        SET(0x0208,fmt);rsx_ra_draw(&a,r,5,0,1);
        assert(!a.events[RSX_RA_FRAMEBUFFER_SRGB]);
    }
    SET(0x0208,8);SET(0x1FEC,0);rsx_ra_draw(&a,r,5,0,1);
    assert(!a.events[RSX_RA_FRAMEBUFFER_SRGB]);
    SET(0x1FEC,1);SET(0x0220,0);rsx_ra_draw(&a,r,5,0,1);
    assert(!a.events[RSX_RA_FRAMEBUFFER_SRGB]);
    SET(0x0220,1);SET(0x0324,0x01000000);rsx_ra_draw(&a,r,5,0,1);
    assert(!a.events[RSX_RA_FRAMEBUFFER_SRGB]); /* Alpha-only output is linear. */
    SET(0x0324,0x00010000);rsx_ra_draw(&a,r,5,0,1);
    assert(a.events[RSX_RA_FRAMEBUFFER_SRGB]==1);
    SET(0x1D60,0x4E);SET(0x0A74,0);rsx_ra_draw(&a,r,5,0,1);
    assert(!a.events[RSX_RA_DEPTH_EXPORT]);
    SET(0x0A74,1);SET(0x0A70,0);rsx_ra_draw(&a,r,5,0,1);
    assert(a.events[RSX_RA_DEPTH_EXPORT]==1); /* Can affect tests without writes. */
    assert(!a.events[RSX_RA_WINDOW_OFFSET]);
    SET(0x02B8,0x00400080);rsx_ra_draw(&a,r,5,0,1);
    assert(a.events[RSX_RA_WINDOW_OFFSET]==1);
    SET(0x02B8,0);rsx_ra_draw(&a,r,5,0,1);
    assert(a.events[RSX_RA_WINDOW_OFFSET]==1);
    rsx_ra_program(&a,"TODO: unhandled FP opcode",1);
    assert(a.events[RSX_RA_FP_OPCODE]==1 && a.events[RSX_RA_PIPELINE_FAILED]==1);
    SET(0x1800,0x01000010); /* Report and upload values must not leak into TSV. */
    SET(0x0B80,0xDEADBEEF);SET(0x1A00,0x1234ABCD);
    rsx_ra_flush(&a,r,1);assert(a.events[RSX_RA_REPORTS]==1);
    FILE* f=fopen(argv[1],"rb");assert(f);char text[32768];
    size_t n=fread(text,1,sizeof text-1,f);fclose(f);text[n]=0;
    assert(strstr(text,"texcoord_2d_override\tmissing\t"));
    assert(strstr(text,"fill_polygon_offset\timplemented\t"));
    assert(strstr(text,"method\t0x0B80\twrite_count\t"));
    assert(!strstr(text,"DEADBEEF") && !strstr(text,"1234ABCD"));
    /* No report data exists on a clean reinitialization. */
    assert(!rsx_ra_init(&a,argv[1],RSX_RA_CAP_TEXCOORD_2D));assert(!a.attempted_draws);
    assert(!strcmp(rsx_ra_status(&a,RSX_RA_TEXCOORD_2D),"implemented"));
    rsx_ra_draw(NULL,r,5,1,1);rsx_ra_program(NULL,"bad",1);rsx_ra_flush(NULL,r,1);
    remove(argv[1]);free(r);puts("Feature gating, counts, status and no-data report pass.");return 0;
}
