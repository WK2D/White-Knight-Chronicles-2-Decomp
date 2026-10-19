# Runtime patch notes

Engineering notes for the numbered SDK patches in [`patches/`](../patches), newest
first. Each note records what the patch changes and how it was checked. Measured
results and investigations are in the [findings log](FINDINGS.md); build and run
instructions are in the [build guide](BUILD.md). These notes were previously part
of the build guide.

| Patches | Topic |
| --- | --- |
| Patch 0120 | [DirectX fragment input requests](#directx-fragment-input-requests-patch-0120) |
| Patch 0119 | [Opt-in DirectX presentation diagnostics](#opt-in-directx-presentation-diagnostics-patch-0119) |
| Patch 0118 | [DirectX near/far clipping](#directx-nearfar-clipping-patch-0118) |
| Patches 0116-0117 | [SPU running status and fallback entry guards](#spu-running-status-and-fallback-entry-guards-patches-0116-0117) |
| Patches 0112–0115 | [DirectX clear/comparison/bias and kernel deadline](#directx-clearcomparisonbias-and-kernel-deadline-patches-01120115) |
| Patch 0107 | [DirectX display and sampler preferences](#directx-display-and-sampler-preferences-patch-0107) |
| Patch 0106 | [DirectX live clear rectangles](#directx-live-clear-rectangles-patch-0106) |
| Patch 0105 | [Guest lightweight mutex tokens](#guest-lightweight-mutex-tokens-patch-0105) |
| Patch 0104 | [Host-layout SPU byte shuffle](#host-layout-spu-byte-shuffle-patch-0104) |
| Patch 0103 | [Vulkan constant upload reuse](#vulkan-constant-upload-reuse-patch-0103) |
| Patch 0102 | [Vulkan batch resource lifetimes](#vulkan-batch-resource-lifetimes-patch-0102) |
| Patch 0101 | [Grouped vertex conversion](#grouped-vertex-conversion-patch-0101) |
| Patch 0100 | [SPIR-V translation cache](#spir-v-translation-cache-patch-0100) |
| Patch 0099 | [Platform query outputs and syscall warnings](#platform-query-outputs-and-syscall-warnings-patch-0099) |
| Patch 0098 | [Vulkan window-depth clip range](#vulkan-window-depth-clip-range-patch-0098) |
| Patches 0095-0096 | [RSX request audit and fragment inputs](#rsx-request-audit-and-fragment-inputs-patches-0095-0096) |
| Patch 0094 | [Vulkan polygon offset](#vulkan-polygon-offset-patch-0094) |
| Patches 0091-0093 | [Task completion and SPU byte operations](#task-completion-and-spu-byte-operations-patches-0091-0093) |
| Patches 0085-0090 | [Runtime and input follow-up](#runtime-and-input-follow-up-patches-0085-0090) |
| Patch 0084 | [Windows analogue response](#windows-analogue-response-patch-0084) |
| Patch 0083 and native runner | [Window close](#window-close-patch-0083-and-native-runner) |
| Patches 0081-0082 | [Native graphics settings](#native-graphics-settings-patches-0081-0082) |
| Patch 0080 | [Direct Vulkan window presentation](#direct-vulkan-window-presentation-patch-0080) |
| Patch 0079 | [Vulkan texture LOD bias](#vulkan-texture-lod-bias-patch-0079) |
| Patch 0078 | [Exact AVX2 SPU FNMS](#exact-avx2-spu-fnms-patch-0078) |
| Patch 0077 | [Optional Windows frame timeline](#optional-windows-frame-timeline-patch-0077) |
| Patch 0076 | [Vulkan batch scheduling](#vulkan-batch-scheduling-patch-0076) |
| Patch 0075 | [FIFO user-command delivery](#fifo-user-command-delivery-patch-0075) |
| Patch 0074 | [Texture-comparison direction](#texture-comparison-direction-patch-0074) |
| Patch 0073 | [Vulkan depth comparison](#vulkan-depth-comparison-patch-0073) |
| Patch 0072 | [Asynchronous SPU worker ownership](#asynchronous-spu-worker-ownership-patch-0072) |
| Patch 0071 | [Depth snapshot generations](#depth-snapshot-generations-patch-0071) |
| Patches 0067-0070 | [Depth sampling and real flip completion](#depth-sampling-and-real-flip-completion-patches-0067-0070) |
| Patch 0065 | [Vulkan window-title counter](#vulkan-window-title-counter-patch-0065) |
| Patches 0062-0064 | [FIFO producer wake and SPU execution helpers](#fifo-producer-wake-and-spu-execution-helpers-patches-0062-0064) |
| Patches 0060-0061 | [SPU conversion scales and DMA ownership](#spu-conversion-scales-and-dma-ownership-patches-0060-0061) |
| Patch 0058 | [SPURS job-queue physics](#spurs-job-queue-physics-patch-0058) |
| Patches 0053-0057 | [Renderer coherence and execution overhead](#renderer-coherence-and-execution-overhead-patches-0053-0057) |
| Earlier patches | [Earlier runtime fixes and validation history](#earlier-runtime-fixes-and-validation-history) |

## DirectX fragment input requests (patch 0120)

Live DirectX ignored TC0–7 2D input override requests even though the shared
Vulkan path supplied them to the fragment decompiler. A native plains trace
contains nonzero masks on 2,079 of 2,448 recorded draws. DirectX now forwards
the mask in both buffered and literal constant modes and includes it in PSO
identity. Both renderers use one decoder for the register bits; Vulkan retains
its existing behavior. The new literal decompiler entry rejects unsupported bits,
and zero-mask output stays byte-identical to the earlier entry.

`wkc2_fragment_inputs` checks all eight coordinates, perspective modes, enabled/
disabled requests, upper register bits, unsupported input bits and both constant
modes through 64 DirectX shader compilations. The existing Vulkan GPU shader
request oracle remains covered. Leaf-shadow detail, cloud shade and grass
appearance remain separate visual acceptance checks; this patch is not an FPS
or full rendering-parity claim. The native `20261020-003932` route verifies
44 shader variants consuming requested overrides; broad solid foliage shadows
still appear in the inspected final view.

## Opt-in DirectX presentation diagnostics (patch 0119)

The benchmark ring and CSV were previously active without a switch. They now
require `RSX_PRESENT_MEASURE=1`; player mode always disables them. Successful
presentations still update the optional FPS overlay. Eight production-recorder
fixture cases cover default/disabled/enabled settings, scheduling details,
player-mode precedence and CSV output. See [clean-play checks](BUILD.md#clean-play-diagnostics-patch-0119).


## DirectX near/far clipping (patch 0118)

Live DirectX now explicitly enables `DepthClipEnable` before creating a graphics
pipeline. Its zero-initialized descriptor had disabled near/far clipping, unlike
the ordinary Vulkan path. In the reproduced plains view, behind-camera ground
then incorrectly covered Leonard's lower body. The single flag change removes
that artifact after transition, movement and camera rotation.

The production helper is exercised by eight WARP cases covering out-of-range,
boundary/interior and non-unit-W depths, including two old disabled-state
controls. All 107 Windows checks pass in Debug and Release. Non-default RSX
depth-clamp modes, other parity gaps and performance remain separate work.
See the [capture and limits](GALLERY.md#2026-10-19--directx-plains-clipping-fixed).

## SPU running status and fallback entry guards (patches 0116-0117)

0116 establishes `SPU_STATUS_RUNNING` at `spu_run_with_halt` entry before
compiled execution. SPURS job reset could leave it stopped, while the old
interpreter silently changed it. The title tests cover five prior status values
and reset host-call state. 0117 adds `SPU_FALLBACK_STRICT=1`, which rejects
interpreter or microstep entry before any instruction executes and exits with
code 86. Separate child-process tests exercise both fatal entrances; default
fallback and census behavior remain available.

The project build also adds five hash-checked static entry hints, preserving the
24-image registry and skipping known obsolete duplicate wrappers. Forty private
function replays match complete state and continuation. A seven-minute DirectX
saved-town route with the strict guard enabled records zero fallback entries;
all five compiled entries execute in a separate instrumented run. Full-game
coverage and readable geometry replacements remain unverified. All 106 Windows
checks pass in Debug and Release. See [native execution work](NATIVE_CODE.md).

## DirectX clear/comparison/bias and kernel deadline (patches 0112–0115)

0112 preserves unselected color channels during partial clears. 0113 supplies
live DirectX comparison shaders/samplers with texture-specific reversed operand
order and D24 precision. 0114 forwards fill polygon bias through keyed D32
pipeline state. 0115 fixes early optional-kernel mutex timeouts against a
monotonic deadline. All 103 Windows tests pass in Debug and Release.

The GPU tests cover 128 clear cases, 144 comparison results and sixteen constant
bias cases. Native tests execute the new rendering paths, but character-shadow
appearance remains unresolved. See [findings](FINDINGS.md#directx-shadow-follow-up-and-parity-review-patches-01120115),
[build checks](BUILD.md#directx-clears-comparison-sampling-and-polygon-bias-patches-01120114),
and the [remaining parity gaps](DIRECTX_PARITY.md).

## DirectX display and sampler preferences (patch 0107)

The native menu now forwards window mode, output window size, VSync and
anisotropic filtering to the DirectX live renderer. Window-mode changes drain
GPU references, preserve windowed placement and resize flip-model backbuffers
before the next presentation. VSync-off uses tearing only when DXGI reports
support and the chain is windowed. This follows Microsoft's
[DXGI swap-chain requirements](https://learn.microsoft.com/en-us/windows/win32/direct3d12/swap-chains).

Sampler caching now compares complete decoded descriptors instead of a 32-bit
key that discarded LOD bits. Signed LOD bias is applied; Off/2x/4x/8x/16x
preferences preserve point-filtered UI and non-mipmapped textures. Descriptor
changes flush earlier GPU work before sampler slots are reused.

`wkc2_d3d12_graphics` checks the production display-transition helper with
recording Win32/DXGI calls, tearing restrictions, signed bias, exact LOD matching
and the filtering policy. The menu regression checks DirectX controls after a
simulated restart. All 96 Windows checks pass in Debug and Release. A private
native run changed 720p to 1080p, toggled VSync and selected 16x filtering through
the actual panel. Read-only inspection confirmed the renderer applied those
values and the window client resized to 1920x1080. See the
[DirectX capture](GALLERY.md#2026-10-19--directx-graphics-settings).

Output size is separate from internal rendering resolution. Real fullscreen
switches, FXAA, comparison sampling, fresh DirectX avatar capture and complete
transition parity remain open. Patch 0109 adds the in-game FPS overlay and
verifies its menu toggle. Higher FPS defects are deferred; the native run stayed
at 30. Vulkan remains the default until a different API is selected and restarted.

## DirectX live clear rectangles (patch 0106)

The selected DirectX path uses `rsx_live_draw`, independently of the shared
Vulkan engine. Its clear callback ignored the scissor and treated an explicit
zero depth/stencil word as depth 1. Patch 0106 decodes the live register values
through the common D3D12 clear helper, clips clear rectangles to the color
surface, and clears the selected depth resource with the requested values.
Color clears retain their existing channel-mask limitation.

The WARP surface-clear regression now uses the production register decoder,
including zero depth, far depth, separate aspects, clipped and empty rectangles,
and padded depth targets. All 95 Windows checks pass in Debug and Release.
Isolated 30 FPS DirectX Continue and menu captures were visually checked before
and after the fix. No visible improvement is established in this view. Graphics
preferences, comparison samplers and generated-portrait readback remain separate
work.

## Guest lightweight mutex tokens (patch 0105)

**Problem.** Uncontended guest lightweight mutexes entered the Windows semaphore
API on both acquisition and release. A WKC census found over 99.7% of attempts
uncontended.

**Fix.** Adapt [JonathanDC64's mutex change](https://github.com/JonathanDC64/ps3recomp/commit/098690d409bfaea44051288a4f1d752a4354d918)
as a portable C++ binary token, with a condition variable on contention.
Preserve recursive ownership, cross-thread release, timeouts, reused slots and
the existing guest condition implementation. Token and waiter atomics plus a
shared sleep mutex prevent lost wakeups. Slot addresses use acquire/release
publication. `PS3RECOMP_LWMUTEX_KERNEL=1`, selected at startup, retains the
previous semaphore backend for comparison.

**Evidence.** `wkc2_lwmutex_user` and `wkc2_lwmutex_kernel` exercise the actual
handlers and project condition implementation. Wake, contention, recursion,
deadline, recreation and repeated critical-section checks pass. All 95 Windows
checks pass in Debug and Release, including the separate host portrait check.
Clean application, prefix-104 upgrade and no-op reapplication pass. A regular
Release Continue run reaches the town; its GPU readbacks were inspected.

**Limits.** Synthetic uncontended token cost is lower, but the four alternating
native comparison runs show no consistent FPS gain. The desktop remains on its
earlier checkpoint until the next checked installation. See the
[measurements](FINDINGS.md#remaining-fork-candidates-and-mutex-measurement).

## Host-layout SPU byte shuffle (patch 0104)

**Problem.** The SSE4.1 SHUFB helper swapped both inputs and the control vector
into SPU byte order, selected bytes, then swapped the result back. Geometry jobs
call this helper frequently.

**Fix.** Adapt [JonathanDC64's host-layout shuffle](https://github.com/JonathanDC64/ps3recomp/commit/6a019d53d4ce195d1f469c75ad1c9564513b7f85):
select the source host byte with `(selector & 15) ^ 3`, use bit 4 to select the
input and retain the special zero/255/128 selector classes. This removes four
byte-order conversions. Other operations and float modes are unchanged. The
portable implementation remains available, with a compile-time scalar override
for testing. The SDK MIT license is retained.

**Evidence.** Both new checks use an independent numerical-word byte oracle over
204,352 synthetic cases, including every selector in every output lane. Removing
the host-lane XOR fails the same check. All 92 Windows checks pass in Debug and
Release; clean application, prefix-103 upgrade and no-op reapplication pass.
A private image-19 verifier compares 5,351,813 sampled actual helper calls with
the scalar reference, with zero mismatches. The regular Release build reloads
Continue and completes short town movement; GPU readbacks were inspected.

**Limits.** The synthetic four-million-operation benchmark falls from 4.63 to
3.31 ms on this host. Capped saved-town samples do not establish a consistent
game FPS gain. Mac/Linux native behavior remains unverified. See the
[full conditions and unlocked probe](FINDINGS.md#host-layout-spu-byte-shuffle-and-unlocked-headroom-patch-0104).

## Vulkan constant upload reuse (patch 0103)

**Problem.** Many consecutive draws bind identical constants. The backend copied
them into a CPU shadow and uploaded another copy for every draw, even while the
previous immutable block was still usable in the same live batch.

**Fix.** Compare incoming constants against the producer-owned CPU shadow. Keep
one uniform-buffer offset per shader stage when its bytes, size, buffer identity,
alignment and live arena range match. Changed or absent bindings invalidate the
stage; retirement and shutdown invalidate both stages. Reserve still runs before
offset lookup because it can flush and switch arenas. No cache-hit reads of
write-combined GPU memory are needed. `RSX_VK_CONSTANT_REUSE=0` restores the
original shadow copies and per-draw uploads.

**Evidence.** The new `wkc2_vk_constant_reuse` executes extracted production
binding, upload and reset helpers over 10,000 changing draw pairs, delayed
synthetic arenas, allocation failure and a same-handle reset/size-change case.
Removing the retirement invalidation fails the byte comparison. All 90 Windows
checks pass in Debug and Release. The 103-patch stack applies from clean,
upgrades prefix 102 by two files and reapplies unchanged. A private native
verifier checks at least 6,159,744 reused blocks with zero byte mismatches.
The regular Release build reloads Continue and completes short town movement;
its GPU readbacks were inspected.

**Limits.** Alternating samples in one NPC-facing town view average 28.58 rather
than 25.14 presentations/s, with 43.9% of uniform-upload bytes skipped. These
samples do not establish sustained smooth 30 FPS, animation fidelity or a
flicker fix. See the [conditions and full results](FINDINGS.md#vulkan-constant-upload-reuse-patch-0103).

## Vulkan batch resource lifetimes (patch 0102)

**Problem.** A closed recording batch could still have a submitted spare batch
using a released texture. Separately, window acquisition could skip presentation
after submitting the frame, bypassing the later fence before resource retirement.

**Fix.** Immediate texture release drains the spare slot first. Skipped window
acquisition drains submitted GPU work before retiring the frame. Failed fence
waits preserve the pending slot and deferred images, suppress batch reuse and
stop new batch recording. Deferred-list flush failure retains the incoming
texture handle instead of clearing ownership.

**Evidence.** `wkc2_vk_batch_lifetime` executes extracted production helpers
against delayed/failing completion. `wkc2_vk_present_source` also checks skipped
and failed window acquisition. Removing either wait fails its regression. All
89 Windows checks pass in Debug and Release. The 102-patch stack applies from
clean, upgrades prefix 101 by one file and reapplies unchanged. A regular Release
build reloads Continue and completes a short hidden, muted town route.

**Limits.** These are verified lifetime faults, not demonstrated causes of town
lag or foot flicker. A separate native allocation census sees no occurrence of
the corrected closed-release case and no failed fence waits. Skipped acquisition
is covered by the recording-API test, not a native window test. Robust device-loss
recovery and further allocation pooling remain open. See the
[findings](FINDINGS.md#vulkan-batch-resource-lifetimes-patch-0102).

## Grouped vertex conversion (patch 0101)

**Problem.** Compact vertex preparation repeated attribute and format decisions
for each vertex and component, even though a draw uses one layout.

**Fix.** `rsx_vertex_fetch_batch` groups work by attribute and specializes the
decoder for each format outside the vertex/component loops. It preserves the
existing byte-order and numeric conversions, instancing divisors, prepared-span
bounds, inline-draw defaults and fatal invalid-position behavior. Failed draws
discard their partial output. No guest pointers or decoded output are retained
between draws; guest-memory validation and GPU resource ownership are unchanged.

**Evidence.** `wkc2_vertex_batch` compares 69,140 synthetic cases against
`rsx_vertex_fetch_one`, including all half encodings, complete/partial/missing
prepared spans, invalid formats and instancing. An inverted divider rule fails
the test. All 88 Windows checks pass in Debug and Release. The 101-patch stack
applies from clean, upgrades prefix 100 by three files and reapplies unchanged.
A private native wrapper checks 4,846,180 draws with zero byte/status mismatches.
The regular Release build reloads Continue and completes a short town route.

**Limits.** The fixed-work conversion benchmark takes 44.6 ms rather than
72.3 ms for 500,000 batches on this host. This is not a game FPS result. Sustained
traversal speed, full rendering fidelity and other platforms remain unverified.
See the [findings and conditions](FINDINGS.md#grouped-vertex-conversion-patch-0101).

## SPIR-V translation cache (patch 0100)

**Problem.** Different render-state pipelines can use the same generated shader
text. Vulkan compiled both stages again for each such pipeline, stalling FIFO
consumption and, in observed town jobs, SPU output-ring allocation.

**Fix.** Cache successful HLSL-to-SPIR-V translations by exact stage and source
bytes. Hashes only accelerate comparison. The process-local LRU retains at most
1,024 entries and 16 MiB of allocated source/output payload, plus fixed entry
metadata. One incoming allocation can temporarily add up to another 16 MiB.
Allocation failure preserves existing entries and returns the newly compiled
output normally. Invalid programs are not cached. A cache hit preserves the
original output-capacity checks. Access and glslang initialization are serialized.

`RSX_SPIRV_CACHE=0` disables reuse. The statistics API reports requests, hits,
compilations, elapsed compiler/admission time, retained bytes/entries, evictions
and admission failures. No shader data is written to disk.

**Evidence.** `wkc2_spirv_cache` checks stage/source identity, deliberate hash
collisions, LRU eviction, byte/count limits, overflow rejection and allocation
failure. Two translation tests cover reuse enabled/disabled, byte-identical
compiled/cached VS/FS/CS, invalid input, small output buffers and concurrent calls.
A deliberately broken collision comparison fails the regression. All 87 Windows
checks pass in Debug and Release, including media checks with their dependencies.
The 100-patch stack upgrades a 99-patch checkout and reapplies without changes.

**Limits.** The cache removes repeated compilation, not first-use compilation,
GPU pipeline creation, all traversal lag or the remaining SPU interpreter fallback.
See the [measurement conditions](FINDINGS.md#shader-translation-stalls-and-cache-patch-0100).

## Platform query outputs and syscall warnings (patch 0099)

Timezone retrieval and retail GPIO queries now initialize guest output buffers
with checked bounds and guest byte order. `wkc2_platform_queries` tests their
production registration and error paths, plus concurrent per-ID warning gates.
The loader reports each valid unhandled syscall ID once; the old global budget
of 30 warnings is removed. IDs outside the 1024-entry table share one warning.
Probes expose `unimplemented_syscall_ids`, independently of the RSX audit switch.
Full logs remain private. An empty list does not verify registered handlers.

All 84 Windows checks pass in Debug and Release. A hidden/muted final candidate
reloads Continue and exits 0 without either old stub warning. POSIX host-timezone
behavior and full guest-query usage remain unverified. See
[the findings](FINDINGS.md#platform-queries-and-missing-syscall-reporting-patch-0099).

## Vulkan window-depth clip range (patch 0098)

The shared engine now pairs vertex-depth normalization and an optional backend
viewport interval. Vulkan uses unrestricted depth range when supported. Unsupported
or unverified intervals restore legacy mapping and appear as `clip_range_fallback`
in the audit. The interval is dynamic, not a new pipeline variant. Other backends
retain their earlier behavior.

`wkc2_vk_depth_range_gpu` checks depth preservation and near/far clipping on four
depth formats; `wkc2_draw_engine` checks forwarding, reset, rejection and pipeline
reuse. All 83 checks pass in Debug and Release. A hidden/muted saved-town comparison
exits normally without establishing a flicker fix or speed gain. See
[the evidence and limits](FINDINGS.md#vulkan-window-depth-intervals-patch-0098).

## RSX request audit and fragment inputs (patches 0095-0096)

`tools/probe.ps1` now enables `RSX_ENGINE_AUDIT` by default. It writes a private
`work/probes/<tag>.rsx-audit.tsv` and lists unresolved features in the result JSON
and console output. Missing reports produce a warning rather than implying
coverage. Use `-NoRsxAudit` for timing comparisons; keep diagnostic conditions
consistent. Direct launches only enable the audit when that environment variable
names a writable private file.

The census records known feature names, method addresses and counts, without
register values or game program/texture bytes. Method classification and active
draw-time coverage are separate: an unclassified write may be handled elsewhere.
This is a bounded set of checks, not a complete RSX conformance audit. Counts of
backend submissions do not prove GPU completion or correct rendering.

Patch 0096 applies TC0-7 2D overrides and per-instruction perspective correction
before source modifiers, and includes the override mask in the pipeline key.
Predicated writes now select components without `lerp`, preserving unselected
signed zero and avoiding NaN contamination. TC8/9 remain unsupported.

`wkc2_rsx_request_audit` checks feature gates, report contents and disabled mode.
`wkc2_rsx_shader_requests_gpu` checks 512 synthetic GPU result lanes, plus fragment
shader compilation; `wkc2_draw_engine` checks coordinate-state pipeline reuse.
Negative controls fail for each old shader fault and an omitted cache-key field.
All 82 checks pass in Debug and Release. A hidden/muted saved-town run exits
normally, but establishes no FPS or flicker improvement. See
[evidence and remaining gaps](FINDINGS.md#active-rsx-request-coverage-and-fragment-inputs-patches-0095-0096).

Patch 0097 extends the same census and regression with framebuffer sRGB output,
fragment depth export and window offsets. sRGB requires integer colour output,
half-register exports and writable RGB channels; depth export requires depth
testing, even when depth writes are disabled. These features remain unimplemented.
They were not active in the 97-patch Continue/town check. All 82 checks pass in
both configurations; the existing gallery frame retains its 96-patch provenance.

## Vulkan polygon offset (patch 0094)

The shared engine now decodes fill enable `0x0A68`, float scale `0x0A78` and
float bias `0x0A7C`, and forwards zero factors whenever fill offset is disabled
or the primitive is a point/line. An optional backend callback preserves older
backends. Vulkan enables bias for filled triangle topologies and records core
`vkCmdSetDepthBias` on each draw. Dynamic factors stay outside pipeline keys.
NVIDIA fixed D24 targets halve the constant factor; D32 is passed directly.
Point/line offset and the D3D12/Metal paths remain unimplemented.

The extended `wkc2_draw_engine` regression checks forwarding, changing signs,
disabling/re-enabling, line isolation and pipeline reuse. The new
`wkc2_vk_polygon_offset_gpu` creates no window and uses synthetic vertex shaders
and GPU depth readbacks. It checks constant/slope bias direction and zero reset
on supported D32/D32S8/D24S8/X8D24 formats, plus host-side vendor/format selection.
The prior engine fails the forwarding regression. Disabling raster bias makes
the GPU readback regression fail. These checks do not establish RSX parity at
every depth/slope or on other vendors. Guest Z16 bias precision also needs
validation against the backend-selected host depth format.

All 80 Windows checks pass in Debug and Release. A hidden/muted native candidate
reloads Continue, moves in town and switches offset off/on in one process.
The short frame sequences do not establish a flicker reduction or FPS gain.
See [findings](FINDINGS.md#vulkan-polygon-offset-patch-0094) and
[the comparison captures](GALLERY.md#2026-10-18---vulkan-polygon-offset-check).

## Task completion and SPU byte operations (patches 0091-0093)

0091 makes task completion's bit updates and descriptor snapshots coherent
with SPU atomic commits, keeping the lifetime lock and avoiding worker launch
under the reservation lock. 0092 masks task ELF flag bits before image lookup.
`wkc2_spu_async_lifetime` now links the real coherence registry and tests the
compare/commit interleaving, reservation-loss event, flagged images and existing
ownership/cleanup rules. The old paths and a missing-notification variant fail
independently. Other SPURS producer paths remain separate work.

0093 uses SSSE3 byte shuffles for nine quadword byte-shift/rotate helpers. Builds
without SSSE3 retain scalar C; `PS3RECOMP_SPU_BYTE_SCALAR=1` forces that fallback
for comparison. `wkc2_spu_byte_vector` and `wkc2_spu_byte_scalar` compare all
counts and byte-order/wrap/zero-fill cases with an independent architectural
oracle. Existing floating-point modes and exact AVX2 FNMS are retained.

All 79 Windows checks pass in Debug and Release. A saved-town hidden/muted run
works and exits normally. The isolated helper benchmark improves, but the
repeated native comparison is mixed; no sustained game FPS gain is claimed.
See [measurement scope](FINDINGS.md#spurs-completion-coherence-and-spu-byte-operations-patches-0091-0093).

## Runtime and input follow-up (patches 0085-0090)

0085 fixes the absolute SMC load/store address scale. 0086 fixes RR SPU multiply
accumulation and its unsigned form; 0089 fills four additional integer interpreter
cases. `wkc2_spu_microstep_profile` checks independent loads/stores and guard
addresses. `wkc2_spu_interp_ops` checks destinations, aliases, modular overflow,
borrow boundaries, immediate signs and halfword shifts.

0087 extends the Windows stick mapping with a 5% outer margin and smooth
diagonal range expansion. The behavioral reference is RPCS3's
[default stick shape](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Io/pad_config.h)
and the paired-axis conversion linked above; the implementation is project-written C.
0088 serializes pad APIs and event-pump polling. `wkc2_pad_report` uses a paused
recording driver to reproduce the report race; `wkc2_pad_exports` validates all
twelve generated HLE registrations. Physical controller feel remains unverified.

0090 translates PPU `vsubuws`, including sticky VSCR saturation and BE word
lanes. `wkc2_ppu_vsub` decodes original synthetic opcodes, runs their actual
lifted output and checks aliases and saturation boundaries. Regenerate existing
PPU output with the updated lifter to replace any previous TODOs; applying the
SDK patches alone does not rewrite already generated game sources.

These regressions fail against the preceding implementations. All 77 checks pass
in Debug and Release. A private Vulkan run reloads the save and verifies steady
synthetic input reaching the game through partial and full directional probes.
This batch makes no FPS or complete physical walk/run compatibility claim.

## Windows analogue response (patch 0084)

Historical first change; see patches 0087-0088 below for the follow-up after
the user reported remaining diagonal walk/run inconsistency.

The old XInput conversion applied a separate deadzone to each axis. At a full
45-degree tilt, it sent only about 87% of the straight-ahead magnitude, so the
game could receive different walk/run input depending on direction.
The new project-written conversion works on the stick vector before quantizing
it to PS3 axes. It preserves full magnitude around the circular gate, uses a
small noise gate and a continuous partial-tilt curve, and inverts Y before
quantization. The right stick uses its own XInput deadzone landmark.

RPCS3's [XInput defaults](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Input/xinput_pad_handler.cpp)
and [paired-axis conversion](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Io/PadHandler.cpp)
were reviewed for expected behavior. This patch contains an independent C
implementation, not copied emulator source. The `wkc2_pad_stick` regression
checks neutral/noise, exact endpoints, Y direction, 720 full-tilt angles and
monotonic partial tilt. Private native tests supply raw XInput values before
production conversion and observe partial/full-tilt town movement. Physical
controller feel, other devices and the SDL backend are not established by this test.

## Window close (patch 0083 and native runner)

The Vulkan event pump now accepts `SDL_WINDOWEVENT_CLOSE` as well as `SDL_QUIT`.
The runner responds to a close result with normal process exit. Previously it
disabled RSX and continued running, leaving guest threads waiting for rendering
work that could no longer complete. Standard exit invokes the existing media
worker cleanup handlers.

The recording-API regression feeds close, quit, Escape and unrelated window
events through the production pump. Private native integration tests inject the
same SDL close event into the real event queue: title and saved-town settings
runs exit with code 0 without an external forced termination. This tests event
handling and process exit; it is not a physical mouse-click automation test.

## Native graphics settings (patches 0081-0082)

Open Triangle -> System -> Graphics Settings. The original game widgets and
controller navigation are reused. X cycles an option; Circle goes back.

All eleven options and Back appear together in the original full Game Settings
panel, including its frame, native scrollbar, row highlight and selection hand.
Up/Down selects a row; Left/Right changes its value, and X advances it.
Circle or Back returns to System with the original transition. The regular
Game Settings page remains separate and retains its original controls.

| Control | Options |
|---|---|
| Graphics API | Vulkan / DirectX 12 (restart required) |
| Window Mode | Windowed / Borderless / Fullscreen |
| Output Resolution | 720p / 1080p / 1440p / 2160p |
| VSync | Off / On |
| Frame Rate Limit | 30 / 40 / 60 / 120 / 144 / Unlocked |
| Anisotropic Filtering | Off / 2x / 4x / 8x / 16x |
| Anti-Aliasing | Off / FXAA |
| World Distance, NPC Distance, Distance Fog | Pending; inactive placeholders |
| FPS Counter | Off / On |

Output resolution controls the window/presentation size, not the game's internal
render-target resolution. Borderless uses the desktop mode. Unsupported fullscreen
modes can fail; the preference is not a guarantee that the monitor accepts it.
VSync falls back to FIFO where the device lacks the preferred present mode.
Anisotropy is clamped to device support and applies to eligible mipmapped textures.
FXAA filters the completed guest image, including its menus, before scaling and
the optional FPS overlay. It is an inexpensive spatial pass, not MSAA or TAA.
Its luminance/edge method follows the
[NVIDIA FXAA white paper](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf);
the patch contains a project-written shader rather than copied vendor sample code.
The game's menu background blur remains; a clear live preview is still pending.

Preferences persist in `graphics-settings.ini` beside the executable. Writes are
atomic; malformed/unknown/duplicate values reject the file and retain defaults.
Missing newer keys retain defaults. Remove the file to reset preferences.
Graphics API changes are marked with an asterisk until the next launch.
Vulkan is the default; DirectX 12 is experimental. Display settings other than
the frame limit and API selector currently apply only to Vulkan. Its fallback
presentation synchronizes actual flip boundaries and resizes scanout buffers
before copying a differently sized guest image. Renderer parity is unverified.

The frame limit changes the runner's vblank cadence as well as its host limiter.
The normal two-tick interval remains for fixed caps; Unlocked bypasses the guest
interval and extra-wait flag on the game graphics thread, restoring the original
requested values when leaving Unlocked. Game time still comes from the monotonic
clock. Higher caps do not guarantee that the CPU/GPU can finish at that rate.
Short hidden/muted, log-free saved-scene probes exceed 30 completed frames/s
with approximately one game-clock second per real second; see
[measured scope](FINDINGS.md#native-graphics-settings-and-frame-rate-control).
They do not prove full-game high-FPS compatibility or unique animation per frame.
An earlier diagnostic change to Unlocked with VSync on stopped advancing frames.
The runner now ticks on new flip boundaries, with a 59.94 Hz idle/boot heartbeat,
instead of on every FIFO poll. New flips can tick immediately, so the heartbeat
does not impose a frame cap. Repeated native live switches and VSync toggles
remain responsive in the tested saved-town run; general high-FPS stability remains unverified.

`wkc2_graphics_menu`, `wkc2_graphics_settings`, `wkc2_graphics_timing` and
`wkc2_vk_graphics` cover full-panel routing, original-panel isolation, Left/Right and X changes,
persistence, interval restoration,
window/sampler configuration and the overlay without a black background.
`wkc2_depth_shader_binding` also compiles the production FXAA shader and verifies
its descriptor bindings. The full Windows suite passes 79 checks in Debug and
Release. Native runs verify menu navigation and saved-scene rendering in both
APIs, with FXAA initialization on Vulkan; not every display mode or monitor is tested.

## Direct Vulkan window presentation (patch 0080)

The shared engine passes the selected guest surface and its own dimensions to
window presentation. A 1920x1080 frame no longer passes through the fixed
1280x720 diagnostic image on its way to the swapchain. Normal presents omit
that intermediate full-frame blit; a diagnostic center pixel is read directly
from the guest surface. Explicit full-frame dumps and the optional shutdown
hold retain their legacy image. The command-allocation fallback also uses the
selected guest source. Matching dimensions use nearest filtering; differing
window dimensions use one linear scaling operation.

`wkc2_vk_present_source` executes the production functions against recording
Vulkan calls. It covers source selection, larger/smaller/equal window extents,
center readback, command-buffer allocation failure, unavailable swapchain,
headless operation, explicit dumps and shutdown-hold behavior. The previous
implementation fails the regression. All 68 Windows checks pass in Debug and
Release at that checkpoint. Subsequent 82-patch native runs execute this path
and the gallery now includes final swapchain readbacks. This does not establish
visual parity with RPCS3 or a speed improvement attributable to patch 0080.

## Vulkan texture LOD bias (patch 0079)

The shared draw engine decodes signed 4.8 texture LOD bias and forwards it to
Vulkan samplers, clamped to the device limit. Sampler identity retains the bias
and exact LOD bounds. The optional legacy Vulkan path and Metal do not gain
this behavior. No forced sharpening, anisotropy or render-resolution change is
introduced. `wkc2_draw_engine` covers register decoding; `wkc2_vk_sampler_lod`
drives the production sampler builder with a recording Vulkan entry point,
covering cache reuse, signed bias, fine LOD steps, limits and comparison state.
The regression fails against the old sampler builder. All 67 Windows checks
pass in Debug and optimized Release. See [visual evidence and remaining
state gaps](FINDINGS.md#texture-detail-and-rendering-state-audit-patch-0079).

## Exact AVX2 SPU FNMS (patch 0078)

`spu_fnms` uses four independent 64-bit lanes when compiled with AVX2. The
normal Debug build retains the scalar fallback; the optimized `/arch:AVX2`
build exercises the vector form. No CPU ISA requirement is added to the
default build. The helper retains the scalar arithmetic contract, including
finite exponent 255 and saturation. Apple Silicon remains untested.

`wkc2_spu_fnms_vector` checks independent lanes, zero/subnormal inputs, extended
values and cancellation against the scalar reference. On x86 it runs all
rounding/FTZ/DAZ combinations and verifies unchanged MXCSR. The existing
`wkc2_spu_fp` independent oracle remains enabled. Native A/B results and their
limits are in [the findings](FINDINGS.md#exact-spu-vector-math-patch-0078).

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

## Depth snapshot generations (patch 0071)

Every sampled depth snapshot must match its source target's latest write or
clear generation, including when a later pass has bound a different target.
Patch 0071 moves that validation into the shared snapshot helper, preventing
stale shadow-depth reuse across target switches. Four synthetic regression
assertions fail without the change and pass with it. The 73-patch native town run
includes this change but still shows foot-shadow faults.

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
slowed while idle. The historical 76-patch stack passed 64 checks in Debug and
Release; see the [timings and worker census](FINDINGS.md#frame-pacing-and-game-speed).

## Vulkan window-title counter (patch 0065)

Historical implementation: patch 0108 removes these window-title statistics
from Vulkan and both DirectX hosts. The optional in-game FPS overlay remains
a separate setting.

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
[suggestion branch](https://github.com/WK2D/White-Knight-Chronicles-2-Recomp/blob/6ca620d7f328996eeb4bf118652d78528d90107a/patches/0039-memory-range-reuse.patch).
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
the current Windows Vulkan suite.

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

## Static window captions (patch 0108)

Keep the original game-name caption on Vulkan, the live DirectX null-window
host, and the standalone DirectX backend. Remove the caption refresh loops
and their private timing fields. Diagnostic measurements and the separately
controlled in-game FPS overlay are unchanged. This is a UI-only change and
does not alter frame pacing or rendering.

All 96 Windows checks pass in Debug and Release. Hidden, muted startup runs
confirmed static captions on Vulkan and live DirectX after 12 seconds without
controller input. Standalone DirectX was compiled, but was not launched.

## DirectX in-game FPS overlay (patch 0109)

The live DirectX renderer honors the native FPS Counter option. A small mapped
upload buffer holds original bitmap glyphs; foreground-only copy spans preserve
all background pixels. The existing presentation fence retires those reads
before the next CPU update or destruction. Off records no overlay work and
allocates no overlay storage. The movie and ordinary presentation paths use
the same composition helper; movie playback itself has not been rechecked.

Count S_OK presentations, excluding positive occlusion statuses and failures.
Publish an estimate after a one-second window and return zero after a two-second
presentation stall. This measures accepted presentations, not guest animation
updates or display scanout. Window captions remain static. LD_FRAME_DUMP now
reads the final backbuffer after composition; guest render targets are untouched.
LD_RECORD and individual surface diagnostics retain their original-surface scope.

The new wkc2_present_metrics check covers clock origin/reset, errors, occlusion,
stalls and bounded glyph generation. wkc2_d3d12_counter_gpu checks seven WARP
cases through the production helper, including every RGBA background pixel,
clipping, Off and fenced buffer reuse. The native menu regression checks that
DirectX can toggle this setting independently. FXAA was unavailable at this
checkpoint and is added by patch 0110.
All 98 Windows checks pass in both Debug and Release. An isolated saved-town run
verifies the menu Off/On selections against renderer state and real backbuffer
captures. No frame-rate improvement or broad rendering-parity claim follows.

## Shared FXAA and live DirectX filtering (patch 0110)

Extract the original Vulkan FXAA shader math into rsx_fxaa_hlsl.h without changing
its expressions. Vulkan retains its bindings and the production shader fixture;
DirectX uses t0/s0/u0 and a separate compute pipeline. The native Anti-Aliasing
control is now available on DirectX. Source RGBA8 pixels and alpha are preserved;
the filtered image is a separate UAV. Transition it to COPY_SOURCE after dispatch,
restore the source state, copy the result, then add sharp counter glyphs. Off
returns the original source without allocating or dispatching. Previous GPU
references retire at the existing presentation fence before resize, descriptor
updates or destruction. Movie presentation uses the same filter helper but has
not been revalidated in a native movie run.

wkc2_d3d12_fxaa_gpu verifies eight real WARP dispatch/copy cases against an
independent CPU implementation, including every source and alpha byte, flat and
diagonal/checker images, odd dimensions, a one-pixel image, resizing, reuse and
Off. The RGB tolerance is two UNORM8 values. The native menu regression and
Vulkan production shader-binding check also pass. All 99 Windows checks pass
in Debug and Release. Isolated native menu/world captures and read-only applied
settings verify Off/FXAA switching and a separate 1920x1080 output allocation;
there are no FXAA-unavailable or D3D-failure messages in that run. This does
not establish full visual fidelity, smooth traversal or movie/transition parity.

## DirectX guest-surface readback (patch 0111)

Add a render-owner-only readback entry point for exact-base linear ARGB8 targets.
Record a GPU copy, restore the source's render-target state, wait at the existing
fence and convert RGBA8 to the guest byte order. Honor the declared guest pitch;
leave padding and bytes outside the requested span unchanged. Unknown/aliased,
unsupported and failed reads return failure. The live target now remembers its
declared layout; the incorrect rsx_dispatch.h comment is corrected to format 8.

The separate host avatar bridge now chooses live DirectX when active rather than
calling the inactive shared engine, whose no-op previously returned success.
Loading CPU-resident portraits still bypasses readback. The extended routing
test fails on the previous bridge. Eight WARP production-helper cases verify
ARGB/RGBA, portrait dimensions, padded pitch, partial pixels/rows, guards and
invalid spans. All 100 Windows checks pass in Debug and Release.

Private native run 20261019-191430 uses ordinary New Game/creator controls,
isolated saves and verified muting. The 256x144 rendered face is copied with
pitch 1024 and 147,456 nonzero bytes; the ready flag is 1 and the image was
visually inspected. This verifies capture only: same-session menu display and
generated-image save/reload remain pending. Cutscene skips later showed stale
portrait/empty geometry frames; cause and baseline comparison remain open.
Normal startup input is unchanged, and no game data or private paths are added.
