/* C-side calls must reach the same epoch used by the C++ test and real VM. */
#ifndef _WIN32
#error This regression uses the Windows VM and VirtualQuery.
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "vm_mapping_epoch.h"

/* Observe the ordering at the actual OS release boundary. All VM operations
 * below still use the SDK's inline implementation and the real VirtualFree. */
static void* watched_release_address;
static uint64_t release_epoch;
static unsigned release_count;
static BOOL release_succeeded;

static BOOL WINAPI vm_mapping_epoch_test_virtual_free(
    void* address, SIZE_T size, DWORD free_type)
{
    const int watched = watched_release_address &&
        address == watched_release_address && size == 0 &&
        free_type == MEM_RELEASE;
    if (watched) {
        release_epoch = vm_mapping_epoch_read();
        ++release_count;
    }
    const BOOL result = VirtualFree(address, size, free_type);
    if (watched) release_succeeded = result;
    return result;
}

#define VirtualFree vm_mapping_epoch_test_virtual_free
#include "vm.h"
#undef VirtualFree

uint8_t* vm_base;
uint32_t ppu_vm_size;
uint32_t ppu_hle_inject_base = 0x20000000u;

uint64_t vm_mapping_epoch_test_c_read(void)
{
    return vm_mapping_epoch_read();
}

void vm_mapping_epoch_test_c_invalidate(void)
{
    vm_mapping_epoch_invalidate();
}

int32_t vm_mapping_epoch_test_c_init(void)
{
    return vm_init();
}

int32_t vm_mapping_epoch_test_c_commit(uint32_t address, uint32_t size)
{
    return vm_commit(address, size);
}

int32_t vm_mapping_epoch_test_c_protect(
    uint32_t address, uint32_t size, int read, int write, int exec)
{
    return vm_protect(address, size, read, write, exec);
}

void vm_mapping_epoch_test_c_shutdown(void)
{
    watched_release_address = vm_base;
    release_epoch = 0;
    release_count = 0;
    release_succeeded = FALSE;
    vm_shutdown();
    watched_release_address = NULL;
}

uint64_t vm_mapping_epoch_test_c_shutdown_release_epoch(void)
{
    return release_epoch;
}

unsigned vm_mapping_epoch_test_c_shutdown_release_count(void)
{
    return release_count;
}

int vm_mapping_epoch_test_c_shutdown_release_succeeded(void)
{
    return release_succeeded != FALSE;
}
