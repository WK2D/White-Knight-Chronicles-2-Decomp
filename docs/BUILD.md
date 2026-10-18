# Current Windows build workflow

This builds the current static recompilation/runtime project, including an
observed SPU interpreter fallback. It does not establish fully static CPU
coverage. See [architecture and modding scope](ARCHITECTURE.md).

**Latest rendering milestone (2026-10-18):** the 76-patch Windows Vulkan build
reaches the WKC1 starting town, loads the verified Logic Stone save through
Continue, and has normal shadow polarity. The clean one-minute stationary
sample averages 28.03 presentations/s; locked 30 fps, smooth traversal and full
game-speed equivalence remain unverified. See the
[latest gallery entry](GALLERY.md#2026-10-18---vulkan-batch-scheduling-in-the-saved-town)
and [timing conditions](#vulkan-batch-scheduling-patch-0076).

The current source stack contains 77 SDK patches. All 65 Windows Vulkan checks pass
in normal Debug (12.62 seconds) and optimized Release (12.70 seconds). These suites
ran three tests in parallel, so their elapsed times are not game performance.
Reapplying the 77-patch stack changes no SDK files. Patch 0074 restores normal shadow
polarity in the observed native town; black foot flicker remains. Patch 0075
repairs the FIFO user-command route to the registered HLE callback. Its native Logic Stone
interaction, save write and restart/Continue reload into town are verified.
The preceding 70-patch town run initially reaches 16.57 presentations/s, then
falls to about 7 while idle as asynchronous SPU workers accumulate. The
73-patch ten-minute idle test completes 17,104 task workers with 58–59 host
threads and no worker accumulation. It averages 14.25 presentations/s with the
sampling profiler off, five-second readbacks and a short capture burst;
this is not a matched performance comparison.
Earlier native records retain their original scope.

The port sets frame-pacing defaults at startup, each overridable by setting it
first: `GCM_FLIPCB_ONTICK=1` delivers flip callbacks at vblank after a real flip
request, and `RSX_VK_PRESENT_MODE=mailbox` prefers mailbox presentation when the
device supports it, with FIFO fallback. Flips wait for the next vblank
(`WKC2_FLIP_ON_VBLANK=0` turns that off). Vblanks are timed on the high-resolution
counter at 59.94 Hz. Note that the
window title labels the counter `Present/s`; repeated animation states still
count, so this can exceed the game's own update rate. The startup defaults also
set `GCM_SEMA_ACQUIRE=1` to honor the game's two-vblank semaphore gate and
`RSX_ENGINE_ZTEX_GPU=1` for supported GPU depth re-encoding. Patch
0068 allows flip completion only after an actual FIFO or immediate flip request;
`ResetFlipStatus` by itself does not create a completion. This uses the guest
pacing mechanism rather than adding a host 30 fps limit.

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
`tools/import_spu_misses.py` is available for discovering additional jobs.

`tools/prepare_sdat.py <private-USRDIR>` authenticates and caches the supported
SDAT archives. The build applies the numbered runtime patches idempotently
and runs 64 checks: real video decoding, sparse video timing, asynchronous video queues, LPCM queues/conversion, stereo/surround AC-3,
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
The current source stack contains 76 numbered SDK patches, and all 64 checks
pass in both Windows suites. The newest town capture uses 76 patches; saving
was first verified at 75, normal shadow polarity at 74, and the physics
checkpoint at 58. Earlier NPC-surface and controls captures retain their
52- and 42-patch provenance; the first town world-view capture used 39. Patch 0043 passed
the earlier 43-check Windows suite and remains covered by the current suite;
its town-rendering comparison is recorded separately.
Patch 0045's three float-mode checks and patches 0046-0051's new and extended
checks pass in the native Windows clang-cl build. All 64 checks pass with
76 patches in normal Debug and optimized Release; Wine's MinGW TLS workaround is not needed here.
Patch 0042 and its interpreter, disabled-mode and actual microstep/handoff
regressions now pass on Windows. Native census results are recorded in the
findings log.

## SPURS job-queue physics (patch 0058)

The physics update submits jobs through `_cellSpursJobQueuePortCopyPushBody`.
Its queue creation, submission and synchronization imports were unresolved,
so the runtime returned success without executing this work. Patch 0058 adds
guest-address queue/port records, owned descriptor copies and ordered native
SPU dispatch. Execution is synchronous per queue. Missing images and reported
staging failures persist through synchronization and error queries.

The job CRT also requires a manager pointer in the low 28 bits of context
word `+0x14`. The runner now supplies a separate, initialized control block
alongside the context. This implements the observed handoff; it does not
recover the complete original job-manager structure.

The newly observed 21,408-byte physics job is extracted from the pinned
executable and statically translated with the other inputs. Its filename
sorts after the previous jobs, preserving their diagnostic image IDs.

`wkc2_spurs_job_queue` tests real submission-side dispatch, descriptor ownership
and reuse, output visibility, bounds and persistent errors with synthetic data.
The extended `wkc2_spurs_abi` uses the real staging code to test manager-block
initialization on repeated jobs and descriptor sizes 64, 128, 256 and 1024.
At that checkpoint, all 51 Windows Vulkan checks passed with 58 patches. Native town validation is
recorded in the [findings](FINDINGS.md#secondary-bone-physics-and-town-stretching).

## SPU conversion scales and DMA ownership (patches 0060-0061)

Patch 0060 constructs exact power-of-two conversion scales directly from
binary64 exponent bits. All 256 valid RI8 immediates produce normal doubles,
so compiled constant conversions can avoid `exp2`/`ldexp` calls. Arithmetic,
saturation and NaN handling retain the previous host behavior. The extended
`wkc2_spu_fp` compares 4,308,992 lane results against the previous CRT-based
expressions, covering every immediate, boundaries and randomized operands.
This equivalence check does not establish complete architectural SPU accuracy.

Patch 0061 gives each SPU context its own MFC queue and tag state. The old
eight-slot registry retained stopped contexts and handed later contexts one
shared engine, acquiring a global lock on each lookup miss. The new state
has the context's lifetime and is cleared by both full initialization and
the SPURS job prefix reset. Channel execution on one context still requires
one owner; sequential handoff between host threads preserves its state.
All SPU translation units must be rebuilt because the host context layout changes.

The extended `wkc2_spu_dma_poll` checks twelve contexts, independent stalled
DMA lists, same-address reuse, repeated release and sequential host-thread
handoff. The same isolation regression fails against patch 0060. A separate
concurrent SDK fixture passes 1,193 checks across twelve contexts and 32 rounds.
At that checkpoint, all 51 Windows Vulkan checks passed with 61 patches in both
the normal Debug build and the private optimized Release-CRT build, with native
regression assertions active.
All 61 patches also apply to a clean SDK checkout (58 files updated).
The 59-to-60 and 60-to-61 upgrades and repeat application are verified.
See the [town measurements](FINDINGS.md#town-performance-after-the-physics-fix)
for the native validation scope.

## FIFO producer wake and SPU execution helpers (patches 0062-0064)

Patch 0062 wakes the existing FIFO event when a completed, mapped ordinary SPU
DMA PUT changes the exact self-JUMP word at which the renderer is parked. The
parked address and expected command are published together in one 64-bit atomic
descriptor. The walker arms that descriptor and rereads the command; if it has
already changed, the walker retries immediately. Sequentially consistent
publication and the producer's full StoreLoad fence close the write-before-arm
race within the runtime's existing guest-memory concurrency model. They do not
make all concurrent guest memory accesses ISO C data-race-free.

Notification runs after the DMA copy and reservation notifications, before the
pushbuffer coalescing path can return. An overlapping copy that leaves the
expected command unchanged does not signal. Nonoverlapping or unmapped PUTs
also stay quiet. The event is initialized once and remains an auto-reset event;
`GCM_FIFO_KICK=0` retains the bounded sleep fallback. The existing four-millisecond
idle timeout, guest clocks, FIFO consumption and completion labels are unchanged.
Atomic SPU commits and PPU stores still rely on the existing timeout. The current
Windows runner uses this event; its non-Windows branch still uses `Sleep(4)`, so
this change does not establish a Linux/macOS wake-latency improvement.

`GCM_PRODUCER_PROFILE` enables four host-only aggregate counters:
`g_gcm_producer_park_count`, `g_gcm_producer_overlap_wake_count`,
`g_gcm_producer_unchanged_overlap_count` and `g_gcm_producer_retry_count`.
Set the switch before launch. Unset profiling leaves the counters zero and emits
no per-event log. The wake count records successful `SetEvent` calls for changed
words; it is not a count of distinct consumed event wakes. The full memory fence
still runs for mapped PUTs with profiling off, so its cost belongs in any timing
comparison.

Patch 0063 computes `CNTB`, the SPU population count for each byte, with SSE2
masks, shifts and byte arithmetic on supported x86 hosts. Masks remove bits
shifted across byte boundaries; each result remains the count of the same input
byte, so no guest byte-order remapping is needed. Other targets retain the
previous scalar implementation. It adds no SSSE3, POPCNT or AVX2 requirement.
The regression compares 1,114,112 byte results against an independent fixed
8-bit reference, including all 256 byte values at every position with randomized
neighbors. Separate baseline-SSE2 and AVX2 fixtures pass. Private replay of eight
image-19 geometry jobs produces 176 byte-identical local-store/register output
pairs through the inspected partial pipeline; this is not complete job or native
rendering validation.

Patch 0064 makes the depth-limit cache, recent-site ring and report budget local
to the executing host thread. `host_depth` remains in the SPU context. The default
limit of 2000, base-zero `SPU_HOST_DEPTH_MAX` parsing and halt at equality or above
are unchanged. Explicit zero and invalid settings retain the original zero-limit
behavior and repeated lookup. Set the limit before starting workers. Reporting
is bounded to one report per host thread; suppression never suppresses a halt.
The last 24 diagnostic sites follow that host thread across reused jobs/contexts,
rather than mixing concurrent workers' history. This does not repair the
underlying guest call recursion.

The eight added checks are:

- `wkc2_gcm_producer_wake`, `wkc2_gcm_producer_wake_disabled` and
  `wkc2_gcm_producer_wake_profile_off`: production FIFO handoffs, initialization,
  unchanged and changed partial/coalesced overlaps, writes before registration
  and between arm/reread, reset/termination and launch switches.
- `wkc2_spu_cntb`: independent per-byte population-count equivalence.
- `wkc2_spu_depth_guard`, `wkc2_spu_depth_guard_configured`,
  `wkc2_spu_depth_guard_zero` and `wkc2_spu_depth_guard_invalid`: the extracted
  production guard, two interleaved workers, threshold decisions, repeated real
  halts and separate bounded histories for default/hexadecimal/zero/invalid limits.

All 59 Windows Vulkan checks pass with 64 patches in both the normal Debug build
and the private optimized Release-CRT build, with native regression assertions
active. Earlier patch sections retain their historical check counts. The
[town measurements](FINDINGS.md#town-performance-after-the-physics-fix) record
the native 64-patch Release-CRT recheck and its limits. Tests and replay checks
alone do not establish a frame-rate gain.

## Vulkan window-title counter (patch 0065)

Patch 0065 originally added an FPS and maximum interval readout to the Vulkan
window title. Patch 0070 renames its rate label to `Present/s`; the counting
method remains unchanged. At the 65-patch checkpoint, the Windows Vulkan build
and all 59 existing checks passed.
Counter updates are verified in the visible native title menu and initial
character creator. It adds no new CTest entry.

The counter accepts only `vkQueuePresentKHR` calls returning `VK_SUCCESS` or
`VK_SUBOPTIMAL_KHR`, and uses monotonic elapsed time over approximately one-second
windows. `FPS` is accepted queue presentations per elapsed second; `max` is the
longest interval between accepted presentations in that window, in milliseconds.
These are not counts of unique guest animation frames or exact display scanout.
Average FPS can hide stutters, so inspect the maximum interval while rotating or
moving. If rendering stalls, the displayed title can remain unchanged until
rendering resumes. The base window title is retained, and counter state resets
with the renderer lifecycle. No diagnostic environment switch is required.

At the patch-0065 checkpoint, the normal Windows build passed all 59 checks
in 22.17 seconds. The private
optimized Release-CRT build passes the 54 non-media checks; its five media checks
also pass when rerun with their required decoder-library environment. Upgrading
a 64-patch SDK changes one file, and repeating all 65 patches makes no changes.

The native counter check used a visible window, no sampling profiler and
five-second GPU readbacks. The desktop shortcut also launches the same promoted
binary. These historical observations verify the counter display and shortcut target;
they are not a quantified character-creator benchmark or a rotation-smoothness
fix. The exact scripted rotation gesture was not confidently verified, and the
reported stutter remains unresolved.

## Depth sampling and real flip completion (patches 0067-0070)

Patch 0067 keeps the compute depth shader's explicit source/output bindings at
0 and 1; graphics texture/sampler shifts still apply to graphics shaders. It
masks the high depth byte to match the CPU byte conversion at the depth-1.0
rounding edge, and orders reuse of the shared transfer staging buffer after the
previous snapshot read. `RSX_ENGINE_ZTEX_GPU=1` selects GPU depth re-encoding
and is a project startup default. The SDK retains the opt-in switch. Wider
visual and cross-vendor comparisons remain required.

`wkc2_depth_shader_binding` compiles the extracted production compute shader
and a graphics control shader, then checks their SPIR-V descriptor bindings.
A separate private Vulkan comparison checked all 16,777,216 D24 depth values
on an RTX 3090 with `TEX_RGBA=0` and `TEX_RGBA=1`, finding zero CPU/GPU byte
mismatches. This verifies the tested conversion on that device, not cross-vendor
GPU behavior or complete depth-of-field fidelity.

Patch 0068 separates a real outstanding display request from polling status.
The `wkc2_gcm_flip_completion` fixture exercises consumed FIFO markers,
immediate requests, reset-only and unconsumed requests, no duplicate completion,
termination reset and the guest's two-vblank semaphore release before a flip.

Patch 0069 moves disabled PPU memory-watch formatting and stack setup behind a
cached launch-option guard. Live store hooks and self-arming barrier watches
remain checked. Patch 0070 changes the title label to `Present/s` without dividing
or otherwise changing its counter. The 70-patch stack passed all 61 normal
Windows regressions. Its starting-town run initially improved, then progressively
slowed while idle. The current 76-patch stack passes 64 checks in Debug and
Release; see the [timings and worker census](FINDINGS.md#frame-pacing-and-game-speed).

## Depth snapshot generations (patch 0071)

Every sampled depth snapshot must match its source target's latest write or
clear generation, including when a later pass has bound a different target.
Patch 0071 moves that validation into the shared snapshot helper, preventing
stale shadow-depth reuse across target switches. Four synthetic regression
assertions fail without the change and pass with it. The 73-patch native town run
includes this change but still shows foot-shadow faults.

## Asynchronous SPU worker ownership (patch 0072)

The dispatcher claims each guest task/workload before creating a detached host
thread and holds that claim through runner and stack cleanup. Duplicate active
submissions coalesce. Generation and claim tickets reject stale cleanup, while
host-thread creation failures release their claims. Active taskset/workload
reuse or removal returns `BUSY`; this does not add worker cancellation or bound
arbitrary unowned diagnostic dispatch.

`wkc2_spu_async_lifetime` covers competing claims, 10,000 duplicate submissions,
completed-slot reuse, duplicate/stale cleanup and more than eight tasksets. The
extended SPURS production-dispatch regression checks claim-before-create and
ownership through delayed cleanup; the reversed claim order and stale-cleanup
negative controls fail. The 73-patch ten-minute native idle test completes
17,104 task workers while keeping host thread count at 58-59 and observing no
worker accumulation. Wider gameplay and cancellation remain unverified.

## Vulkan depth comparison (patch 0073)

Supported single-level 2D depth textures now use the guest comparison function
and native comparison samplers. Linear filtering compares depths before blending
the results (PCF). Projective instructions divide coordinates and reference
depth by w. D32 snapshots and per-unit bindings permit ordinary and comparison
samplers in one fragment shader. Multilevel/cube textures and backends without
comparison capability retain the earlier shader path.

`wkc2_shadow_compare` compiles synthetic TEX/TXP/TXB/TXL programs through the
production decompiler and SPIR-V compiler. It checks comparison instructions,
descriptor bindings, mixed sampling, unnormalized coordinates and legacy API
compatibility. The draw-engine regression covers mixed sampled units too.
The initial headless GPU comparison checks passed with an incorrect ordered
comparison oracle; the 73-patch native run exposes a bright foot-shadow fault.
Patch 0074 corrects that oracle and the sampler direction. The D32 snapshot-to-byte path also matches the CPU for all
16,777,216 normalized D24 values on both devices and both `TEX_RGBA` settings.
The depth-byte checks remain valid; comparison results are superseded below.
Native foot-shadow correctness and a steady 30 fps remain unverified.

## Texture-comparison direction (patch 0074)

RSX texture ZFUNC compares stored texel depth against the reference. Host
comparison samplers reverse those operands, so ordered operators must reverse
when selecting the sampler function. A dedicated helper implements all eight
mappings without changing framebuffer depth or stencil tests.

The corrected production GPU oracle passes 37/37 checks on each of the RTX 3090
and Intel UHD 770. The 73-patch sampler fails 14 checks on each device. The public
shadow regression covers all eight mappings. Both full Windows suites pass all
63 checks with 74 patches. Normal shadow polarity is confirmed in the native town;
black foot flicker remains.

## FIFO user-command delivery (patch 0075)

The FIFO walker consumed `0xEB00` through the low-level ISR event route before
reaching the native HLE callback path. A game using `cellGcmSetUserHandler`
therefore lost these interrupts when it had no low-level interrupt queue.
The walker now queues the registered HLE callback for deferred delivery, and
retains the low-level route when no HLE handler is registered. Zero is a valid
cause. Both supported user-command method aliases follow the same route.

The extended `wkc2_gcm_flip_completion` fixture submits actual FIFO words and
checks zero/nonzero causes, deferred and exactly-once delivery, the method
alias, and low-level fallback. It fails on the old walker. All 63 Windows
checks pass in Debug and optimized Release. Native callback invocation, Logic Stone proximity highlighting, its Save menu,
and restart/Continue loading back into town are verified. This covers one
local starting-town slot, saved and reloaded on patch 0075, then loaded on
patch 0076. Other save compatibility is unverified. The current ZPASS report
values use a conservative visible fallback (`0xFFFF`), not measured GPU samples;
correct callback delivery does not establish accurate occlusion queries.

## Vulkan batch scheduling (patch 0076)

The saved-town comparison averages **27.18 presentations/s before** and
**28.03 after** over separate one-minute samples. Both use the same saved slot,
a visible window, optimized Release build, no file logging or sampling profiler,
and five-second GPU readbacks. These stationary measurements do not establish a
locked 30 fps, smooth traversal, distinct animation updates or RPCS3 timing equivalence.
See [the recorded conditions](../verification/performance.json).

A missing descriptor pool caused a submission before the two batch slots swapped.
Ordinary and shadow-layout pools could stay stranded in opposite slots, forcing
six submissions per town frame. Pools are now created for the active slot without
submitting solely for a layout change. Removing those submissions alone moved the
wait to presentation and did not improve the retained-log diagnostic run.

The final change also submits after at least 256 draws when the spare slot is
already complete. If it is busy, recording continues and readiness is rechecked
every 64 draws. Real capacity exhaustion retains its existing wait and resource
lifetime rules. Measured mid-frame submission time falls from roughly 8-9 ms to
0.15-0.17 ms per frame; occasional longer frames remain.

The host-only `wkc2_vk_arena_reserve` test extracts the actual production reservation
function. It checks alternating ordinary/shadow layouts, pool creation after a
slot swap, allocation/submission failures, full pools, and busy versus ready spare
fences. It fails with the old reservation. All **64 checks** pass in normal Debug
(25.31 seconds) and optimized Release (22.91 seconds). All 76 patches reconstruct
from the pinned SDK, and reapplying them changes no files.

## Windows renderer

Vulkan is the default build and drives the shared RSX draw engine, which also
has a Metal backend. This is native host GPU rendering
alongside the statically recompiled C/C++ game code. Apple Silicon execution has
not been tested.

```powershell
./tools/build.ps1 -Workspace <private-workspace> -Renderer Vulkan
./tools/probe.ps1 -Workspace <private-workspace> -Renderer Vulkan -Seconds 600
```

This uses a separate `work/build/wkc2-vulkan` directory. The build invokes
`tools/prepare-vulkan.ps1`, which checks out and builds pinned public dependencies
inside the private workspace: SDL2 2.32.10, glslang 16.6.0 and its SPIR-V tools,
plus Vulkan headers 1.4.328. Exact revisions are in that script. Existing source
changes or different revisions cause an error; the helper does not reset them.
The standard preparation script builds optimized dependencies with the Debug
CRT and C++ ABI used by `build.ps1`. The measured private Release build uses
separate matching Release-CRT dependencies; the two ABIs must not be mixed.
A Vulkan-capable driver and its loader are runtime requirements.

D3D12 is retained as a comparison/fallback build:

```powershell
./tools/build.ps1 -Workspace <private-workspace> -Renderer D3D12
./tools/probe.ps1 -Workspace <private-workspace> -Renderer D3D12 -Seconds 600
```

For direct CMake configuration, `-DPS3RECOMP_RSX_VULKAN=OFF` selects D3D12.
The choice of primary renderer does not establish complete game compatibility.
Linux can reuse the Vulkan renderer in future; Linux platform integration and
execution remain untested. macOS can use the shared engine's Metal backend.

The SDK patch helper reconstructs the complete stack before updating source.
It accepts an unchanged SDK or any known earlier prefix, including one already
fully patched. Overlapping later patches do not make earlier patches appear
missing. Unrecognized edits to affected files cause an error before any SDK
source is changed.

Reconstruction fixes Git's line-ending policy and tolerates whitespace in patch
context for the upstream mixed CRLF/LF files. Known-prefix comparison still
rejects unrelated source edits, including indentation changes. The full stack
was checked with both line-ending settings and produced the same source after
CRLF normalization.

For periodic native Vulkan GPU readbacks, create a private directory and set
`RSX_ENGINE_SNAP_DIR` to it and `RSX_ENGINE_SNAP_EVERY_S` to the interval in seconds
before launching. This produces PPM files. The probe's `-CaptureFrames` controls
select the separate D3D12 capture path.

Patches 0035 and 0036 use the shared-engine and Windows Vulkan work from
[Soulcalibur2HD-recomp](https://github.com/n0madic/Soulcalibur2HD-recomp/tree/88e6cc842a7853e093e8dbab8ac053ac1ad51cc7).
The toolkit patches retain the SDK's MIT license; see the retained
`LICENSE.ps3recomp`. Patch 0038 implements colour-target snapshots after inspecting
`off_rt_snapshot` in the MIT-licensed
[Demon's Souls SDK fork](https://github.com/JonathanDC64/ps3recomp/blob/61909373a2cb59f8679d8b96382e2899d656e4d0/libs/video/rsx_d3d12_backend.c).
These are implementation sources actually used by this port.

## Renderer coherence and execution overhead (patches 0053-0057)

Patch 0053 gives depth-only passes a scratch colour target that cannot alias a
guest display buffer. It also queues host writes for the shared renderer, which
reloads overlapping GPU surfaces before sampling them. Together these changes
remove the red flashes in the inspected WKC1 cutscene. The draw-engine and Vpost
regressions cover the two faults and the reported host-write range.

Patch 0054 wakes job-guard waiters with a condition variable and yields when an
SPU repeatedly reads an unchanged reservation line. The native loading route
completes without the earlier pile-up of spinning task threads.

Patches 0055 and 0056 reduce SPURS dispatch and setup overhead: sampled job-image
fingerprint caching, direct local-store staging, a bit-equivalent `FI` scale,
per-thread context reuse, selective clearing and selective launch checks. Set
`SPURS_JOB_ZERO_LS=1` to restore full local-store clearing or
`SPU_LAUNCH_CHECK_ALL=1` to run every launch check when comparing private traces.

Patch 0057 lets a full Vulkan draw batch submit while recording continues in a
second buffer set, and increases descriptor capacity. Readback, presentation and
release paths still drain pending work. In the recorded pre-0058 town run this
reduced renderer blocking from about 31 ms to 13 ms per frame, with town frame
rate still about 8 fps. These are observations from that run, not a performance
guarantee for the current physics-enabled build. See the
[performance findings](FINDINGS.md#cutscene-red-flash-and-frame-rate).

## Earlier runtime fixes and validation history

**Verified on 2026-10-17:** the 64-patch Windows Vulkan build reaches the live
WKC1 town with the long stretched hair/clothing strips removed in the inspected
views. Short movement and camera rotation are rechecked. Earlier town testing
also verified pause/resume input. At that checkpoint, all 59 runtime checks
passed with 64 SDK patches in normal Debug and private optimized Release-CRT builds.
Full material/rendering fidelity, sustained traversal and combat remain unverified.
Starting-town local save/reload was verified at patch 0075 and reused for patch 0076. See the
[latest gallery entry](GALLERY.md).

The results below describe each patch's validation checkpoint. Reports of the
stretched strip or speckled NPC surfaces are historical: patch 0052 improved the
NPC surfaces, and patch 0058 removes the long secondary-bone strips in the
currently inspected town views. Full rendering fidelity remains unverified.

Patch 0034 keys the D3D12 fragment pipeline cache by instruction contents rather
than the guest upload address, ignoring embedded constants and bytes after END.
It checks exact instruction equality after hashing. Synthetic cases cover moved
programs, changed constants, different operands and malformed input. A native
D3D12 speed improvement has not been established.

Patch 0037 waits for the Windows host thread to finish before a PPU join frees
its slot. The guest-exit event occurs before the host epilogue; freeing at that
event let the old epilogue mark a joined or reused slot finished again. The
regression holds that epilogue open and checks that join blocks, then reuses the
slot 128 times. It fails against the earlier runtime. The thread limit is unchanged.

Patch 0038 copies a current colour target immediately before a draw samples it.
Units in the same draw share the snapshot, refreshed after colour writes or
guest transfers and released on resize/shutdown. Backends without a working GPU
copy use fresh guest-memory readback for supported RGBA8 surfaces. The production
engine regression covers copy ordering, refresh, texture remapping, lifetime
and that fallback; ten checks fail against the previous engine.

Patch 0039 disables the automatic cutoff after 256 repeated DMA transfers.
Valid SPU jobs can repeatedly read a GPU completion label while waiting for
buffer space. The old cutoff halted geometry jobs before they completed,
leaving unpatched command-buffer self-jumps. The regression performs 1,024
valid reads and observes a producer update after read 768; forcing the old
cutoff reproduces the failure. `SPU_DMA_REPEAT_LIMIT` is now an opt-in diagnostic:
zero or an absent value disables it, and a positive value requests a cutoff.
Its counters are local to each host execution thread. A native run with this
fix passes the earlier loading stall, reaches the next cutscene and enters
the live WKC1 town. Short movement and camera response were verified later
with all 42 patches. At that checkpoint, world geometry and NPC surfaces were
visibly corrupt; sustained traversal, combat and save/load were unverified.

Patch 0040 incorporates the allocator fix reviewed from the
[suggestion branch](https://github.com/WK2D/White-Knight-Chronicles-2-Decomp/blob/6ca620d7f328996eeb4bf118652d78528d90107a/patches/0039-memory-range-reuse.patch).
`sys_memory_allocate` now reuses aligned ranges released by `sys_memory_free`
and zeroes reused bytes. The old allocator exhausted its address window after
two freed 96 MiB allocations despite reporting free memory. The Windows
regression reproduces that failure and passes repeated cycles, zeroed reuse,
gap filling, alignment and non-overlap with the fix. The full 42-patch native run
reaches the town; reuse behavior has not been isolated in-game. Review added protection for mapper reservations
and shared mappings in the same address window; mixed-API cases fail without
that protection and pass with it. The same branch's patch-0015 context correction
applies to the pinned SDK without changing the reconstructed source.

Patch 0041 extends the optional private `PAD_FILE` controller probe with
analog sticks. A line can contain `mask polls lx ly rx ry`, with stick values
from 0 to 255 (128 is neutral). For example, `0 120 128 0 128 128` requests
left-stick up for 120 input polls. The existing `mask polls` format still works.
After the hold expires, the underlying host/`PAD_STICK` state is restored.
Incomplete or out-of-range numeric stick fields are rejected. The regression exercises
the production guest input packet, finite holds, stick-only input, digital
compatibility and invalid input. Short starting-town movement and camera response have now been observed
in the full 42-patch native run; sustained traversal remains unverified. This probe is separate from the planned gamepad-operated keyboard.

Patch 0042 adds an opt-in census to guide replacement of the remaining SPU
fallback paths with validated static C/C++ implementations. Set
`SPU_INTERP_PROFILE=<path>`, or pass `probe.ps1 -InterpProfile`. Completed
interpreter and runtime-code microstep sessions are grouped by path, image,
entry address and an FNV-1a fingerprint of the 64 local-store bytes at entry.
The fingerprint distinguishes different entry windows, rather than identifying
an entire overlay. Interpreter handoffs are counted separately from instructions
the microstepper executes itself.

The TSV records sessions, instructions and the longest session, sorted by
instruction count. It writes new rows immediately and refreshes existing rows
at most once a second. Initialization is synchronized across host threads;
Windows file replacement retains the previous complete snapshot on failure.
The default mode skips hashing, table updates and file output. Checks exercise
actual interpreter execution, concurrent initialization, disabled mode, actual
microstep dispatch and a handoff to the interpreter.

This is a census of completed sessions, not proof of complete coverage.
Unfinished or nonlocally exited sessions and updates since the latest snapshot
can be absent after a forced stop. The 4096-row table reports dropped sessions.
Lifted instructions are not counted, so it does not measure fallback's share
of all execution. An empty table alone must not trigger removal of the fallback.

Patch 0043 corrects two vertex-program translation faults that can misplace
vertices. An NV40 vertex-program instruction issues a vector and a scalar
operation together, and both read their operands before either writes. The
translator emitted them in sequence, so a vector write to the scalar operand's
temp register, or an `ARL` to the address register indexing it, reached the
scalar operation early. Skinning and other indexed-constant shaders use this
pattern. The scalar operand is now read first. This operand-order change leaves
single-unit instruction text unchanged. Separately, `LIT` repeats its operand four times,
and an indexed, absolute, negated constant exceeded the 256-byte expression
buffer. `snprintf` truncated the expression, so the shader failed to compile.
The buffer now matches the vector unit's.

The regression decodes hand-encoded programs and checks temp and
address-register ordering, the full `LIT` expression and unchanged single-unit
output. Windows negative controls fail independently for the co-issue fault and
the smaller `LIT` buffer; the corrected test passes. Synthetic hardware vertex
shader tests also produce the expected temp/address values with the fix, and a
vector-only program retains byte-identical shader text. The branch's Linux
validation reports valid SPIR-V and no compile regressions across 1,500 random
programs, including seven formerly failing programs that now compile.

The patch-0043 native Vulkan town recheck still showed the stretched strip and
corrupt NPC surfaces. Its 65 distinct captured vertex programs contain 330 co-issued
instructions, zero temp/address/condition hazards and no `LIT` instructions.
Both old and new translations compile through the production Vulkan compiler
and validate as SPIR-V. These results cover the captured route, not every game
shader. To scan private vertex-program captures without printing game bytes:

```text
python tools/scan_vp_coissue.py <capture-directory>
```

It reports temp and address-register hazards, which patch 0043 corrects. It
also counts co-issued condition-register updates that predicate the scalar
write, which this patch leaves unchanged. Counts identify potential dependencies
in captured code, rather than proving those instructions executed. Missing,
empty or malformed captures cause an error. Repeated program names in different
frame directories retain separate report entries.

Patch 0044 extends the shared renderer's existing private shader dump:
set `RSX_ENGINE_SHADER_DUMP=<existing-private-directory>` before launching Vulkan
to save `vs_<fingerprint>.bin` alongside each newly created pipeline's HLSL and
fragment-program diagnostics. The raw vertex programs can be scanned with the
same command above. `RSX_CAP_GEOMETRY` alone selects the older D3D12 capture path
and does not collect these inputs from Vulkan. Dumped game programs stay private.

Patch 0045 tested whether SPU floating-point differences caused the then-open
geometry faults. Patch 0028 gave `FNMS` the SPU single-precision rules:
truncation, denormal inputs read as zero, exponent 255 as a finite value, and
saturation on overflow. `FA`, `FS`, `FM`, `FMA`, `FMS`, the four integer/float
conversions and the four compares still use host IEEE arithmetic, which rounds
to nearest, keeps denormals and treats exponent 255 as infinity or NaN.
`SPU_FLOAT_MODE` selects the behavior at startup:

| Value | Behavior |
| --- | --- |
| unset | Host results, unchanged from earlier builds |
| `audit` | Host results; lanes that differ from the SPU rules are counted per instruction as `rounding` (one unit, both normal) or `special` |
| `exact` | SPU rules for all thirteen instructions |

Audit logs each count when it reaches a power of two, with one sample of the
input and both results, and prints a summary at normal exit. `audit` retains
host results while measuring differences; `exact` enables comparisons of their
effect on native behavior. Neither mode corrected the target town faults in
the recorded comparison below.

The original bodies remain as `spu_*_host`; the helpers test one flag and call
out-of-line code in `spu_float_mode.c` only when a mode is set. In a Linux
benchmark at the patch-0045 checkpoint, memory-fed transform-like chains used
no hardware FMA. The default measured the same as before (3.6 ns per vector
operation); exact and audit measured about 29 ns. With `-mfma`, the unmodified
helpers measured 1.0 ns and the default-mode dispatch measured 2.8 ns. These
Linux measurements do not establish the cost in the optimized Windows build,
which now uses `/arch:AVX2`.

Host `CFLTS`/`CFLTU` previously converted a NaN through an undefined cast. They
now return `0x80000000` and `0`, the values x86 produced. Other default-mode
results are unchanged.

The regression compares the general SPU routine with patch 0028's `FNMS` on
four million random operand sets, covering every exponent. It also checks sums,
products and fused results against an exact double-precision reference with
truncation, with fixed SPU edge cases and the vector helpers in all three
modes. The SDK's SPU helper and numeric tests give identical results in
default and exact modes. The three float-mode checks also pass on Windows
with clang-cl; Linux runs included AddressSanitizer and UBSan. Native audit
and exact runs also reach the WKC1 town. Audit records both rounding and
special differences; that exact-mode run still showed Leonard's stretched strip
and corrupt NPC surfaces. Host arithmetic remains the default. See the
[native results](FINDINGS.md#second-runtime-review) for scope and limits.

Patch 0046 moves the guest stacks that host threads use to call guest code
(GCM flip and vblank handlers, sysutil and other HLE callbacks) out of RSX
local memory. Patch 0007 placed them at fixed addresses from 0xC7FF0000 to
0xCFFF0000, one 1 MiB slot per host thread; the SDK code it replaced used
0xCFFE0000. `cellGcmSys.c` reports 256 MiB of local memory at 0xC0000000, so
these frames lay in the top half of video memory, and each callback overwrote
whatever the title had placed below its slot's top: textures, vertex buffers
or render targets. Slots now come from the PPU stack allocator at 0xD0000000,
with the same guard page as thread stacks, and keep their memory for reuse.
The 51-patch native town recheck still showed the stretched geometry and NPC
surface faults. This combined run does not isolate patch 0046's effect.
`wkc2_callback_stack` checks that every slot and nested slice lies in the stack
region with room for the callee's register saves, and fails against the
earlier runtime.

Patch 0047 fixes PPU thread lifetime. `sys_ppu_thread_create` ignored its
flags, so a thread created without `SYS_PPU_THREAD_CREATE_JOINABLE` kept its
slot after it ended, waiting for a join that lv2 would refuse; once the
64-entry thread table filled, creation failed with EAGAIN. A detached thread that ended through
`sys_ppu_thread_exit` was freed by the exit and then marked finished by its
host epilogue, which leaked the slot or overwrote a thread that had reused it.
Each started thread now ends once, in its epilogue, which wakes the joiner
under the table lock. As in lv2, joining oneself returns EDEADLK, a second
joiner gets EINVAL and detaching a thread being joined gets EBUSY. Released
guest stacks are reused, zeroed, instead of taking new stack region for every
creation, which ran out (ENOMEM) after about 250 threads with the default
1 MiB stack. The SDK's two
self-checks that join a thread now create it joinable.
`wkc2_ppu_thread_lifetime` ends 192 detached and 128 joinable threads,
including exits from a nested guest context, and checks the error codes,
detaching a running thread, zeroed reuse and that stack region use stops
growing. It fails against the earlier runtime. It passes on Linux under
AddressSanitizer/UBSan and ThreadSanitizer, whose only reports are the test's
own polling. The Windows path of this test and the existing `wkc2_ppu_join`
passed under Wine with a MinGW build and now pass in the native Windows
clang-cl suite.

Patch 0048: `cellSpursJobGuardReset` restored the guard's current reset value,
which a guard without autoReset sets to zero when it releases. Such a guard
stayed open after a reset and ran its job again without a notify. Reset now
restores the initial notify count, as lv2's does. `wkc2_spurs_job` adds a
two-guard lap that fails against the earlier runtime; it passes under Wine and
in the native Windows suite.

Patch 0049: `SPU_CRI_IMAGE=-1`, which this runner sets, is also the image id
of pure SPU interpretation (`RD_SPU_INTERP`). Interpreted SPU threads matched
the CRI checks and received that task's EXIT, syscall and DMA special cases.
A negative value now matches no context. Runs without `RD_SPU_INTERP` are
unaffected. `wkc2_spu_title_*` adds an image -1 task that must end at its
first EXIT; that check is part of the game executable and runs in the native
suite. The selector itself was checked on Linux.

Patch 0050 corrects NV3089 scaled-image blits, which the FIFO walker performs
in guest memory for both renderers. `IMAGE_IN` gives the source point in 12.4
fixed point; the copy loops shifted it as if it were 12.12, so any start below
256 pixels was dropped and the copy read from the source origin. The point is
now read as RPCS3 reads it, including the half-pixel offset of a centre origin.
Centred unit copies from the origin, the common case, are unchanged. X8R8G8B8
sources (format 4) were skipped because the accepted list had R5G6B5 and Y8
labelled as the 32-bit formats; they are now copied like A8R8G8B8, as in RPCS3.
`wkc2_nv3089_blit` drives the walker's own NV3062/NV3089 methods for unit,
offset, centred and 2:1 copies through both the row and per-pixel paths, and
for X8R8G8B8 and R5G6B5 sources. It fails against the earlier runtime.

Patch 0051 keeps 2D-object methods out of the shared draw engine's 3D state.
The FIFO walker mirrors every method into the engine, which masked off the
subchannel and so stored NV3062, NV308A and NV3089 methods in NV4097
registers at the same offsets: a scaled blit's `SET_OPERATION` and
`IMAGE_OUT_POINT` became the alpha-test and blend enables of the following
draws, and an inline transfer's pitch and point did the same to alpha test.
The engine now ignores subchannels 2-6, which the walker treats as 2D objects,
and subchannel 1 when `GCM_SUBCH1_2D` is set. `RSX_ENGINE_ALL_SUBCHANNELS=1`
restores the old behaviour. This affects the Vulkan path only; D3D12 never
received these methods. `wkc2_draw_engine` adds a check that a blit and an
inline transfer between two draws leave blending and alpha test unchanged; it
fails against the earlier runtime.

The 51-patch native town comparison with `RSX_ENGINE_ALL_SUBCHANNELS=1` still
showed the same target faults. Neither route corrected the stretched strip or
NPC surfaces. An empty private `GCM_SUBCH_GATE` file,
created for ten seconds in town, confirms complete NV0039 transfer sequences
on subchannel 1. This 51-patch trace established the missing-copy defect addressed by
patch 0052 below. Both default
float runs log inline writes, with no skipped-NV3089 or unhandled-2D messages.
Absence of those messages does not prove complete copy support.

Patch 0052 implements NV0039 byte copies. The runner defaults
`GCM_SUBCH1_2D=1` because native WKC1 traces confirm the copy object on slot 1.
The FIFO walker consumes those methods before either 3D renderer; the SDK's
other users retain its default slot-1 3D behaviour. An explicit
`GCM_SUBCH1_2D=0` restores that route for comparison.

The handler supports LOCAL/MAIN source and destination mappings, report-record
sources, signed and zero row pitches, byte strides 1/2/4, and overlapping
regions through a complete source snapshot. MAIN I/O pages are translated
individually. Zero rows or columns do nothing. Every row is checked before
writes; invalid mappings, unsupported formats, notification requests and
transfers over the 64 MiB staging limit produce explicit diagnostics.
Notification completion is not implemented. Report sources expose the existing
runtime's report records; this does not add GPU query accuracy.

For the shared Vulkan renderer, copies store overlapping RGBA8 targets and
pending resolves before reading or partially overwriting their bytes, upload
changed targets, and invalidate same-frame texture hashes. Other colour-target
layouts are rejected rather than copied from stale guest bytes; depth-target
copies remain unverified. The D3D12
renderer has not been validated for copies involving GPU-owned surfaces.

`wkc2_nv0039_copy` exercises the production FIFO walker, the 12/16-byte
transfer shapes seen in the town trace (the native run also performs larger,
unlogged transfers), noncontiguous mappings, overlap, pitches, strides, bounds,
reservation notifications and separation from both 3D renderers.
`wkc2_nv0039_routing_3d` checks the SDK compatibility override. The extended
`wkc2_draw_engine` verifies an unaligned partial-pixel update preserves adjacent
GPU-owned bytes. All passed in the 50-check checkpoint and remain covered by
the current 64-check Windows Vulkan suite.

Set `GCM_M2MF_VERIFY=1` before a private run to compare copied destination bytes
against the staged source. This re-read confirms the writes landed at the
computed addresses; it cannot detect wrong addressing or transfer semantics.
`[NV0039] copies=...` reports the first eight copies
and powers of two thereafter, so the last line is a lower bound rather than a
final total. Rejections and verification mismatches have their own immediate,
throttled diagnostics. Logs contain counts and reasons, not payload bytes.

The native town recheck (`20261017-061720`) completes at least
65,536 copies / 14,363,492 bytes with verification enabled, no mismatch
or rejection diagnostics. Previously speckled NPC surfaces look coherent in the
inspected town views, while the stretched geometry remained at that checkpoint.
See [the findings](FINDINGS.md#nv0039-memory-transfers).

Method layout and transfer semantics were checked against the
[envytools NV_M2MF register description](https://github.com/envytools/envytools/blob/master/rnndb/graph/nv_m2mf.xml)
and [RPCS3's NV0039 handler](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/NV47/HW/nv0039.cpp).
The C handler and synthetic tests were written independently.

## Movie decoding

`tools/prepare_ffmpeg.py` obtains FFmpeg 8.1 public headers from the official source
archive, checks SHA-256 `b072aed6871998cce9b36e7774033105ca29e33632be5b6347f3206898e0756a`,
and writes the local public header configuration. The build enables the SDK's
runtime-loaded decoder. A matching libavcodec 62 / libavutil 60 is needed to decode
video; no FFmpeg binary is included here. The tested Windows installation uses
PyAV 18.1.0 in the private venv. `tools/media-env.ps1`, loaded by build and probe,
locates its DLLs and dependencies. Alternatively, set `PS3RECOMP_FFMPEG_AVCODEC`
and `PS3RECOMP_FFMPEG_AVUTIL` to matching library paths and provide their dependencies
on the host loader path. A different-major library is rejected.

The automatic decoder check uses eight original synthetic frames, including
B-frame reordering, with a decoded pixel reference. It exercises the SDK's existing
guest API test, two sequence passes, a changed pixel and a dropped-unit negative
control. It skips when the runtime library cannot load; a skip does not verify
decoding. The separate private logo check covered all 210 frames at 1280x720.
Patch 0008 fixes that SDK test's 64-bit seen-set limit so sequences above 64 frames
are checked without undefined shifts.

`pamf.cpp` supplies project-local context handlers for the nine imported PAMF APIs
before SDK fallback registration. The reader test uses synthetic headers by default.
`wkc2_pamf_test <private-movie-file>` additionally checks a local 2048-byte header;
the file is not copied into the source tree or test fixtures. This covers header
reading, not demuxing or movie playback.

`dmux.cpp` supplies project-local demux handlers, including QueryAttr and QueryEsAttr,
which were absent from the SDK library's handler surface. Work sizes describe the
project implementation's buffers. The worker queues actual units in guest resources
and calls guest OPDs; it waits when resources fill until the guest releases units.
No completion or timestamps are synthesized to skip movie content.

Offline private media validation (PyAV is an optional verification dependency):

```text
wkc2_mpegps_test <private-movie-file> <private-output-prefix>
python tools/verify_movie_units.py <private-movie-file> <private-output-prefix> --record <private-record.json>
```

The test exports elementary video/audio plus video-unit sizes to the private prefix.
The verifier decodes those video units and compares every frame against FFmpeg's
independent MPEG demux/decode of the original movie. It also decodes exported AC-3
when present. Records contain hashes and counts, never game bytes or host paths.
The checked samples are in `verification/movie-demux.json`. Native runs separately
verify continuous playback of startup logos, the title background and observed
attract-movie playback. The offline checks do not establish complete movie
coverage or the game's final audio mix.

`vpost.cpp` implements the four imported video-postprocessor APIs with guest field
layouts and real progressive YUV420 processing. Tested paths include legal-range
black/white, cropped/scaled RGBA output, planar output, guest metadata and output
window guards. Field deinterlacing and non-bilinear filters are not implemented.

Set `WKC2_UPDATE_DIAGNOSTICS=1` for dispatcher/scene state samples. The generated
update hook is applied by the build script and is disabled by default at runtime.
Probe logs and GPU captures go to the private workspace.

`tools/apply_port_hooks.py` installs synchronization before the original guest
installation request: a pending background content check must finish first.
This prevents a faster native caller from losing the install request to the
guest helper's pending-request guard. Allocation, copying and completion checks
remain in the original guest path. The hook is reapplied after regeneration.
`tools/trace_calls.py --env WKC2_CONTENT_TRACE` supports a separate trace switch
for focused content diagnostics, without enabling other instrumented functions.

`Wkc2Decompile.java` is a Ghidra 12.1.4 headless script for selected-function analysis
using `PowerPC:BE:64:64-32addr`. It takes a private TSV inventory (hex start, exclusive
end, TOC), a private output directory, and hex function addresses. It is an analysis
aid; generated pseudocode is not source distributed by this repository.

`adec.cpp` implements eight guest audio decoder APIs for PAMF LPCM and AC-3. It owns output
slots inside the supplied guest work resource and keeps metadata/samples until
consumption. Supported packed depths are 16/20/24 bits, with dummy-channel removal,
channel reordering and sample-count-based missing timestamps. The AC-3 path uses FFmpeg, channel-pointer mapping, requested mono/stereo downmix
or full 5.1, PCM scaling and line/RF dynamic range modes. Unsupported custom
compression, forced stereo/dual-mono and karaoke modes return an argument error.

The LPCM test exercises backpressure, distinct metadata and output queues, 16/24-bit
samples, mono dummy channels, 5.1 reordering and 1,000 concurrent submissions.
An optional private test compares every first-logo sample with FFmpeg PCM decoding:

```text
wkc2_adec_test <private-s16be-input> <private-f32be-output>
python tools/verify_lpcm.py <private-s16be-input> <private-f32be-output> --record <private-record.json>
wkc2_vdec_decode_test timing <private-mpeg2-stream> 30000 1001
```

The PCM comparison starts from assembled elementary samples; it does not independently
verify demuxing or the game's final audio mix. Sparse video timing is tested after
B-frame reordering and across two sequence starts. Patch 0009 removes fabricated
job-completion broadcasts; lifted SPU event instructions retain event delivery.
Patch 0010 supplies decoded video rate/colour/field metadata and derives missing
presentation timestamps at the decoded rational frame rate.

`vdec.cpp` supplies asynchronous context handlers for the eight imported video
APIs and uses the SDK's real FFmpeg backend for pixels. It copies accepted input
records/data, bounds pending input separately from decoded pictures, and delays
work when output fills. Consumption wakes the decoder so pending inputs can
complete. This replaces the SDK's synchronous output-count busy path, which can
leave the guest waiting after all accepted inputs already reported completion.
The asynchronous test checks pixels, sparse timestamps, callback counts, sequence
reset and immediate reuse of the submitted input buffers. The private logo test
also fills the decoded-output queue before it starts consuming frames.

AC-3 checks use original synthetic six-channel tones with independent float
references for stereo and 5.1. Every sample is compared across two sequence starts.
The optional private verifier generates a reference and runs the same native API
check against a complete elementary stream:

```text
python tools/verify_ac3.py <private-ac3-stream> <private-reference.f32be> --channels 6 --decoder-test <wkc2_adec_test.exe> --record <private-record.json>
```

The first-logo LPCM and opening/title AC-3 records are in
`verification/movie-demux.json`. Audio comparisons begin with assembled elementary
streams; independent container audio demux and original-console mix fidelity are
not established. Patch 0011 resolves FFmpeg's option setter for decoder downmix/DRC.
Matching runtime libraries are required; unavailable AC-3 decoders fail explicitly.

For a focused private job trace, set `SPURS_JOB_TRACE_FP` to a hexadecimal SPU
binary fingerprint before launching the probe. Patch 0012 logs dispatch and return
for at most eight matching jobs without enabling global per-job verbose output.
A successful return alone does not validate the job's output.

`WKC2_FIFO_TRACE=1` enables a read-only observation at the original game's graphics
wait timeout. It records at most three expected/current completion-label samples
and command words around GET/PUT in the private probe log. It does not advance
the command queue or change completion values.

With that observation enabled, `WKC2_FIFO_REGION=<hex-EA>:<hex-byte-length>`
also retains the two most recent observed writers for each word in the selected
command-buffer allocation. Patch 0013 supplies separate PPU-store and SPU-DMA
observation hooks. Timeout reports identify the PPU function/thread or SPU
image/PC that wrote the parked command and nearby words. Tracking is opt-in,
uses host memory proportional to the selected region, and can affect timing.
Host bulk copies and other unobserved write paths are outside its coverage.

Patch 0014 executes `JOBLIST` chain commands, using the list's guest-endian count,
descriptor size and array address. Every descriptor runs through the existing
SPU job dispatcher before the walker advances. The SPURS check covers 128-byte
and 256-byte lists, guarded publication, list order, a following single job,
empty lists and invalid sizes/ranges. The previous walker fails the list test.

Patch 0015 recognizes the canonical `cellSpursGetSpuId` library routine during
SPU lifting and binds it to job-manager handoff metadata while running a native
job. The serial chain walker currently exposes virtual SPU 0. Task/raw contexts
retain their original LS reads. This avoids overwriting executable job bytes
with a kernel structure. `SPURS_JOB_ABI_TRACE=1` reports the first eight bound
queries per host thread. Existing private SPU lifts need regeneration to acquire the binding.
The ABI test lifts synthetic instructions, runs the query through the actual
job staging code, checks preserved LS bytes and tests a similar unbound routine.

Patch 0016 adds `GCM_FIFO_PROGRESS=1` for a read-only view inside FIFO drains.
At the game's observed timeout it reports an approximate in-flight packet,
phase, packet count and the last sixteen branch words. Individual fields use
host atomics; the sample is not a coherent memory snapshot. This distinguishes
in-flight work from the GET value published only after a drain completes.
The trace changes timing and does not advance GET, consume commands or release
labels. Raw words and addresses remain in private logs.

Patch 0017 keeps vertex-program cache IDs stable for deferred draws and keys each
entry on the actual program through END, with bytecode comparison after hashing.
The cache grows as distinct programs appear and releases retained shader blobs at
backend shutdown. Its regression covers 128 queued IDs, copied bytecode, deliberate
hash collisions, unrelated bytes after END, invalid input and single release of
each compiled object. A negative control with the old moving 16-slot policy fails.
This fixed the customization UI at that checkpoint; later patches restore
avatar geometry, shading and the town view described in the gallery.

Patch 0018 adds `RSX_CAP_GEOMETRY=1` to completed-frame private captures. It
records the exact cached vertex programs, each draw's vertex constants and the
first sixteen uploaded vertices (all sixteen float4 attributes). The geometry
manifest records offsets and counts. Data is read after frame completion from
the retained parity buffers; source bytes remain private.

Patch 0019 records the guest depth-test enable, write mask and comparison with
each draw, includes that state in fragment-pipeline identity and applies it to
the D3D12 pipeline. It supports the eight GL comparisons and the runtime's
initializer ordinal. The helper regression checks comparison mapping, deferred
state identity and a UI-quad/model occlusion scenario. Restoring unconditional
depth testing/writes fails that scenario. Primary fallback pipelines still have
fixed depth behavior; this change covers the guest fragment-program pipelines.
The avatar model was still missing in that probe; subsequent fixes are recorded
in the gallery and findings log.

Patch 0020 adds optional intermediate GPU readbacks. Set `RSX_DRAW_SNAP_FILE`
to a private request file containing up to eight decimal draw-operation indices,
separated by spaces. The backend consumes the request on the next rendered frame,
queues a copy after each selected draw and maps the copies after GPU completion.
BMP files go to `RSX_CAP_DIR`; logs identify the actual guest pipeline and its
depth, culling and alpha state. Clear operations and unsubmitted draws produce
no image. This changes diagnostic timing without replacing shaders, render state
or guest memory. Use a completed-frame manifest to select operation indices.
Also set `RSX_DRAW_SNAP_DEPTH=1` to retain the depth plane after those draws.
Each `.depth.bin` begins with four little-endian uint32 values (width, height,
row pitch and device footprint format), followed by the original plane bytes.
The current D24 source uses its low 24 bits for normalized depth. The device
may describe the copy footprint as `R32_TYPELESS`; the stored samples remain
packed depth. These buffers, like shader and geometry captures, remain private.

Patch 0021 decodes `SET_ZMIN_MAX_CONTROL`, including its reset value, and retains
the depth clipping/clamping selection in each draw's raster-state cache key.
Bit 3 of the logged `cull` key now means depth clipping; bits 0-2 still describe
face culling. Guest fragment pipelines set D3D12 `DepthClipEnable` from that key.
A command-processor regression covers the reset, disable/restore transitions,
clamping, dirty-state notification and retained draw keys. Nonstandard ignore-W
behavior and primary fallback-pipeline clipping remain outside this change.

Patch 0022 decodes primitive-restart enable/value commands and assembles indexed
triangle strips and fans with restart boundaries and consistent winding. The
cursor survives contiguous command packets within one BEGIN/END stream, including
packets too short to produce a triangle. A C++ regression checks winding,
degenerate triangles, packet boundaries, repeated markers, disabled restart,
32-bit markers and fan anchors. Independently removing restart or winding
handling makes the regression fail.

Optional `RSX_INDEX_TRACE=N` captures up to 64 indexed strip/fan input packets in
`RSX_CAP_DIR`. Each contains the original indices, converted vertex attributes
and input-state description. These files include game data and remain private.

Patch 0023 extends private geometry captures with each draw's actual fragment
constant buffer (`op_NNN.fragment-constants.bin`) and a state description in
`fragment-inputs.txt`. The current host buffer stride is 2,304 bytes, including
unused capacity. Set `RSX_CAP_GEOMETRY_FULL=1` alongside `RSX_CAP_GEOMETRY=1` to
retain every converted vertex in `op_NNN.vertices-full.bin`; the original
16-vertex sample files remain available. This observes completed-frame inputs
without changing shaders, render states or draw order. Complete vertex buffers
and fragment constants contain game data and remain private.

Patch 0024 adds optional `RSX_DRAW_AUDIT=1` observation to private frame captures.
`draw-inputs.txt` records up to 8,192 draw callbacks per frame, with an explicit
omitted count, resource availability and geometry/record counters before and
after each original callback. Completed-frame counter and parity accompany the
records. The option also logs each unhandled RSX method once. It does not alter
the callback's inputs or resulting draws.

Patch 0025 adds optional `SPURS_JOB_EXIT_TRACE=1` observation. It logs up to 64
distinct image/status/stop/PC combinations per host thread after job execution.
The return register is included in each sample but is not part of the distinct
key. Reaching the native manager's return sentinel does not verify job outputs.
Logs and captures remain private because they can contain game data.

Patch 0026 makes the SDK's legacy YDKJ CRI task behavior select an image through
`SPU_CRI_IMAGE`, retaining 22 as the SDK default. This runner sets it to -1 before
starting SPU threads: WKC's image ordinals must not activate another title's
bootstrap, argument or context handling. The selection follows the MIT-licensed
[runtime used by Drakengard 3](https://github.com/soyjxck/ps3recomp/blob/de08397c0a485cd7705c049ca7532dd90fadc704/runtime/spu/spu_context.h).
Three regression cases execute the linked SPU task EXIT path with CRI handling
disabled or assigned to image 22 or 23. They check normal task termination and
the selected task's first-return/subsequent-exit behavior. Running the disabled
case with the old image-22 selection fails the actual return-path assertion.

Patch 0027 exposes an optional read-only SPURS job observer before and after the
same job. The port's observer is enabled only when `WKC2_JOB_CAPTURE_FILE` and
`WKC2_JOB_CAPTURE_DIR` name private request/output locations. Write three decimal
integers to the request file: an increasing request ID, a job count from 1 to 32,
and the image ID. For example, `1 4 19` selects the next four jobs of image 19.
Up to 128 jobs can be captured in one run. Each capture contains the full local
store and register file before/after execution, plus up to 512 ordinary DMA PUT
records and 1 MiB of their payload. Limits, nested jobs and I/O errors are
reported explicitly; atomic commits are outside this observer's scope. The
existing command-buffer writer observer remains connected when both are enabled.
These files contain game code/data and must stay in the private workspace.

Patch 0028 implements FNMS (`c - a*b`) using integer significands for SPU
single-precision extended-range values. Exponent 255 is finite; exponent-zero
inputs/results flush to zero, overflow saturates and results truncate toward
zero. In particular, zero multiplied by an extended value does not become a
host NaN. A separate unbounded-integer oracle generates 7,099 synthetic cases
covering extended operands, zero, cancellation, exponent boundaries and random
values. The native check compares all four lanes, totaling 28,396 results.
This patch covers FNMS result bits; it does not add FPSCR exception flags or
change the other floating-point operations.

Patch 0029 decodes RSX type-6 CMP vertex attributes as one big-endian word
containing signed X11/Y11/Z10 values. The fields expand by 5/5/6 bits and divide
by 32767, with W=1, including when the format's nominal size is one packed word.
The regression checks signed boundaries, distinct components, byte order,
stride, frequency selection and disabled constant attributes. Optional
`RSX_CMP_TRACE=1` logs at most four fetched samples per attribute; it observes
the actual format, source word and converted vector without changing them.

Patch 0030 restores the three fragment source-range modifiers encoded in DWORD2.
The supported ranges are full, half-range, [-2,2], [-1,1] and [0,1]; mode 5
remains a no-op. Saturation precedes ABS, signed range clamps follow ABS and
negation is last. The third source's ABS bit is also decoded. These are range
operations; this patch does not implement complete fp16 register arithmetic.
The Windows regression executes 72 synthetic source/modifier programs through
D3D11 WARP and compares float readbacks with independently calculated values.
It also checks a negative lighting term through SAT, LG2, EX2 and ADD, where
ignoring saturation previously generated a NaN. No game assets are used.

Patch 0031 records surface clears in submission order, including display clears
when guest-flip accumulation is enabled. It preserves the depth/stencil values,
aspect mask and scissor bounds at recording time. A depth-only clear no longer
becomes a colour clear. Captures include immutable surface dimensions, zeta
offset/pitch and clear parameters for pass diagnosis.

The Windows `wkc2_surface_clear_gpu` check runs production clear helpers on D3D12
WARP. It checks every pixel of synthetic colour, depth and stencil attachments
for independent aspect masks, exact depth values, clipped/empty scissors and
surface clip bounds. It creates no window and uses no game inputs. Partial
colour-component masks remain unsupported and are reported instead of clearing
unrequested components. Separate depth attachments, stencil write masks and
depth-texture sampling remain outside this patch.

Patch 0033 raises the deferred draw-record limit from 2048 to 4096. A native
WKC1 cutscene transition reached the old limit before the final display draw.
The follow-up retains 2149 operations, including that final composite, with
2246 audited callbacks and none omitted. Its vertex upload remains within
the existing capacity. This verifies record retention, not a correct scene.

Patch 0032 increases the offscreen colour-target table from 16 to 32 entries.
A captured later WKC1 frame submits 22 distinct targets; six post-processing
targets were absent from the host table. Capacity failures now emit a bounded
diagnostic instead of silently returning an unavailable target.

Shader descriptor ranges are also separated: atlas at 0, uploaded textures at
1..96, persistent targets at 97..128, and per-draw texture windows from 129.
The previous upload range overlapped both persistent-target descriptors and
the first draw windows. Uploading a later texture could overwrite an earlier
draw's descriptor before command submission.

## Guest frame presentation

The runner sets `RSX_ACCUM_FRAME=1` before graphics initialization. Its existing
pending-FIFO-flip path presents complete batches; display-clear heuristics can
otherwise retain a larger scene's threshold and stop presenting a subsequent
small movie batch. This selects frame completion policy and leaves the guest's
shaders, depth and culling state intact.

On Windows the idle wait uses the SDK's FIFO kick event, signalled by guest
fence polling and the changed parked-word producer notifications from patch
0062. The wait is bounded by four milliseconds or the next vblank deadline.
This allows a kick to wake the consumer promptly while retaining presentation
at guest flips. The
POSIX path keeps its existing wait; macOS execution remains unverified.

## Native text entry

`osk.cpp` replaces the imported keyboard handlers and integrates their events
with the guest's sysutil callback polling. It reads guest pointers and UTF-16
explicitly, preserves the caller's output capacity, and supports ordinary and
continuous entry/cancel/abort lifetimes. `osk_win32.cpp` supplies a Windows editor;
the platform boundary is declared in `osk.hpp`. A macOS editor is not implemented.

One regression uses synthetic guest memory and a controlled UI backend. It covers
Unicode boundaries, output canaries, callback order, duplicate/stale completion,
cancellation, abort, platform-open failure and continuous-mode text retrieval.
It also exercises optional scripted UTF-8 input through the same completion path.
Set `WKC2_OSK_INPUT_FILE` to a private request file for this path: `submit <text>`
submits a line and `cancel` requests cancellation. Requests are consumed only while
the keyboard is open. Malformed UTF-8 and embedded NUL bytes are rejected. No
default player name is supplied. Entered text is not written to the diagnostic log.
A second Windows regression exercises the production edit control and window
procedure with hidden test windows. It checks Unicode text, OK, Cancel, closure,
reuse and orderly shutdown.

`l10n.cpp` registers the UCS2stoUTF8s and UTF8stoUCS2s imports used around name
entry. Its synthetic regression checks guest byte order, exact counts (including
embedded NUL), output canaries, size queries and partial-conversion errors.
These UCS-2 APIs reject supplementary characters; the keyboard protocol itself
supports UTF-16 surrogate pairs. Shift-JIS conversion is not implemented here.

The intended replacement for the temporary Windows editor is an in-game keyboard
with complete gamepad navigation, acceptance and cancellation, sharing the same
guest protocol on Windows and macOS. That interface remains planned work.

## Playing and performance options

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

The Vulkan window title includes `Present/s` and the longest
presentation interval without a diagnostic switch; see
[counter scope](#vulkan-window-title-counter-patch-0065).

Runtime switches, all off unless set:

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

## Optional Windows frame timeline (patch 0077)

This checkpoint preserves the rendering baseline and adds an optional diagnostic;
it does not claim smoother traversal or a speed gain. With both variables unset,
recording stays off. On Windows, set `WKC_TRACE_GATE` to an initially absent
private file path and `WKC_TRACE_OUT` to a private output path before launching.
Load the town save, settle in the view to inspect, then create the gate file.
The frame-clock thread records ten seconds, stops writer admission, waits for
active publishers and writes the trace. Keep the process alive until that file
is complete. Recording allocates a bounded buffer and reports dropped records;
discard overflowed traces. Imports and syscalls shorter than 20 us are filtered.

Analyze locally with `python tools/report_frame_timeline.py <private-trace>`.
The report includes overlapping wall-time spans, not exclusive CPU costs. SPU
entries may include channel waits; the GPU-fence spans measure host waits, not
GPU execution timestamps. Open calls that have not returned at capture end are
not covered. Trace collection adds overhead and must be disabled for speed A/B.
The Windows recorder is a no-op on other platforms, which remain untested.

Raw traces include host thread IDs, call sites and timings. Keep them private;
the public tree contains the source and synthetic regression only. A native
diagnostic precursor was used in town, while this portable publication form
has so far been checked by the Windows build and regression suites.
