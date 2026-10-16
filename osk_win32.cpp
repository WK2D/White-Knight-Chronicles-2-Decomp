#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include "osk.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace {
struct Window {
    uint64_t generation;
    std::u16string prompt,initial;
    uint32_t limit;
    HWND handle=nullptr,edit=nullptr;
    std::atomic<bool> closing=false;
    bool answered=false;
    std::atomic<bool> visible=true;
};
std::mutex windows_mutex;
std::map<uint64_t,std::shared_ptr<Window>> windows;
std::once_flag registered;
std::once_flag shutdown_registered;
std::mutex thread_mutex;
std::thread ui_thread;
bool class_ready=false;
void shutdown() {
    std::lock_guard thread_lock(thread_mutex);
    {
        std::lock_guard lock(windows_mutex);
        for(auto& [generation,window]:windows) {
            window->closing=true;
            if(window->handle) PostMessageW(window->handle,WM_APP+1,0,0);
        }
    }
    if(ui_thread.joinable()) ui_thread.join();
}
const wchar_t* wide(const std::u16string& text) {
    static_assert(sizeof(wchar_t)==sizeof(char16_t));
    return reinterpret_cast<const wchar_t*>(text.c_str());
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
    auto window=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        window=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(window));
    }
    if(!window) return DefWindowProcW(hwnd,message,wparam,lparam);
    if(message==WM_COMMAND && LOWORD(wparam)==IDOK) {
        int length=GetWindowTextLengthW(window->edit);
        std::u16string text(size_t(length)+1,u'\0');
        GetWindowTextW(window->edit,reinterpret_cast<wchar_t*>(text.data()),length+1);
        text.resize(size_t(length));
        if(!wkc2_osk_complete(window->generation,0,text)) { MessageBeep(MB_ICONWARNING);return 0; }
        return 0; // The core closes normal dialogs after FINISHED is queued.
    }
    if((message==WM_COMMAND && LOWORD(wparam)==IDCANCEL)||message==WM_CLOSE) {
        if(!window->answered && !window->closing.load())
            wkc2_osk_complete(window->generation,1,{});
        return 0;
    }
    if(message==WM_APP+1) { window->answered=true;DestroyWindow(hwnd);return 0; }
    if(message==WM_APP+2) { SetWindowTextW(window->edit,L"");return 0; }
    if(message==WM_APP+3) {
        ShowWindow(hwnd,wparam ? SW_SHOWNOACTIVATE : SW_HIDE);return 0;
    }
    if(message==WM_DESTROY) { PostQuitMessage(0);return 0; }
    return DefWindowProcW(hwnd,message,wparam,lparam);
}
void run(std::shared_ptr<Window> window) {
    std::call_once(registered,[] {
        WNDCLASSW cls={};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
        cls.lpszClassName=L"WKC2NativeTextInput";
        class_ready=RegisterClassW(&cls)!=0;
    });
    HWND hwnd=nullptr;
    if(class_ready && !window->closing.load()) {
        const int width=560,height=208;
        hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"WKC2NativeTextInput",
            L"White Knight Chronicles - Text Input",WS_CAPTION|WS_SYSMENU,
            (GetSystemMetrics(SM_CXSCREEN)-width)/2,(GetSystemMetrics(SM_CYSCREEN)-height)/2,
            width,height,nullptr,nullptr,GetModuleHandleW(nullptr),window.get());
    }
    if(hwnd) {
        auto child=[&](const wchar_t* cls,const wchar_t* text,DWORD style,
                       int x,int y,int width,int height,int id) {
            HWND control=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,
                hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            return control;
        };
        child(L"STATIC",wide(window->prompt),0,20,18,510,40,0);
        window->edit=child(L"EDIT",wide(window->initial),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,
            20,65,510,28,100);
        SendMessageW(window->edit,EM_SETLIMITTEXT,window->limit,0);
        if(!window->limit) SendMessageW(window->edit,EM_SETREADONLY,TRUE,0);
        child(L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON,330,116,94,30,IDOK);
        child(L"BUTTON",L"Cancel",WS_TABSTOP,436,116,94,30,IDCANCEL);
        { std::lock_guard lock(windows_mutex);window->handle=hwnd; }
        if(window->closing.load()) DestroyWindow(hwnd);
        else {
            bool scripted=std::getenv("WKC2_OSK_INPUT_FILE")!=nullptr;
            ShowWindow(hwnd,!window->visible ? SW_HIDE : scripted ? SW_SHOWNOACTIVATE : SW_SHOW);
            if(!scripted && window->visible) { SetForegroundWindow(hwnd);SetFocus(window->edit); }
            SendMessageW(window->edit,EM_SETSEL,0,-1);
            std::fprintf(stderr,"[wkc2-osk-ui] opened generation=%llu\n",
                (unsigned long long)window->generation);
        }
        MSG message;
        while(GetMessageW(&message,nullptr,0,0)>0) {
            if(!IsDialogMessageW(hwnd,&message)) { TranslateMessage(&message);DispatchMessageW(&message); }
        }
    } else if(!window->closing.load()) {
        std::fprintf(stderr,"[wkc2-osk-ui] window creation failed\n");
        wkc2_osk_complete(window->generation,1,{});
    }
    { std::lock_guard lock(windows_mutex);windows.erase(window->generation); }
}
}
bool wkc2_osk_ui_open(uint64_t generation,const std::u16string& prompt,
                      const std::u16string& initial,uint32_t limit,bool visible) {
    std::lock_guard thread_lock(thread_mutex);
    std::call_once(shutdown_registered,[] { std::atexit(shutdown); });
    if(ui_thread.joinable()) ui_thread.join();
    auto window=std::make_shared<Window>();window->generation=generation;
    window->prompt=prompt;window->initial=initial;window->limit=limit;window->visible=visible;
    { std::lock_guard lock(windows_mutex);windows.emplace(generation,window); }
    ui_thread=std::thread(run,window);return true;
}
void wkc2_osk_ui_close(uint64_t generation) {
    std::lock_guard lock(windows_mutex);
    auto found=windows.find(generation);if(found==windows.end()) return;
    found->second->closing=true;
    if(found->second->handle) PostMessageW(found->second->handle,WM_APP+1,0,0);
}
void wkc2_osk_ui_clear(uint64_t generation) {
    std::lock_guard lock(windows_mutex);
    auto found=windows.find(generation);if(found==windows.end()) return;
    if(found->second->handle) PostMessageW(found->second->handle,WM_APP+2,0,0);
}
void wkc2_osk_ui_show(uint64_t generation,bool visible) {
    std::lock_guard lock(windows_mutex);
    auto found=windows.find(generation);if(found==windows.end()) return;
    found->second->visible=visible;
    if(found->second->handle) PostMessageW(found->second->handle,WM_APP+3,visible ? 1 : 0,0);
}
