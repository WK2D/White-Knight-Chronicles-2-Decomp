# Current Windows build workflow

This builds the current static recompilation/runtime project, including an
observed SPU interpreter fallback. It does not establish fully static CPU
coverage. See [architecture and modding scope](ARCHITECTURE.md).

**Current validation (2026-10-20):** the stack contains 120 SDK patches. All
112 Windows checks pass in normal Debug and optimized Release. Reapplying the
complete stack changes no SDK files. Vulkan Continue and short town movement
were rechecked on patch 0105; isolated DirectX Continue, menu FXAA toggling
and a short world movement are checked on patch 0110.
[Host-layout SPU shuffle](FINDINGS.md#host-layout-spu-byte-shuffle-and-unlocked-headroom-patch-0104)
reduces synthetic helper cost with tested byte equivalence; capped native views
show no consistent FPS gain.
[Vulkan constant upload reuse](FINDINGS.md#vulkan-constant-upload-reuse-patch-0103)
reduces redundant uniform-buffer traffic and improves one alternating town-view
comparison; sustained smooth 30 FPS remains unverified. The
[batch lifetime fixes](FINDINGS.md#vulkan-batch-resource-lifetimes-patch-0102)
cover pending-spare texture release and skipped window presentation. The
[grouped vertex conversion](FINDINGS.md#grouped-vertex-conversion-patch-0101)
reduces fixed-work conversion cost without an established game FPS gain. The
[shader-cache comparison](FINDINGS.md#shader-translation-stalls-and-cache-patch-0100)
reduces compilation hitches; the [latest town images](GALLERY.md#2026-10-19---window-depth-clip-range-check)
retain their earlier build provenance.

Triangle -> System -> Graphics Settings is available in the native game, including
Off/FXAA, frame-rate limits and an experimental DirectX 12 selector. See the
[menu screenshots](GALLERY.md#2026-10-18---full-graphics-settings-panel-and-window-close)
and [settings and timing details](PATCHES.md#native-graphics-settings-patches-0081-0082).

Suite timings are not game performance. Full rendering fidelity, smooth traversal,
combat and a locked 60 fps remain unverified. Historical measurements below retain
their original build and diagnostic conditions.

The port sets frame-pacing defaults at startup, each overridable by setting it
first: `GCM_FLIPCB_ONTICK=1` delivers flip callbacks at vblank after a real flip
request, and `RSX_VK_PRESENT_MODE=mailbox` prefers mailbox presentation when the
device supports it, with FIFO fallback. Flips wait for the next vblank
(`WKC2_FLIP_ON_VBLANK=0` turns that off). Vblanks are timed on the high-resolution
counter at 59.94 Hz for the default 30 FPS option; higher caps select twice
the requested rate, and Unlocked services ticks without the fixed interval.
The optional in-game FPS overlay counts presentations; repeated animation states
still count, so this can exceed the game's own update rate. The startup defaults also
set `GCM_SEMA_ACQUIRE=1` to honor the game's two-vblank semaphore gate and
`RSX_ENGINE_ZTEX_GPU=1` for supported GPU depth re-encoding. Patch
0068 allows flip completion only after an actual FIFO or immediate flip request;
`ResetFlipStatus` by itself does not create a completion. This uses the guest
pacing mechanism. The graphics menu also applies a host cap; raising that cap
alone was insufficient, which the current guest-timing hooks correct.

## Contents

- [Requirements and workspace](#requirements-and-workspace)
- [Building and regression checks](#building-and-regression-checks)
- [Playing and performance options](#playing-and-performance-options)
- [Diagnostic probes](#diagnostic-probes)
- [Runtime patch notes](PATCHES.md) (moved from this page, newest first)
- [Runtime subsystem notes](SUBSYSTEMS.md): renderer, movie decoding, frame presentation and text entry

The Vulkan translator retains up to 1,024 exact source/stage entries and 16 MiB
of allocated source/SPIR-V payload in memory. `RSX_SPIRV_CACHE=0` disables reuse
for comparison. It writes no shader cache to disk.

## Requirements and workspace

The source checkout and private working directory can be separate. Pass the
private workspace with `-Workspace`; the build script gets project sources from
its own location. Its default workspace layout is three parent directories above
the script and is intended for the original development layout.

Required tools: PowerShell, Git, Python 3.11.4+, Clang/clang-cl, CMake 3.20+, Ninja,
MSVC Build Tools headers/libraries and the Windows 10 SDK. Install the Python
dependencies listed by the pinned PS3Recomp SDK, plus PyCryptodome. Keep downloaded
tools and dependency repositories in the private workspace.

The current `windows-env.ps1` locates Clang in `work/tools/llvm/bin` and uses
MSVC 14.29.30133 / Windows SDK 10.0.19041.0. Adjust these tool locations for a
different installation. They are build prerequisites, not runtime requirements.

Private workspace layout:

```text
work/
  deps/ps3recomp/          # commit a2c20108af45174dbb7d3e2f87f6acaaef09c6b4
  deps/DirectX-Headers/   # commit 461ef5138bd30922b44fbad77c4e47662812d2d5
  tools/llvm/bin/
  venv/Scripts/python.exe
  wkc2-inspection/PS3_GAME/USRDIR/EBOOT.elf
  wkc2-disc/              # privately extracted disc tree used by the VFS
  wkc2-analysis/
  wkc2-generated/ppu/
  wkc2-generated/spu/
```

Supply your own decrypted BLUS30784 v01.00 executable and extracted disc tree.
The analyzed ELF is 20,455,904 bytes, SHA-256
`333400e0b0915e3008a22f29daf95ed2c9b03f3c2a2787cf5705222d73a5ceeb`.
No executable, asset or generated game source is distributed here.

## Building and regression checks

```powershell
./tools/build.ps1 -Workspace <private-workspace> -Regenerate
./tools/probe.ps1 -Workspace <private-workspace> -Seconds 15
```

`-Regenerate` analyzes and lifts the PPU plus five embedded SPU ELFs and 19 raw
SPURS jobs. `tools/extract_raw_jobs.py` checks the pinned executable's SHA-256
before extracting those jobs. Six jobs initially found through runtime captures
were subsequently located byte-for-byte in that executable. A clean extraction
and static lift now reproduces all 24 registered image inputs without captures.
A clean PPU/SPU regeneration and full native build have also been verified.
The regenerated inputs have been built with the current runtime and verified
through the title menu, WKC1 selection, avatar acceptance and live town. Short
movement and camera response are observed there. Starting-town local saving
and Continue reload are verified separately; sustained traversal, combat, wider
save compatibility and full rendering fidelity remain unverified.
`tools/apply_native_geometry.py` then installs the hash-checked native C++
matrix-blend, position and cofactor hooks; rerunning it validates the retained
references and wrapper. The earlier blend-only and blend/position versions upgrade
after the same validation.
See [kernel evidence and limits](NATIVE_CODE.md#readable-c-geometry-prefix).
`tools/import_spu_misses.py` is available for discovering additional jobs.

`tools/prepare_sdat.py <private-USRDIR>` authenticates and caches the supported
SDAT archives. The build applies the numbered runtime patches idempotently
and runs the regression suite, covering real video decoding, sparse video timing,
asynchronous video queues, LPCM queues/conversion, stereo/surround AC-3,
Vpost conversion/windows, callback stacks,
demux ABI/lifetimes, MPEG packet/access-unit parsing, PAMF reader, lightweight
conditions, memory containers, LFQueue, event queues, RSX texture formats and
SPURS guard lifetime/notification behavior, ordered job-list dispatch and the
native SPURS kernel-ID binding, vertex-program cache identity/lifetime, guest depth
state, packed vertex normals, fragment source precision and the keyboard ABI/lifecycle.
The current suite also covers fragment-program cache identity, ordered surface
clears, Windows PPU join lifetime and the shared draw engine's colour feedback.
It also covers persistent DMA label polling, reuse of freed memory ranges and
finite digital/analog input through the private controller probe file, NV0039
and NV3089 copies, SPURS job-queue dispatch, filtered FIFO producer wakes,
per-byte population counts, host-thread recursion diagnostics, real flip/user-command
callback delivery, depth-comparison shaders and Vulkan arena reservation.
The current source stack contains 120 numbered SDK patches, and all 112 checks
pass in both Windows suites. Graphics panel captures use 83 patches; saving
was first verified at 75, normal shadow polarity at 74, and the physics
checkpoint at 58. Earlier NPC-surface and controls captures retain their
52- and 42-patch provenance; the first town world-view capture used 39. Patch 0043 passed
the earlier 43-check Windows suite and remains covered by the current suite;
its town-rendering comparison is recorded separately.
Patch 0045's three float-mode checks and patches 0046-0051's new and extended
checks pass in the native Windows clang-cl build and remain covered by both
current suites; Wine's MinGW TLS workaround is not needed here.
Patch 0042 and its interpreter, disabled-mode and actual microstep/handoff
regressions now pass on Windows. Native census results are recorded in the
findings log.

## Playing and performance options

For the installed player copy, run:

```powershell
./tools/play.ps1 -Workspace <private-workspace>
```

This uses `work/play-native/WKC2Native.exe`, keeps manual title/Continue input,
and enables `WKC2_PLAYER_MODE=1`. Before runtime initialization, the executable
clears inherited diagnostic, profiler, capture and input-injection variables,
including legacy names collected from the current SDK at build time. It forces
quiet logging and creates no player log files. Normal graphics/media settings,
shader caching and the optional FPS overlay remain available. Diagnostic probes
are separate research launches; do not use clean-play mode for captures.

The native guard is C++; its Python name collector runs only during the build.
Patch 0119 also makes DirectX's previously unconditional presentation benchmark
ring and CSV opt-in with `RSX_PRESENT_MEASURE=1`. Clean-play mode always blocks
that recorder, while keeping the lightweight FPS overlay metrics functional.


`tools/build.ps1` produces the Debug build used by the regression suite. The
patch-0076 town timing uses an isolated Release `/MD` build with matching
Release dependencies. The example below instead enables optimization in the
standard Debug `/MDd` dependency layout; it does not reproduce that exact
benchmark configuration. After the standard build has prepared dependencies
and regenerated the current game inputs,
configure it as follows in PowerShell, replacing the two placeholder paths:

```powershell
$workspace = (Resolve-Path '<private-workspace>').Path
$checkout = (Resolve-Path '<checkout>').Path
. "$checkout/tools/windows-env.ps1" -Workspace $workspace
. "$checkout/tools/media-env.ps1" -Workspace $workspace
$optimized = Join-Path $workspace 'work/build/wkc2-optimized'
cmake -S $checkout -B $optimized -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  '-DCMAKE_C_FLAGS_DEBUG=/MDd /O2 /Ob2' `
  '-DCMAKE_CXX_FLAGS_DEBUG=/MDd /O2 /Ob2' `
  '-DCMAKE_C_FLAGS=/w /arch:AVX2' '-DCMAKE_CXX_FLAGS=/w /arch:AVX2' `
  "-DPS3RECOMP_DIR=$workspace/work/deps/ps3recomp" `
  "-DRECOMP_DIR=$workspace/work/wkc2-generated/ppu" `
  "-DSPU_DIR=$workspace/work/wkc2-generated/spu" `
  "-DDIRECTX_HEADERS_DIR=$workspace/work/deps/DirectX-Headers" `
  "-DPython3_EXECUTABLE=$workspace/work/venv/Scripts/python.exe" `
  -DPORT_ENABLE_SPU=ON -DWKC2_BUILD_TESTS=ON `
  -DPS3RECOMP_FFMPEG_RUNTIME=ON `
  "-DPS3RECOMP_FFMPEG_INCLUDE_DIR=$workspace/work/deps/ffmpeg-8.1" `
  -DPS3RECOMP_RSX_VULKAN=ON `
  "-DCMAKE_PREFIX_PATH=$workspace/work/deps/vulkan-prefix" `
  "-DPS3RECOMP_VULKAN_INCLUDE_DIR=$workspace/work/deps/Vulkan-Headers/include"
if ($LASTEXITCODE) { throw 'Optimized configuration failed' }
cmake --build $optimized -j 4
if ($LASTEXITCODE) { throw 'Optimized compilation failed' }
ctest --test-dir $optimized -R wkc2_ --output-on-failure
if ($LASTEXITCODE) { throw 'Optimized regression checks failed' }
# Play-like timing: keep profiling and per-line diagnostic file writes off.
$env:WKC2_DISCARD_LOGS = '1'
Remove-Item Env:WKC2_SAMPLE_PROFILE -ErrorAction SilentlyContinue
& "$checkout/tools/probe.ps1" -Workspace $workspace -Renderer Vulkan `
  -Executable "$optimized/WKC2Native.exe" -Seconds 900 -ShowWindow
```

This retains the Debug C runtime and C++ ABI (`/MDd`) used by the prepared
glslang and SDL2 libraries while enabling compiler optimization. It is not a
Release-CRT build. `/arch:AVX2` permits hardware FMA for the SPU helpers' explicit
`fmaf` operations and requires a CPU with AVX2 and FMA support. Omit that option
on older processors; their performance has not been measured.

Use Continue to load the verified town save for repeat runs. Repeating the
intro and avatar creator is unnecessary when that slot is present. The save is
private game data and is not distributed by this repository.

Window titles show only the game name on Vulkan and DirectX. Patch 0108
removes the former title-bar statistics. The optional in-game FPS overlay is
controlled separately in Graphics Settings and now works on live DirectX too.

Research switches below are cleared by the player launcher. Use a separate
probe environment to enable them:

- `RSX_PRESENT_MEASURE=1` enables the DirectX benchmark sample ring and CSV.
  `RSX_SCHEDULE_DIAG=1` adds process/thread times only when that recorder is enabled.
- `WKC_NATIVE_SKINNING_TRACE=1` counts native blend calls for coverage; normal
  play keeps it off. `WKC_NATIVE_SKINNING=0` retains the compiled reference path.
- `WKC2_SKIP_INTRO=1` skips the copyright and save notices and the publisher
  and middleware logos. The boot sequencer's system-setup steps still run.
- `WKC2_DISCARD_LOGS=1` sends stdout/stderr to NUL. Per-line diagnostic writes
  accounted for about a third of the main thread's time in the recorded
  cutscene profile. Leave logs enabled for diagnostic runs.
- `WKC2_SAMPLE_PROFILE=<file>` rewrites `<file>` every ten seconds with each
  thread's CPU share and its most-sampled module offsets; link with `/MAP` to
  resolve them. This Windows profiler briefly suspends threads to sample them
  and changes timing.
- `RSX_ENGINE_PRESENT_LOG=1` reports presents of surfaces nothing drew since
  their last present, plus frame rate and per-site GPU synchronisation cost
  every five seconds; `RSX_ENGINE_FPS_FILE=<file>` also writes the frame rate
  there, which survives `WKC2_DISCARD_LOGS`.
- `RSX_ENGINE_DRAW_CAPTURE=<dir>` writes every draw of one frame (state,
  fetched vertices, constants, indices) while `<dir>/arm` exists.
  `RSX_ENGINE_RED_TRIGGER=<dir>` keeps a per-frame draw log and saves it with
  the presented image when a sampled patch is strongly red.
  `RSX_ENGINE_WATCH_TARGET=<hex offset>` prints draws into one colour target.
- `RSX_VK_FLUSH_FILE=<file>` writes Vulkan flush counts and timing by source
  line every five seconds. Negative lines identify the asynchronous batch path;
  capacity-driven submissions can still wait for a busy spare slot. Patch 0076
  only performs optional early submissions when that slot is already complete.
- `RSX_VK_PRESENT_MODE=mailbox` or `immediate` requests that Vulkan present
  mode when the driver supports it; otherwise the backend retains FIFO.

Patch 0059 caches seven launch-time diagnostic settings per host thread:
`RSX_DRAW_AUDIT`, `RSX_INDEX_TRACE`, `SPURS_JOB_TRACE_FP`,
`SPURS_JOB_DESCDUMP`, `SPURS_JOB_DUMP`, `SPURS_JOB_EXIT_TRACE` and
`SPU_ARGWATCH`. Set them before launch; changing them in a running process
does not refresh a thread's cache. Presence-based switches still enable with
an empty value or `0`; descriptor levels and fingerprint parsing are retained.
At patch 0059, all 51 Windows checks passed. Existing fixtures also verify six
presence switches set to `0` still emit diagnostics while preserving their normal output.

`tools/probe.ps1 -ShowWindow` keeps the game window visible during a probe.
Draw/shader captures, profiler files and logs stay in the private workspace.
These options can change timing. With the physics fix included, two one-minute
hidden-window starting-town samples measured 8.1-8.3 fps at patch 0058 and
9.3-9.6 fps at patch 0059, using the optimized Debug-ABI build, sampling profiler
and five-second readbacks. The private benchmark launcher omitted `-ShowWindow`. The immediate target is a steady 30 fps, followed by validated 60 fps
support. Later hidden-window 64-patch Release-CRT samples measure 12.5-13.1 fps;
see the conversion/DMA section above. These are relative diagnostic benchmarks
and do not establish visible gameplay FPS or character-rotation smoothness; see
[conditions and records](FINDINGS.md#town-performance-after-the-physics-fix).

## Diagnostic probes

Run `tools/probe.ps1 -Seconds 15` for a bounded diagnostic attempt. Logs and a
result JSON go into `work/probes/`; unrecognized SPU jobs go into the private
analysis directory. Probes record the last observed guest flip and presented-frame
counts every five seconds; these counters alone do not verify visible content.
For D3D12, `-CaptureFrames` saves four backend BMP readbacks by default in a
private probe subdirectory. `-CaptureFrameCount`, `-CaptureEvery` and `-CaptureSkip` select a
longer sampling interval. Vulkan rejects `-CaptureFrames`; set
`RSX_ENGINE_SNAP_DIR` and `RSX_ENGINE_SNAP_EVERY_S` for its PPM readbacks instead.
Probes accept durations up to 3600 seconds, with each
host wait bounded to five seconds. `PAD_SCRIPT` can supply timed controller pulses
through the SDK's controller path; the probe records that script in its result.
Capture suppresses the runner's pre-flip placeholder presents, changing
presentation timing for diagnostics. `WKC2_THREADS=1` adds guest thread register
snapshots at each progress report. `-InterpProfile` writes the SPU interpreter
census described under patch 0042 to `work/probes/<tag>.spu-interp.tsv`.
Use `-Executable <private-executable>` to inspect a separately prepared build.
Earlier new-game diagnostic routes exceeded ten minutes. Current repeat-town
validation uses Continue and the verified saved slot. Allow for loading and
initial shader creation before timing the settled view; do not treat a probe
that ends during loading as a town performance result.
`tools/trace_calls.py` optionally adds opt-in return/argument
tracing to selected generated functions. Those diagnostics are disabled unless
`WKC2_TRACE_CALLS=1` is set.

### DirectX FXAA (patch 0110)

The native Off/FXAA option now reaches the live DirectX renderer. Both renderers
share the project-written shader math; API bindings remain separate. DirectX
filters RGBA8 scanout into a separate image before copying it to the swap chain
and adding the sharp FPS overlay. Output and descriptor reuse follow the
existing completed presentation fence. Off has no AA allocation or dispatch.

The new wkc2_d3d12_fxaa_gpu check exercises eight WARP cases against independent
CPU math, with a tolerance of two UNORM8 RGB values and exact alpha/source preservation,
odd dimensions, Off, resize and fenced reuse. The existing Vulkan descriptor
check still compiles the production shader. All 99 Windows checks pass in Debug
and Release. A hidden, muted isolated saved-scene run verifies the menu toggle,
real pipeline/output allocation and return to world rendering with short motion.
Native movie/fullscreen/area-transition parity remain separate checks.

### DirectX guest-surface readback (patch 0111)

The avatar-copy bridge now selects the active renderer. Live DirectX can copy
an exact linear ARGB8 render target into guest RAM after GPU completion. Pitch
padding and bytes outside the request remain untouched. The path rejects
unknown or ambiguous targets and unsupported layouts; it is not a general
surface-coherence implementation. A fresh isolated DirectX creator run verifies
the real generated image and ready flag. Same-session menu display and saving
that generated image remain unverified; cutscene-transition rendering faults
were observed afterward. The user's original save was unchanged.

wkc2_graphics_readback now checks both backend routes and failure propagation;
the previous host code fails that regression. The new wkc2_d3d12_surface_readback
uses WARP to verify eight exact ARGB/RGBA cases, including 256x144 portrait data,
padding, partial pixels/rows and invalid ranges. All 100 Windows checks pass
in Debug and Release. Normal launch still sends no automatic controller input.

### DirectX clears, comparison sampling and polygon bias (patches 0112–0114)

`wkc2_d3d12_masked_clear` checks 128 WARP cases for individual RGBA masks,
RGBA8/FP16, clipping and depth/stencil preservation. `wkc2_d3d12_shadow` checks
144 comparison/PCF/border results using the production sampler decoder.
`wkc2_d3d12_depth_bias` checks sixteen D32 depth cases using the production
bias decoder. `wkc2_shadow_compare` additionally checks the literal-constant
comparison entry and zero-mask compatibility. The complete stack has 103
passing Windows checks in Debug and Release at patch 0115.

Native Continue/movement/camera runs execute the comparison path and polygon
bias. The character shadow still needs visual validation; there is no complete
shadow, darker-grass or flicker fix claim. The [parity audit](DIRECTX_PARITY.md)
tracks other Vulkan features that the live DirectX caller does not yet forward.

### Kernel mutex deadline (patch 0115)

An existing optional-kernel-lock test returned before its 15 ms timeout.
The kernel path now retries early timeout results until a monotonic deadline;
other errors, successful acquires and infinite/zero waits retain their behavior.
The regression forces three early results and still requires the full deadline.
Both complete suites and ten repeated Release kernel tests pass. The default
user-token backend is unchanged.

### Static SPU entries and strict diagnostics (patches 0116-0117)

`tools/build.ps1 -Regenerate` uses `tools/build_spu.py` to add five validated
indirect entries to the pinned baseline images. Hash mismatches, missing inputs
or unreviewed extra images fail explicitly. The registry remains 24 inputs;
known obsolete diagnostic wrappers are skipped without deleting private files.

For route validation, set `SPU_FALLBACK_STRICT=1` before launch. The log must say
`[spu-fallback] strict entry guard enabled`. Any interpreter or microstep entry
logs `[spu-fallback-forbidden]` with image/address and terminates with code 86,
before execution. This is an opt-in diagnostic, not a normal-play default.
Set `SPU_INTERP_PROFILE` to a private TSV path as well to retain an explicit
zero-row table on a route with no fallback. Unset strict mode after testing.

The new entry-manifest check and two forbidden-entry negative controls pass
alongside the existing profile on/off tests. Both complete Windows suites pass
106 checks. The scoped saved-town run and replay limits are in
[native execution work](NATIVE_CODE.md); do not infer complete game coverage or
an FPS improvement from a zero-row census.

### DirectX near/far clipping (patch 0118)

Live DirectX explicitly enables near/far rasterizer clipping. The eight-case
`wkc2_d3d12_raster_clip` WARP regression checks rejection, clip boundaries,
homogeneous W and old disabled-state controls. Both Windows suites pass 107
checks. The final native candidate reaches Balastor Plain after Continue and
preserves Leonard's lower body through movement/camera inspection with zero
strict SPU fallback entries. Non-default depth-clamp modes remain unimplemented.
See [screenshots and scope](GALLERY.md#2026-10-19--directx-plains-clipping-fixed).

## Clean-play diagnostics (patch 0119)

The live DirectX recorder previously collected QPC samples on every successful
presentation and dumped a CSV at its benchmark frame or shutdown even without a
diagnostic switch. It now runs only with `RSX_PRESENT_MEASURE=1`, and never in
player mode. `wkc2_present_measure` compiles the actual SDK recorder functions
into a GPU-free fixture and checks eight default, opt-in, scheduling, player-mode
and file-output cases. The normal FPS overlay metrics still update.
`wkc2_play_environment` poisons a child environment and verifies both CRT and
Windows API variable removal, preserved media/PATH settings, no diagnostic output
and an unaffected explicit research-mode control.

## DirectX fragment coordinates (patch 0120)

The live path now forwards TC0–7 2D input override masks in both fragment constant
modes, with the mask included in PSO identity. Vulkan uses the same decoder.
`wkc2_fragment_inputs` verifies 64 DirectX shader compilations across eight
coordinates, perspective modes and request masks, plus old zero-mask compatibility
and invalid-bit rejection. The native [foliage/cloud/grass audit](DIRECTX_PARITY.md#foliage-cloud-shade-and-grass-comparison)
keeps observed state requests separate from unverified visual causes.
