#ifdef NDEBUG
#undef NDEBUG
#endif
#include "pad_stick.h"
#include <assert.h>
#include <stdio.h>
static float axis(uint8_t value) { int d=(int)value-128;return d/(d<0?128.0f:127.0f); }
static float radius(uint8_t x,uint8_t y) { return sqrtf(axis(x)*axis(x)+axis(y)*axis(y)); }
int main(void) {
    uint8_t x,y;
    for(unsigned zone=7849;zone<=8689;zone+=840) {
        pad_stick_radial(0,0,zone,&x,&y);assert(x==128 && y==128);
        pad_stick_radial(300,-400,zone,&x,&y);assert(x==128 && y==128);
        pad_stick_radial(32767,0,zone,&x,&y);assert(x==255 && y==128);
        pad_stick_radial(-32768,0,zone,&x,&y);assert(x==0 && y==128);
        pad_stick_radial(0,32767,zone,&x,&y);assert(x==128 && y==0);
        pad_stick_radial(0,-32768,zone,&x,&y);assert(x==128 && y==255);
        /* Every full-tilt direction must reach the same run magnitude. */
        for(unsigned degrees=0;degrees<360;degrees++) {
            float angle=degrees*0.017453292519943295f;
            int16_t rx=(int16_t)lroundf(32767.0f*cosf(angle));
            int16_t ry=(int16_t)lroundf(32767.0f*sinf(angle));
            pad_stick_radial(rx,ry,zone,&x,&y);
            float r=radius(x,y);assert(r>0.985f && r<1.015f);
        }
        /* Gentle tilts stay below full speed and increase monotonically. */
        float previous=0;
        for(int raw=0;raw<=32767;raw+=31) {
            pad_stick_radial((int16_t)raw,0,zone,&x,&y);
            float r=radius(x,y);assert(r>=previous && r<=1.0f);previous=r;
        }
        pad_stick_radial(12000,0,zone,&x,&y);float gentle=radius(x,y);
        assert(gentle>0.20f && gentle<0.35f);
        pad_stick_radial(8485,8485,zone,&x,&y);
        assert(fabsf(radius(x,y)-gentle)<0.015f);
        pad_stick_radial(32767,32767,zone,&x,&y);assert(radius(x,y)>0.985f && radius(x,y)<1.015f);
    }
    puts("Neutral, endpoint/Y direction, 720 full-tilt angles and progressive partial tilts pass.");
}
