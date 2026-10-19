#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_present_metrics.h"
#include "rsx_fps_bitmap.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    rsx_present_metrics m={0};
    assert(rsx_present_metrics_fps(&m,0)==0.0);
    rsx_present_metrics_record(&m,0,0); /* Zero is a valid clock origin. */
    for(unsigned i=1;i<=100;i++) rsx_present_metrics_record(&m,i*10,0);
    assert(m.started && rsx_present_metrics_fps(&m,1000)==100.0);
    assert(m.published_maximum_ms==10);
    const rsx_present_metrics accepted=m;
    rsx_present_metrics_record(&m,1500,0x087A0001); /* DXGI_STATUS_OCCLUDED */
    rsx_present_metrics_record(&m,2000,(int32_t)0x887A0005u);
    assert(memcmp(&m,&accepted,sizeof m)==0);
    assert(rsx_present_metrics_fps(&m,3001)==0.0);
    rsx_present_metrics_record(&m,4000,0);
    assert(fabs(rsx_present_metrics_fps(&m,4000)-1.0/3.0)<1e-12);
    assert(m.published_maximum_ms==3000);
    rsx_present_metrics_record(&m,1,0); /* Clock reset cannot underflow. */
    assert(m.last_ms==1 && m.fps==0 && m.intervals==0);
    assert(rsx_present_metrics_fps(&m,0)==0.0);
    for(unsigned i=1;i<=30;i++) rsx_present_metrics_record(&m,1+i*34,0);
    assert(fabs(m.fps-1000.0/34.0)<1e-12);

    enum {bytes=RSX_FPS_BITMAP_PITCH*RSX_FPS_BITMAP_HEIGHT};
    uint8_t guarded[bytes+32],zero[bytes],sanitized[bytes];
    memset(guarded,0xA5,sizeof guarded);
    rsx_fps_bitmap(guarded+16,29.9);
    for(unsigned i=0;i<16;i++) assert(guarded[i]==0xA5 && guarded[bytes+16+i]==0xA5);
    const uint8_t* first=guarded+16;
    /* Top F row: ten foreground pixels, then the two-pixel character gap. */
    for(unsigned x=0;x<10;x++) assert(first[x*4]==240 && first[x*4+3]==255);
    assert(first[10*4+3]==0 && first[11*4+3]==0);
    unsigned lit=0;
    for(unsigned i=0;i<bytes;i+=4) {
        if(first[i+3]) {assert(first[i]==240 && first[i+1]==240 && first[i+2]==240 && first[i+3]==255);++lit;}
        else assert(first[i]==0 && first[i+1]==0 && first[i+2]==0);
    }
    assert(lit>300 && lit<1000);
    rsx_fps_bitmap(zero,0.0);
    rsx_fps_bitmap(sanitized,-20.0);assert(memcmp(zero,sanitized,bytes)==0);
    rsx_fps_bitmap(sanitized,NAN);assert(memcmp(zero,sanitized,bytes)==0);
    rsx_fps_bitmap(guarded+16,DBL_MAX);
    rsx_fps_bitmap(sanitized,9999.9);assert(memcmp(guarded+16,sanitized,bytes)==0);
    for(unsigned i=0;i<16;i++) assert(guarded[i]==0xA5 && guarded[bytes+16+i]==0xA5);
    puts("Accepted-present timing, occlusion/errors, stalls, clock reset and bounded transparent glyphs pass.");
    return 0;
}
