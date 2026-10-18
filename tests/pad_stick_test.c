#ifdef NDEBUG
#undef NDEBUG
#endif
#include "pad_stick.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
        /* The PS3-shaped perimeter has up to 25% more diagonal range.
         * A 95%-tilt rim trace must reach that perimeter without dropouts. */
        for(unsigned degrees=0;degrees<360;degrees++) {
            float angle=degrees*0.017453292519943295f;
            int16_t rx=(int16_t)lroundf(31200.0f*cosf(angle));
            int16_t ry=(int16_t)lroundf(31200.0f*sinf(angle));
            pad_stick_radial(rx,ry,zone,&x,&y);
            float expected=1+0.25f*sinf(2*angle)*sinf(2*angle);
            float ex=fmaxf(-1,fminf(1,cosf(angle)*expected));
            float ey=fmaxf(-1,fminf(1,-sinf(angle)*expected));
            assert(fabsf(axis(x)-ex)<0.009f && fabsf(axis(y)-ey)<0.009f);
            assert(radius(x,y)>0.985f);
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
        assert(fabsf(radius(x,y)-gentle*1.25f)<0.015f);
        pad_stick_radial(32767,32767,zone,&x,&y);assert(radius(x,y)>1.235f && radius(x,y)<1.265f);
        /* Partial-tilt sweeps remain continuous: no axial deadzone, snap to
         * neutral or sudden walk/run change when crossing a diagonal. */
        for(int raw=4000;raw<=27000;raw+=1000) {
            float previous_radius=0,first=0;
            uint8_t previous_x=128,previous_y=128;
            for(unsigned degrees=0;degrees<=360;degrees++) {
                float angle=degrees*0.017453292519943295f;
                pad_stick_radial((int16_t)lroundf(raw*cosf(angle)),
                                 (int16_t)lroundf(raw*sinf(angle)),zone,&x,&y);
                float r=radius(x,y);assert(r>0.025f);
                if(degrees) {
                    assert(abs((int)x-previous_x)<=4 && abs((int)y-previous_y)<=4);
                    assert(fabsf(r-previous_radius)<0.025f);
                } else first=r;
                if(degrees==360) assert(fabsf(r-first)<0.01f);
                previous_x=x;previous_y=y;previous_radius=r;
            }
        }
    }
    puts("Neutral/endpoints, PS3 diagonal range, imperfect rim and continuous partial-tilt sweeps pass.");
}
