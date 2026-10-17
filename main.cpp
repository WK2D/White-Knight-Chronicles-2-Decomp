/*
 * White Knight Chronicles II native-port runner.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The shared scaffold provides ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * title-specific integration: backend selection, frame pacing, startup and
 * installation hooks, system-service registration and optional diagnostics.
 *
 * Windows x86-64 builds and limited WKC1 town execution are verified.
 * Linux and macOS integration and execution remain unverified; this port
 * does not currently have a CI workflow.
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#include <dbghelp.h>
extern "C" uint32_t g_last_hle_nid;
extern "C" const char* g_last_hle_name;
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;

#include <tlhelp32.h>
#include <algorithm>
#include <map>
#include <string>
#include <thread>
#include <vector>

/* Copy up to `cap` stack words starting at `rsp` from a stopped thread.
 * ReadProcessMemory is a plain system call: it takes no user-mode lock the
 * stopped thread could hold and reports unreadable memory instead of
 * faulting. Shorter reads are retried for a stack near its top. */
static size_t wkc2_profile_copy_stack(uintptr_t rsp, uint64_t* out, size_t cap)
{
    for (size_t want = cap; want >= 16; want /= 4) {
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)rsp, out, want * 8, &got))
            return (size_t)got / 8;
    }
    return 0;
}

/* WKC2_SAMPLE_PROFILE=<file>: every ten seconds, rewrite <file> with each
 * thread's CPU share over that window and the module-relative instruction
 * addresses it was sampled at most often (resolve them with the link map).
 * A background thread suspends each other thread briefly every 2 ms. */
