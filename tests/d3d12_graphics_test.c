/* Exercise the production display/sampler helpers without changing the desktop. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_5.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rsx_d3d12_sampler.h"

static LONG_PTR style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
static RECT window_rect = {100, 80, 1396, 839};
static unsigned positions, targets, switches;
static BOOL fullscreen, reject_fullscreen;
static LONG_PTR mock_get_style(HWND w, int index) { (void)w; return index == GWL_STYLE ? style : 0; }
static LONG_PTR mock_set_style(HWND w, int index, LONG_PTR value) {
    (void)w; assert(index == GWL_STYLE); LONG_PTR old = style; style = value; return old;
}
static BOOL mock_get_rect(HWND w, RECT* rect) { (void)w; *rect = window_rect; return TRUE; }
static BOOL mock_adjust(RECT* rect, DWORD s, BOOL menu, DWORD ex) {
    assert(s & WS_CAPTION); assert(!menu && !ex); rect->right += 16; rect->bottom += 39; return TRUE;
}
static HMONITOR mock_monitor(HWND w, DWORD flags) { (void)w; assert(flags == MONITOR_DEFAULTTONEAREST); return (HMONITOR)1; }
static BOOL mock_monitor_info(HMONITOR m, MONITORINFO* info) {
    assert(m == (HMONITOR)1 && info->cbSize == sizeof(*info)); info->rcMonitor = (RECT){0, 0, 1920, 1080}; return TRUE;
}
static BOOL mock_position(HWND w, HWND after, int x, int y, int width, int height, UINT flags) {
    (void)w; assert(!after && (flags & SWP_NOACTIVATE) && (flags & SWP_NOZORDER));
    assert(flags & SWP_FRAMECHANGED); assert(!(flags & SWP_SHOWWINDOW));
    window_rect = (RECT){x, y, x + width, y + height}; ++positions; return TRUE;
}
static HRESULT STDMETHODCALLTYPE mock_fullscreen(IDXGISwapChain3* swap, BOOL on, IDXGIOutput* output) {
    (void)swap; assert(!output); ++switches;
    if (on && reject_fullscreen) return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    fullscreen = on; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mock_target(IDXGISwapChain3* swap, const DXGI_MODE_DESC* desc) {
    (void)swap; assert(desc->Format == DXGI_FORMAT_R8G8B8A8_UNORM);
    assert(desc->Width == 1280 && desc->Height == 720);
    assert(!desc->RefreshRate.Numerator && !desc->RefreshRate.Denominator); ++targets; return S_OK;
}
#define GetWindowLongPtrA mock_get_style
#define SetWindowLongPtrA mock_set_style
#define GetWindowRect mock_get_rect
#define AdjustWindowRectEx mock_adjust
#define MonitorFromWindow mock_monitor
#define GetMonitorInfoA mock_monitor_info
#define SetWindowPos mock_position
#include "rsx_d3d12_window.h"

static void samplers(void)
{
    rsx_dsp_texture t = {0};
    t.filter = (6u << 16) | (2u << 24);
    t.wrap = 1u | (2u << 8) | (4u << 16);
    t.control0 = 2560u << 7; /* maximum LOD 10 */
    D3D12_SAMPLER_DESC a = rsx_d3d12_sampler_decode(&t, 0);
    assert(a.Filter == D3D12_FILTER_MIN_MAG_MIP_LINEAR && a.MaxAnisotropy == 1);
    assert(a.AddressU == D3D12_TEXTURE_ADDRESS_MODE_WRAP && a.AddressV == D3D12_TEXTURE_ADDRESS_MODE_MIRROR && a.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER);
    assert(a.MinLOD == 0 && a.MaxLOD == 10 && a.MipLODBias == 0);
    for (unsigned level = 2; level <= 16; level *= 2) {
        D3D12_SAMPLER_DESC b = rsx_d3d12_sampler_decode(&t, level);
        assert(b.Filter == D3D12_FILTER_ANISOTROPIC && b.MaxAnisotropy == level);
    }
    t.filter |= 0x1f00u; D3D12_SAMPLER_DESC negative = rsx_d3d12_sampler_decode(&t, 0);
    assert(negative.MipLODBias == -1 && !rsx_d3d12_sampler_equal(&a, &negative));
    t.filter = (6u << 16) | (2u << 24) | 0x100u;
    D3D12_SAMPLER_DESC positive = rsx_d3d12_sampler_decode(&t, 0);
    assert(positive.MipLODBias == 1 && !rsx_d3d12_sampler_equal(&positive, &negative));
    t.filter &= ~0x1fffu;
    /* These minimum LOD bits were lost by the old truncated 32-bit key. */
    t.control0 |= 4u << 19; D3D12_SAMPLER_DESC shifted = rsx_d3d12_sampler_decode(&t, 0);
    assert(shifted.MinLOD == 4.0f / 256 && !rsx_d3d12_sampler_equal(&a, &shifted));
    D3D12_SAMPLER_DESC identical = shifted;
    assert(rsx_d3d12_sampler_equal(&identical, &shifted));
    identical.BorderColor[3] = 1; assert(!rsx_d3d12_sampler_equal(&identical, &shifted));
    t.filter = (1u << 16) | (1u << 24); t.control0 = 0;
    a = rsx_d3d12_sampler_decode(&t, 16);
    assert(a.Filter == D3D12_FILTER_MIN_MAG_MIP_POINT && a.MaxAnisotropy == 1);
    t.filter = (2u << 16) | (2u << 24);
    a = rsx_d3d12_sampler_decode(&t, 16); assert(a.MaxAnisotropy == 1 && a.MaxLOD == 0);
}
int main(void)
{
    samplers();
    rsx_graphics_config config = {0, 1280, 720, 1, 0, 30, 0, 0};
    assert(rsx_graphics_config_valid(&config));
    rsx_graphics_config bad = config; bad.height = 0; assert(!rsx_graphics_config_valid(&bad));
    bad = config; bad.anisotropy = 3; assert(!rsx_graphics_config_valid(&bad));
    bad = config; bad.frame_limit = 31; assert(!rsx_graphics_config_valid(&bad));
    bad = config; bad.window_mode = 3; assert(!rsx_graphics_config_valid(&bad));
    bad = config; bad.width = bad.height = 0; assert(rsx_graphics_config_valid(&bad));
    IDXGISwapChain3Vtbl table = {0}; table.SetFullscreenState = mock_fullscreen; table.ResizeTarget = mock_target;
    IDXGISwapChain3 swap = {&table}; rsx_d3d12_window_state state = {0};
    assert(rsx_d3d12_window_apply(&swap, (HWND)1, &config, &state) == 1);
    assert(window_rect.left == 100 && window_rect.top == 80 && window_rect.right == 1396 && window_rect.bottom == 839);
    config.window_mode = 1; assert(rsx_d3d12_window_apply(&swap, (HWND)1, &config, &state) == 1);
    assert(state.mode == 1 && (style & WS_POPUP) && !(style & WS_CAPTION));
    assert(window_rect.left == 0 && window_rect.top == 0 && window_rect.right == 1920 && window_rect.bottom == 1080);
    config.window_mode = 2; assert(rsx_d3d12_window_apply(&swap, (HWND)1, &config, &state) == 1);
    assert(state.mode == 2 && state.fullscreen && fullscreen && targets == 2 && switches == 1);
    assert(rsx_d3d12_present_flags(0, 1, state.fullscreen) == 0);
    config.window_mode = 0; assert(rsx_d3d12_window_apply(&swap, (HWND)1, &config, &state) == 1);
    assert(!fullscreen && !state.fullscreen && switches == 2 && (style & WS_CAPTION));
    assert(window_rect.left == 100 && window_rect.top == 80 && positions == 3);
    reject_fullscreen = TRUE; config.window_mode = 2;
    assert(rsx_d3d12_window_apply(&swap, (HWND)1, &config, &state) == -1 && !state.fullscreen);
    assert(rsx_d3d12_present_flags(1, 1, 0) == 0 && rsx_d3d12_present_flags(0, 0, 0) == 0);
    assert(rsx_d3d12_present_flags(0, 1, 0) == DXGI_PRESENT_ALLOW_TEARING);
    puts("DirectX display transitions, tearing policy and exact sampler matching pass");
    return 0;
}
