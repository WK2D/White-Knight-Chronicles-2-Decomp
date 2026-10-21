# Reference

Quick reference for the terms, runtime switches and game addresses used across the
documentation. The [build guide](BUILD.md) explains how to run the game; the
[lessons page](LESSONS.md) explains the bugs these tools helped find.

## Glossary

| Term | Meaning |
| --- | --- |
| **PPU** | The PS3's main PowerPC core. Its code is lifted to C++ ahead of time. |
| **SPU** | One of the PS3's vector coprocessors. Each runs code from its own 256 KB local store. |
| **Local store (LS)** | An SPU's private memory. Code and data are moved in and out by DMA. |
| **MFC** | Memory Flow Controller: an SPU's DMA engine, with command queue and tag groups for completion. |
| **Reservation / lock line** | A 128-byte line an SPU reserves with `GETLLAR` and commits with `PUTLLC`; the basis of SPU atomics. |
| **SPURS** | Sony's SPU task and job scheduler. WKC uses taskset tasks, job chains and job queues. |
| **Job chain / job queue** | SPURS lists of small SPU jobs. Chains run job commands in order; queues accept pushed jobs. |
| **Job guard** | A SPURS counter a job chain waits on until the PPU notifies it. |
| **Edge** | Sony's SPU geometry library (skinning, culling). WKC's geometry jobs use it. |
| **RSX** | The PS3 GPU. The port translates its command stream to Vulkan. |
| **GCM** | The PS3 graphics library that writes RSX commands and manages display buffers. |
| **FIFO / command buffer** | The stream of RSX method writes the game submits; `put` and `get` mark progress. |
| **Method** | One RSX register write in the command stream, identified by its offset (for example `0xEB00`). |
| **Subchannel / object** | RSX command slots bound to classes such as NV4097 (3D) or NV0039 (memory copy). |
| **Label / semaphore** | GPU-written memory values that the game and SPUs poll to know the GPU's progress. |
| **Flip** | Switching the displayed buffer. In vsync mode a flip completes at a vblank. |
| **Vblank** | The display's vertical blank, about 59.94 times a second; the game counts these for timing. |
| **Surface / render target** | An RSX colour or depth buffer that draws write into. |
| **Zeta** | The RSX depth/stencil surface. |
| **Snapshot** | A GPU copy of a surface taken so it can be sampled while the original is still being drawn. |
| **HLE** | High-level emulation: a native implementation of a PS3 library function instead of its original code. |
| **Syscall** | A call into the PS3 kernel (LV2), implemented natively by the runtime. |
| **Lifting** | Translating PPU or SPU machine code to C/C++ ahead of time. |
| **Interpreter fallback** | SPU code run instruction by instruction because no lifted version exists for it. |
| **Presentation** | Handing a finished frame to the host window. Presentation counters count these, including repeated animation states. |

## Environment variables

Desktop play uses `WKC2_PLAYER_MODE=1` to clear inherited research switches
before startup and force quiet logs. The table describes separate diagnostic
launches; those switches cannot enable diagnostics in player mode.

Switches read by the runtime at startup. Diagnostics are off unless set; the build
guide's [playing and performance options](BUILD.md#playing-and-performance-options)
gives the play defaults. Keep any files these write private.

### Player mode and native geometry

