/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
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

static LONG WINAPI wkc2_crash_filter(EXCEPTION_POINTERS* ep)
{
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
unsigned cellGcm_flip_request_count(void);
extern ppu_context* volatile g_ppu_main_ctx;
void wkc2_dump_threads(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
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
        Sleep(4);
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

int main(int argc, char** argv)
{
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
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    wkc2_register_lwcond();  /* register before the SDK's fallback ctx handlers */
    wkc2_register_lfqueue();
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
