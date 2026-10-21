#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <assert.h>
/* Compile the actual API with a driver whose in-progress poll is controllable. */
static DWORD WINAPI report_get_state(DWORD port,XINPUT_STATE* state);
static BOOL WINAPI report_counter(LARGE_INTEGER* now);
static BOOL WINAPI report_frequency(LARGE_INTEGER* frequency);
#define XInputGetState report_get_state
#define QueryPerformanceCounter report_counter
#define QueryPerformanceFrequency report_frequency
#include "cellPad.c"
#undef XInputGetState
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency

uint8_t* vm_base;
/* No SPUs or diagnostic watches exist in this isolated pad/VM test. */
uint32_t g_ww_lo,g_ww_hi;
int g_resv_store_active;
void ps3_ww_report_inline(uint32_t a,uint64_t v,int w) {(void)a;(void)v;(void)w;assert(0);}
int spu_coh_is_reserved(uint32_t a) {(void)a;return 0;}
void spu_lockline_lock(void) {assert(0);}
void spu_lockline_unlock(void) {assert(0);}
void spu_coh_notify_write(uint32_t a) {(void)a;assert(0);}
void ppu_resv_break_store(uint64_t a) {(void)a;assert(0);}
static HANDLE entered,release_poll,read_done,read_ready;
static volatile LONG block_poll;
static ULONGLONG test_now=100; /* milliseconds on the pad's poll clock */
static BOOL WINAPI report_counter(LARGE_INTEGER* now) {now->QuadPart=(LONGLONG)test_now*1000;return TRUE;}
static BOOL WINAPI report_frequency(LARGE_INTEGER* frequency) {frequency->QuadPart=1000000;return TRUE;}
static DWORD WINAPI report_get_state(DWORD port,XINPUT_STATE* state) {
    if(port) return ERROR_DEVICE_NOT_CONNECTED;
    memset(state,0,sizeof *state);
    if(InterlockedCompareExchange(&block_poll,0,0)) {
        SetEvent(entered);assert(WaitForSingleObject(release_poll,5000)==WAIT_OBJECT_0);
        state->Gamepad.wButtons=XINPUT_GAMEPAD_A;
        state->Gamepad.sThumbLX=32767;state->Gamepad.sThumbLY=-32768;
        state->Gamepad.sThumbRX=-32768;state->Gamepad.sThumbRY=32767;
    }
    return ERROR_SUCCESS;
}
static DWORD WINAPI poll_thread(LPVOID unused) {(void)unused;cellPad_poll();return 0;}
static DWORD WINAPI read_thread(LPVOID unused) {
    (void)unused;SetEvent(read_ready);
    assert(cellPadGetData(0,(CellPadData*)(uintptr_t)0x10000)==0);
    SetEvent(read_done);return 0;
}
static unsigned button(unsigned offset) {
    return ((unsigned)vm_base[0x10004+2*offset]<<8)|vm_base[0x10005+2*offset];
}
int main(void) {
    assert(_putenv_s("PAD_NO_KEYBOARD","1")==0);
    vm_base=VirtualAlloc(NULL,UINT64_C(0x100000000),MEM_RESERVE,PAGE_READWRITE);assert(vm_base);
    assert(VirtualAlloc(vm_base+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE));
    entered=CreateEvent(NULL,TRUE,FALSE,NULL);release_poll=CreateEvent(NULL,TRUE,FALSE,NULL);
    read_done=CreateEvent(NULL,TRUE,FALSE,NULL);assert(entered && release_poll && read_done);
    read_ready=CreateEvent(NULL,TRUE,FALSE,NULL);assert(read_ready);
    assert(cellPadInit(1)==0);test_now=106;InterlockedExchange(&block_poll,1);
    HANDLE poll=CreateThread(NULL,0,poll_thread,NULL,0,NULL);assert(poll);
    assert(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    HANDLE reader=CreateThread(NULL,0,read_thread,NULL,0,NULL);assert(reader);
    assert(WaitForSingleObject(read_ready,5000)==WAIT_OBJECT_0);
    DWORD premature=WaitForSingleObject(read_done,50);
    SetEvent(release_poll);
    assert(WaitForSingleObject(reader,5000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(poll,5000)==WAIT_OBJECT_0);
    assert(premature==WAIT_TIMEOUT); /* no publication during a driver poll */
    uint8_t lx,ly,rx,ry;
    pad_stick_radial(32767,-32768,PAD_LEFT_STICK_DEADZONE,&lx,&ly);
    pad_stick_radial(-32768,32767,PAD_RIGHT_STICK_DEADZONE,&rx,&ry);
    assert(button(CELL_PAD_BTN_OFFSET_DIGITAL2)==0x40);
    assert(button(CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X)==lx && button(CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y)==ly);
    assert(button(CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X)==rx && button(CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y)==ry);
    assert(cellPadEnd()==0);
    CloseHandle(reader);CloseHandle(poll);CloseHandle(entered);CloseHandle(release_poll);CloseHandle(read_done);CloseHandle(read_ready);
    assert(VirtualFree(vm_base,0,MEM_RELEASE));
    puts("Concurrent event/guest polling cannot publish an incomplete pad report.");
}