static void wkc2_start_sample_profiler(void)
{
    const char* out = getenv("WKC2_SAMPLE_PROFILE");
    if (!out || !*out) return;
    static std::string path; path = out;
    std::thread([] {
        const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
        const uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        struct Info { HANDLE h = NULL; std::wstring name; uint64_t cpu0 = 0;
                      std::map<uint64_t, uint32_t> hits, leaves; uint32_t samples = 0; };
        std::map<DWORD, Info> threads;
        DWORD last_scan = 0, window_start = GetTickCount();
        auto cpu_of = [](HANDLE h) {
            FILETIME c, e, k, u;
            if (!GetThreadTimes(h, &c, &e, &k, &u)) return (uint64_t)0;
            return (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) +
                   (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
        };
        for (;;) {
            const DWORD now = GetTickCount();
            if (now - last_scan > 1000) {
                last_scan = now;
                HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                THREADENTRY32 te; te.dwSize = sizeof te;
                if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) do {
                    if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
                    if (threads.count(te.th32ThreadID)) continue;
                    Info& t = threads[te.th32ThreadID];
                    t.h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                     THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                    PWSTR desc = NULL;
                    if (t.h && SUCCEEDED(GetThreadDescription(t.h, &desc)) && desc) {
                        t.name = desc; LocalFree(desc);
                    }
                    t.cpu0 = t.h ? cpu_of(t.h) : 0;
                } while (Thread32Next(snap, &te));
                if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
            }
            for (auto& [id, t] : threads) {
                if (!t.h || SuspendThread(t.h) == (DWORD)-1) continue;
                CONTEXT ctx; ctx.ContextFlags = CONTEXT_FULL;
                /* Only the register and stack copies happen while the thread
                 * is stopped: no allocation, file or loader calls that could
                 * wait on a lock it holds. */
                uintptr_t leaf = 0, caller = 0;
                static uint64_t stack[512];
                size_t words = 0;
                const int got = GetThreadContext(t.h, &ctx);
                if (got) {
                    leaf = (uintptr_t)ctx.Rip;
                    if (!(leaf >= base && leaf - base < 0x40000000u))
                        words = wkc2_profile_copy_stack((uintptr_t)ctx.Rsp, stack, 512);
                }
                ResumeThread(t.h);
                if (!got) continue;
                /* Outside the module: the nearest stack word pointing into its
                 * code approximates the game-side caller. */
                for (size_t i = 0; i < words && !caller; i++) {
                    const uintptr_t a = (uintptr_t)stack[i];
                    if (a < base + 0x1000 || a - base >= 0x0A000000u) continue;
                    const uint8_t* c = (const uint8_t*)a;
                    if (c[-5] == 0xE8 || (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10) ||
                        (c[-2] == 0xFF && (c[-1] & 0x38) == 0x10) ||
                        (c[-3] == 0xFF && (c[-2] & 0x38) == 0x10))
                        caller = a;
                }
                t.samples++;
                const bool in_leaf = leaf >= base && leaf - base < 0x40000000u;
                uint32_t tag = 0;
                uintptr_t leaf_off = 0;
                if (!in_leaf) {
                    HMODULE m = NULL;
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCSTR)leaf, &m) && m) {
                        char mod[MAX_PATH] = "";
                        GetModuleFileNameA(m, mod, sizeof mod);
                        const char* slash = strrchr(mod, 92);
                        static std::map<std::string, uint32_t> ids;
                        static std::vector<std::string> names;
                        std::string key = slash ? slash + 1 : mod;
                        if (!ids.count(key)) {
                            ids[key] = (uint32_t)names.size() + 1; names.push_back(key);
                            if (FILE* legend = fopen((path + ".modules").c_str(), "w")) {
                                for (size_t i = 0; i < names.size(); i++)
                                    fprintf(legend, "%zu %s%c", i + 1, names[i].c_str(), 10);
                                fclose(legend);
                            }
                        }
                        tag = ids[key];
                        leaf_off = leaf - (uintptr_t)m;
                    }
                }
                if (!in_leaf && tag) t.leaves[((uint64_t)tag << 32) | (uint32_t)leaf_off]++;
                const uintptr_t rip = in_leaf ? leaf : caller;
                const uint32_t rva = rip >= base && rip - base < 0x40000000u ? (uint32_t)(rip - base) : 0xFFFFFFFFu;
                t.hits[((uint64_t)tag << 32) | rva]++;
            }
            if (now - window_start >= 10000) {
                const double wall = (now - window_start) * 10000.0;   /* 100 ns units */
                FILE* f = fopen(path.c_str(), "w");
                if (f) {
                    std::vector<std::pair<double, DWORD>> order;
                    for (auto& [id, t] : threads)
                        if (t.h) { const uint64_t c = cpu_of(t.h); order.push_back({(c - t.cpu0) / wall, id}); }
                    std::sort(order.rbegin(), order.rend());
                    fprintf(f, "window %.1f s\n", (now - window_start) / 1000.0);
                    for (auto& [share, id] : order) {
                        Info& t = threads[id];
                        if (share < 0.02) continue;
                        fprintf(f, "\nthread %lu %ls cpu %.0f%% samples %u\n", id, t.name.c_str(),
                                share * 100.0, t.samples);
                        std::vector<std::pair<uint32_t, uint64_t>> top;
                        for (auto& [rva, n] : t.hits) top.push_back({n, rva});
                        std::sort(top.rbegin(), top.rend());
                        std::vector<std::pair<uint32_t, uint64_t>> lt;
                        for (auto& [k, n] : t.leaves) lt.push_back({n, k});
                        std::sort(lt.rbegin(), lt.rend());
                        for (size_t i = 0; i < lt.size() && i < 12; i++)
                            fprintf(f, "  leaf %6u dll %u off 0x%08X%c", lt[i].first,
                                    (uint32_t)(lt[i].second >> 32), (uint32_t)lt[i].second, 10);
                        for (size_t i = 0; i < top.size() && i < 40; i++)
                            fprintf(f, "  %6u 0x%08X dll %u\n", top[i].first,
                                    (uint32_t)top[i].second, (uint32_t)(top[i].second >> 32));
                    }
                    fclose(f);
                }
                for (auto& [id, t] : threads) { t.hits.clear(); t.leaves.clear(); t.samples = 0; if (t.h) t.cpu0 = cpu_of(t.h); }
                window_start = now;
            }
            Sleep(2);
        }
    }).detach();
}

