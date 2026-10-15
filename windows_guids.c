/* Materialize DXGI interface GUIDs for C callers. The legacy Windows SDK
 * import libraries do not provide IID_IDXGIFactory6. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_6.h>
