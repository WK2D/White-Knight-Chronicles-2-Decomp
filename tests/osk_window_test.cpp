#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <mutex>
#include <string>
#include <cstdio>
#include <cstdlib>
// Keep test windows hidden; all other production window/control code is used.
static BOOL hidden_show(HWND window,int) { return ShowWindow(window,SW_HIDE); }
#define ShowWindow hidden_show
#include "../osk_win32.cpp"
#undef ShowWindow
static std::mutex test_mutex;
static unsigned completed=0;
static uint64_t actual_generation=0;
static uint32_t actual_status=99;
static std::u16string actual_text;
bool wkc2_osk_complete(uint64_t generation,uint32_t status,const std::u16string& text) {
    std::lock_guard lock(test_mutex);++completed;actual_generation=generation;
    actual_status=status;actual_text=text;return true;
}
static HWND find_window() {
    HWND result=nullptr;
    EnumWindows([](HWND window,LPARAM value)->BOOL {
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        wchar_t name[64];GetClassNameW(window,name,64);
        if(pid==GetCurrentProcessId() && wcscmp(name,L"WKC2NativeTextInput")==0) {
            *reinterpret_cast<HWND*>(value)=window;return FALSE;
        } return TRUE;
    },reinterpret_cast<LPARAM>(&result));return result;
}
static HWND wait_window() {
    for(unsigned i=0;i<300;++i) { HWND window=find_window();if(window && GetDlgItem(window,IDOK)) return window;Sleep(10); }
    std::exit(10);
}
static void gone() { for(unsigned i=0;i<300;++i) { if(!find_window()) { Sleep(30);return; }Sleep(10); }std::exit(11); }
int main() {
    _putenv_s("WKC2_OSK_INPUT_FILE","test-only");
    if(!wkc2_osk_ui_open(1,u"Name",u"Initial",8,true)) return 1;
    HWND window=wait_window();wchar_t initial[32];
    GetWindowTextW(GetDlgItem(window,100),initial,32);if(wcscmp(initial,L"Initial")) return 2;
    SetWindowTextW(GetDlgItem(window,100),L"N\u00E9\U0001F600");
    SendMessageW(window,WM_COMMAND,IDOK,0);wkc2_osk_ui_close(1);gone();
    { std::lock_guard lock(test_mutex);if(completed!=1||actual_generation!=1||actual_status!=0||actual_text!=u"N\u00E9\U0001F600") return 3; }
    wkc2_osk_ui_open(2,u"Name",u"",8,true);window=wait_window();SendMessageW(window,WM_CLOSE,0,0);wkc2_osk_ui_close(2);gone();
    { std::lock_guard lock(test_mutex);if(completed!=2||actual_generation!=2||actual_status!=1) return 4; }
    wkc2_osk_ui_open(3,u"Name",u"",8,true);wait_window();wkc2_osk_ui_close(3);gone();
    { std::lock_guard lock(test_mutex);if(completed!=2) return 5; }
    wkc2_osk_ui_open(4,u"Name",u"",8,false);wkc2_osk_ui_close(4);
    wkc2_osk_ui_open(5,u"Name",u"",8,false);wait_window();
    std::puts("Production Win32 editor: Unicode initial/edit/OK, cancellation, close, early close, reuse and shutdown passed");
    return 0;
}
