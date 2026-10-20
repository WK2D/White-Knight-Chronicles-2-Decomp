# Current Windows build workflow

This builds the current static recompilation/runtime project, including an
observed SPU interpreter fallback. It does not establish fully static CPU
coverage. See [architecture and modding scope](ARCHITECTURE.md).

**Current validation (2026-10-20):** the stack contains 126 SDK patches. All
117 Windows checks pass in normal Debug (11.93 seconds) and optimized Release
(11.77 seconds). Reapplying the complete stack changes no SDK files.
Strict DirectX and Vulkan routes on patch
0121 confirm Continue, the plains transition, walking, camera rotation and the
leaf-cutout shadow correction. Menu FXAA toggling was checked separately on 0110.
What each patch changes, how it was checked and its measured
results are in the [patch notes](PATCHES.md).

The latest source connects a public readable C++ runtime for complete supported
image-19 geometry jobs to main. It acquires original descriptors and inputs, then
executes the typed pipeline and allocation/export/publication coordinator through
actual SDK memory, native CAS and PUT operations. All 32 recorded jobs replay with
byte-exact external outputs and zero mismatches. C++ owns the job inputs and
outputs. Seven host arithmetic/comparison operations now use the
[authored C++ policy](../native_geometry_host_math.h); special negative multiply,
reciprocal estimates and conversions still use SDK adapters. See the
[native output transaction evidence](NATIVE_CODE.md#native-allocation-export-and-command-construction)
and [public runtime](../native_geometry_runtime.cpp).
The live route is explicitly opt-in; unsupported constructor/layout domains retain
compiled fallback before native external effects. Failures after effects stop
execution after local cleanup, without replaying fallback. Original compiled SPU
code remains linked, and no complete SPU image has been replaced.
The first instrumented comparison regressed with debug logging enabled;
profiling found `WriteFile` hot. A later quiet A/B used the same executable with
the FPS overlay enabled and compared two stationary town views:

| View | Native fps | Compiled fps | Native CPU ms/presentation | Compiled CPU ms/presentation |
| --- | --- | --- | --- | --- |
| Logic Stone | 29.914 | 29.996 | 127.13 | 139.71 |
| Facing the entrance | 19.746 | 20.581 | 182.36 | 188.20 |

Those initial samples show near parity and no FPS gain. A later
[classifier-only A/B](NATIVE_CODE.md#native-classification-and-packet-construction)
improves stationary crowded views from 7.583/7.750 to 9.915/9.999 FPS inside the
street. The subsequent connected host-arithmetic run measures 13.164 FPS in one
unpaired sample, with a 393.468 ms maximum interval; lag remains. These samples
do not prove smooth walking, animation speed or 30 FPS. Earlier movement segments ended
at the Leave area exit prompt and were excluded, so there is no benchmark for
walking among NPCs on the main road or for area transitions. Broader layouts and
NPC-heavy traversal remains unverified. No complete image has been replaced.
The local desktop player has now been updated at the user's request: supported
native jobs enabled, manual startup, 30 FPS baseline, diagnostics disabled and
saves preserved. Other graphics preferences are unchanged.
Patch 0123 corrects the SPU machine-status interrupt bit; this source change has
passed regression tests but has no new live game route or desktop installation.

Suite timings are not game performance. Full rendering fidelity, smooth traversal,
combat and a locked 60 fps remain unverified. Historical measurements below retain
their original build and diagnostic conditions.

## Contents

- [Requirements and workspace](#requirements-and-workspace)
- [Building and regression checks](#building-and-regression-checks)
- [Playing and performance options](#playing-and-performance-options)
- [Diagnostic probes](#diagnostic-probes)
- [Runtime patch notes](PATCHES.md): what each SDK patch changes, newest first
- [Runtime subsystem notes](SUBSYSTEMS.md): renderer, movie decoding, frame presentation and text entry

The public player launcher supports `-NativeGeometryJob` to enable this
supported native route without enabling diagnostics. It explicitly writes `0`
when the switch is omitted, preventing an inherited experimental selection.
The clean-player guard preserves that functional selection while stripping
native-job diagnostics, capture, profiling and input-injection switches.

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
matrix-blend and complete supported batch hooks; rerunning it validates the retained
references and wrapper. The earlier blend-only, blend/position and cofactor versions upgrade
after the same validation.
See [kernel evidence and limits](NATIVE_CODE.md#readable-c-geometry-prefix).
`tools/apply_native_normals.py` then installs the independently hash-checked
final-normal output boundary. It retains a private compiled reference and
rejects edited bodies or wrappers. The same hook regression checks both
installations. See [normal-output evidence](NATIVE_CODE.md#readable-final-normal-output).
`tools/apply_native_stream.py` then installs the supported complete skinning
routine, retaining a private compiled reference for unsupported interfaces.
The hook regression checks its hash, edited-wrapper rejection and idempotence.
See [whole-routine evidence](NATIVE_CODE.md#readable-whole-skinning-stream).
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
The current source stack contains 126 numbered SDK patches, and all 117 checks
pass in normal Debug and optimized Release. Graphics panel
captures use 83 patches; saving
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

Triangle -> System -> Graphics Settings is available in the native game, including
Off/FXAA, frame-rate limits and an experimental DirectX 12 selector. See the
[menu screenshots](GALLERY.md#2026-10-18---full-graphics-settings-panel-and-window-close)
and [settings and timing details](PATCHES.md#native-graphics-settings-patches-0081-0082).

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
- `WKC_NATIVE_GEOMETRY_JOB=1` enables the public runtime for complete supported
  image-19 geometry jobs in Windows host-float mode. Unset or `0` leaves this
  route's dispatch, scope gates and PUT observers unregistered and silent.
  Unsupported constructor/layout domains decline before native external effects
  and run the retained compiled job. A failure after effects is fatal after local
  cleanup; compiled fallback cannot run over partially published output.
  `WKC_NATIVE_GEOMETRY_DIAGNOSTICS=1` separately enables sampled admission and
  result logs; it does not enable the runtime by itself. This remains a research
  option while performance optimization and broader route validation continue.
- `WKC_NATIVE_SKINNING_TRACE=1` reports native calls, declines and verification
  counts; normal play keeps it off. `WKC_NATIVE_SKINNING=0` retains the earlier
  batch's compiled reference path.
- `WKC_NATIVE_SKIN_STREAM=0` disables the whole-routine hook and selects its
  compiled reference. `WKC_NATIVE_SKIN_STREAM_VERIFY=1` compares complete state
  for the first 32 attempts and every 256th attempt thereafter, aborting on a
  mismatch. This allocates/copies private state and changes performance; use it
  only in research runs.
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

### Strict SPU fallback guard

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
Do not infer complete game coverage or an FPS improvement from a zero-row
census; see [native execution work](NATIVE_CODE.md) and
[patches 0116-0117](PATCHES.md#spu-running-status-and-fallback-entry-guards-patches-0116-0117).

### Presentation measurement

`RSX_PRESENT_MEASURE=1` enables the live DirectX presentation recorder and its
CSV output. Player mode always disables it; the normal FPS overlay still updates.
See [patch 0119](PATCHES.md#opt-in-directx-presentation-diagnostics-patch-0119).