static LONG WINAPI wkc2_crash_filter(EXCEPTION_POINTERS* ep)
{
    fflush(stdout);
    HMODULE module = GetModuleHandleA(NULL);
    auto* er = ep->ExceptionRecord;
    fprintf(stderr, "[CRASH] code=0x%08lX host_rva=0x%llX last_hle=0x%08X (%s)\n",
        er->ExceptionCode,
        (unsigned long long)((uintptr_t)er->ExceptionAddress - (uintptr_t)module),
        g_last_hle_nid, g_last_hle_name ? g_last_hle_name : "");
    if (g_active_ctx)
        fprintf(stderr, "[CRASH] guest lr=0x%08X ctr=0x%08X r3=0x%08X\n",
            (uint32_t)g_active_ctx->lr, (uint32_t)g_active_ctx->ctr,
            (uint32_t)g_active_ctx->gpr[3]);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        fprintf(stderr, "[CRASH] access=%llu address=0x%llX\n",
            (unsigned long long)er->ExceptionInformation[0],
            (unsigned long long)er->ExceptionInformation[1]);
    void* frames[32];
    USHORT count = RtlCaptureStackBackTrace(0, 32, frames, NULL);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE owner = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &owner);
        char path[MAX_PATH] = {};
        GetModuleFileNameA(owner, path, sizeof path);
        alignas(SYMBOL_INFO) char symbol_buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = (SYMBOL_INFO*)symbol_buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        BOOL resolved = SymFromAddr(GetCurrentProcess(), (DWORD64)frames[i], &displacement, symbol);
        fprintf(stderr, "[CRASH] #%u %s+0x%llX %s+0x%llX\n", i, path,
            (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)owner),
            resolved ? symbol->Name : "?", (unsigned long long)displacement);
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     wkc2_register_lwcond(void);
void     wkc2_register_lfqueue(void);
void     cellGame_init_from_paramsfo(const char*);
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
#ifdef _WIN32
void     cellGcm_fifo_kick_wait(unsigned ms); /* guest fence wake, bounded idle */
#endif
unsigned cellGcm_flip_request_count(void);
extern ppu_context* volatile g_ppu_main_ctx;
void wkc2_dump_threads(void);
void wkc2_dump_dmux(void);
void wkc2_dump_adec(void);
void wkc2_dump_vdec(void);
void wkc2_init_graphics_writers(void);
void wkc2_init_spu_job_capture(void);
int  spu_float_mode_init(void);
void wkc2_dump_graphics_writers(uint32_t);
void wkc2_register_pamf(void);
void wkc2_register_dmux(void);
void wkc2_register_vpost(void);
void wkc2_register_adec(void);
void wkc2_register_vdec(void);
void wkc2_register_osk(void);
void wkc2_register_l10n(void);
void wkc2_shutdown_vdec(void);
void wkc2_shutdown_adec(void);
void wkc2_shutdown_dmux(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* Vulkan is the default Windows backend; D3D12 is optional. The Apple Metal
 * and other-platform null-backend branches are unverified in this port.
 * Each backend exposes the same entry points to the frame clock. */
#if defined(_WIN32) && defined(PS3RECOMP_RSX_VULKAN)
extern "C" int  rsx_vulkan_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_vulkan_backend_present(void);
extern "C" int  rsx_vulkan_backend_pump_messages(void);
#  define rsx_backend_init    rsx_vulkan_backend_init
#  define rsx_backend_present rsx_vulkan_backend_present
#  define rsx_backend_pump    rsx_vulkan_backend_pump_messages
#  define RSX_BACKEND_NAME    "Vulkan"
#elif defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

extern "C" void ppu_dump_bctrl_ring(uint32_t thread_id, const char* tag);

static uint32_t g_startup_object = 0;
static uint32_t g_startup_state = 0;

/* WKC2_SKIP_INTRO=1: the boot sequencer (0x00325248) steps through a 13-entry
 * table: copyright and two save notices (0-2), system setup (3-5), publisher
 * and middleware logos (6-10), the title (11) and the attract movie (12, which
 * wraps to 6). Move past the notices and logos so setup still runs and the
 * title follows it directly. Off by default. */
extern "C" int32_t wkc2_boot_step_remap(int32_t step)
{
    static int skip = -1;
    if (skip < 0) { const char* e = getenv("WKC2_SKIP_INTRO"); skip = e && atoi(e); }
    if (!skip) return step;
    if (step >= 0 && step <= 2) return 3;
    if (step >= 6 && step <= 10) return 11;
    return step;
}

/* The original installer request drops a request if another command-2 check
 * is pending. Native execution can reach it before the worker gets CPU time.
 * Finish that check before entering the original request path; the guest still
 * performs the allocation, filesystem installation and completion checks. */
extern "C" void wkc2_wait_for_content_check(void)
{
    uint32_t content = vm_read32(0x0134C544);
    if (!content) return;
    bool waited = false;
    while (vm_read8(content+2) && !vm_read8(content+0x12)) {
        if (!waited) fprintf(stderr, "[content-sync] waiting for pending check before installation request\n");
        waited = true;
        Sleep(1);
    }
    if (waited) fprintf(stderr, "[content-sync] check completed; entering original installation request\n");
}

/* Read-only observation at the game's own graphics timeout. All addresses
 * and command words remain in the private probe log. */
extern "C" uint32_t* cellGcmGetLabelAddress(uint8_t index);
extern "C" void* cellGcmGetControlRegister(void);
extern "C" uint32_t cellGcmResolveIO(uint32_t offset);
extern "C" void cellGcmDebugFifoSnapshot(void);
extern "C" void wkc2_observe_graphics_wait(ppu_context* ctx)
{
    if (!getenv("WKC2_FIFO_TRACE")) return;
    static unsigned samples = 0;
    if (samples++ >= 3) return;
    uint32_t object = (uint32_t)ctx->gpr[29];
    uint32_t label = (uint32_t)(uintptr_t)cellGcmGetLabelAddress(0x48);
    uint32_t control = (uint32_t)(uintptr_t)cellGcmGetControlRegister();
    uint32_t put = vm_read32(control), get = vm_read32(control + 4);
    fprintf(stderr, "[graphics-wait] object=%08X expected=%08X label48=%08X get=%08X put=%08X ref=%08X\n",
        object, vm_read32(object + 0x30), vm_read32(label), get, put, vm_read32(control + 8));
    wkc2_dump_graphics_writers(cellGcmResolveIO(get));
    cellGcmDebugFifoSnapshot();
    for (unsigned end = 0; end < 2; ++end) {
        uint32_t center = end ? put : get;
        for (int k = -8; k < 24; ++k) {
            uint32_t io = center + (uint32_t)(k * 4);
            uint32_t ea = cellGcmResolveIO(io);
            if (!ea || ea == UINT32_MAX) continue;
            fprintf(stderr, "[graphics-fifo] %s%+d io=%08X ea=%08X word=%08X\n",
                end ? "put" : "get", k * 4, io, ea, vm_read32(ea));
        }
    }
    fflush(stderr);
}

extern "C" void wkc2_observe_content_request(ppu_context* ctx)
{
    if (!getenv("WKC2_CONTENT_TRACE")) return;
    fprintf(stderr, "[content-request] caller=%08X cmd=%u args=%u/%u/%u/%u/%u\n",
        (uint32_t)ctx->lr, (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4],
        (uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[7], (uint32_t)ctx->gpr[8]);
    uint32_t content = vm_read32(0x0134C544);
    uint32_t cmd = (uint32_t)ctx->gpr[3];
    if (content && cmd < 12) fprintf(stderr, "[content-request] issued=%u done=%u current=%u\n",
        vm_read8(content+cmd), vm_read8(content+cmd+0x10), vm_read32(content+0x4C));
}

extern "C" void wkc2_observe_content_worker(ppu_context* ctx)
{
    if (!getenv("WKC2_CONTENT_TRACE")) return;
    uint32_t content=vm_read32(0x0134C544);
    fprintf(stderr, "[content-worker] r31=%08X r30=%08X result=%08X current=%u issued2=%u done2=%u\n",
        (uint32_t)ctx->gpr[31],(uint32_t)ctx->gpr[30],(uint32_t)ctx->gpr[3],
        vm_read32(content+0x4C),vm_read8(content+2),vm_read8(content+0x12));
}

extern "C" void wkc2_observe_startup(uint32_t object, uint32_t state_context)
{
    if (!getenv("WKC2_UPDATE_DIAGNOSTICS")) return;
    g_startup_object = object;
    g_startup_state = state_context ? vm_read32(state_context) : 0;
}

/* Called on the guest update thread, so dispatcher fields form a coherent sample. */
extern "C" void wkc2_observe_update(uint32_t app)
{
    if (!getenv("WKC2_UPDATE_DIAGNOSTICS")) return;
    static ULONGLONG next_report = 0;
    ULONGLONG now = GetTickCount64();
    if (now < next_report) return;
    next_report = now + 5000;
    unsigned count = vm_read16(app + 0x32C8);
    fprintf(stderr, "[update] app=%08X count=%u enabled=%04X excluded=%04X\n", app, count,
        vm_read16(app + 0x32D2), vm_read16(app + 0x32D0));
    uint32_t root_cell = vm_read32(0x013539E4);
    uint32_t root = root_cell ? vm_read32(root_cell) : 0;
    uint32_t scene = root ? vm_read32(root + 0x48) : 0;
    uint32_t model = root ? vm_read32(root) : 0;
    if (model) fprintf(stderr, "[game-state] model=%08X stage=%04X substage=%04X\n", model,
        vm_read16(model+0x114), vm_read16(model+0x14A));
    if (g_startup_object) {
        uint32_t object = g_startup_object;
        fprintf(stderr, "[startup] object=%08X state=%u next=%d current=%d requested=%d command=%u active=%u f61=%u f62=%u time_bits=%08X\n",
            object, g_startup_state, (int32_t)vm_read32(object+0x50), (int32_t)vm_read32(object+0x54),
            (int32_t)vm_read32(object+0x58), vm_read32(object+0x5C), vm_read8(object+0x60),
            vm_read8(object+0x61), vm_read8(object+0x62), vm_read32(object+0x64));
        uint32_t pending = vm_read32(object+0x6C);
        uint32_t content = vm_read32(0x0134C544);
        uint32_t operation = content ? vm_read32(content+0x1FF4) : 0;
        fprintf(stderr, "[startup-wait] pending=%08X step=%u content=%08X operation=%08X\n",
            pending, pending ? vm_read32(pending) : 0, content, operation);
        if (content) fprintf(stderr, "[content-state] command=%u a0=%08X a1=%08X a2=%08X freeKB=%08X sizeKB=%08X needKB=%08X issued2=%u done2=%u\n",
            vm_read32(content+0x4C),vm_read32(content+0x50),vm_read32(content+0x54),vm_read32(content+0x58),
            vm_read32(content+0xF04),vm_read32(content+0xF08),vm_read32(content+0x2018),vm_read8(content+2),vm_read8(content+0x12));
        if (operation) for (unsigned offset=0; offset<0x80; offset+=0x10)
            fprintf(stderr, "[content-op] +%02X %08X %08X %08X %08X\n", offset,
                vm_read32(operation+offset), vm_read32(operation+offset+4),
                vm_read32(operation+offset+8), vm_read32(operation+offset+12));
    }
    if (scene) {
        fprintf(stderr, "[scene] root=%08X scene=%08X mode=%u phase=%u kind=%u\n", root, scene,
            vm_read32(scene + 0x10E0), vm_read32(scene + 0x10E4), vm_read32(scene + 0x10EC));
        fprintf(stderr, "[scene] result=%u category=%u flags=%02X/%02X/%02X/%02X/%02X/%02X\n",
            vm_read32(scene+0x10E8), vm_read32(scene+0x111C), vm_read8(scene+0x1128),
            vm_read8(scene+0x1129), vm_read8(scene+0x112A), vm_read8(scene+0x112B),
            vm_read8(scene+0x112C), vm_read8(scene+0x112D));
        for (unsigned offset = 0x110; offset < 0x1D0; offset += 0x10)
            fprintf(stderr, "[scene] +%04X %08X %08X %08X %08X\n", offset,
                vm_read32(scene+offset), vm_read32(scene+offset+4),
                vm_read32(scene+offset+8), vm_read32(scene+offset+12));
        for (unsigned i=0; i<72; ++i) {
            uint32_t node = scene + 0x330 + i*0x28;
            uint32_t object = vm_read32(node+4);
            if (!object || vm_read8(node+0x14)) continue;
            uint32_t vtable = vm_read32(object);
            uint32_t opd = vtable ? vm_read32(vtable+0x2C) : 0;
            fprintf(stderr, "[scene-node] i=%u object=%08X state=%d previous=%d active=%u target=%08X\n",
                i, object, (int32_t)vm_read32(node+8), (int32_t)vm_read32(node+12),
                vm_read32(node+0x20), opd ? vm_read32(opd) : 0);
        }
    }
    for (unsigned i = 0; i < count && i < 128; ++i) {
        unsigned index = vm_read16(app + 0x12E8 + i * 2);
        if (index >= 240) continue;
        uint32_t slot = app + index * 0x14;
        uint32_t opd = vm_read32(slot + 0x38);
        fprintf(stderr, "[update] id=%04X mask=%04X state=%u previous=%u target=%08X\n",
            vm_read16(slot + 0x28), vm_read16(slot + 0x2A), vm_read32(slot + 0x30),
            vm_read32(slot + 0x34), opd ? vm_read32(opd) : 0);
    }
}

static void present_guest_frame(void)
{
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    ULONGLONG next_tick = GetTickCount64();
    ULONGLONG next_report = next_tick + 5000;
    const bool report_progress = getenv("WKC2_PROGRESS") != nullptr;
    const bool guest_present_only = getenv("WKC2_GUEST_PRESENT_ONLY") != nullptr;

    for (;;) {
#ifdef _WIN32
        /* Fence polling already signals the SDK's auto-reset kick event.
         * Wait for it so each pass can resume immediately; a flat Sleep(4)
         * leaves that event unread and adds latency to every fence wave. */
        cellGcm_fifo_kick_wait(4);
#else
        Sleep(4);
#endif
        ULONGLONG now = GetTickCount64();
        if (report_progress && now >= next_report) {
            fprintf(stderr, "[verification] guest_flip_requests=%u guest_frames_presented=%u\n",
                cellGcm_flip_request_count(), ppu_boot_frames_presented());
            if (getenv("WKC2_THREADS")) {
                ppu_context* main_ctx = g_ppu_main_ctx;
                if (main_ctx) fprintf(stderr,"[threads] main cia=%08X lr=%08X ctr=%08X r3=%08X r4=%08X r30=%08X r31=%08X\n",
                    (uint32_t)main_ctx->cia,(uint32_t)main_ctx->lr,(uint32_t)main_ctx->ctr,
                    (uint32_t)main_ctx->gpr[3],(uint32_t)main_ctx->gpr[4],
                    (uint32_t)main_ctx->gpr[30],(uint32_t)main_ctx->gpr[31]);
                wkc2_dump_threads();
                wkc2_dump_dmux();
                wkc2_dump_adec();
                wkc2_dump_vdec();
            }
            if (getenv("BCTRL_RING")) ppu_dump_bctrl_ring(1, "frame-progress");
            next_report = now + 5000;
        }

        int fired = 0;
        while ((long long)(now - next_tick) >= 0 && fired < 240) {
            cellGcmTickVBlank();
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick += 16;             /* ~60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
            }
            cellGcm_rsx_process_fifo();

            if (rsx_backend_pump() != 0) {
                rsx_ok = 0;              /* window closed */
                continue;
            }
            /* A drain can decode a flip. Consume that pending boundary here;
             * observing only the request counter leaves the pending bit set
             * and presents the same boundary again on the next iteration. */
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
            } else if (cellGcm_flip_request_count() == 0 && !guest_present_only) {
                rsx_backend_present();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

#ifdef WKC2_BUILD_SELF_TESTS
int wkc2_test_spu_title(int selected);
extern "C" int wkc2_test_spu_dma_poll(int expect_halt);
extern "C" int wkc2_test_pad_file(const char* path);
extern "C" int wkc2_test_spu_microstep_profile(const char* path);
#endif

int main(int argc, char** argv)
{
#ifdef WKC2_BUILD_SELF_TESTS
    if (argc == 3 && !strcmp(argv[1], "--test-spu-title"))
        return wkc2_test_spu_title(atoi(argv[2]));
    if (argc == 3 && !strcmp(argv[1], "--test-spu-dma-poll"))
        return wkc2_test_spu_dma_poll(atoi(argv[2]));
    if (argc == 3 && !strcmp(argv[1], "--test-pad-file"))
        return wkc2_test_pad_file(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--test-spu-microstep-profile"))
        return wkc2_test_spu_microstep_profile(argv[2]);
#endif
    // This title's image registry contains no YDKJ CRI media task.
#ifdef _WIN32
    /* Diagnostics are written line by line, often with an explicit flush, so
     * each line costs a WriteFile; on a scanned log file that cost the main
     * game thread about a third of every cutscene frame. A player does not
     * need them: WKC2_DISCARD_LOGS=1 sends both streams to NUL. Research runs
     * keep their logs. */
    if (getenv("WKC2_DISCARD_LOGS")) {
        FILE* ignored = NULL;
        freopen_s(&ignored, "NUL", "w", stdout);
        freopen_s(&ignored, "NUL", "w", stderr);
    }
    _putenv_s("SPU_CRI_IMAGE", "-1");
    // Native FIFO traces confirm NV0039 memory copies on this title's slot 1.
    if (!getenv("GCM_SUBCH1_2D")) _putenv_s("GCM_SUBCH1_2D", "1");
    // Present the completed batch at the guest's FIFO flip. The backend's
    // clear-based fallback can retain a large scene's draw-count threshold
    // across a transition and indefinitely withhold a smaller movie batch.
    _putenv_s("RSX_ACCUM_FRAME", "1");
#ifdef PS3RECOMP_RSX_VULKAN
    if (!getenv("PS3RECOMP_VK_GUEST_PROGRAMS"))
        _putenv_s("PS3RECOMP_VK_GUEST_PROGRAMS", "1");
    if (!getenv("PS3RECOMP_VK_WINDOW"))
        _putenv_s("PS3RECOMP_VK_WINDOW", "1");
#endif
#else
    setenv("SPU_CRI_IMAGE", "-1", 1);
    setenv("GCM_SUBCH1_2D", "1", 0);
    setenv("RSX_ACCUM_FRAME", "1", 1);
#endif
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    SetUnhandledExceptionFilter(wkc2_crash_filter);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");
    atexit([] {
        fprintf(stderr, "[verification] guest_flip_requests=%u guest_frames_presented=%u\n",
            cellGcm_flip_request_count(), ppu_boot_frames_presented());
    });

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);
    char sfo_path[1200];
    snprintf(sfo_path, sizeof sfo_path, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
    cellGame_init_from_paramsfo(sfo_path);

    ppu_recomp_register();   /* lifted function table -> address map */
    wkc2_init_graphics_writers();
    wkc2_init_spu_job_capture();
#ifdef _WIN32
    wkc2_start_sample_profiler();
#endif
    spu_float_mode_init();   /* SPU_FLOAT_MODE, read before any SPU thread starts */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    wkc2_register_lwcond();  /* register before the SDK's fallback ctx handlers */
    wkc2_register_lfqueue();
    wkc2_register_pamf();
    wkc2_register_dmux();
    wkc2_register_vpost();
    wkc2_register_adec();
    wkc2_register_vdec();
    wkc2_register_osk();
    wkc2_register_l10n();
    atexit(wkc2_shutdown_vdec);
    atexit(wkc2_shutdown_adec);
    atexit(wkc2_shutdown_dmux);
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
