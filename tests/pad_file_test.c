#include "cellPad.h"
#include "spu_dma.h"
#include <assert.h>

static uint16_t read_button(unsigned index) {
    const uint8_t* p=vm_base+0x10004+index*2;
    return (uint16_t)((uint16_t)p[0]<<8|p[1]);
}
static void request(const char* path,const char* line) {
    FILE* file=fopen(path,"wb");assert(file);
    assert(fputs(line,file)>=0);assert(fclose(file)==0);
}
static void sample(unsigned mask,unsigned lx,unsigned ly,unsigned rx,unsigned ry) {
    assert(cellPadGetData(0,(CellPadData*)(uintptr_t)0x10000)==0);
    assert(read_button(CELL_PAD_BTN_OFFSET_DIGITAL1)==(mask&255));
    assert(read_button(CELL_PAD_BTN_OFFSET_DIGITAL2)==(mask>>8));
    assert(read_button(CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X)==lx);
    assert(read_button(CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y)==ly);
    assert(read_button(CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X)==rx);
    assert(read_button(CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y)==ry);
}
int wkc2_test_pad_file(const char* path) {
    assert(!vm_base);
    _putenv_s("PAD_FILE",path);_putenv_s("PAD_NO_KEYBOARD","1");
    _putenv_s("PAD_STICK","128,128,128,128");
    vm_base=VirtualAlloc(NULL,(SIZE_T)UINT64_C(0x100000000),MEM_RESERVE,PAGE_READWRITE);assert(vm_base);
    assert(VirtualAlloc(vm_base+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE));
    assert(cellPadInit(1)==0);
    request(path,"0x4000 3 0 255 64 192");
    for(unsigned i=0;i<3;++i) sample(0x4000,0,255,64,192);
    sample(0,128,128,128,128);
    request(path,"0 2 255 0 192 64");
    for(unsigned i=0;i<2;++i) sample(0,255,0,192,64);
    sample(0,128,128,128,128);
    request(path,"0x0040 1");sample(0x0040,128,128,128,128);
    sample(0,128,128,128,128);
    request(path,"0x0040 1 256 128 128 128");sample(0,128,128,128,128);
    request(path,"0x0040 1 0 128");sample(0,128,128,128,128);
    request(path,"");sample(0,128,128,128,128);
    assert(cellPadEnd()==0);
    assert(remove(path)==0);assert(VirtualFree(vm_base,0,MEM_RELEASE));vm_base=NULL;
    puts("Pad file digital compatibility, stick-only input, expiry and invalid ranges passed");
    return 0;
}