| Variable | Effect | Details |
| --- | --- | --- |
| `WKC2_PLAYER_MODE=1` | Native clean-play guard; clears inherited research/input switches and forces quiet logs | [Build guide](BUILD.md#playing-and-performance-options) |
| `RSX_PRESENT_MEASURE=1` | Opt-in DirectX benchmark ring/CSV, always blocked in player mode | [Build guide](BUILD.md#presentation-measurement) |
| `WKC_NATIVE_GEOMETRY_JOB=1` | Enables admitted image-19 geometry jobs and image23 visibility modes 1–4. Image19 and visibility modes 1/2/4 use Windows host-float math. Unset or `0` leaves dispatch, scope gates and PUT observers unregistered and silent. | [Build guide](BUILD.md#playing-and-performance-options) |
| `WKC_JOB_CAPTURE=<dir>`, `WKC_JOB_CAPTURE_IMAGE=<id>`, `WKC_JOB_CAPTURE_COUNT=<n>` | Record complete job transactions (descriptor, every DMA GET and PUT) for one image once `<dir>/arm` exists; cleared in player mode | [Patch 0129](PATCHES.md#dma-read-trace-hook-and-job-capture-patch-0129) |
| `WKC_GEOM_PIPELINE=<n>` | Worker threads for pipelined image-19 jobs; `0` runs them serially. Kept in player mode | [Patches 0127-0128](PATCHES.md#pipelined-geometry-jobs-and-sub-millisecond-sleeps-patches-0127-0128) |
| `WKC_GEOM_PIPELINE_VERIFY=1` | Re-prepares every 64th pipelined job serially and aborts on any difference; cleared in player mode | [Patches 0127-0128](PATCHES.md#pipelined-geometry-jobs-and-sub-millisecond-sleeps-patches-0127-0128) |
| `WKC_NATIVE_GEOMETRY_DIAGNOSTICS=1` | Separately enables sampled whole-job admission/result logs when the native job runtime is enabled; does not enable it by itself | [Build guide](BUILD.md#playing-and-performance-options) |
| `WKC_NATIVE_SKINNING_TRACE=1` | Count native blend calls for coverage | [Native kernel](NATIVE_CODE.md#readable-c-geometry-prefix) |
| `WKC_NATIVE_SKINNING=0` | Use retained compiled geometry prefix for research comparison | [Native kernel](NATIVE_CODE.md#readable-c-geometry-prefix) |

The whole-job route declines other constructor/layout domains before native
external effects and retains compiled fallback. Failures after effects are fatal
after local cleanup, without fallback replay. Original compiled SPU code remains
linked. Ordinary host operations use authored C++; special negative multiply,
reciprocal estimates and conversions retain SDK adapters. No complete image
replacement or new visibility FPS gain is established.
The player launcher preserves its explicit `-NativeGeometryJob` selection;
native-job diagnostics, captures, profilers and input injection remain blocked
in clean player mode. Separate diagnostic launches are required for those
measurements.

### SPU coverage diagnostics

| Variable | Effect |
| --- | --- |
| `SPU_FALLBACK_STRICT=1` | Reject interpreter and microstep entrances before execution, log the image/address and terminate with code 86. Opt-in diagnostic; unset for normal play. |
| `SPU_INTERP_PROFILE` | Private completed-session TSV path; startup creates an explicit zero-row table when no fallback occurs. |

See [route coverage and limits](NATIVE_CODE.md).

### Play and pacing defaults

| Variable | Effect | Documented in |
| --- | --- | --- |
| `WKC2_SKIP_INTRO=1` | Skips the copyright and save notices and the publisher and middleware logos | [Build guide](BUILD.md#playing-and-performance-options) |
| `WKC2_DISCARD_LOGS=1` | Sends stdout and stderr to NUL | [Build guide](BUILD.md#playing-and-performance-options) |
| `GCM_FLIPCB_ONTICK=1` | Delivers flip callbacks at vblank after a real flip request (startup default) | [Build guide](BUILD.md) |
| `WKC2_FLIP_ON_VBLANK=0` | Turns off waiting for the next vblank at each flip | [Build guide](BUILD.md) |
| `GCM_SEMA_ACQUIRE=1` | Honours the game's two-vblank semaphore gate (startup default) | [Frame pacing](FINDINGS.md#frame-pacing-and-game-speed) |
| `RSX_VK_CONSTANT_REUSE=0` | Disables exact constant-block offset reuse and restores per-draw Vulkan uniform uploads | [Patch 0103](PATCHES.md#vulkan-constant-upload-reuse-patch-0103) |
| `RSX_VK_PRESENT_MODE` | `mailbox` or `immediate` requests that Vulkan present mode; otherwise FIFO | [Build guide](BUILD.md#playing-and-performance-options) |
| `RSX_ENGINE_ZTEX_GPU=1` | GPU re-encoding of depth read as colour (startup default) | [Patches 0067-0070](PATCHES.md#depth-sampling-and-real-flip-completion-patches-0067-0070) |
| `RSX_ACCUM_FRAME=1` | Presents complete batches at guest flips (set by the runner) | [Frame presentation](SUBSYSTEMS.md#guest-frame-presentation) |
| `GCM_FIFO_KICK=0` | Keeps the bounded sleep fallback instead of the FIFO kick event | [Patches 0062-0064](PATCHES.md#fifo-producer-wake-and-spu-execution-helpers-patches-0062-0064) |
| `GCM_SUBCH1_2D=1` | Treats subchannel 1 as the NV0039 copy object (runner default) | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |

### Frame rate, timing and profiling

| Variable | Effect | Documented in |
| --- | --- | --- |
| `RSX_ENGINE_PRESENT_LOG=1` | Reports stale presents, frame rate and per-site GPU synchronisation cost every five seconds | [Build guide](BUILD.md#playing-and-performance-options) |
| `RSX_ENGINE_FPS_FILE=<file>` | Also writes the frame rate to a file, which survives `WKC2_DISCARD_LOGS` | [Build guide](BUILD.md#playing-and-performance-options) |
| `RSX_VK_FLUSH_FILE=<file>` | Writes Vulkan flush counts and timing by source line every five seconds | [Build guide](BUILD.md#playing-and-performance-options) |
| `WKC2_SAMPLE_PROFILE=<file>` | Built-in sampling profiler: each thread's CPU share and most-sampled offsets every ten seconds | [Build guide](BUILD.md#playing-and-performance-options) |
| `WKC_TRACE_GATE`, `WKC_TRACE_OUT` | Ten-second Windows frame timeline, started by creating the gate file | [Patch 0077](PATCHES.md#optional-windows-frame-timeline-patch-0077) |
| `GCM_PRODUCER_PROFILE` | Host-only counters for FIFO producer parking and wakes | [Patches 0062-0064](PATCHES.md#fifo-producer-wake-and-spu-execution-helpers-patches-0062-0064) |

### Rendering capture and inspection

| Variable | Effect | Documented in |
| --- | --- | --- |
| `RSX_ENGINE_SNAP_DIR`, `RSX_ENGINE_SNAP_EVERY_S` | Periodic GPU readbacks of the presented frame as PPM files | [Diagnostic probes](BUILD.md#diagnostic-probes) |
| `RSX_ENGINE_DRAW_CAPTURE=<dir>` | Writes every draw of one frame while `<dir>/arm` exists | [Build guide](BUILD.md#playing-and-performance-options) |
| `RSX_ENGINE_RED_TRIGGER=<dir>` | Saves the draw log and image when a sampled patch is strongly red | [Build guide](BUILD.md#playing-and-performance-options) |
| `RSX_ENGINE_WATCH_TARGET=<hex>` | Prints draws into one colour target | [Build guide](BUILD.md#playing-and-performance-options) |
| `RSX_ENGINE_AUDIT` | RSX request audit (enabled by `tools/probe.ps1` by default) | [Patches 0095-0096](PATCHES.md#rsx-request-audit-and-fragment-inputs-patches-0095-0096) |
| `RSX_SPIRV_CACHE=0` | Disables reuse of translated Vulkan shaders, for comparison | [Patch 0100](PATCHES.md#spir-v-translation-cache-patch-0100) |
| `RSX_ENGINE_SHADER_DUMP=<dir>` | Saves vertex-program binaries and HLSL for each new pipeline | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `RSX_ENGINE_ALL_SUBCHANNELS=1` | Restores the old behaviour of applying all subchannels to the 3D engine | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `RSX_DRAW_AUDIT=1` | Draw observation in private frame captures | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `RSX_DRAW_SNAP_FILE`, `RSX_DRAW_SNAP_DEPTH=1` | Readbacks after selected draw indices, optionally with depth | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `RSX_CAP_DIR`, `RSX_CAP_GEOMETRY=1`, `RSX_CAP_GEOMETRY_FULL=1` | Frame captures with geometry input; FULL keeps every converted vertex | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `RSX_INDEX_TRACE=N` | Captures up to 64 indexed strip/fan input packets | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `RSX_CMP_TRACE=1` | Logs a few fetched samples per vertex attribute | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `TEX_RGBA=0/1` | Selects how A8R8G8B8 texels are stored on the host | [Patch 0073](PATCHES.md#vulkan-depth-comparison-patch-0073) |
| `DEPTH_OFF=1` | Existing depth-off diagnostic, used to show the room and avatar during early investigation | [Startup checkpoints](FINDINGS.md#startup-and-early-wkc1-checkpoints) |
| `SCREEN_AS_TEX=1` | Opt-in legacy screen-size texture substitution | [Investigation history](FINDINGS.md#investigation-history) |

### GCM command stream

| Variable | Effect | Documented in |
| --- | --- | --- |
| `GCM_M2MF_VERIFY=1` | Compares copied destination bytes against the staged source | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `GCM_SUBCH_GATE=<file>` | While the file exists, traces subchannel method sequences | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `GCM_FIFO_PROGRESS=1` | Read-only view inside FIFO drains | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `WKC2_FIFO_TRACE=1` | Observation at the game's graphics wait timeout | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `WKC2_FIFO_REGION=<ea>:<len>` | With FIFO tracing, keeps the last writers of each command-buffer word | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |

### SPU and SPURS

| Variable | Effect | Documented in |
| --- | --- | --- |
| `SPU_FLOAT_MODE` | Host float results, `audit` counting, or exact SPU rules | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `SPU_DMA_REPEAT_LIMIT=N` | Opt-in cutoff after N identical DMA transfers; absent or zero disables it | [Completion-label polling](FINDINGS.md#wkc1-town-and-spu-completion-label-polling) |
| `SPU_HOST_DEPTH_MAX` | Host recursion limit for lifted SPU code (default 2000) | [Patches 0062-0064](PATCHES.md#fifo-producer-wake-and-spu-execution-helpers-patches-0062-0064) |
| `SPU_INTERP_PROFILE=<file>` | Profiles interpreter fallback use | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `SPU_CRI_IMAGE` | Image selected for the legacy CRI task path (runner sets `-1`) | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `RD_SPU_INTERP` | Pure SPU interpretation mode | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `PS3RECOMP_SPU_BYTE_SCALAR=1` | Forces scalar SPU byte operations for comparison | [Patches 0091-0093](PATCHES.md#task-completion-and-spu-byte-operations-patches-0091-0093) |
| `SPURS_JOB_ZERO_LS=1` | Restores full local-store clearing between jobs | [Patches 0053-0057](PATCHES.md#renderer-coherence-and-execution-overhead-patches-0053-0057) |
| `SPU_LAUNCH_CHECK_ALL=1` | Runs every task launch check, for comparing traces | [Patches 0053-0057](PATCHES.md#renderer-coherence-and-execution-overhead-patches-0053-0057) |
| `SPURS_JOB_TRACE_FP=<hex>` | Focused trace of one SPU job binary by fingerprint | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `SPURS_JOB_DESCDUMP`, `SPURS_JOB_DUMP`, `SPURS_JOB_EXIT_TRACE`, `SPU_ARGWATCH` | Job descriptor, job state, exit and argument tracing (cached at launch) | [Build guide](BUILD.md#playing-and-performance-options) |
| `SPURS_JOB_ABI_TRACE=1` | Reports the first bound job ABI queries per thread | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `WKC2_JOB_CAPTURE_FILE`, `WKC2_JOB_CAPTURE_DIR` | Private job capture request and output locations | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |

### Input, threads and services

| Variable | Effect | Documented in |
| --- | --- | --- |
| `PAD_FILE=<file>` | Private controller probe: timed buttons and analog sticks written to a file | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `PAD_SCRIPT` | Timed controller pulses through the SDK's controller path | [Diagnostic probes](BUILD.md#diagnostic-probes) |
| `PAD_STICK` | Fixed analog stick positions | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `WKC2_OSK_INPUT_FILE=<file>` | Scripted text entry: `submit <text>` or `cancel` | [Native text entry](SUBSYSTEMS.md#native-text-entry) |
| `WKC2_THREADS=1` | Guest thread register snapshots at each progress report | [Diagnostic probes](BUILD.md#diagnostic-probes) |
| `WKC2_TRACE_CALLS=1` | Enables the return/argument tracing that `tools/trace_calls.py` adds to selected functions | [Diagnostic probes](BUILD.md#diagnostic-probes) |
| `WKC2_UPDATE_DIAGNOSTICS=1` | Dispatcher and scene state samples | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |
| `PS3RECOMP_FFMPEG_AVCODEC`, `PS3RECOMP_FFMPEG_AVUTIL` | Paths to matching FFmpeg libraries for movie decoding | [Movie decoding](SUBSYSTEMS.md#movie-decoding) |

## Known game addresses

Addresses in the USA BLUS30784 v01.00 executable that the documentation names.

| Address | Kind | Role | Documented in |
| --- | --- | --- | --- |
| `0x00325248` | PPU | Boot sequencer; hooked by `WKC2_SKIP_INTRO` | [Cutscene red flash and frame rate](FINDINGS.md#cutscene-red-flash-and-frame-rate) |
| `0x00AEF65C` | PPU | The game's own graphics wait, polling label `0x48` | [Town performance](FINDINGS.md#town-performance-after-the-physics-fix) |
| `0x0015D9EC` | PPU | Applies secondary-bone physics results to the posed skeleton | [Secondary-bone physics](FINDINGS.md#secondary-bone-physics-and-town-stretching) |
| `0x0019F2C4` | PPU | Fetches a mapped physics bone's world matrix | [Secondary-bone physics](FINDINGS.md#secondary-bone-physics-and-town-stretching) |
| `0x00A7B0EC` | PPU | Builds skinning palettes from posed bones and inverse bind matrices | [Secondary-bone physics](FINDINGS.md#secondary-bone-physics-and-town-stretching) |
| `0x00B0FA18` | PPU | Copies pose data | [Secondary-bone physics](FINDINGS.md#secondary-bone-physics-and-town-stretching) |
| Image 19 | SPU job | Geometry decoding and skinning | [Secondary-bone physics](FINDINGS.md#secondary-bone-physics-and-town-stretching) |
| Image 24 | SPU job | Secondary-bone physics job (21,408 bytes) | [Patch 0058](PATCHES.md#spurs-job-queue-physics-patch-0058) |
| `0xC0000000` | Memory | RSX local memory, 256 MiB | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
| `0xD0000000` | Memory | PPU stack allocator region | [Earlier fixes](PATCHES.md#earlier-runtime-fixes-and-validation-history) |
