# Findings

Updated 2026-10-19. Target: White Knight Chronicles II, USA BLUS30784 v01.00.

**Current source checkpoint:** 102 SDK patches / 89 Windows checks in Debug and
Release. The native System menu now has Graphics Settings, Off/FXAA and a
restart-selected experimental DirectX 12 path. Higher frame limits now change
the guest timing gate; short saved-scene probes exceed 30 completed frames/s
without accelerating the sampled game clock. Rendering fidelity and lag remain open.
Normal shadow polarity and the starting-town Logic Stone save/reload work.
Black foot flicker, rotating Logic Stone light flicker and broader game-speed
fidelity remain open. Windows analogue mapping has a further diagonal-range and report-race fix;
physical walk/run consistency is still unverified. Future restarts use
Continue and the verified save. Earlier measurements below retain their original
logging, profiler and visibility conditions; they are not interchangeable.

## Contents

**Status and scope**

- [Goal and reporting](#goal-and-reporting)
- [CPU execution qualification](#cpu-execution-qualification)
- [Verified](#verified)
- [Still unverified](#still-unverified)
- [Implemented fixes](#implemented-fixes)

**Performance, timing and presentation**

- [NPC visibility and continuing geometry work](#npc-visibility-and-continuing-geometry-work)
- [Vulkan batch resource lifetimes](#vulkan-batch-resource-lifetimes-patch-0102)
- [Grouped vertex conversion](#grouped-vertex-conversion-patch-0101)
- [Shader translation stalls and cache](#shader-translation-stalls-and-cache-patch-0100)
- [Native texture write-tracking experiment](#native-texture-write-tracking-experiment)
- [Guest memory and write-tracking follow-up](#guest-memory-and-write-tracking-follow-up)
- [SPURS polling stalls and register-local candidates](#spurs-polling-stalls-and-register-local-candidates)
- [GPU command spans and the town wait chain](#gpu-command-spans-and-the-town-wait-chain)
- [Saved-town movement baseline](#saved-town-movement-baseline)
- [Frame pacing and game speed](#frame-pacing-and-game-speed)
- [Drakengard 3 performance and settings comparison](#drakengard-3-performance-and-settings-comparison)
- [Native graphics settings and frame-rate control](#native-graphics-settings-and-frame-rate-control)
- [Full Graphics Settings panel and window close](#full-graphics-settings-panel-and-window-close)
- [Direct Vulkan window presentation (patch 0080)](#direct-vulkan-window-presentation-patch-0080)
- [Frame timeline checkpoint (patch 0077)](#frame-timeline-checkpoint-patch-0077)
- [Vulkan batch scheduling (patch 0076)](#vulkan-batch-scheduling-patch-0076)
- [Window-title presentation timing](#window-title-presentation-timing)
- [Town performance after the physics fix](#town-performance-after-the-physics-fix)
- [Cutscene red flash and frame rate](#cutscene-red-flash-and-frame-rate)

**Rendering and RSX**

- [Vulkan window-depth intervals (patch 0098)](#vulkan-window-depth-intervals-patch-0098)
- [Request-audit follow-up (patch 0097)](#request-audit-follow-up-patch-0097)
- [Active RSX request coverage and fragment inputs (patches 0095-0096)](#active-rsx-request-coverage-and-fragment-inputs-patches-0095-0096)
- [Vulkan polygon offset (patch 0094)](#vulkan-polygon-offset-patch-0094)
- [RSX command-reference cross-check](#rsx-command-reference-cross-check)
- [Texture detail and rendering state audit (patch 0079)](#texture-detail-and-rendering-state-audit-patch-0079)
- [Secondary-bone physics and town stretching](#secondary-bone-physics-and-town-stretching)
- [NV0039 memory transfers](#nv0039-memory-transfers)
- [Vertex-program co-issue review](#vertex-program-co-issue-review)
- [Shared renderer and colour feedback](#shared-renderer-and-colour-feedback)
- [Avatar normals and fragment shading](#avatar-normals-and-fragment-shading)
- [Guest flip presentation through scene changes](#guest-flip-presentation-through-scene-changes)
- [Ordered surface clears](#ordered-surface-clears)
- [Scene render targets and descriptor ranges](#scene-render-targets-and-descriptor-ranges)
- [Scene draw-record capacity](#scene-draw-record-capacity)

**SPU, SPURS and CPU execution**

- [SPURS completion coherence and SPU byte operations (patches 0091-0093)](#spurs-completion-coherence-and-spu-byte-operations-patches-0091-0093)
- [Exact SPU vector math (patch 0078)](#exact-spu-vector-math-patch-0078)
- [Reproducing the SPU inputs from the executable](#reproducing-the-spu-inputs-from-the-executable)
- [WKC1 town and SPU completion-label polling](#wkc1-town-and-spu-completion-label-polling)
- [SPU fallback census and host-runtime review](#spu-fallback-census-and-host-runtime-review)
- [Native census and limited town controls](#native-census-and-limited-town-controls)

**Runtime services, input and saving**

- [Platform queries and missing-syscall reporting (patch 0099)](#platform-queries-and-missing-syscall-reporting-patch-0099)
- [Runtime correctness and input follow-up (patches 0085-0090)](#runtime-correctness-and-input-follow-up-patches-0085-0090)
- [Windows stick magnitude and walk/run response (patch 0084)](#windows-stick-magnitude-and-walkrun-response-patch-0084)
- [Proximity interaction and saving (patch 0075)](#proximity-interaction-and-saving-patch-0075)
- [Character-name input](#character-name-input)
- [Reviewed allocator suggestion and probe input](#reviewed-allocator-suggestion-and-probe-input)

**Startup and early checkpoints**

- [Startup investigation](#startup-investigation)
- [Startup and early WKC1 checkpoints](#startup-and-early-wkc1-checkpoints)

**Reviews and comparisons**

- [Killzone 3 SDK comparison](#killzone-3-sdk-comparison)
- [Second runtime review](#second-runtime-review)

**Content and history**

- [Update 1.01 and DLC integration](#update-101-and-dlc-integration)
- [Investigation history](#investigation-history)

## Goal and reporting

Develop native Windows C/C++ execution through the title screen, gameplay and
verified save/load, with an architecture suitable for later Apple Silicon work.
Completion remains unproven until those behaviors have been tested.
Start gameplay validation with White Knight Chronicles I, included on the disc.
Provide an in-game keyboard operated entirely by gamepad for both Windows and
macOS. The native Windows text-entry window is a temporary development interface.

Maintain this log as research progresses. Publish checked code, concise findings
and new verified screenshots together when a meaningful milestone is reached.
Keep full traces, generated game code, assets and local paths in the private workspace.

## CPU execution qualification

The current build uses ahead-of-time PPU/SPU recompilation plus a PS3 runtime
layer. An audit of the running build found actual SPU interpreter fallback use
from the runtime-code microstep path in image 17. It is therefore a hybrid;
neither complete static CPU coverage nor an interpreter-free port is verified.
The build compiles 56,580 generated PPU functions and 24 known SPU image/job
inputs. The latest retained fallback census contains six image-17 entry
fingerprints; its share of execution has not been measured. See
[architecture and modding scope](ARCHITECTURE.md) for the distinction from a
source-level decompilation and the remaining work. Rendering milestones below
do not establish interpreter-free execution.

## Verified

| Result | Evidence | Limit |
| --- | --- | --- |
| Native Windows build succeeds | 76-patch Clang build and 64 runtime regression checks in Debug and Release | Compilation alone does not verify gameplay |
| Logic Stone proximity targeting and local saving work | Patch 0075: X opens the stone menu, Save writes a slot, restart/Continue returns to town; patch 0076 also loads that slot | One starting-town slot; other scenes/slots and PS3/RPCS3 interchange unverified; occlusion samples remain a conservative fallback |
| Normal shadow polarity is restored | Patch 0074 GPU comparison and user-confirmed native town view | Black foot flicker and complete shadow fidelity remain open |
| Saved-town presentation throughput approaches the 30 fps target | Separate clean one-minute samples: patch 0075 27.18 presentations/s; patch 0076 28.03; [conditions](../verification/performance.json) | Stationary view, not locked 30 fps, distinct animation updates or RPCS3-equivalent game speed |
| Vulkan renders the WKC1 avatar and room without its initial red tint | Native GPU readback in the [gallery](GALLERY.md); production-engine feedback regression | This colour-fix capture covers character creation; later town validation is recorded below; full visual fidelity remains unverified |
| WKC1 reaches avatar Review with the entered name retained | Readable UI, room, full default male avatar, face shading and textured clothing; [capture](GALLERY.md) | Avatar acceptance and entry into the town are now observed; full visual fidelity remains unverified |
| WKC1 enters the live town after the opening cutscenes | Native Vulkan [readback](GALLERY.md) shows Leonard, NPCs, minimap and save crystal; pause/resume responds in the same run | The long strips are removed with patch 0058 ([secondary-bone physics](#secondary-bone-physics-and-town-stretching)); NPC surfaces improve with patch 0052 ([NV0039 transfers](#nv0039-memory-transfers)); full material fidelity unverified; short movement and camera response verified; starting-town save/reload verified below; sustained traversal, combat and wider save compatibility unverified |
| Initial town movement and camera response work | First observed with 42 patches; rechecked with 58 patches after the physics fix, including walking/facing changes and camera rotation | Short starting-town probes only; sustained traversal, combat and analogue walk/run consistency unverified; local save/reload verified separately |
| Language-selection screen renders | GPU readback in the [gallery](GALLERY.md) | Startup UI only |
| Language selection accepts input | CROSS through the controller path advances startup | This check covers startup input; limited town controls are verified above |
| Warning and autosave notices render | Separate GPU readbacks in the gallery | The autosave notice does not verify saving |
| Lifted SPU decoder produces correct output for sampled assets | Eight outputs match independent raw-DEFLATE decoding byte for byte; [record](../decompression-verification.json) | Sampled blocks, not all assets |
| Native guest installer writes the large data archives | Three installed archives, totaling 4,364,470,879 bytes, hash-identical to their disc data; [record](../verification/installation.json) | Installation progress, not gameplay or save/load |
| First movie header parses through the guest ABI | Real header reports two streams and 1280x720 video; native execution advances to demux/decoder startup | Header parsing scope; later playback is recorded below |
| MPEG-2 access-unit extraction matches independent decoding | All 210 logo frames and 3,596 opening-movie frames match FFmpeg's direct demux/decode pixel for pixel; [record](../verification/movie-demux.json) | Offline validation, not native playback |
| Extracted opening-movie AC-3 decodes | 3,750 audio frames / 5,760,000 samples per channel at 48 kHz | Elementary-stream decode; native API comparison is recorded below |
| Native demux bridge delivers actual access units | Guest video decoder receives the first 19,356-byte unit; audio decoder receives a 960-byte LPCM unit | Decoder delivery evidence; later native playback is recorded below |
| Real video decoding matches reference pixels through the guest API | All 210 logo frames match on two sequence passes, including B-frame reordering; [record](../verification/movie-demux.json) | Standalone decoder validation, not continuous in-game playback |
| Decoded first-logo image renders natively | D3D12 GPU readback in the [gallery](GALLERY.md) | First-image milestone; later playback is recorded below |
| Two LPCM startup movies advance natively | Successive readbacks, decoder lifecycle and later Havok/Dolby capture in one 120-second probe with no input after movie start | Startup-clip evidence; title rendering is recorded below |
| Native LPCM sample conversion matches FFmpeg | All 336,480 stereo sample frames from the first logo match; [record](../verification/movie-demux.json) | Assembled elementary input; independent audio demux and final mix fidelity unverified |
| Native title menu renders over an animated background | GPU readback shows New Game, Continue and Options; idle execution proceeds to the attract movie | Menu rendering checkpoint; menu input and limited town controls were verified later |
| AC-3 samples match the reference through the guest API | Opening: 3,750 frames in stereo and 5.1; title: 938 frames in 5.1; two sequence passes each | Assembled elementary input; original-console mix fidelity unverified |
| Guest flips continued during an early black-screen investigation | A 120-second probe observed 6,171 flips and 6,171 presentation calls | Historical diagnostic; counters alone did not establish visible rendering; later screenshots verify startup and town output |

## Still unverified

Open audio issue: audible distortion in character-creation music. Device buffer
starvation, guest mixer scheduling and source decoding have not yet been
distinguished. Decoder comparisons do not establish real-time mixer fidelity.

The long-strip fix and NPC surface improvement establish specific rendering
corrections. Full material, lighting and physics fidelity still need wider
validation. The remaining work includes:

- Sustained traversal, combat, later scenes and broader save compatibility.
  Starting-town local save/reload is verified on patch 0075; patch 0076 also loads that slot.
- Sustained visible performance: the clean patch-0076 one-minute saved-town
  sample averages 28.03 presentations/s versus 27.18 for patch 0075. This does
  not establish locked 30 fps, smooth traversal or full game-speed fidelity.
  The earlier ten-minute worker-stability test used patch 0073 and different
  diagnostic conditions. Validated 60 fps support remains a later goal.
- Flickering foot shadows, the rotating Logic Stone light, and complete in-engine
  audio/animation synchronization.
- The custom avatar's menu portrait is reported missing in the native build and
  the user's RPCS3 comparison. The supplied [menu reference](https://lparchive.org/White-Knight-Chronicles/Update%2002/)
  is retained for comparison; portrait generation, storage and sampling have not
  been traced, and the matching symptoms do not establish a shared cause.
- Physical-controller walk/run comparison after patches 0087-0088, and loading/cutscene speed compared with RPCS3.
- All character-creation options and an in-game keyboard usable by gamepad.
- Complete static SPU coverage, including the six observed image-17 fallback
  fingerprints; source-level reconstruction and a supported modding framework.
- Linux and macOS Apple Silicon platform integration and native testing.
- Update 1.01, regional DLC, translated Japanese-exclusive content and optional
  Japanese voiceovers, including content and save compatibility.
- Online functionality.

Fresh extraction and static generation supply all 24 known SPU inputs and the
PPU code used by the current town build. This does not establish complete job
coverage across untested gameplay.

See the [build limitations](BUILD.md) and [capture manifest](../verification/gallery.json)
for reproduction requirements and screenshot provenance.

## Implemented fixes

The investigation sections record successive checkpoints, including failures
later resolved. Test counts, proposed next steps and limitations within those
accounts describe that checkpoint. They are not the current task list. The
verified table above and the opening source checkpoint record the current status;
[Still unverified](#still-unverified) records the remaining validation work.

- Correct lightweight-condition layout and wait/signal behavior.
- Allocate and account for memory-container allocations instead of acknowledging
  allocation without providing memory.
- Support large archive offsets and authenticated, atomic SDAT caches.
- Lift observed heap-backed SPU jobs and avoid duplicate synthetic completions.
- Preserve SPU-produced FIFO segments while their producer fills them.
- Bind all 16 fragment texture units, preserve inline constants, and use the
  current shader-decompiler interfaces.
- Publish event queues only after host synchronization objects are initialized.
- Wait for a pending background content check before entering the original
  installation request, preventing that request from being dropped.
- Implement the nine imported PAMF reader APIs with guest-endian fields,
  64-bit file sizes, actual stream descriptors and copyable guest reader state.
- Parse MPEG program-stream packets incrementally and assemble MPEG-2, LPCM and
  AC-3 access units across packet and input-buffer boundaries.
- Supply demux work-memory queries, guest-resident AU records and payloads,
  guest callback messages, queue backpressure, flush and release lifetimes.
- Give callback threads separate guest stack slots, with separate slices for
  nested callbacks and slot reuse when host threads exit.
- Implement imported Vpost open/query/close/exec interfaces, with the actual
  argument/field layout and progressive YUV420 crop, bilinear resize and RGBA output.
- Enable real FFmpeg MPEG-2 decoding using pinned public headers and a separately
  supplied library. The optional decoder check reports a skip if no library loads.
- Render the avatar under normal depth/culling, preserve frame presentation
  through scene changes, and restore Vulkan colour-target feedback.
- Implement the observed NV0039 transfers and isolate their methods from 3D
  state; inspected town NPC surfaces improve with patch 0052.
- Preserve depth-only targets and reload GPU surfaces after video conversion
  writes; patch 0053 removes the observed cutscene red flashes.
- Reduce SPURS dispatch/setup costs and avoid Vulkan mid-frame fence waits
  with patches 0054-0057; town performance remains below the immediate 30 fps
  target and the later 60 fps goal.
- Execute the statically compiled secondary-bone physics job through its
  SPURS queue/port imports; patch 0058 removes the long town strips in the
  inspected views.

Patches 0059-0061 cache launch diagnostics, avoid repeated SPU conversion-scale
CRT calls and keep each logical SPU context's DMA engine independent. The
[town measurements](#town-performance-after-the-physics-fix) record their scope.

Regression checks cover real video decoding, sparse video timestamps, LPCM sample
conversion/concurrent queues, stereo/surround AC-3, Vpost conversion/windows, callback
stacks, demux ABI/lifetimes, packet/access-unit parsing, PAMF reader ABI,
lightweight conditions, memory containers, LFQueue, event-queue recycling,
SPURS job chains/queues, SPU arithmetic, 2D transfers and shared rendering.
PAMF checks use synthetic video/audio descriptors,
including declared data larger than 4 GiB, copied readers and malformed headers.
An optional private-header check also passes against the first logo file.
Their scope is narrower than full-game compatibility.

## NPC visibility and continuing geometry work

**Problem.** The user noticed NPCs fading out when outside the view and asked
whether visibility changes explain the lag. Earlier movement probes mixed this
with texture loading, camera changes and different character positions.

**Cause.** Not established. The measurements support an ongoing geometry/animation
cost after streaming, but do not isolate NPCs from buildings, shadows or camera
collision. Fading-specific work has not been identified.

**Investigation.** A private one-object backend census counts device allocations,
texture uploads, draws, submitted vertices and constant-upload bytes. After
Continue, six stationary views change only the right stick. Each inspected GPU
frame is captured before a separate 12-second timing window; an eight-second host
CPU sample follows. Tests remain hidden and verified muted.

**Evidence.** At one saved position, the initial stone-facing view measures
30.00 presentations/s and the first NPC-facing view 26.67, with no new texture
allocations or image uploads in either. Geometry-worker CPU rises from 62.29%
to 75.38% of one core even though submitted vertices decrease. Returning near
the original angle restores 29.91; a second NPC-facing angle measures 28.92.
Other nearby angles measure 25.83 and 23.25, so this is not a clean NPC-only test.

A separate route records 20 new textures going forward, 168 coming back and ten
during camera movement. The final slow idle window has zero allocations and zero
image uploads. Vertex, index and constant uploads still occur; the stationary
views submit roughly 12–17 MB of constants per present callback.

The existing private local-register geometry-kernel candidate is rechecked on
the current runtime: 226,404 sampled complete-context/local-store/continuation
comparisons have zero mismatches. Its 1,536 synthetic cases also pass. Six
same-process original/local timing phases average 26.77/28.02 presentations/s,
but one local phase is worse and carries more geometry. This remains an
experiment, with no consistent end-to-end FPS gain claimed or runtime promotion.

**Limits.** Camera angles are approximate and NPCs continue moving. CPU samples
are separate from timing windows; neither their percentages nor the live
comparison measure identical game work. Completed presentations do not establish
correct animation speed. Next isolate per-kernel cost on identical inputs and
reduce repeated per-draw work without removing NPCs or changing their visibility
rules. Reviewed aggregates are in [the performance record](../verification/performance.json).

## Vulkan batch resource lifetimes (patch 0102)

**Problem.** Reviewing allocation reuse found two ownership gaps. Texture release
used a closed recording batch as evidence of GPU idleness, although the spare
slot could still be pending. Window presentation submitted the frame before
acquisition; a skipped acquisition bypassed the presentation fence but still
retired the frame's resources.

**Cause.** Recording state, submitted work and completed work are different
states. Releasing textures or resetting upload/descriptor arenas requires actual
completion of their last GPU use.

**Fix.** Texture release drains a pending spare slot before immediate destruction.
Skipped acquisition waits for submitted GPU work before retirement. Failed waits
preserve ownership and inhibit further batch reuse/recording. A failed full-list
flush retains the incoming texture handle. This prepares the lifetime rules for
further pooling; it does not yet add a new allocation pool.

**Evidence.** On 19 October, production retirement helpers pass recording-API
tests for pending slots, delayed/failed completion and full-list failure. The
presentation regression checks skipped/error acquisition and failed idle wait.
Both removed-wait negative controls fail. All 89 Windows checks pass in Debug
and Release. The regular Release binary loads Continue and completes the short
town route, hidden and verified muted. A separate native census records 833
device-memory allocations, 233 frees, 708 texture-create calls and 2,550 uploads
(626,451,828 uploaded bytes) across startup, loading, movement and extra idle.
These are cumulative totals, not peak resident memory or per-frame costs.

**Limits.** The native census sees zero closed releases with a pending spare and
zero failed fence waits. The corrected ownership cases are reproduced by the
recording API; their occurrence in the native route is not established. There is
no FPS, flicker or full device-loss recovery claim. The return route still
stutters. Further pooling, per-phase allocation measurements and CPU/guest-memory
work remain open. See [reviewed aggregates](../verification/performance.json).

## Grouped vertex conversion (patch 0101)

**Problem.** Vertex preparation is still a CPU cost after removing repeated
shader compilation. Per-vertex fetching repeats format decisions shared by the
whole draw. A separate decoded-vertex cache also reduces conversion work, but
its key construction, page validation and copying have not shown a consistent
FPS win; an unexplained startup failure in the combined private tracking/cache
candidate also needs investigation before promotion.

**Cause.** The compact layout, attribute descriptors and formats are fixed for
one draw. Selecting each format outside the vertex/component loops avoids
repeated dispatch without relying on cross-draw invalidation.

**Fix.** The draw engine uses a grouped batch fetch, preserving existing numeric
conversions, instance-frequency rules, fallback defaults and invalid-position
failure. It retains no source/output data after the draw and introduces no GPU
resource or write-tracking dependency.

**Evidence.** On 19 October, 69,140 synthetic cases agree byte for byte on
successful output and agree on failure status. This includes all 65,536 half
representations; a wrong-divider negative control fails. All 88 Windows checks
pass in Debug and Release. A private wrapper around the production C function
compares 4,846,180 native draws with zero mismatches while loading Continue,
moving briefly in town and idling afterward. The regular Release build then
reloads the same save and completes the short route. Testing is hidden and
verified muted. The synthetic 500,000-batch fixture takes 72.3 ms with individual
fetches and 44.6 ms with grouped fetches; both checksums match.

**Limits.** This measures conversion efficiency, not a demonstrated game FPS
gain or smoother animation. Earlier private alternating samples vary with NPC
motion, and the final sample may overlap build work, so they are not used as a
speed claim. The native differential wrapper recomputes both paths and is not
the production performance build. No new screenshot is needed for an unchanged
frame. Mac/Linux testing, consistently smooth traversal, game-clock tracing,
write-tracking lifecycle review and the remaining allocation costs stay open.
Reviewed aggregates are in [the performance record](../verification/performance.json).

## Shader translation stalls and cache (patch 0100)

**Problem.** Returning toward the busier part of the starting town produces
long frame intervals despite relatively short measured GPU command spans.

**Cause.** Sparse instrumentation identifies the long image-19 loop as an
output-ring allocator polling GCM label 216, not useful geometry math. During a
377 ms job, simultaneous host samples place the renderer in SPIR-V optimization.
The FIFO reaches consumer-label publication only after preceding host rendering
work. Separately, translation counters prove that most shader compilations on
the return leg repeat text already compiled in that process.

**Fix.** Patch 0100 adds bounded, exact-source/stage SPIR-V reuse. Label ordering,
SPU polling semantics and the generated game instructions are unchanged. Cache
identity and failure handling are covered by the [patch regressions](PATCHES.md#spir-v-translation-cache-patch-0100).

**Evidence.** Four hidden, muted runs use one private executable, the same save
and wall-clock controller sequence, in off/on/on/off order. Each return-leg
measurement lasts about ten seconds. Output is 720p, the internal scene remains
1920×1080, the frame limit is 30, VSync is off, and no CPU sampler or GPU readback
runs during these timing samples.

| Cache | Presentations/s | 95th-percentile interval | Longest interval | Compilations / requests | Compiler/admission time |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off, first | 16.90 | 147.99 ms | 758.11 ms | 188 / 188 | 2,954.83 ms |
| On, first | 21.90 | 67.62 ms | 103.98 ms | 8 / 196 | 148.19 ms |
| On, repeat | 21.30 | 67.16 ms | 105.05 ms | 8 / 208 | 149.62 ms |
| Off, repeat | 15.70 | 135.67 ms | 698.49 ms | 194 / 194 | 3,005.61 ms |

The enabled runs reuse 188 and 200 translations respectively on this leg.
The production patch also passes all 87 Windows checks in Debug and Release.
A separate regular Release reload, without the private fast-memory or register-local
experiments, measures 30.00 presentations/s at rest, 29.50 on the forward leg and
21.30 returning, with a 100.43 ms longest return interval. Subsequent stationary
CPU samples find vertex decoding/fetch, texture hashing, FIFO/driver work, guest
memory access and SPU job work; shader optimization is absent from the renderer's
reported top 25 leaves. This supports continuing the memory/vertex/texture work.
Raw game shaders and captures remain private; reviewed aggregates are in the
[performance record](../verification/performance.json).

**Limits.** These are instrumented private candidates with the ordinary-read
experiment enabled, not measurements of the earlier desktop binary. They compare
the same input durations, not identical world positions: lag can change movement
progress. Presentation counts do not establish unique animation updates, visible
scanout or console-equivalent game speed. A roughly 100 ms hitch still misses
three 30 Hz frame budgets. Consistently smooth traversal remains open, as do
first-use compilation and the remaining CPU/graphics work.

## Native texture write-tracking experiment

**Problem.** Re-hashing unchanged texture bytes remains CPU work after the
shader-cache fix. A tracker must see native stores as well as guest accessors,
and prevent host I/O or memory commits from silently bypassing protection.

**Private implementation.** A separate 100-patch SDK snapshot connects the tracker
to demand paging, reserved-only commits, relevant protection changes, synchronous
file reads, socket receive buffers and the draw engine's texture hashing.
Individual page-generation tokens validate each texture range. Kernel-write
leases keep destinations writable until calls return, including nested leases.
The default verifier still performs every original hash and compares proposed
reuses byte-for-byte with retained source bytes. A test deliberately resets page
protection outside the wrapper and changes a byte; the verifier detects it and
forces verification mode. Replay use before tracker initialization remains on
the original I/O path.

**Evidence.** Two hidden, muted native runs reload Continue and complete the short
town route. Their endpoints record 775,284 and 619,642 proposed reuses checked,
with zero byte mismatches and zero failed I/O leases. Verification also continues
while stationary after the route; these counts are not movement-only samples.
Across both endpoints, about 87.95 GB of source bytes are compared. No hashes are
skipped in these two runs.

A later same-process comparison switches actual hash skipping and original
hashing in fast/original/original/fast/original/fast order. Three twelve-second
fast phases perform 236,565 skips, hashing 22.81 MB, versus 15.18 GB hashed in
three original phases. Mean presentation rates are 27.63/s and 27.44/s; mean
process CPU time per presentation is 163.31 ms and 165.81 ms. Scene variation is
large enough that a consistent FPS improvement is not established. The
[reviewed record](../verification/performance.json) retains every phase.

**Next work.** Finish kernel-output and shutdown/lifetime coverage, test more
scenes and verify real resource reuse before promotion. Vertex decoding/fetch
and the remaining guest-memory/SPU costs are separate remaining work.

**Limits.** This is private experimental code, not part of the desktop build.
Fast phases do not do the verifier's byte comparison; correctness evidence comes
from the separate verification runs and synthetic failure tests. The prototype
has no verified macOS backend or vertex-cache integration. Presentation counts
do not establish correct animation speed, visible smoothness or locked 30 FPS.

## Guest memory and write-tracking follow-up

**Problem.** With repeated shader compilation removed, guest-memory access,
texture hashing and vertex decoding remain visible CPU costs.

**Experiment.** A private candidate keeps patch 0100's shader cache enabled and
switches only ordinary guest reads between the original and guarded paths inside
one process. Special pages and diagnostics retain their slow paths; writes and
SPU reservation handling are unchanged. Six twelve-second stationary samples use
fast/original/original/fast/original/fast order after the Continue movement route.

**Evidence.** Mean presentation throughput is 25.22/s with fast reads and 25.80/s
with original reads; mean process CPU time per presentation is 165.08 ms and
168.70 ms respectively. The phases vary enough that this does not establish a
frame-rate improvement. The private read path is not in the desktop build.
The [aggregate record](../verification/performance.json) retains all six phases.

A separate private Windows write-watch prototype passes synthetic checks for
native stores, concurrent writers, atomic stores, overlapping ranges, unchanged
page protection after reserved-only commits and nested kernel-write leases.
A real `ReadFile` writes into the leased guest destination, and a lease held
longer than the normal hot-page quarantine prevents reprotection. Removing the
write-generation update makes the regression fail. Page generations are compared
individually rather than trusting a combined hash of their counters.

**Next work.** Connect tracking to every commit/protection and relevant I/O path,
then compare actual resource hashes against every proposed cache reuse in native
runs before enabling hash skipping. In particular, a kernel write must keep its
pages unprotected until completion; a timeout alone cannot establish completion.

**Initial limits.** At that checkpoint this was a tested tracker core, not an integrated texture/vertex cache,
a measured tracking speedup or verified macOS support. Live NPC movement changes
stationary scene cost. Presentation counts and untraced game-clock accumulators
do not prove unique animation updates or correct game speed.

## SPURS polling stalls and register-local candidates

**Problem.** The main-thread condition trace identifies an SPURS dependency, but
does not explain why completion arrives late during town movement. Optimizing
the busiest arithmetic routine alone need not address the largest stalls.

**Cause.** Further private traces on 2026-10-19 distinguish the work from its
completion notification. Short image-21 jobs commit the flags consumed by the
waiting PPU thread. In one back-phase sample, an image-19 job runs for 364.33 ms,
followed by the observed completion of a 366.77 ms event-flag wait. The matched
commit-to-return interval is only 0.0058 ms. Another job/wait pair takes
279.23/281.99 ms, with 0.0056 ms from commit observation to return.

The long jobs also accumulate substantial per-thread CPU cycles. These counts
include kernel and user work and are not converted to elapsed time; see
[QueryThreadCycleTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).
They do not by themselves identify useful arithmetic rather than active polling.
A separate probe associates actual instruction-pointer samples with individual
job intervals using their common QPC clock. Its longest completed image-19 job
takes 377.22 ms and has 150 matching samples: 55 in `spu_rdch`, 57 in the system
library, and 25 in the lifted `0x2D24`/`0x2C58` routines. The generated `0x2D24`
loop reads `SPU_RdDec` 25 times; its caller submits DMA and tests progress before
retrying. This directs the next investigation toward that polling loop's producer
and runtime cost. It does not yet prove a decrementer-rate, DMA or wakeup error.

**Fix.** No runtime performance change is promoted. Two isolated optimization
prototypes now have correctness evidence:

- A PPU post-pass uses the reviewed
  [Drakengard SDK register-local implementation](https://github.com/soyjxck/ps3recomp/blob/de08397c0a485cd7705c049ca7532dd90fadc704/tools/ppu_nv_dirty.py)
  on the existing WKC lift, retaining its instruction fixes. The first candidate
  rewrites one 610-function chunk, writes back registers that may have changed,
  and reloads all selected registers after calls, including nonvolatile GPRs.
  Whole-program liveness is not enabled. A grouped-read variant identifies 607
  access groups containing 4,488 static accesses. Only read-only eligible groups
  can use the direct path; stores retain the existing coherent runtime path.
  A generation check invalidates cached eligibility when the private read gate
  changes. Verification checks computed group addresses before direct loads.
- An SPU leaf-kernel prototype keeps register values in locals. Unrestricted
  float reordering changes NaN payloads in differential tests, so that version
  is rejected. A guarded version restores any tentative local-store writes and
  runs the unchanged reference when a floating operation encounters or produces
  a nonfinite value. Diagnostics, alternate float modes and unsuitable contexts
  also retain the reference path. One slower kernel variant is discarded.

**Evidence.** Three synthetic PPU variants—plain, locals and verifying—produce
identical observed outputs across 300 randomized runs through the pinned WKC
lifter. The grouped version passes another 300-run comparison. Focused group
checks cover unaligned/endian reads, special and wrapping spans, unbacked pages,
address verification, generation invalidation and runtime store dispatch.
Removing generation validation makes its negative control fail. The native
610-function candidates load the verified save through Continue and complete
short forward/back movement with zero retained register-verifier errors; the
grouped variant also reports zero group-address errors.

The guarded SPU kernel passes 1,024 arbitrary-state and 512 finite-source-state
comparisons of the complete context, local store and next branch. Most of the
latter inputs still produce nonfinite intermediates and exercise the reference
fallback. A separate native diagnostic run completes 148,024 sampled full-state
comparisons with zero mismatches. A deliberately changed register is detected by
the verifier's negative check. Fixed-pose on/off timing passes remain too variable
to establish a consistent gameplay improvement.

**Limits.** These prototypes remain private and are not part of the published
99-patch/84-check runtime. Static access/write-back counts are not executed traffic
measurements. Native verifier results cover only exercised paths; the full suite
has not been rerun against these candidates. Sampling perturbs job timing, and
the job/sample windows differ. Timed routes still lack spatial waypoint capture.
The standalone clock experiment does not establish a reason to replace the
existing Windows clock, and no decrementer reads are removed from the playable.
Fast stores, whole-program PPU liveness, verified texture/vertex write tracking,
further graphics pooling, internal scene scaling/MSAA, sustained smooth 30 FPS
and higher-rate/game-speed validation remain pending. The desktop playable,
settings and verified save are unchanged; tests use hidden, verified-muted
sessions. No new visible milestone or gallery image is claimed.

## GPU command spans and the town wait chain

**Problem.** The saved-town movement route still stalls despite near-30
presentation throughput in its initial stationary view. GPU fence waits alone
could not distinguish graphics execution from CPU scheduling and submission gaps.

**Cause.** Measurements on 2026-10-19 narrow the investigation but do not establish
one cause for every stall. An isolated native Vulkan candidate records TOP/BOTTOM
GPU timestamps for each command buffer. Query pools follow the actual command
buffer handles through slot swaps; completed results are read without an added
wait. The observer unions overlapping spans rather than adding them twice.
Timestamp conversion follows the device's period and queue's valid-bit count;
see the [Vulkan query specification](https://docs.vulkan.org/spec/latest/chapters/queries.html).

The instrumented route uses Continue from the same save, 720p output, original
30 FPS limit, VSync/AF/AA off and the same timed stick holds. The test window is
hidden and its Windows audio session is muted and verified. GPU spans and host
staging-copy/allocation counters are collected separately from stack sampling.

| Phase | Presentations/s | GPU span union per presentation | Device allocations | Allocation CPU time | Engine staging-copy CPU time |
| --- | ---: | ---: | ---: | ---: | ---: |
| Initial neutral | 29.84 | 8.76 ms | 0 | 0.00 ms | 192.96 ms |
| Forward | 27.93 | 9.17 ms | 28 | 3.56 ms | 184.77 ms |
| Back | 15.27 | 13.60 ms | 137 | 12.36 ms | 142.01 ms |
| Camera turn | 24.15 | 11.71 ms | 4 | 0.54 ms | 206.78 ms |
| Final neutral | 25.94 | 11.21 ms | 0 | 0.00 ms | 210.37 ms |

CPU times in the last two columns cover each approximately ten-second phase,
not one frame. The back phase's longest individual GPU command span is 20.40 ms,
while its independently observed longest presentation gap is 779.82 ms. There
is a 547.96 ms gap between recorded GPU command spans in that phase. There
are no missing timestamp records, unavailable results or query errors in this
route. These observations favor investigating CPU work, job dependencies and
submission/pacing gaps before expecting allocation pooling alone to cure this
route. They do not exclude GPU improvements in other workloads.

A separate stack sample resumes each thread before unwinding its captured stack
copy. It identifies image-19 geometry work, renderer vertex work and a polling
guest thread in `sys_timer_usleep`. Main-thread waits lead through the native
lightweight-condition implementation. An additional condition-event trace then
identifies the next link: guest thread 13 signals the main thread, and its sampled
stack is predominantly in `cellSpursEventFlagWait` through `func_00AB3E48`.
The main thread reaches its wait through `func_00A7EC4C` and `func_00A74AFC`.

In that separate instrumented movement run, 156 completed main-thread waits in
the back phase have median 31.06 ms, 95th percentile 115.09 ms and maximum
365.37 ms. All have an explicit matching signal. Signal-to-wake observation is
at most 0.067 ms, measured **before guest mutex reacquisition**. This establishes
the dependency and prompt condition-variable wake in the sampled cases; it does
not establish the mutex-reacquisition cost or why the SPURS producer completes
late. A blanket change to condition timeouts is not justified by this trace.

**Fix.** No gameplay performance fix is promoted at this checkpoint. Ordinary
read candidates remain isolated: outlining their diagnostic bodies shrinks the
compiled `vm_read32` entry from 3,168 to 64 bytes, but fixed-pose on/off passes
still show no consistent end-to-end gain. Stores and SPU reservations retain
their original implementation. The desktop playable and preferences are unchanged.

**Evidence.** The [performance record](../verification/performance.json) retains
numeric results and private-record hashes. Focused checks exercise actual loader
read bodies, bounds, MMIO, reference polling, diagnostic controls and caller
attribution; three deliberately broken read variants are rejected. A mocked
Vulkan check covers query availability, command-buffer ownership, valid-bit
masking, disabled/unsupported paths, counters and cleanup; removing the
availability guard fails its negative control. Existing lightweight-condition
regression checks also pass with the private trace disabled and enabled. These
are exploratory checks, not additions to the published 84-check suite.

**Limits.** GPU command spans include dependencies and are not GPU hardware
busy-cycle counters or calibrated CPU/GPU absolute timestamps. Result retirement
and independent observers have slightly different phase boundaries. Staging
counters cover engine arena and texture-row copies, not all decode/upload work.
Instrumented routes are diagnostic evidence, not clean speed comparisons. Fixed
stick durations do not guarantee identical final views when game speed varies.
Distinct animation speed, the sampled guest clocks' exact roles, sustained
30 FPS, higher-rate correctness and the full set of requested optimizations
remain unverified. No new screenshot is published because this is a measurement
checkpoint without a verified visible rendering change.

## Saved-town movement baseline

Measured 2026-10-19 on the unchanged 99-patch optimized native executable,
loading the verified starting-town save through Continue. The copied executable
matches the published playable checkpoint's SHA-256. Its separate preferences
select Vulkan, 720p output, the original 30 FPS limit, VSync off, AF off and AA off.
The test uses a hidden window. Its original launcher requested audio mute through
an unsupported environment variable, so that run's audio mute was not verified;
later runs above use verified Windows session muting. It does not alter the desktop playable or its
preferences. Diagnostic logs, automatic snapshots, the sampling profiler and the
frame timeline are disabled during this timing pass.

After warm-up, the route holds the left stick forward for eight seconds, then
back for eight seconds, then the right stick horizontally for eight seconds.
Each phase has a ten-second observation window; neutral phases bookend the route.
The pad-file path records actual hold start/end times. This exercises injected
input, not physical-controller walk/run calibration. Camera and character
positions are not yet recorded as reproducible spatial waypoints.

| Phase | Presentations/s | Median interval | 95th percentile | Longest interval |
| --- | ---: | ---: | ---: | ---: |
| Initial neutral | 29.49 | 32.65 ms | 35.61 ms | 52.71 ms |
| Forward | 27.90 | 32.68 ms | 50.13 ms | 202.93 ms |
| Back | 13.70 | 51.21 ms | 163.04 ms | 783.04 ms |
| Camera turn | 19.40 | 50.10 ms | 62.68 ms | 68.18 ms |
| Final neutral | 19.50 | 50.13 ms | 62.65 ms | 67.65 ms |

A read-only observer polls the native presentation counter approximately every
two milliseconds. Its longest read gap is 6.47 ms across these phases, and it
observes every counter increment. These are host presentation intervals with
observer uncertainty, not display scanout or independently counted animation
updates. The changed view/workload matters: standing still after turning is much
slower than the initial neutral pose. One route is a baseline, not a statistical
comparison or a verified optimization.

During the slow back phase, two sampled guest clock accumulators advance 10.36 s
and 7.22 s over 10.00 s of host observation. Their distinction and frame-time
clamping need tracing before interpreting the latter as an exact simulation-speed
measurement or changing game timing.

A separate ten-second CPU sample follows the route, suspending only the six
hottest host threads briefly to read their instruction pointers. It is not used
for the timing table. Hot sites include the image-19 geometry program,
`vm_read32` and the other guest-memory helpers, vertex decoding/fetch and texture
content hashing. One polling thread consumes about 96% of a core; its sampled
locations are predominantly in the system library, with `sys_timer_usleep` among
its native callers. The sleep implementation has a sub-millisecond yielding spin;
the actual wait dependency must be identified before replacing it.

An earlier exploratory timeline records overlapping SPU and FIFO work with short
reported GPU fence waits in a near-30-presentations/s segment. Its million-record
dump can introduce pauses, so it is excluded from the timing table. That baseline
does not include GPU execution timestamps or an allocation/upload breakdown;
the [later diagnostic checkpoint](#gpu-command-spans-and-the-town-wait-chain)
records command spans and host allocation/staging costs.
The next candidates should address measured CPU/memory and renderer work while
retaining the same input route and separate timing/diagnostic passes. No runtime
performance improvement is claimed by this measurement checkpoint.

## Drakengard 3 performance and settings comparison

Source review on 2026-10-19: Drakengard 3 port
[`96df4c3`](https://github.com/soyjxck/dod3-recomp/tree/96df4c364a20c306724a48886a43f8f14ce5bedd),
whose SDK submodule points to
[`bbcf9a5`](https://github.com/soyjxck/ps3recomp/tree/bbcf9a53c7efef9fe7ca84c058f0292ed998d0cb).
Compared with the current WKC stack, this identifies implementation gaps rather
than a measured WKC speedup. No runtime code changes or new game benchmark were
made for this review.

| Area | Source comparison | WKC follow-up |
| --- | --- | --- |
| Guest memory | Their [inline accessors](https://github.com/soyjxck/ps3recomp/blob/bbcf9a53c7efef9fe7ca84c058f0292ed998d0cb/runtime/ppu/ppu_vm_fast.h) use page eligibility bits and retain slow paths for special accesses. WKC generated code calls out-of-line `vm_read*`/`vm_write*`; its loader still performs diagnostic and reservation checks there. | Measure these calls during movement, then test an equivalent fast path that preserves MMIO, bounds, diagnostic hooks and SPU reservation invalidation. This requires runtime integration, not just including the header. |
| Generated CPU code | Their [lifter documentation](https://github.com/soyjxck/ps3recomp/blob/bbcf9a53c7efef9fe7ca84c058f0292ed998d0cb/docs/PPU_RECOMP.md#registers-in-locals---nonvolatile-locals) describes local registers, selective write-back, split condition fields and grouped memory checks. WKC's build invokes the older lifter without this pass. | Evaluate a separate regenerated candidate with differential checks. Preserve WKC's existing instruction fixes and native hooks across direct calls, indirect calls and callback boundaries. |
| Texture validation | Their [draw engine](https://github.com/soyjxck/ps3recomp/blob/bbcf9a53c7efef9fe7ca84c058f0292ed998d0cb/libs/video/rsx_draw_engine.c) uses protected-page write tracking to skip unchanged texture hashes, with an optional verification hash. WKC hashes cached texture contents once per presented frame. | Measure bytes and CPU time spent hashing. Test tracking against the full-hash path, covering PPU, SPU, host/HLE and GPU-to-guest writes before trusting skipped work. |
| Graphics allocation | Their [technical notes](https://github.com/soyjxck/dod3-recomp/blob/96df4c364a20c306724a48886a43f8f14ce5bedd/docs/TECHNICAL.md#rendering) describe pooled uploads and Vulkan memory suballocation. WKC's Vulkan image and buffer helpers allocate device memory per resource. | Measure allocation and upload stalls during traversal; test pooled resource retirement against in-flight GPU work. |
| Resolution and AA | Their rendering implementation scales internal targets and related pass state and supports multisampled scene targets. WKC's menu changes output resolution and offers present-stage FXAA; it does not implement internal-resolution scaling or scene MSAA. | Treat internal scale and MSAA as separate renderer features, with depth, viewport, scissor, screen-coordinate, resolve and UI checks. Output enlargement alone does not establish higher scene detail. |

Their [runner](https://github.com/soyjxck/dod3-recomp/blob/96df4c364a20c306724a48886a43f8f14ce5bedd/src/main.cpp)
also separates fence wakeups from frame limiting. Its UE3 tick-rate, loading and
movie hooks are specific to that engine; WKC needs its own equivalent behavior
traced. Their reported benchmark gains are not predictions for WKC.

Priority is to profile movement from the verified save, separate guest CPU,
memory-access, texture-validation and GPU/upload costs, and compare one candidate
at a time on the same route. Native hooks should have comparison modes wherever
possible. A higher presentation count alone does not establish smoother motion
or correct game speed. The earlier Drakengard frame-clock review remains recorded
in the [town performance investigation](#town-performance-after-the-physics-fix).

## Platform queries and missing-syscall reporting (patch 0099)

The broader log audit found two calls falling through to the loader's generic
success stub: syscall 144 (`sys_time_get_timezone`) and 631 (`sys_gpio_get`).
The generic stub did not initialize either query's output buffers. The runtime
now registers timezone retrieval and retail GPIO get/set semantics. The timezone
query stores signed standard-offset and summer-offset minutes as guest-endian
32-bit values. Retail LED/DIP status stores eight zero bytes; unknown devices
and invalid output buffers return errors. DIP writes are rejected; retail LED
writes succeed without controlling a host device.

Both timezone output ranges are checked before either is changed. Bounds checks
use widened address arithmetic, including near-end and wrapping guest pointers.
The host timezone configuration is not logged or published. Windows execution
is verified; the POSIX host-offset path remains unverified on Linux/macOS.
The ABI and retail behavior were checked against
[RPCS3's syscall table](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/lv2/lv2.cpp),
[timezone query](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/lv2/sys_time.cpp)
and [GPIO implementation](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/lv2/sys_gpio.cpp).

The same audit exposed a logging blind spot: the loader stopped warning after
30 unhandled calls in total. Repeated timezone requests consumed that budget.
A thread-safe bitset now warns once for each valid ID, 0..1023, regardless of
earlier traffic. Invalid IDs outside that table share one overflow warning.
The return behavior of other unimplemented syscalls is unchanged. Probes list
`unimplemented_syscall_ids` in their result JSON and console warnings. This
does not detect every semantic approximation inside registered HLE handlers.

`wkc2_platform_queries` tests production registration, output encoding, retail
GPIO behavior, pointer boundaries and unchanged memory on failed queries.
Restoring either missing output or omitting registration fails the regression.
Eight concurrent workers make 163,840 reporting checks and obtain exactly one
first report for each of the 1,024 valid IDs; a later distinct ID also survives
1,000 repetitions of an earlier ID. All 84 Windows checks pass in Debug and
Release; all 99 patches reconstruct the SDK and reapply without changes.

The final hidden/muted candidate used Continue, moved briefly and exited 0
(probe `20261019-004357`). Neither former stub warning appeared. The clip-range
audit reports implemented handling with no fallback or pipeline-failure event.
This confirms a native smoke run, not inspection of every returned guest byte,
an FPS improvement or a flicker fix. The [latest gallery frames](GALLERY.md#2026-10-19---window-depth-clip-range-check)
come from this final candidate. Visibility reports and window-origin semantics
remain the active rendering-audit leads.

## Vulkan window-depth intervals (patch 0098)

A private, draw-correlated town census found 194,618 nondefault clip-range draws
out of 7,464,866 sampled pipeline-stage attempts. All 194,618 had depth testing
and depth writes enabled. Ten distinct intervals appeared, with 45,260 draws
requesting a far limit slightly above 1. The census was reset after Continue;
these are diagnostic execution counts, not frame or performance measurements.
The old renderer ignored these limits and always clipped against the ordinary
0..1 host volume.

The engine now pairs two operations: normalize the guest viewport's window Z
into the requested clip interval, and set that interval on the host viewport.
This clips out-of-range geometry while preserving stored depth inside the
interval. Dynamic state does not change pipeline identity. Every draw resets
the range, so a narrow interval cannot leak into a later ordinary draw.
The optional backend callback leaves older backends' vertex mapping unchanged.
A follow-up seeds the hardware far-plane reset value and tests a write to only
the near endpoint, retaining the far default.

Vulkan enables `VK_EXT_depth_range_unrestricted` only when advertised, allowing
the observed far limits above 1. Unsupported, nonfinite, equal or reversed ranges
restore the previous 0..1 mapping and produce a separate `clip_range_fallback`
audit warning. The implemented `clip_range` row and fallback row are distinct.
Depth clamping and degenerate/reversed-interval hardware behavior remain open.
Other backends have not implemented the callback. This does not establish full
RSX precision or parity on other GPU vendors.

The pairing was cross-checked with
[RPCS3's vertex normalization](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/GLSLSnippets/RSXProg/RSXVertexPrologue.glsl)
and [Vulkan viewport state](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKGSRender.cpp),
plus the [Vulkan extension's clipping rules](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_depth_range_unrestricted.html).
The port uses its existing vertex transform and a small independent coefficient
helper; no RPCS3 implementation was imported.

The synthetic GPU regression passes 12 cases on each of D32, D32S8, D24S8 and
X8D24: stored-depth preservation, near/far rejection, narrow WKC-like intervals,
different clip-W values, ordinary-range reset and an upper limit above 1.
Expected depths are checked within 5e-7, not bit-for-bit hardware matching.
Negative controls independently restore old vertex normalization or old viewport
state; both fail GPU assertions. Disabling engine forwarding fails the engine
regression. All 83 Windows checks pass in Debug and Release; all 98 patches
reconstruct the build SDK and reapply without changes.

The hidden/muted candidate used Continue and short movement, then alternated
off/on/off/on in three fixed camera views (eight GPU readbacks per mode, 96
frames total). The same executable supplied both modes. Idle character poses
and Logic Stone animation differ, so these are not identical-frame comparisons.
No clear foot/stone flicker reduction or FPS gain is established. The process
exited normally (probe `20261019-002322`); unrestricted depth range was available,
and the audit recorded no range fallbacks or failed pipeline builds. Visibility
reports remain approximate and window-origin semantics remain unverified.
See [the enabled-mode captures](GALLERY.md#2026-10-19---window-depth-clip-range-check).

The broader log review also found stubbed syscall 144 (timezone query, 29 logged
calls) and 631 (GPIO status, one logged call). These currently return success
without filling their output buffers. Patch 0099 above implements these query
outputs; those logged counts describe the earlier candidate. They are not established
explanations of lag or visual corruption.

## Request-audit follow-up (patch 0097)

The method-write review added three missing census checks: framebuffer sRGB
output (`0x1FEC`), fragment depth export (`0x1D60`) and nonzero window offsets
(`0x02B8`). The implementation gaps remain; this patch makes relevant requests
visible automatically. The sRGB check excludes 32-bit exports, float surfaces,
disabled colour targets and alpha-only output. Depth export only counts when
depth testing is active, including tests with depth writes disabled. Synthetic
regressions cover the exclusions and active cases. All 82 Windows checks pass
in Debug and Release, with all 97 patches applying and reapplying cleanly.

The sRGB eligibility and depth-export enable conditions were cross-checked with
[RPCS3's fragment-program setup](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/RSXThread.cpp)
and [register decoding](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/rsx_decode.h).
No game programs or raw register dumps are published.

A new hidden/muted candidate loaded Continue and reached town (probe
`20261018-215910`, normal exit 0). None of the three added feature conditions
appeared. The existing clip-range, visibility-report and window-origin warnings
remain. This run establishes neither an FPS gain nor a new visual fix.

A separate review of 6,224 private captured fragment-shader files found no
direct WPOS or fog-input reads. This is a historical capture sample, not proof
that other scenes do not use them. A 20-second, unsuspended town register sample
also found zero window offset, smooth shading, filled polygons and polygon
smoothing disabled. Reads can span different draws; they cannot attribute
clipping or sRGB behavior to a particular pass. Depth-clamp control was unwritten,
so its zero register image is not evidence of an explicit request to disable
depth clipping. Clip limits change between passes and need a draw-correlated
comparison before changing vertex normalization or viewport depth range.

## Active RSX request coverage and fragment inputs (patches 0095-0096)

The draw-time audit found active TC0-7 2D coordinate overrides that were only
stored, and 13 compiled fragment programs using arithmetic predication. The
shared decompiler now implements coordinate overrides and instruction-level
perspective correction, and emits conditional component writes instead of
numeric `lerp`. The latter prevents unselected infinities/NaNs from contaminating
results and preserves signed zero. Changing override state creates the correct
pipeline variant and reuses it when unchanged.

The input semantics were checked against RPCS3's
[fragment decompiler](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/FragmentProgramDecompiler.cpp),
[instruction fields](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/RSXFragmentProgram.h)
and [Vulkan fragment inputs](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKFragmentProgram.cpp).
For a pixel shader, position W is reciprocal clip W. A 2D override sets Z to zero
and W to clip W before optional perspective correction. Source swizzles and
modifiers apply afterward. TC8/9 still need implementation.

The production decompiler passes 512 synthetic Vulkan GPU lane comparisons:
all 16 component masks, two register banks and two condition registers, plus
coordinate/perspective combinations. Generated programs also compile as fragment
shaders. Independent negative controls restore the old predication, ignore
coordinate overrides, ignore perspective correction, or omit the pipeline key;
each fails its relevant regression. This does not verify full raster interpolation,
all guest instructions or other renderer implementations. All **82 Windows
checks** pass in normal Debug and optimized Release; all 96 patches reconstruct
the SDK and reapply with zero changes.

The hidden/muted candidate used Continue, moved briefly in town and closed with
exit code 0 (probe `20261018-212515`). Its boot-plus-town census records:

| Active feature | Events | Status |
|---|---:|---|
| TC0-7 2D override | 5,335,538 | Implemented by 0096; state presence does not prove each shader reads Z/W |
| Fill polygon offset | 395,647 | Implemented by 0094 |
| Nondefault clip range | 85,021 | Depth clipping semantics need verification |
| Visibility reports | 33,950 | Approximate host-visible result; GPU queries/publication remain open |
| Bottom shader-window origin | 6,224,730 | Input use and coordinate semantics need verification |

The run reached 6,224,730 pipeline-stage draw attempts and the same number of
backend submissions, with no recorded pipeline-creation failures. Submission
counts are not GPU completion counts. No old arithmetic-predication pattern,
unsupported-opcode marker or call/return marker was found in compiled programs.
The checked multisample, conditional-render, gamma, alpha-kill, 3D-texture and
independent-MRT states were not detected in this run; other scenes may use them.

Probes now enable this census by default and summarize unresolved requests in
their result files. Direct launches remain opt-in; `-NoRsxAudit` disables it for
timing comparisons. It checks a known subset of active state and generated-shader
markers, not every RSX command. Raw method classification is reported separately
and does not establish feature support. Full logs and captures remain private.
See the [latest frame](GALLERY.md#2026-10-18---fragment-input-and-request-coverage-check).
No visible flicker fix or performance gain is established by this batch.

## Vulkan polygon offset (patch 0094)

The command-reference review below identified active fill-offset requests which
never reached Vulkan. The shared engine now forwards their float scale/bias
through an optional dynamic callback. Disabled fill offset and point/line draws
send zero, preventing a previous draw's factors leaking into later draws.
Vulkan enables bias on filled triangle topologies, including expanded quads and
fans, and records the factors per draw without adding changing values to its
pipeline cache. Point/line offset and other renderer implementations remain open.

The backend uses the NVIDIA D24 constant-factor adjustment noted below; D32
factors pass directly. Synthetic GPU depth readbacks pass for D32, D32S8, D24S8
and X8D24 on the tested device. Positive/negative constant and slope factors
move depth in the expected direction, and returning to zero restores baseline.
An independent planar-depth calculation checks the zero-bias result. Host-side
cases cover vendor/format selection; hardware parity on other GPUs and the full
RSX depth range remain unverified. Depth attachments still use the backend-selected
host format; matching guest Z16 bias precision is additional work.

The production engine regression checks forwarding, sign changes, line
isolation, enable/reset and pipeline reuse. It fails with the prior engine.
A separate negative control disables raster bias and fails the GPU depth
readback assertion. All **80 Windows checks** pass in Debug and Release, and
all 94 patches reconstruct the build SDK and reapply without changes.

The hidden, muted native candidate loaded the verified save, moved away from
the Logic Stone and changed camera pitch. Two off/on cycles captured four
frames per mode in each view: one near the stone, one aimed down at Leonard's
feet. Offset switching used a private control in one process; animation and
NPC poses differed. The captures show no clear reduction in the reported
flicker, so neither foot flicker nor Logic Stone light flicker is marked fixed.
This was a correctness comparison with readbacks and logging, not a performance
benchmark. The process closed normally with exit code 0. See the
[unmodified comparison frames](GALLERY.md#2026-10-18---vulkan-polygon-offset-check).

## RSX command-reference cross-check

Reviewed the [RSX FIFO command reference](https://www.psdevwiki.com/ps3/RSXFIFOCommands)
against the 93-patch SDK before patch 0094. The table retains that review's
state; the implementation above supersedes its polygon-offset omission. It used the existing
18-second saved-town register sample described under
[texture detail and rendering state](#texture-detail-and-rendering-state-audit-patch-0079);
no new native run or performance measurement was made for that source review.

| Commands | Current implementation and evidence | Next check |
|---|---|---|
| Polygon offset: fill enable `0x0A68`, scale `0x0A78`, bias `0x0A7C` | The dispatcher stores the words, but `rsx_draw_engine_decode_render_state` and `rsx_be_render_state` omit them. Vulkan's engine pipeline leaves depth bias disabled. The town sample found fill offset enabled with negative, nonzero scale and bias. | Forward the requested state and verify depth-format-dependent bias units with a GPU regression before comparing the foot/Logic Stone flicker. Its effect remains unverified. |
| Report DMA context `0x01A8`, GET_REPORT `0x1800`, render enable `0x1E98` | `cellGcmSys.c` records reports only in the host table and returns a constant visible value for ZPASS. The shared draw path has no conditional-render evaluation. Type-1 reports were observed in town; conditional drawing and the requested report location have not been established. | Capture these three commands together before implementing query publication/predication. Extra drawing is a possible cost, not a measured explanation of lag. |
| Surface AA bits in `0x0208` and AA control `0x1D7C` | The shared surface extraction omits AA sample factors; Vulkan engine pipelines use one rasterization sample. The Graphics Settings FXAA option is a separate post-process. | Establish WKC's actual per-pass sample layout before changing surface dimensions, addressing or resolves. |
| Depth bounds `0x0380`-`0x0388`, fog `0x08CC`/`0x08D0` | These features were inactive in the sampled town state. | Keep them separate from the observed active polygon-offset omission; check other scenes before attributing a symptom. |
| Depth comparison/mask/test `0x0A6C`/`0x0A70`/`0x0A74` | The shared engine decodes these and Vulkan applies them. | Preserve the existing shadow-polarity fix; these commands are not another missing depth-test implementation. |
| NV0039, NV3089 and inline-copy subchannels | Transfer implementations already exist, including patch 0052's NV0039 path. The FIFO walker routes them separately from the 3D register bank. | Use actual object bindings/traces to check routing; the reference's usual slot assignment alone does not prove a missing engine. |

A stored register or a dispatcher coverage label is not proof that rendering
uses its value. The trace-to-register-to-backend path matters. In particular,
the old D3D12 `uvdbg` message labels reads of `0x0A78`/`0x0A7C` as depth
write/function even though those are offset scale/bias; that message is not
reliable depth-state evidence.

The command list identifies addresses, but does not establish the conversion
from RSX offset values to a host depth format. The
[Vulkan rasterization specification](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-depthbias)
defines constant/slope bias and format-dependent precision. RPCS3's
[Vulkan draw-state implementation](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKDraw.cpp)
also adjusts constant bias for NVIDIA D24 targets. That adjustment is a useful
validation lead, not a factor to apply blindly to every depth format or GPU.
No rendering behavior changed in this review.

## Killzone 3 SDK comparison

A source review compares all 62 entries in the Killzone 3 runtime catalogue
against the 84-patch stack. Missing SPU correctness paths, vector-helper/HLE
performance leads, rendering gaps and service issues are recorded alongside
existing equivalents. The original audit was read-only; the first implementation
batch below addresses concrete instruction faults. No FPS gain is established. See the [complete comparison and priorities](KILLZONE_FIX_AUDIT.md).

## SPURS completion coherence and SPU byte operations (patches 0091-0093)

**0091:** task completion previously cleared ready/pending/enabled bits under
the host lifetime lock, outside the lock used by SPU atomic line commits. It
now updates those bits under the reservation lock and issues coherence
notifications. The scan takes a short bitset snapshot, then rechecks eligible
descriptors under that lock; image parsing and worker creation remain outside
it. Lifetime claims and stale-completion protection are retained.

The regression runs the production completion function against a modelled
SPU compare/commit window while using the real lock and coherence registry.
The old implementation permits completion inside that window; a subsequent
full-line commit can resurrect the retired task. The fixed code preserves the
new sibling bits and retires the completed task. A separate negative control
removes notification and fails the reservation-loss/LR assertion. This covers
completion/scan mutations, not every SPURS producer: shared bitset-builder
read/modify/write helpers still need review.

**0092:** task scans now mask the ELF pointer's low three flag bits before
sizing/fingerprinting the image. All seven nonzero combinations launch the
correct synthetic sibling image in the actual scheduler fixture; the old code
fails. These are correctness fixes, not demonstrated WKC FPS fixes.

**0093:** nine byte-shift/rotate entry points now use project-written SSSE3
shuffle helpers on supported builds. The scalar path remains available and is
tested separately. Comparisons use architectural byte order, every relevant
count, random register high bits, wrap/zero-fill cases and extreme immediate
values. An intentionally wrong byte-order mask fails the vector test. Exact
FNMS and all floating-point modes are unchanged.

Five alternating helper microbenchmark passes, with 5,000,000 calls per case
and matching checksums, measure median speedups of **1.17-1.81x** for the six
register-count forms. These are isolated helper calls with loop/call overhead,
compiled at `/O2 /arch:AVX2`; they are not game-frame improvements.

A hidden, muted Vulkan run reloads the saved town and exits with code 0. It
alternates the exact previous helpers and SIMD in one diagnostic binary,
using a live mode switch, neutral controls, no sampling profiler and no
repeated GPU captures during measurement. Eight ten-second windows follow
0/1/1/0/0/1/1/0. Early samples favour SIMD; later samples slow in both modes.
A sustained FPS gain is **not established**. The counter measures accepted
presentations, not unique game animation. The diagnostic mode branch and
hidden window limit transfer to normal play. The preliminary comparison used
a nonidentical scalar body and is excluded. See the
[structured measurements](../verification/performance.json).

All **79 Windows checks** pass in Debug and Release. The 93-patch stack
reconstructs 72 SDK files from the pinned baseline. ARM/macOS execution,
physical-controller gait and a smooth 30 fps during town traversal remain open.

## Runtime correctness and input follow-up (patches 0085-0090)

The user still reports intermittent walk/run transitions, particularly at angles,
after patch 0084. Patches 0087-0088 expand the circular XInput vector toward
the PS3 diagonal range, allow a 5% outer calibration margin, and serialize the
complete host report with its freshness flag. The event pump and guest pad API
previously accessed those bytes without synchronization.

The new recording-driver test pauses a host poll and verifies that a concurrent
guest read cannot consume an unfinished report. It fails on the old code. The
stick test checks imperfect-rim endpoints and continuous partial-tilt angle sweeps,
and also fails on the old mapping. A new export check ensures all twelve pad API
functions remain registered after source scanning.

A hidden, muted native Vulkan run loaded the verified town save. Read-only
sampling of the game's cached pad state found steady partial/full input and no
neutral dropouts across twelve full-tilt directions. This verifies synthetic
input delivery; it does **not** establish physical-controller gait consistency,
calibration for every device or smooth animation. The SDL mapping is unchanged.

The first audit implementation batch also fixes:

- **0085 / S2:** absolute SMC `lqa`/`stqa` use word-scaled addresses, with
  independent load/store checks, guards and upper-address/alignment cases.
- **0086 / S6:** RR `mpyhha` writes its actual destination; `mpyhhau` executes.
  Signed accumulation now uses modular unsigned addition to avoid host overflow UB.
- **0089 / S6:** interpreter cases for `bgx`, `mpyui`, `rothmi` and `rotmahi`,
  tested at borrow/sign/shift boundaries.
- **0090 / P1:** `vsubuws` now translates unsigned saturation with big-endian
  word lanes, register aliases and sticky VSCR.SAT. All four WKC TODO sites
  were regenerated from the updated lifter. Whether those sites execute in
  this saved scene remains unmeasured.

The old implementations fail their regressions. All **77 Windows checks** pass
in normal Debug (12.10 s) and optimized Release (11.80 s). The **90-patch stack**
reconstructs 71 SDK files from the pinned baseline. These are correctness results;
no game-speed or rendering improvement is established by this batch. Remaining
performance, SPURS, rendering and service work stays in the
[audit queue](KILLZONE_FIX_AUDIT.md).

## Windows stick magnitude and walk/run response (patch 0084)

Historical first change; the user reported remaining problems, addressed further
by patches 0087-0088 above.

The previous XInput path applied its deadzone separately to X and Y. A full
circular diagonal was reduced to roughly 87% of straight-ahead magnitude, and
partial tilts lost both direction components independently. The corrected radial
conversion preserves direction and full magnitude, retains a continuous walking
range, and handles asymmetric signed endpoints and Y inversion before encoding.
The right stick uses its own deadzone landmark. The implementation is native C.

The regression checks 720 full-tilt angles across the two stick configurations,
neutral/noise, exact endpoints, monotonic partial tilt and equal straight/diagonal
magnitude. A private native input harness supplies raw signed XInput values
before this production conversion. Short town probes show partial-tilt movement
and a stronger full-tilt running response. This does not establish controller
calibration, every directional gait transition or sustained traversal. Actual
physical-controller feel still needs user comparison.
See [implementation and reference scope](PATCHES.md#windows-analogue-response-patch-0084).

## Full Graphics Settings panel and window close

Graphics Settings now opens the game's full Game Settings panel rather than
reusing the narrow eight-slot System list. The appended System action preserves
the original callback and object adjustment. Hooks scope the port's labels and
preferences to this entry; ordinary Game Settings keeps its original behavior.
All eleven options and Back fit together, with the native frame, scrollbar,
selection arrows, highlight, hand and exit transition. Up/Down selects;
Left/Right changes a value, X advances it, and Circle or Back returns to System.
Nested widget calls retain their own register context and tail-call chain.

Native controller checks verify opening the panel, moving the highlight,
changing 30 to 40 and back to 30, returning with Back, and reopening the regular
Game Settings panel with its original labels and values. The menu regression
checks routing, values, pending controls, persistence and original-panel isolation.
The requested clear background preview and distance controls remain pending.

The window-close hang was caused by the runner disabling RSX but leaving the
process alive. Patch 0083 also recognizes the SDL window-close event explicitly.
The runner now takes the normal exit path, invoking registered media cleanup.
Private title and saved-town settings tests deliver that event through the
production SDL queue and exit with code 0, without forced termination.
The window-event regression also verifies unrelated events leave the game open.

## Native graphics settings and frame-rate control

The native game now has a separate Triangle -> System -> Graphics Settings page,
using the game's own widgets, input and text. It contains window mode, named
output resolution, VSync, anisotropic filtering, anti-aliasing and frame limit.
All controls, including the renderer selector and FPS overlay, now appear
together in the full native settings panel.
World/NPC distance and fog are visibly pending and do not change the game.
The background blur has not been removed. Output size does not alter internal
render-target resolution. See [controls and limitations](PATCHES.md#native-graphics-settings-patches-0081-0082)
and [actual menu captures](GALLERY.md#2026-10-18---full-graphics-settings-panel-and-window-close).

Patch 0081 adds Vulkan preferences and a transparent text FPS overlay. Patch
0082 adds a project-written FXAA compute pass over the completed image, before
window scaling/overlay, and deferred flip plus dimension-matched scanout for the
experimental DirectX 12 live-draw path. The published source contains no private
capture, hidden-window or mute controls. DirectX 12 loads the saved scene and
its Graphics Settings page, but differs visually and lacks Vulkan's display
preferences/FXAA/overlay integration. Changing API requires restarting.

The first frame-rate menu only raised a host limiter; the original game still
released its render semaphore after two vblanks, leaving a 30 FPS ceiling.
The runner now changes vblank cadence for fixed caps. The default preserves
59.94 Hz; 40/60/120/144 request 80/120/240/288 Hz. Unlocked also removes the
original interval/extra-wait gate through hooks on the guest graphics thread.
Changing away from Unlocked restores the requested interval and flag.
The time source and the game's normalized 30-tick-per-second clock are unchanged.

Short stationary saved-scene probes, optimized Release, hidden/muted with file
logging, sampling profiler and periodic screenshots off:

| Setting | Wall seconds | Completed frames/s | Game-clock seconds | 30-scaled clock ticks |
|---|---:|---:|---:|---:|
| 60 | 12.002 | 37.83 | 12.009 | 360.27 |
| Unlocked, VSync off | 10.002 | 37.19 | 10.018 | 300.55 |

These are separate runs of the timing candidate, with asynchronous read-only
2 ms samples, not a matched FPS improvement comparison. GPU/CPU work still
limits throughput; the results do not establish a locked 60 FPS, unique animation
states, correct cutscenes/physics at every rate, or smooth traversal. They do
show that the previous hard ceiling is removed and the sampled game clock
stays close to wall time. The later menu screenshots use diagnostic readbacks;
their displayed counters are not performance benchmarks.

An earlier diagnostic menu run stopped advancing after a live change from 30 to
Unlocked with VSync on. Investigation found that Unlocked published vblank ticks
on every FIFO/render-loop pass. The port now ticks immediately at each new flip
boundary and uses a normal idle heartbeat between them. Fence polling no longer
creates extra callbacks, and the heartbeat does not cap actual flip frequency.

A 120-second diagnostic on the old path averaged 15,424.7 host ticks/s and
4,129.6 delivered guest ticks/s. A 30-second candidate run, including live
VSync toggles, averaged 71.4 ticks/s for both. Both ran hidden/muted with file
logging while another native copy was open; scenes and durations differ. These
are callback-volume diagnostics, not a matched FPS improvement comparison.
Repeated 30/Unlocked switches and ten live VSync toggles remained responsive;
the candidate subsequently returned to town and exited normally with code 0.
The earlier rare freeze has not been independently reproduced, so these results
do not prove that every possible high-FPS stall is fixed.

An end-to-end check on the final 82-patch menu build changed 30 -> 40 -> 60
using the native menu, returned to the scene, and sampled 385 completed frames
in 10.002 seconds (38.49/s). Game-clock time advanced 9.989 seconds and vblanks
ran at 119.97 Hz. FXAA was enabled. This verifies live menu application; it is
not a comparison with the earlier FXAA-off samples or a traversal benchmark.

All 73 checks pass in normal Debug (12.60 s) and optimized Release (12.56 s).
The complete 84-patch stack reconstructs 70 SDK files from the pinned baseline.
The new timing regression covers removing/restoring interval and wait state;
menu tests cover full-panel routing, input and restart-only API persistence. The FXAA shader
compiles through the production compiler and its descriptor bindings match the
pipeline. Native Vulkan initializes the compute pipeline and displays the menu;
DirectX 12 reaches the same saved scene and menu. Full AA quality and API parity
comparisons remain open.

## Direct Vulkan window presentation (patch 0080)

The shared engine rendered the guest's 1920x1080 presentation surface into a
fixed 1280x720 image, then scaled that reduced image into the window. This
could discard detail even after the texture LOD correction. Patch 0080 sends
the selected guest image and dimensions directly to the swapchain blit. Normal
presentation performs one scale and omits the intermediate full-frame copy.
The debug center pixel comes from the guest surface; explicitly requested
frame dumps and shutdown-hold captures preserve their previous format.

The queue/submission arrangement, presentation counter and game timing are
unchanged. The allocation-failure path preserves the full-resolution source.
The recording-API regression fails against the previous code and covers equal,
smaller and larger window sizes, diagnostic paths, missing/acquire-unavailable
swapchains, headless operation and allocation fallback. All 68 Windows checks
pass in Debug and Release at that checkpoint. The later 82-patch native runs
execute this path and provide final-window menu readbacks. A matched RPCS3
comparison, an FPS gain attributable to this patch and full visual fidelity
remain unverified.

The discussion in [ps3recomp issue 183](https://github.com/sp00nznet/ps3recomp/issues/183#issuecomment-6136128339)
explicitly identifies synchronous Vulkan submission and incomplete Windows
validation upstream. Our local batching/Windows changes require their own
measurements. The issue does not diagnose WKC's SPU behavior, establish a
D3D12 speed advantage for this build, or remove the need for renderer A/B tests.

## Texture detail and rendering state audit (patch 0079)

The native frame was visibly softer than the RPCS3 reference. Read-only
sampling found repeated **-1.5 LOD bias** requests with trilinear filtering;
our shared engine discarded that field. Patch 0079 decodes the signed 4.8
value, forwards it to Vulkan and prevents different bias/LOD values from
aliasing in the sampler cache. It clamps bias to the device limit.

A hidden, muted run loaded the verified save and switched zero/requested bias
in one executable, with a fixed camera. Guest audio mixing remained active.
The requested-bias capture restores clothing grain, masonry and foliage detail.
Idle animation and the Logic Stone rotation differ between frames. These are
original pixels from the guest presentation surface before window scaling;
they do not establish final-window fidelity or an FPS gain. Private A/B and
visibility/audio controls are absent from the published patch. Both Windows
suites pass all 67 checks; the sampler regression fails on the previous builder.
See the [newest gallery comparison](GALLERY.md#2026-10-18---requested-texture-detail-restored).

The subsequent read-only audit sampled render registers over 18 seconds at the
loaded save. Register-write totals are not equivalent to draw coverage, and
sampling can miss brief states. Dispatcher TODO labels alone are also unreliable:
some such states are already consumed elsewhere. The following observations
were checked against the shared engine and Vulkan implementation:

| Request/state | Observation | Current handling / next check |
|---|---|---|
| Texture LOD bias | Repeated -1.5 requests | Fixed for the shared Vulkan engine in 0079 |
| Polygon offset | Fill offset enabled; nonzero negative bias and slope observed | Missing from Vulkan raster state; concrete flicker lead, effect unverified |
| Depth clip limits | Changing clip-min/max values observed | Not forwarded separately; check interaction with the vertex depth transform before changing behavior |
| Texture-coordinate controls | Several units toggle control bit 0 | Missing explicit forwarding; determine interpolation semantics and affected shaders |
| Presentation scaling | Guest surface readbacks are 1920x1080; intermediate display image is fixed at 1280x720 | Source path corrected by 0080; native window comparison pending |
| Anisotropy / texture gamma / texture alpha kill | 1x / zero / disabled in sampled enabled units | General omissions remain, but these samples do not implicate them |
| Fog / logic operations / depth bounds | Not enabled in sampled registers | No evidence these cause the current view's mismatch |
| Extra render-target blend/masks | Written as zero in samples | Shared pipeline mirrors target A; inspect actual MRT passes before claiming a fault |
| Framebuffer sRGB output | Disabled in samples | No evidence for this mismatch in this view |

This is a prioritized audit of one loaded scene, not a complete-game support
claim. Polygon offset, interpolation, depth behavior and final presentation
remain open; the LOD fix does not establish parity with RPCS3. RPCS3's
[texture register semantics](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/RSXTexture.cpp)
and [Vulkan state application](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKDraw.cpp)
were used as read-only references; their implementation was not copied.

## Exact SPU vector math (patch 0078)

The busy-town profile identified exact FNMS (fused negative multiply-subtract)
inside repeated SPU geometry calculations. Patch 0078 runs the existing integer
algorithm in four AVX2 lanes. Sticky alignment, truncation, zero handling,
extended exponent-255 values and saturation are unchanged. Leading-bit lookup
uses exact double subtractions, which preserve MXCSR under all tested rounding,
FTZ and DAZ settings. Builds without AVX2 retain the scalar implementation;
Apple Silicon remains untested.

The standalone differential exercise matches 16 million vectors / 64 million
lane results under all 16 host-control combinations. The independent FNMS
oracle's 7,099 vectors also pass. A new production-helper regression compares
1,048,576 independent-lane vectors against the scalar reference on x86 builds.
All 66 Windows checks pass in Debug (scalar fallback) and optimized Release
(AVX2). The isolated all-normal-input helper benchmark improves by about 2.9x;
this is not a whole-game speedup.

One executable alternated scalar/vector/scalar/vector with the same stationary
camera and loaded save. The window was hidden, audio-session output muted,
guest mixing active, and sampling, trace recording and automatic GPU readbacks
off. A private control value was checked throughout every sample. Each sample
lasted 40 seconds:

| Path | Presentations/s | Busiest worker, % of one CPU core | Worker CPU ms/presentation |
|---|---:|---:|---:|
| Scalar 1 | 19.90 | 66.37 | 33.35 |
| AVX2 1 | 19.55 | 57.89 | 29.61 |
| Scalar 2 | 19.90 | 65.98 | 33.16 |
| AVX2 2 | 19.55 | 58.71 | 30.03 |

CPU time per presentation falls about 10% for that worker. Presentation rate is
about 1.8% lower; a gameplay FPS gain is not established. A separate easier view
stays near the 30-presentation ceiling with either path. These are presentation
counts, not unique animation updates. Keep the change scoped to CPU efficiency;
town smoothness, traversal and the 30 fps target remain open.

The native A/B build uses the same vector calculation plus private switching
and muted/hidden test controls. Those controls are not in patch 0078. The
published source form is separately built and regression-tested; that exact
executable has not had a separate native town run. No new gallery image is
claimed for a math-only change; the patch-0076 image keeps its provenance.

## Frame timeline checkpoint (patch 0077)

The current player report is "slightly smoother but still bad". This is a
checkpoint, not a demonstrated speed fix. The earlier near-30 stationary view
faced the Logic Stone and hid NPCs; it cannot rank movement or busy-town
performance. Both earlier and later candidates stutter during traversal.

The private diagnostic precursor captured ten seconds with 1,146,981 timing
records and none dropped. A first capture overflowed and is excluded. Recording
adds CPU work, and the captured camera is not a matched traversal benchmark.
Many recorded presentation intervals alternate around 17 and 50 ms; this is a
lead to confirm with recording disabled, not proof of the player's exact cadence.
SPU entry spans are wall time and can include waits; they do not equal CPU time.

Patch 0077 adds opt-in Windows timing for imports, syscalls, SPU entries/channel
waits, FIFO drains/semaphore waits, presentation, Vulkan fences and flush reasons.
Its concurrent synthetic test checks 80,000 complete records from eight writers;
all 65 Windows checks pass in Debug and Release. The portable publication form
has not had a separate native run. The observed rendering/math remains the
patch-0076 baseline. Foot-shadow and Logic Stone flicker remain open.

The frame-dependency approach follows an inspected
[inFAMOUS 2 performance report](https://github.com/sp00nznet/ps3recomp/issues/345)
and its [tracer proposal](https://github.com/sp00nznet/ps3recomp/pull/339).
The Windows recorder here is independently implemented for this port. Our
renderer already skips unused texture units, so that report's unused-depth-unit
fix is not missing here. Its general vertex-span PR 357 passed a private Windows
fixture but is not integrated into this checkpoint. Its game-specific lighting
and SSAO replacements are not substitutes for WKC's geometry work.

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

## Proximity interaction and saving (patch 0075)

The user reports that approaching NPCs or the Logic Stone does not highlight
an interaction target, so X cannot start the normal interaction. The 74-patch
native run reproduces this beside the stone. The target manager contains NPCs
and the stone, but inspected entries are marked occluded and the game's cached
visibility results remain zero. Its registered user-command callback reads
those results, yet that callback is absent from the baseline run log.

The production FIFO walker sends `0xEB00` to the low-level ISR event queue and
continues before reaching the deferred HLE callback route. That queue is absent
in this HLE run. Patch 0075 routes consumed user interrupts to the registered
HLE callback, retaining low-level delivery when no HLE handler is registered.
It preserves deferred delivery and a zero cause, without dispatching twice.
The actual-FIFO regression fails before the fix and passes afterward, including
the supported alias and low-level fallback. Both full Windows suites pass all
63 checks. The native log now records callback invocation and the cached visibility results
update. The Logic Stone highlights on approach, X opens its normal menu, and
Save writes a new slot. A full restart followed by Continue reads the 6,815,744-byte
save payload and returns to town; the user also confirms success. These checks
cover the Logic Stone and one local slot, not all NPCs or save compatibility.
The same patch-0075 slot subsequently loads on patch 0076. The runtime still
returns conservative visible values (`0xFFFF`) for ZPASS reports rather than
measuring GPU samples. Fixing callback delivery restores the observed interaction;
it does not establish correct occlusion or every object's targeting behavior.

The user also reports slower loading and slightly slower cutscenes than RPCS3,
less smooth motion despite a nominal 30 reading, inconsistent analogue walk/run
selection, and flicker in the Logic Stone's rotating inner light. The lag, stick response and flicker reports remain open; the Logic Stone
interaction is verified above. These performance reports do not establish their causes. Presentation throughput alone
does not verify game-clock progression or cutscene playback speed.

## Startup investigation

Historical startup checkpoint, before movie/title rendering was restored.

Later startup frames were black. A captured late command batch contains a black
clear and the final composite, without title-scene draws. The update dispatcher
continues executing its callbacks, so a global pause of the update loop does not
explain this sample.

Tracing then found an installation request arriving while a check-only request
was still pending. The guest helper rejected it, leaving startup waiting for
installation work that had never begun. An isolated empty emulated HDD alone
reproduced the same wait.

Synchronization before the original install request lets the check finish first.
The original guest installer then allocates its buffer, copies the data archives
and advances startup. Whole-file hashes verify the three large installed archives. A second run from
an empty isolated emulated HDD, with detailed tracing disabled, reproduces the
installation and all three matching archive hashes.
No completion result or installation progress was fabricated.

## Startup and early WKC1 checkpoints

Historical progression through patch 0028. The missing-model and loading
failures below were resolved by later steps in this record; they do not
describe the current town build.

AC-3 decoding removes the audio-open failure after the startup logos. The file
trace identifies the previously blocked clip as `title02.pam`, the animated title
background. The native title screen displays New Game, Continue and Options.
With no controller input after the startup notices, it enters `wk2rush.pam`, the
attract movie. The [gallery](GALLERY.md) includes the rendered title menu.

The asynchronous video bridge separates pending compressed inputs from decoded
output capacity, preventing the earlier busy/callback deadlock. Both LPCM and AC-3
use real samples, per-output guest records and sample-count-based timestamps.
The game requests six-channel AC-3 output in the observed run. Synthetic stereo
and 5.1 checks pass; all samples from the opening stream and title background
match an independent FFmpeg elementary-stream decode across two sequence starts.
See the [sample counts and hashes](../verification/movie-demux.json).

The AC-3 comparisons cover sample conversion, requested downmix/channel order,
input copies, output backpressure and reset. They do not independently validate
container audio demux or prove identical output to original console hardware.
Live controller input changes the title highlight and opens New Game. Description,
game-selection and confirmation text now render correctly. The font atlas is a
2048x1024 linear A4R4G4B4 texture, with a 4096-byte row pitch. The earlier binding
filter excluded this format, so text draws sampled the previous artwork. Recording
the actual texture and using the shared decoder plus the uploaded DXGI format fixes
the observed menus. The build now runs the SDK texture-layout tests as well.

White Knight Chronicles I was selected and confirmed through the controller path.
The previous loading attempt dispatched a 258,688-byte all-zero image because
its guard had never registered: the fixed host table was full and an unknown
guard incorrectly released the chain. The importer rejects that empty capture.
The runtime now allocates guard entries, tracks the owning chain, synchronizes
notifications/reset/waits and releases entries after joining. It does not dispatch
past an unknown guard. A cleanup-only fix still hit the 32-guard cap, confirming
that simultaneous capacity mattered as well as lifetime.

The regression test covers 100 create/run/shutdown/join cycles, two-notification
waits, reset, queued notifications, 128 simultaneous guards and invalid guards.
The previous implementation fails this test. The native WKC1 attempt now registers
the formerly missing guards and reaches a real 11,856-byte SPU program
(fingerprint 6AEA19F8490A2656). Its private static lift finds 123 functions and
98.2% code coverage, with seven undecoded words. A subsequent native WKC1 run
traces eight dispatches of this program, all returning `rc=0`. No unregistered-job
warning appears in that run. This verifies dispatch/return, not output correctness:
the game subsequently reports a graphics/FIFO error and stalls. Inspect the
produced command buffers and FIFO consumption next. Playable scenes and save/load
remain unverified.

A read-only observer at the game's graphics timeout captures three matching
samples: the completion label is one submission behind the expected value, and
GET points at a JUMP to its own address. That command is a producer wait, so the
next task is to identify the producer responsible for replacing it and verify
its output. Advancing GET or inventing a label completion would skip pending
work and would not establish correct gameplay.

The next WKC1 probe records write provenance for the mapped command-buffer
allocation. All three timeout samples identify SPU image 19 (fingerprint
687D4BB18564813A), at the DMA-writing block beginning at PC 0xB00, as the last
observed writer of the parked command. That transfer writes 704 bytes, beginning
with the self-jump. The previous observed contents at that address were also
written by image 19. This narrows the investigation to its command generation
and the work that should link the resulting segments; it does not establish a
root cause or validate the job's output. The observer covers lifted PPU stores,
their atomic path and completed SPU DMA PUTs, with two records per word. Host
bulk copies and other unobserved write paths remain outside its coverage.
This probe was stopped after the repeated stall was captured. No new playable
scene or screenshot milestone was obtained.

The game also builds `JOBLIST` commands for this rendering pipeline. The SDK
walker treated them as no-ops, so the jobs in those arrays never ran. Patch 0014
dispatches the array entries in order using the list's descriptor size, with
validation of nonempty descriptor sizes and address ranges. Tests cover guarded
publication, 128/256-byte entries, order, a following single job, empty lists and
invalid ranges/sizes. The previous walker fails the added list-dispatch check;
the implementation passes all seventeen regression checks.

A native WKC1 run with this fix reaches a list containing four 256-byte job
descriptors. They reference a previously unregistered 67,200-byte SPU program
(fingerprint 38DCC9863DD4E49F), now captured privately and statically lifted.
Its strings reference Edge vertex processing. The lift discovers 562 functions,
with 93.3% coverage and 98 undecoded words; those numbers alone do not verify
execution or correct output. The next native WKC1 run records all four dispatches
and returns. The graphics wait persists, still one submission behind, and the
last observed writer of the parked command remains fingerprint 687D4BB18564813A.
Job output correctness, character creation, gameplay and save/load remain
unverified.

The geometry program calls the canonical `cellSpursGetSpuId` routine. It reads
the kernel's SPU number at local-store address 0x1C8. Native job staging loads
the job at address zero, where that location holds an instruction (0x18275E25).
The job interprets it as an invalid processor ID and takes its error/stop path.
Patch 0015 recognizes the standard three-instruction library routine during
lifting and binds it to native job-manager metadata. The serial walker exposes
virtual SPU 0; task/raw contexts keep their original local-store reads. No job
instruction bytes are overwritten. A generated-instruction regression exercises
the actual staging code, preserved bytes, raw/task behavior and a similar routine
that must remain unbound. Without the binding the new regression fails.
All eighteen runtime checks pass.

A native WKC1 probe records eight bound queries, with the displaced instruction
word preserved, and advances beyond the earlier self-jump wait to a missing
16,912-byte SPU program (fingerprint 66E2457D0BEDAE24). Its strings refer to
animation jobs. The private static lift finds 176 functions, 94.9% coverage and
20 undecoded words. The next native run records eight dispatch/return pairs for
that program, with 22 SPU images registered. Output correctness remains unverified.
The capture is black and three later graphics-timeout samples agree: the label
is still one submission behind, but the published GET now points at a normal
method header rather than a self-jump. The next observation must distinguish
in-flight command consumption from missing producer output. No new gallery image
or playable-scene milestone is claimed.

Patch 0016 observes the FIFO worker inside a drain using optional host atomics.
The next ten-minute WKC1 probe captures two timeout samples with the same
published GET, while the in-flight packet moves and the packet count increases
by 3,694. Recent branch records include calls into generated command segments
and returns. Further drains complete and presentation resumes, with a later
capture containing 623 recorded operations. The earlier timeout therefore does
not establish that command consumption stopped. A native thread sample also
finds the worker in vertex-attribute fetch, through vertex upload and drawing.
These observations narrow the next investigation to slow rendering and the
remaining black output; they do not prove a root cause. Both the backbuffer and
the main scene render target remain black. Character creation, gameplay and
save/load remain unverified. The observation changes diagnostic timing but does
not write guest completion values or advance the queue.

Two vertex-shader cache defects explain a later rendering failure. The backend
shifted a 16-entry array when full, but queued draws retained the old indices.
It also hashed unrelated resident instructions after the current program's END,
causing false cache misses. Patch 0017 retains stable IDs until backend shutdown,
compares the actual program bytes after hashing and excludes that trailing store
content. A regression covers 128 retained IDs, copied instructions, deliberate
hash collisions, bytes after END, invalid inputs and release lifetime. A negative
control reproducing the moving 16-slot policy fails; all nineteen checks pass.

The next native WKC1 run reaches Customize Avatar with readable menu controls.
Controller input dismisses the introductory downloadable-content notice. A scene
capture contains 21 distinct vertex-shader IDs (0 through 20), confirming that
this path exceeds the previous cache capacity. The gallery image from that checkpoint shows
the UI after the notice is dismissed. The character model is still missing, and
performance remains slow. This verifies the customization screen and that input
response, not completed character creation, gameplay, saving or DLC integration.

Patch 0018 extends private frame captures with exact vertex-program bytes,
constant banks and up to sixteen uploaded vertices per draw. Offline evaluation
of sampled avatar body vertices produces finite clip coordinates, positive W
and positions inside the view. Working UI draws produce their expected screen
positions. This rules out wholly empty/offscreen inputs for those samples; it
does not verify every vertex, GPU rasterization or fragment output.

The backend also forced depth testing and writes for all guest fragment-program
draws. Patch 0019 retains the guest's depth enable, mask and comparison per draw
and includes them in pipeline identity. A regression covers all eight comparison
functions and a UI-before-model occlusion case. An unconditional-depth negative
control fails; all twenty checks pass. The native capture records UI draws with
depth disabled, geometry with depth writes enabled and overlay passes with writes
disabled. The customization UI still renders, but the avatar is still missing.
This corrects depth-state handling without establishing the remaining root cause;
fragment output, culling and other raster state remain under investigation.

Patch 0020 captures a render target immediately after selected GPU draws, before
later passes can overwrite it. A known-good startup-logo readback validates the
copy path. In the customization scene, snapshots after the background and selected
avatar draws have byte-identical RGB pixels, including after the last model draw.
Those draws use guest fragment pipelines with culling disabled. Post-processing
subsequently changes the image. The missing model therefore already fails to
affect the captured color target before post-processing; this does not identify
whether depth rejection, fragment output or another earlier stage is responsible.
These private diagnostic images add no gallery milestone.

A separate run with the existing `DEPTH_OFF=1` diagnostic shows the room and
avatar. Intermediate copies also show nonzero color changes after the sampled
body draws. The override disables both depth testing and writes; this establishes
that those stages affect the missing geometry, not that normal rendering works.
No image from that override is published as a gallery milestone.

The guest fragment pipelines also left D3D12 depth clipping disabled through
zero-initialization. Patch 0021 decodes the guest clipping/clamping control and
retains it in the raster pipeline key. A command-processor regression covers
reset, disable/restore, clamping and deferred state; the previous no-clipping
policy fails it. All twenty-one checks pass. A further native run confirms
clipping-enabled guest pipelines with normal depth testing, but the avatar is
still missing. Clipping control is corrected without establishing the remaining
depth failure. The next diagnostic needs the depth target itself and its writers.

Intermediate depth-plane copies now show that the clear contains 1.0. Room
geometry subsequently fills the sampled avatar positions with nearer depth:
one sample is approximately 0.785 before the body draw and remains unchanged
afterward. Sampled body vertices request depth near 0.9964. A separate D3D11
stream-output replay of the captured vertex shader, constants and attributes
agrees with the offline transform calculation. This verifies those replayed
inputs, not the live D3D12 shader bindings or all mesh vertices. The next work
is to validate the earlier geometry and the depth state around the room/avatar
transition. Normal-depth avatar rendering remains unverified.

The next native input capture identifies a concrete assembly error: the game
enables primitive restart with value 65535, while the indexed strip path treated
that marker as a vertex index. In the captured room packets these reads produce
zero-position vertices, joining separate strips into false triangles. The path
also omitted alternating strip winding and lost continuity between command
packets. Patch 0022 corrects those behaviors. The C++ regression fails when
either restart handling or alternating winding is independently removed.
[RPCS3's index handling](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Common/BufferUtils.cpp)
was used to check restart semantics, including full-width marker comparison for
16-bit indices.

The native capture contains 708 restart markers across 32 room command packets
(5,128 indices). With patch 0022, the room renders with normal guest depth and
culling enabled. At the previously observed body pixel, room depth is now about
0.99853 and the following model draw writes about 0.99640 in front of it, instead
of being rejected behind false room geometry. The gallery records this room
milestone. Some avatar pieces render, but the body remains incomplete; this does
not verify full character rendering, completed character creation or gameplay.
All twenty-two regression checks pass. The next rendering investigation must
distinguish missing mesh geometry, depth-prepass agreement and shader output.

Further paired readbacks show that the examined skin depth passes write depth
and their following color passes change pixels in small head regions. Native
fragment-buffer captures now confirm that constants for six examined clothing,
hair and skin draws are bit-identical to the constants decoded from their
captured programs. This checks those payloads, not every live shader binding.

Complete converted-vertex capture covers 614 draw payloads and 57,630 vertices
in one character-creation frame. All 19 paired skin meshes cover the head region.
A separate D3D11 GPU replay compares every vertex in those pairs: all 3,339
depth-pass and color-pass clip positions match. This does not validate live
D3D12 bindings or establish whether torso and limb draws were issued, filtered
or never produced. The next step is to trace their submission and vertex-data
production. Full avatar rendering remains unverified; there is no new gallery
milestone from these diagnostics. Patch 0023 retains complete vertices and
actual fragment constant buffers for this investigation. All 22 checks pass.

A completed-frame submission audit now observes 630 backend callbacks with no
omitted records. Of these, 629 upload or extend geometry; the remaining callback
contains a single triangle-strip index and leaves the previous geometry intact.
No callback reaches record capacity, and the unchanged callback has its required
resources and position attribute enabled. This narrows the investigation but does
not establish whether the missing body was produced upstream. Patch 0024 keeps
this bounded audit available for private captures.

Patch 0025 observes actual SPURS exit state. The sampled job
images 19 through 22 return to the native manager sentinel without a recorded
STOP or HALT assertion. That is evidence about their exit path, not proof that
they wrote the correct geometry or commands. The body remains incomplete.

Reviewing [Drakengard 3's native port](https://github.com/soyjxck/dod3-recomp/blob/f7d71501ae33aa4b395c6bbf69545f220d4797b3/docs/TECHNICAL.md)
identified a directly applicable runtime assumption: several SDK paths treat
ordinal image 22 as YDKJ's CRI task. Image IDs depend on each title's generated
registry. WKC's image 22 is a different raw SPURS program, so that ordinal
must not enable another game's special context, arguments or bootstrap EXIT.
Patch 0026 adapts the fork's configurable CRI image selection and disables it
for this runner. All 25 checks pass, including three cases exercising the actual
SPU task EXIT implementation; restoring the old selection makes the disabled
case fail. This is not yet evidence of a complete avatar fix or macOS execution.
The native scene check with CRI selection disabled again reaches WKC1 Customize
Avatar. The room and clothing remain visible, while the avatar body is still
incomplete. There is no new visual milestone from this change.
The native C/C++ target and Windows baseline remain unchanged.

The bounded job observer in patch 0027 captures 37 jobs, including 24 geometry
jobs. All 24 write only zero-filled ordinary DMA output, totaling 6,784 bytes,
although transformed vertices exist in their local stores. No capture limit,
nested-job omission or I/O error affects these samples. Atomic writes are outside
this observer's scope. This places a concrete fault before geometry/command output.

An isolated native replay narrows the failure to FNMS in the geometry culling
routine. A zero operand multiplied by bit pattern `FFFFFFFF` produces a host NaN
in the old helper. The SPU treats exponent 255 as a finite extended-range value,
so that product must be zero. The NaN contaminates the culling mask and the replay
returns no surviving indices. [RPCS3's precise SPU arithmetic](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/SPUInterpreter.cpp)
was consulted for the number-format, zero, saturation and truncation semantics;
its implementation was not copied. Patch 0028 implements FNMS result bits with
integer significands. The same compiled replay now returns 282 indices.

An independent exact-integer oracle checks 101,728 exploratory scalar cases
without a mismatch. The reproducible regression checks 7,099 synthetic cases in
all four lanes, totaling 28,396 comparisons; the old helper fails 13,972 of those
comparisons. All 26 regression checks pass. FPSCR exception flags and other SPU
floating-point operations are outside this fix's scope.

The native follow-up confirms the practical result: avatar body geometry appears
in WKC1 Customize Avatar with normal depth and culling. Arms, hands, trousers and
boots are visible. Four sampled settled-scene geometry jobs now each write three
ordinary DMA payloads, including nonzero data, instead of a small zero-filled
block. The gallery records the blank-name scene after dismissing the introductory
notice. Face shading and clothing materials are still incorrect. This milestone
does not verify full visual fidelity, completed character creation or gameplay.

## Avatar normals and fragment shading

Historical checkpoint: patches 0029-0030, with 28 checks at that stage.
Completed character creation and town entry were verified later.

The next captured face draw exposes two separate shading faults. Type-6 CMP
attributes fall through to float decoding in the old shared vertex fetcher.
One normal consequently becomes a tiny float with zero Y/Z components. The
vertex program produces a nearly zero tangent frame and fragment normalization
generates non-finite values. Patch 0029 adds signed 11/11/10 decoding with the
expansion/scaling described by [RPCS3's vertex-fetch shader](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/GLSLSnippets/RSXProg/RSXVertexFetch.glsl).
The C implementation is independently written. A native follow-up confirms
actual type-6 fetches and visibly restores clothing texture and detail, although
the face still has dark shading defects.

After correcting those inputs in an isolated replay, the first non-finite value
moves to a different instruction: LG2 receives a negative lighting term because
the source's [0,1] precision modifier was ignored. Patch 0030 decodes these
modifiers and applies them in the order described by
[RPCS3's fragment decompiler](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/FragmentProgramDecompiler.cpp).
Its implementation was used to check semantics, without copying its code.
LG2 itself retains its normal negative-input behavior.

With both fixes, all 16 sampled face vertices produce positive finite RGB in the
isolated hardware fragment replay, compared with zero of 16 before either fix.
This replay uses CPU-evaluated vertex outputs and a constant shadow input; it is
diagnostic evidence rather than full-scene validation. The synthetic numerical
regression covers all three sources, six range modes, swizzle, ABS and negation,
plus the negative LG2 lighting chain. The old decoder fails the same regression.
All 28 port checks pass, and all 30 patches apply cleanly to the pinned SDK.

The full native follow-up confirms the combined result in WKC1 Customize Avatar:
the default male avatar now has a shaded face and textured clothing, with the
room and full body visible under normal depth testing and culling. The gallery
shows a blank name field after dismissing the introductory notice. This run uses
centred virtual sticks; disabling keyboard fallback alone had left the test pad's
sticks off-centre and rotated the camera in the earlier diagnostic run.
Full visual fidelity, completed character creation, gameplay and save/load remain
unverified.

## Reproducing the SPU inputs from the executable

Six jobs initially discovered through runtime captures were described as
asset-backed. Direct binary comparison corrects that attribution: every byte
of all six appears in a file-backed segment of the pinned baseline executable.
The extraction table now includes 19 observed raw jobs, alongside the five
embedded SPU ELFs. Patch 0058 adds the 21,408-byte secondary-bone physics job
to the previous 23-image set.

A clean directory, populated only from that executable by the public extraction
scripts, supplies all 24 image inputs. Their complete SHA-256 multiset matches
the production inputs with no missing or additional images. Static lifting
succeeds, and the current native build reaches town with the new physics job.
Runtime captures are unnecessary for regenerating the known SPU input set.
The earlier 23-image checkpoint's clean analysis, PPU/SPU regeneration and
full native build passed all 31 then-current checks. That executable reproduced the
title menu, WKC1 level-1 selection and shaded avatar scene, with name entry
retained. Patch 0058's 24-image build is verified through town and limited
controls. Further scenes may expose additional jobs; sustained traversal,
combat and save/load remain unverified.

## Guest flip presentation through scene changes

Historical transition checkpoint; live town rendering was verified later.

The post-avatar intro exposed a presentation stall. Guest flips kept advancing,
while the backend's last completed frame and live-capture request stopped
advancing. Its clear-present counter remained at 420. The SDK disables its
ordinary present path after a clear-triggered present; its remaining clear
heuristic also retains a draw-count threshold from the earlier larger scene.
A smaller movie batch can consequently remain accumulated indefinitely.

A diagnostic reset of that host counter immediately resumed GPU presentation
and consumed the pending capture, showing the intro and its pause menu. This
diagnostic is evidence for the cause, rather than a normal-run validation.
The runner now selects the SDK's existing RSX_ACCUM_FRAME mode before startup,
so complete batches present at pending guest FIFO flips instead of display-clear
heuristics. A normal launch with this policy renders startup movies, the title
menu and the shaded avatar. Avatar confirmation now reaches the opening movie,
and successive captures show different movie scenes with only 17 operations in
the first captured batch. Live requests continue to be consumed without a debugger
reset. The gallery labels this as prerecorded footage. Interactive scene rendering,
gameplay and save/load remain unverified.

## Ordered surface clears

Historical D3D12 checkpoint: patch 0031 and 32 checks. Vulkan is now the
default renderer. These D3D12 limitations do not describe the shared Vulkan
rendering path.

The D3D12 clear callback discarded the guest mask, depth and stencil values.
It recorded offscreen operations as colour clears and omitted display clears
inside an accumulated frame. WKC1 avatar captures include RGBA+Z (`F1`),
RGBA-only (`F0`) and Z-only (`1`) clears, including a 2560x2560 shadow pass.
Those operations must keep their distinct effects and submission order.

Patch 0031 records the values and bounded scissor rectangle, then clears the
requested colour/depth/stencil aspects at the recorded position. Nine synthetic
D3D12 WARP cases check every pixel of all three aspects. All 32 checks at that checkpoint
pass. A native follow-up retains the shaded avatar, room and entered name.
This does not establish that later scenes render correctly. The shared depth
attachment and missing depth-texture sampling still need investigation.

## Scene render targets and descriptor ranges

Historical D3D12 checkpoint: patch 0032. The shared Vulkan renderer used by
the current build has separate depth targets; see
[shared rendering](#shared-renderer-and-colour-feedback).

A later WKC1 frame submits 22 offscreen colour targets, but the backend's
16-entry table omits six post-processing targets. An unavailable target then
falls back to the display buffer during replay. Patch 0032 raises the table
to 32 entries and reports capacity failures. It also separates the shader
descriptor ranges: texture uploads previously overlapped persistent-target
descriptors and the first draw windows.

The native follow-up verifies 22 submitted targets and 22 allocated targets,
with none missing. Startup, WKC1 avatar/name retention and the opening movie
still work. The paused background remains visually incorrect; restoring those
targets alone does not solve the scene. The capture also distinguishes the
1280x720 main zeta surface from a separate 2560x2560 depth-only shadow surface.
The backend still shares one 2048x2048 depth attachment across them and does
not sample the actual shadow depth texture. Those limitations need a separate
rendering fix. No new visual gallery milestone is claimed.

The Windows frame loop now waits on the existing FIFO kick event with a
four-millisecond timeout. Guest fence polling already signals that event;
the previous unconditional sleep left it unread. Presentation stays at guest
flip boundaries. This run verifies continued native presentation, without
establishing a frame-rate improvement. All 32 checks pass; the surface-clear
GPU test also passes with optimizations and `NDEBUG` enabled.

## Scene draw-record capacity

Historical D3D12 checkpoint: patch 0033 and 32 checks.

A WKC1 transition exhausted the backend's 2048 deferred records. The audit
continued receiving draw callbacks, including the final screen composite,
after the record table was full. Vertex-upload space remained available.
Patch 0033 raises the record capacity to 4096 and grows its constant buffers
accordingly. A normal native follow-up retains 2149 operations and the final
display draw; all 2246 callbacks are audited, with none omitted. The follow-up
also retains all 22 observed offscreen targets. The existing 32 checks pass.
This establishes that the later draw is retained, without establishing correct
world rendering. Gameplay and save/load remain unverified.

## Character-name input

Historical name-entry checkpoint: character creation was subsequently
completed and town reached. The Windows editor remains temporary; an
in-game keyboard usable entirely by gamepad is still planned.

Selecting Name reaches a keyboard stub that requires an initialization call
absent from WKC's imports. The stub also lacks completion events. The native
bridge now opens a Windows editor without that dependency. Its guest layouts,
UTF-16 fields and status values were checked against
[RPCS3's public OSK interface](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/cellOskDialog.h).
The implementation is independently written C++.

The game's private call path also contains support for continuous separate-window
entry through GetInputText after INPUT_ENTERED. The observed Name dialog uses
ordinary mode instead: FINISHED is followed by UnloadAsync returning the text.
Both lifecycles were checked against
[RPCS3's OSK lifecycle](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/cellOskDialog.cpp).
An interactive native run returned a four-unit name successfully through Unload,
but the in-game name field remained blank and Quick Start reported a missing
name. The subsequent UCS2stoUTF8s conversion had no registered handler.
`l10n.cpp` now supplies that conversion and UTF8stoUCS2s using big-endian guest
fields, exact input counts, output size queries and partial-conversion errors.
The interfaces were checked against
[RPCS3's L10n module](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/cellL10n.cpp).
Synthetic checks cover ASCII, accented and Japanese characters, embedded NUL,
capacity limits, invalid sequences and UCS-2 surrogate rejection. A new native
run now displays the entered name in the name field and avatar caption. Quick
Start passes the missing-name check and reaches the final Review page, retaining
the name there as well. Accept opens the start-game confirmation. This verifies
name retention and review navigation; completed character creation, gameplay and
save/load remain unverified at this milestone.

Synthetic checks cover the guest ABI, Unicode limits, callback order, stale
generations, cancellation, abort and scripted UTF-8 input. Separate tests of the
production Windows controls cover Unicode editing, OK, Cancel, early closure,
window reuse and shutdown. These checks do not establish completed character
creation, gameplay or saving.

## Shared renderer and colour feedback

The Windows Vulkan build uses the shared draw engine and batched
backend from Soulcalibur2HD-recomp patches 0005/0006, pinned in the port's patches
0035/0036. Native startup, title menus, WKC1 selection, avatar rendering and name
entry work. Its first character scene had a strong red tint while the UI remained
correct. Intermediate GPU readbacks narrowed the change to a post-processing pass:
the room had normal colours before a draw sampled the target it was updating.
That draw writes green and blue while preserving red. The engine excluded the
active target and sampled stale guest-memory bytes instead.

After inspecting `off_rt_snapshot` in the DemonsSoulsRecomp SDK fork, patch 0038
implements a separate pre-draw GPU copy through the shared backend interface.
A normal native run now shows the avatar, clothing and room without the red tint,
with no render-state overrides or debugger writes. The screenshot is an unchanged
GPU readback; it is not evidence of interactive gameplay or a completed renderer.
The implementations actually used, with pinned revisions, are linked in
[the build instructions](SUBSYSTEMS.md#windows-renderer).

The shared engine tracks separate depth targets and sampleable depth snapshots.
The existing D3D12 path still has its shared-depth limitations described earlier.
Neither API's full-game compatibility has been established. The shared engine's
Metal backend is useful for the intended Apple Silicon work, but this port has
not run on macOS yet. Vulkan is the primary Windows development and default
build path; D3D12 remains an optional comparison/fallback renderer. Linux is
another intended Vulkan target, with platform integration and testing outstanding.

A separate Windows PPU join race could exhaust the thread table after repeated
movie transitions: the guest-exit event precedes the host epilogue, which could
mark a joined or reused slot finished again. Patch 0037 waits for host termination
before freeing the slot. A controlled regression fails on the old code and passes
128 slot-reuse iterations on the fix. A native observation now has 34 free slots
instead of the earlier exhausted table. A separate SPU polling cutoff caused
the later loading stall described below; fixing the join race alone did not
resolve that transition.

## WKC1 town and SPU completion-label polling

The next native loading stall left the FIFO on a producer self-jump, with its
completion label one value short. Geometry jobs were repeatedly halted by the
SDK's automatic cutoff after 256 identical DMA transfers. The observed loop
validly reads a GPU completion label until command-buffer space becomes free;
repeated reads alone do not establish a stuck job.

Patch 0039 removes that default cutoff. An explicit positive
`SPU_DMA_REPEAT_LIMIT` remains available for diagnostics; absent or zero leaves
valid waits running. Counters are local to each host thread. A production-path
regression performs 1,024 reads, observes a producer update after read 768 and
checks completion tags. Forcing the old cutoff fails the regression. Separate
checks cover the absent setting and an explicit diagnostic cutoff.

With patches 0001-0039, native execution now passes the previous stall, reaches
the following live cutscene and enters the WKC1 town after normal cutscene-skip
inputs. Sixty-four sampled geometry-job dispatches from image 19 return without
the old halt. Town GPU readbacks show Leonard, NPC names, the minimap and a save
crystal. The pause menu opens and resumes through ordinary controller input.
The [2026-10-16 town capture](GALLERY.md#2026-10-16--wkc1-town-world-view) is an
unchanged world-frame readback, not prerecorded movie footage. No debugger writes or render-state overrides
were used to produce it.

That 39-patch route had conspicuous stretched geometry and corrupt NPC surfaces.
Movement and camera control were not checked in that first town run; the later
42-patch run verified short town movement and camera rotation. Patches 0052
and 0058 subsequently improved the NPC surfaces and removed the long strips
in inspected views, respectively. Sustained traversal,
combat and save/load remain unverified. Seeing a save crystal does not verify
saving. No performance improvement or full-game compatibility is claimed.

## Reviewed allocator suggestion and probe input

Historical checkpoint: 41 patches and 39 runtime checks.

Patch 0040 incorporates the suggestion branch's freed-range allocator fix.
The old allocator can report free memory but exhaust its virtual-address window
after two freed 96 MiB allocations. The Windows regression reproduces that
failure and verifies repeated allocation/free cycles, zeroed reused bytes,
alignment and gap filling on the fix.

Review also found that the proposed allocator could overlap mapper reservations
in the same window. The amended patch tracks those reserved pages and checks
mixed normal, mapper and shared-memory allocations. Those cases fail without
the amendment and pass with it. Mapper free behavior is still conservative;
this is not a complete mapper implementation. The suggestion branch's patch
0015 context correction also applies without changing reconstructed SDK code.

Patch 0041 adds finite analog-stick input to the private `PAD_FILE` probe,
with validation and restoration of underlying input after the hold. Its check
uses the actual guest packet interface. All 39 runtime checks pass with the
complete 41-patch stack, and reconstruction from the pinned SDK is verified.
That initial town run used patch 0039. The later census run below includes the
amended allocator and verifies the analog probe's town response.

## SPU fallback census and host-runtime review

Historical integration checkpoint: 42 patches and 42 runtime checks. The
six-fingerprint fallback remains enabled in the current build; this work
added measurement and recovery fixes, not complete static coverage.

The suggestion branch's census is incorporated as patch 0042, with Windows
follow-up changes. Mixed-line-ending context was rebuilt against the pinned
SDK. Thread-safe initialization replaces unsynchronized flag access, and
Windows snapshot replacement keeps the prior complete file instead of deleting
it before rename. Microstep counting uses its existing loop index rather than
an unconditional per-instruction TLS increment.

The public Windows build workflow passes all 42 checks. The new checks cover
profiling on/off and concurrent initialization. A separate production-dispatch
check verifies a microstep branch and its interpreter handoff without double
counting. Native results are recorded separately below; standalone tests do not
establish gameplay coverage. The goal is to prioritize static implementations
of the remaining fallback code. No fallback path has been removed.

The host-runtime review also produced two confirmed fixes. AC-3 fatal errors
still reject further input, but EndSeq can now drain and permit a fresh sequence;
stereo and surround tests force a single-AU contract error, then restart and
compare valid decoded samples. The demuxer now rejects AUs exceeding the actual
aligned arena capacity instead of waiting for an impossible free block. Both
regressions fail against the earlier code and pass on Windows with the fixes.
The pad-file test now has a POSIX environment-setting path; a Linux/macOS build
has not been run here. The proposed chroma-siting change is deferred pending an
independent pixel comparison. See [the review and disposition](CODE_REVIEW.md).

## Native census and limited town controls

Historical 42-patch run. Its retained census is evidence for that route;
later probes and the physics recheck are recorded below.

The full 42-patch native Vulkan run reaches the WKC1 town after avatar acceptance
and ordinary cutscene-skip inputs. Left-stick probes produce short movement,
facing/walking changes and a changed minimap position; right-stick input rotates
the camera. The [town-controls gallery entry](GALLERY.md#2026-10-17--town-movement-and-camera-response)
records this limited input milestone. Geometry and NPC surfaces were visibly
corrupt in this 42-patch run; later fixes are recorded below. Sustained traversal,
combat and save/load are unverified.

The retained census contains **2,017 completed sessions and 8,744 recorded
instructions across six rows**, all in image 17, with zero dropped sessions.
The snapshot remains parseable after stopping the process. It excludes lifted
instructions, unfinished/nonlocally exited sessions and the latest unwritten
updates; it does not establish a fallback percentage or interpreter-free execution.
The structured record and snapshot hash are in [rendering.json](../verification/rendering.json).

| Path | Entry | Sessions | Recorded instructions |
| --- | --- | --- | --- |
| interp | `0x07D68` | 672 | 2688 |
| interp | `0x07E4C` | 168 | 1848 |
| interp | `0x07E88` | 169 | 1352 |
| microstep | `0x07D60` | 672 | 1344 |
| microstep | `0x07E38` | 168 | 840 |
| microstep | `0x07E78` | 168 | 672 |

All six 64-byte entry fingerprints match the corresponding windows in the
baseline SPU image (text fingerprint `4FE4255D8907D8F3`). This points to missing
static entry coverage as a useful next investigation. The existing lifter's
three primary entry hints (`0x7D60`, `0x7E38`, `0x7E78`) produce a private
Windows-compilable candidate. Whole-function identity, behavioral equivalence
and native execution of that candidate remain unverified. It is not integrated
into this build, and the fallback remains enabled.

The initial 600-second probe ended before the town. Comparison with the earlier
successful run showed that this route can spend more than ten minutes before
the opening menus advance. A longer bounded run was required; the short runs
did not establish an allocator regression. All 42 patches, including reuse,
are present in the town run. Their in-game reuse behavior was not separately
measured. The patch stack also reconstructs consistently under both Git
line-ending settings without changing the resulting source after CRLF normalization.

## Second runtime review

Historical 51-patch comparison, before NV0039 support and the physics fix.
The unchanged target faults in these runs were subsequently improved by
patch 0052 and removed in the inspected strip case by patch 0058.

Reviewed source changes from `claude-suggestions` at `a54d99c` add patches
0045-0051. Windows Vulkan builds with clang-cl and passes all **48 project
checks**, including the new PPU-thread-lifetime and NV3089-copy tests, the
extended callback-stack, SPURS, draw-engine and title-selection tests, and
all three SPU float modes. No MinGW TLS workaround was needed. The separately
regenerated disc-input build also compiles with this runtime.

The default-float town run `20261017-040537` repeats short movement and camera
rotation. Leonard's stretched strip and corrupt NPC surfaces remained in both
the live opening scene and town. Applying these seven patches together did
not correct those faults in that run; this does not isolate each patch's
individual effect.

Repeating the town route with `RSX_ENGINE_ALL_SUBCHANNELS=1` (probe
`20261017-043029`, still using default float arithmetic) shows both faults
again. This qualitative comparison did not establish a correction of either
fault from patch 0051; it is not a pixel-identical output comparison or proof
about every use of the graphics state. That run's ten-second town trace
contains another 39 complete NV0039 sequences.

Patch 0045's native `audit` run (`20261017-045114`) and `exact` run
(`20261017-052058`) also reach town. Both still show the stretched strip and
corrupt NPC surfaces. Applying the thirteen SPU float instructions' exact
rules did not correct either target fault on this route; the default remains
host arithmetic. This is not proof that all floating-point differences are
harmless elsewhere.

The audit reports both rounding and special differences. Its largest logged
power-of-two counts include at least 8,589,934,592 differing FMA rounding
lanes, 2,147,483,648 FM special lanes and 16,777,216 CFLTU special lanes.
These are whole-process lower bounds, including non-graphics SPU work,
rather than final totals, graphics-only counts or a percentage of execution.
The process was stopped after validation, so the normal-exit summary did not
run. Full per-instruction lower bounds are in
[rendering.json](../verification/rendering.json).

A ten-second town trace using an empty private `GCM_SUBCH_GATE` file contains
**33 complete NV0039 copy sequences** on subchannel 1: DMA bindings, source and
destination offsets, pitches, byte and line counts, format, and copy triggers.
This confirms actual memory-to-memory copy use, beyond the ambiguous presence
of individual register addresses. That 51-patch runtime had no NV0039 transfer handler and sent these methods
into its 3D state. The subsequent implementation and native validation are
recorded below.

The run logs contain six inline-write messages (the diagnostic's cap), no
`[NV3089] SKIPPED` messages and no `[GCM2D-UNH]` messages. The latter absence
does not establish complete copy support: the subchannel-1 transfers above
silently take the 3D path. Full logs, command values and captures stay private.
Both short town traces contain subchannels 1, 3 and 5, with no methods on 6
or 7 in those windows. This does not rule out NV3089 activity before the
traced frames or elsewhere in the game.
The default-float run's fallback census retains the same six entry
fingerprints, with 2,221 completed sessions and 9,628 instructions in its
final snapshot. Fallback remains enabled. The four runs' checked aggregate
records and comparison-frame hashes are in
[rendering.json](../verification/rendering.json). Their captures show no
new correction of these faults, so the gallery retains its earlier town
image rather than adding another equivalent milestone.

## NV0039 memory transfers

Historical 52-patch milestone: NPC surfaces improved, but long strips were
still present. The later [physics fix](#secondary-bone-physics-and-town-stretching)
addresses those strips.

Patch 0052 implements the memory-to-memory copies confirmed by the earlier
town trace, and the WKC runner now selects the observed NV0039 binding on
subchannel 1. The walker consumes these methods before either 3D renderer.
This prevents copy offsets, pitches and formats from overwriting unrelated
NV4097 state. The SDK's default for other runners remains unchanged.

The Windows Vulkan build passes all **50 project tests with 52 SDK patches**.
The new FIFO regressions cover the 12/16-byte transfer shapes seen in the
earlier town trace,
overlap, zero/negative pitches, interleaved byte strides, noncontiguous MAIN
mappings, invalid ranges, reservation notifications and the 3D compatibility
override. The draw-engine regression also checks a partial-pixel byte write
preserves adjacent GPU-owned bytes. The stack applies to a clean pinned SDK,
upgrades prefix 51, and reapplies without changes.

The native WKC1 recheck (`20261017-061720`) reaches town using the clean-generated
input build, with the default SPU float mode and `GCM_M2MF_VERIFY=1`.
The log confirms at least **65,536 completed copies / 14,363,492 bytes**,
with no rejection or verification-mismatch diagnostics. These totals are
lower bounds from the diagnostic's power-of-two checkpoints. Between the
32,768 and 65,536 checkpoints, copies average about 420 bytes, so the run
also contains larger transfers than the traced 12/16-byte shapes; those
shapes are not individually logged or modelled by the regressions. The
verification re-reads each destination byte after writing it, which confirms
the writes landed at the computed addresses. It cannot detect wrong addressing
or transfer semantics, so it is a consistency check rather than a correctness
proof. A ten-second
town gate contains **47 NV0039 triggers**. Short movement and camera
response were rechecked, then the sole native process was stopped.

**At this 52-patch checkpoint, NPC surfaces improve while the stretched strip remains.**
Previously speckled/mosaic NPC surfaces look coherent in the inspected town
readbacks, including after short movement and camera rotation. This is a
qualitative comparison with the earlier 42/51-patch captures; full material
fidelity remains unverified. Patch 0052 combines copies and routing correction,
so their individual visual effects have not been isolated. The unchanged
GPU readback is in the [NV0039 gallery entry](GALLERY.md#2026-10-17--npc-surfaces-after-nv0039-transfers).
Aggregate results and the comparison-frame hash are in
[rendering.json](../verification/rendering.json). The selected
game-only readback is published; raw command values, game programs and logs
remain private.

Support covers non-notifying byte copies, signed/zero pitches, strides 1/2/4,
overlap staging, report-record sources and shared-renderer RGBA8 coherence.
Notification completion, other GPU layouts, transfers over 64 MiB and D3D12
GPU-owned surface copies remain limited or unverified. The report path exposes
the runtime's existing records without establishing accurate GPU queries.
See [build and diagnostic details](PATCHES.md#earlier-runtime-fixes-and-validation-history).

## Vertex-program co-issue review

Historical 44-patch checkpoint, preceding the second runtime review above.
Its shader tests passed, but these cases did not explain the long town strips;
the later [physics trace](#secondary-bone-physics-and-town-stretching) located
that fault before rendering.

Patch 0043 incorporates the vertex-program translation fixes reviewed from
`claude-suggestions` at `15fa006`. The instruction's scalar operand is captured
before the vector unit can overwrite its source temp or indexed-constant address.
The [XF ISA description](https://envytools.readthedocs.io/en/latest/hw/graph/xf/isa.html)
records source reads before the vector/scalar operations and result writes.
The scalar expression buffer also grows to avoid truncating long `LIT`
expressions; `snprintf` prevented a memory overwrite but emitted incomplete HLSL.

The full Windows workflow passes 43 checks with 44 SDK patches applied.
Independent negative controls fail for the old operand ordering and for the
smaller `LIT` buffer. Synthetic hardware vertex-shader checks produce the
expected values: the temp case changes from 0.125 to 0.25, and the indexed-address
case from 0.0625 to 0.125. A vector-only shader remains byte-identical. These
checks validate the specific translation cases. The native comparison below
checks their effect on the town faults.

Patch 0044 adds raw vertex-program words to the shared renderer's optional
private shader dump. The older `RSX_CAP_GEOMETRY` path covers D3D12, so enabling
it alone would not collect the Vulkan run's inputs. The scanner rejects empty
or malformed captures, preserves repeated filenames in separate frame folders,
and reports relative names. Co-issued condition-code predicate hazards remain
unfixed and are counted separately. Raw game programs and generated shaders
remain private; only checked aggregate results are published.

The 44-patch native Vulkan recheck reaches the town and repeats short movement,
facing/walking changes and camera rotation. Readbacks compared with the existing
[town-controls capture](GALLERY.md#2026-10-17--town-movement-and-camera-response)
still show the long stretched strip at Leonard and corrupt NPC surfaces. A live
cutscene also shows torn meshes. No correction of these faults was observed.

The captured route contains **65 distinct vertex programs, 3,927 encoded
instructions and 330 co-issued instructions**, with **zero temp, address or
condition hazards** and **no `LIT` instructions**. All 65 programs compile and
validate as Vulkan 1.0 SPIR-V with both translators. HLSL and SPIR-V differ for
36 programs; successful compilation does not prove behavioral equivalence.
These are static counts across the captured startup-to-town route, not execution
frequencies or proof about every shader in the game.

The patch addresses genuine translation faults, but these cases did not explain
the observed town corruption in the captured shaders. The resulting investigation
followed image 19 geometry/skinning outputs back to the omitted secondary-bone
physics job, fixed by patch 0058. Patch 0052 separately improved NPC surfaces.
The comparison frame hash and aggregate results for this earlier run are retained
in [rendering.json](../verification/rendering.json).

The SPU census remains at the same six entry fingerprints, with 1,957 completed
sessions and 8,484 recorded instructions in the retained snapshot. The original
census and its source-entry candidates remain recorded above; the fallback is
still enabled. Full gameplay, combat and save/load remain unverified.

## Cutscene red flash and frame rate

Patches 0053-0057, before the secondary-bone physics milestone. The frame-rate
figures below describe those measured builds and scenes; they are not a new
benchmark of the 58-patch physics build.

The cutscene after avatar acceptance repeatedly flashed a red-tinted close-up
of Leonard. An opt-in renderer trigger (`RSX_ENGINE_RED_TRIGGER`) captured the
draw list of each red frame automatically; two independent faults combine:

1. **Depth-only passes replaced a live surface.** The shadow pass sets
   `SET_SURFACE_COLOR_TARGET` to NONE and leaves colour offset 0, which is
   display buffer 0. The shared renderer bound a 2560x2560 colour target there,
   replacing the 1920x1080 display surface, so later flips of buffer 0
   presented a never-written image. Depth-only passes now draw into a private
   scratch target that aliases no guest memory (patch 0053). The
   `RSX_ENGINE_PRESENT_LOG` stale-present report drops to zero.
2. **Movie frames were sampled from a stale GPU copy.** The cutscene's
   prerendered segments are converted by `cellVpost` into a buffer the game had
   earlier used as a render target. The port wrote the converted R,G,B,A bytes
   to guest memory, but the renderer kept sampling its older GPU copy of that
   surface through the movie's `0x93` crossbar, which turned alpha into red.
   Host code now queues the written range
   (`rsx_draw_engine_note_host_write`), and the renderer reloads overlapping
   surfaces from guest memory on its own thread before the next draw.

A native run through the same cutscene captures no red frames, and the movie
segments show the expected image. Two new draw-engine regressions cover both
faults, and the `cellVpost` test checks the reported write range.

### Frame rate

A built-in sampling profiler (`WKC2_SAMPLE_PROFILE`, link map `/MAP`) and the
renderer's per-site GPU-sync timing located the main costs in the cutscene:

| Change | Cutscene fps |
| --- | --- |
| Debug build (baseline) | ~11 |
| Logging off the game thread | ~25 |
| Optimized `/O2` build with `/arch:AVX2` (hardware FMA for SPU arithmetic) | 25-31 in heavier shots |

- About a third of the main game thread's time was spent in `WriteFile`:
  diagnostics flush each line to a scanned log file. `WKC2_DISCARD_LOGS=1`
  sends both streams to NUL for play; research runs keep their logs. An
  in-process pipe relay was tried and dropped after it deadlocked a run.
- Without AVX2, every SPU multiply-add called the C runtime's software
  `fmaf`; `/arch:AVX2` makes it one instruction with the same result.
- Job-chain walkers polled job guards every 1 ms; they now sleep on a
  condition variable woken by the guard notify (patch 0054).
- Running faster exposed a pile-up during loading: image 4 loader workers spin
  on a lock line (writer count 1001) and starved its holder, peaking at ~160
  spinning threads and 0.1 fps. An SPU that re-reads an unchanged reservation
  line now yields its host time slice; loading completes with at most ~80
  threads (patch 0054). Capping running tasks at six and sleeping back-off
  were tried and rejected: both slowed or stalled progress.

The initial town measurement was about 7 fps with 1,400-1,800 draws per frame. The
renderer records a frame, submits it and waits for the GPU before the next
one, and reads two depth targets back each frame; overlapping frames is the
performance candidate considered at that checkpoint. Later profiling and
patch 0057's mid-frame changes are recorded below. The immediate target is
30 fps, followed by validated 60 fps support. These are qualitative native measurements with the diagnostics shown,
not a reproducible benchmark.

Subsequent town profiling showed the renderer was not the only limit: its
thread was about a quarter busy and the present GPU wait was 4-6 ms. The busiest thread runs SPURS job
chains, and the main game thread mostly waits on their results. Patch 0055
removes per-job host overhead: the job-image fingerprint (up to 67 KB hashed
per dispatch, a third of that thread's time) is cached per address behind a
sampled check, each job is built directly in its context's local store, and
FI no longer calls `ldexpf` (8 million lanes compared bit-identical in
`wkc2_spu_fp`). The town still runs at about 7 fps: the game, its jobs and
the renderer wait on one another, and the job chain runs jobs one at a time.
`RSX_VK_PRESENT_MODE=mailbox` did not change the frame rate, so vertical
blank is not the limit either.

Running a chain's independent jobs side by side was tried and set aside. The
Edge geometry chain keeps one output ring per SPU. Every host job reporting
SPU 0 corrupted that ring, and reporting SPUs 1-3 stalled the GPU on a
semaphore at the title screen, so that chain must stay serial. The one other
multi-SPU chain ran in parallel safely but did not change the town frame
rate. Patch 0056 instead cuts per-job setup: each job thread reuses one
context and clears only the local store earlier jobs could have written,
and the per-step launch check runs only on steps that can act. That removed
both costs from the profile, but the town stayed at 7-8 fps. The Edge
chain's thread now mostly waits: its jobs poll for output-ring space the GPU
frees late, and the renderer thread waits on a fence after each batch flush.

Patch 0057 removes the renderer's mid-frame waits. At about 2,400 draws a
town frame overflowed the 1024-set descriptor pool and the draw arenas, and
each overflow submitted the batch and waited for it: about 31 ms of a 130 ms
frame. A full batch is now submitted without waiting while recording goes on
in a second set of buffers, and the pool holds 8192 sets. Renderer blocking
fell to about 13 ms a frame (the end-of-frame wait), but the town only rose to
about 8 fps. No thread is saturated any more. The PPU, the job chains, the
renderer and the GPU each wait on the next. This checkpoint attributed much of
the Debug C runtime cost to copying/clearing; the later profile below identifies
repeated environment scans as a substantial part of that cost. The sampler's
first-stack-pointer heuristic is not a reliable full call stack.
`RSX_VK_FLUSH_FILE=<path>` reports flushes per frame by source line.

`WKC2_SKIP_INTRO=1` (port hook on the boot sequencer at 0x00325248) skips the
copyright and save notices and the publisher/middleware logos while keeping
the system-setup steps; the title appears about 30 s after launch.

## Window-title presentation timing

Patch 0065 adds a live counter to the Vulkan window title so performance can be
inspected during character rotation, alongside the visible response. The
65-patch Windows Vulkan build and its 59 regressions pass. Counter updates are verified
in the visible title menu and initial creator, including name entry and the Body
tab. The check used no sampling profiler and retained five-second GPU readbacks.
The desktop shortcut launches the same promoted binary. This verifies the title
display and shortcut target, not a quantified character-creator benchmark.
The exact scripted rotation gesture was not confidently verified; reported
rotation stutter remains unresolved. No rotation-smoothness fix is claimed.

The FPS value counts accepted Vulkan queue presentations over monotonic elapsed
time, with an update after approximately one second. The maximum interval in
milliseconds helps reveal long gaps that an average can hide. Neither value
establishes unique guest animation frames or exact display scanout. A stalled
renderer can leave the previous title visible until it resumes. See the
[implementation scope](PATCHES.md#vulkan-window-title-counter-patch-0065).

## Town performance after the physics fix

**Measurement correction (2026-10-18):** all profiled town benchmarks below,
from patches 0058 through 0064, ran with the native window hidden. The private
launcher omitted `-ShowWindow`; the window's hidden state was confirmed through
the Windows API. Their common condition permits the limited relative diagnostic
comparisons recorded here, but these are not visible-gameplay FPS measurements.
They do not establish character-creator or rotation smoothness. Short controls
and GPU readbacks elsewhere are untimed input/visual checks. Visible interactive
timing requires a separate run; no cause for the difference is established.

Patch 0059 caches launch-time diagnostic settings in the renderer and SPU job
dispatcher. Previously, disabled diagnostics repeatedly scanned the host process
environment: `RSX_DRAW_AUDIT` was queried for each RSX method, and six lookups
were performed for each SPURS job. The new per-thread caches retain presence
semantics, descriptor dump levels, selected fingerprint parsing and log budgets.
They do not change guest work or disable physics.

Two private hidden-window native runs compare the same initial WKC1 town view after dismissing
the party and Travelogue dialogs. No movement or camera input was sent during
the measured windows. NPCs continue moving, so this is a limited live-scene
comparison rather than an identical-workload benchmark.

| Build | First 60-second sample | Second 60-second sample |
| --- | --- | --- |
| 58 patches, physics enabled | 500 frames / 8.33 fps | 487 frames / 8.12 fps |
| 59 patches, diagnostic caches | 575 frames / 9.58 fps | 559 frames / 9.32 fps |

The combined averages are 8.22 and 9.45 fps, about a 15% improvement. Both builds
use `/O2 /Ob2 /arch:AVX2`, the Debug C runtime/ABI, Vulkan, logs sent to NUL,
the Windows sampling profiler and five-second GPU readbacks. Frame counts are
read independently from the host presentation counter. They are diagnostic
hidden-window measurements on one machine, not visible gameplay, a clean
profiler-off benchmark or evidence of
a steady 30 fps. The immediate target is 30 fps, followed by validated 60 fps
support. Aggregate counts and conditions are in
[performance.json](../verification/performance.json); raw profiles, game captures
and local paths stay private.

The fresh profile identifies real environment-scan work in `ucrtbased.dll`;
those repeated diagnostic lookups disappear with the caches. Earlier broad
attribution of that module's cost to memcpy/memset was too strong. The profiler
finds a possible caller by scanning stack words, which can find stale return
addresses; a full host-stack check is needed before blaming a specific call.
In particular, apparent SDL event-poll waits did not survive that check.

Two private, reversible host-wait experiments were rejected: removing the
four-millisecond idle wait reduced the 58-patch sample to 7.65 fps; a
one-millisecond wait with 59 patches measured 9.32 fps against 9.27 fps after
restoring four milliseconds. Neither establishes a useful gain. The published
runner keeps the four-millisecond event wait. No reference-register publications
occurred during these town samples, so shortening their publication delay has
no supporting evidence here.

The review of [Drakengard 3's frame code at 96df4c3](https://github.com/soyjxck/dod3-recomp/blob/96df4c364a20c306724a48886a43f8f14ce5bedd/src/main.cpp)
identified its separate limiter and wakeup mechanisms: `DOD3_FPS` replaces UE3's
`GetMaxTickRate`, while guest poll waits can wake the renderer and finish when a
GPU label changes. Its UE3 hook is not a WKC frame-rate patch. WKC's main thread
is observed polling label `0x48` in its own graphics wait at `0x00AEF65C`, with
50-microsecond sleeps, and waiting on job completion. Those dependencies need a
WKC-specific timing comparison before changing wait behavior. None of
Drakengard's FPS code has been imported in patch 0059.

All 51 Windows Vulkan regressions pass. Existing process fixtures verify that
six presence switches still emit diagnostics when set to `0`, with unchanged
normal output. The full 59-patch stack applies cleanly; upgrading a 58-patch
checkout changes four files, and reapplication changes none. The native build
reaches town, responds to short movement and camera input, and retains normal
hair/clothing geometry in inspected views. Sustained traversal, combat, save/load
and full rendering fidelity remain unverified.

### Conversion scales and MFC ownership

The next profile/disassembly check found CRT power-of-two calls in compiled
SPU integer-to-float conversions, including a constant `CSFLT` scale of
`2^-12`. Patch 0060 builds these exact binary64 scales from exponent bits,
retaining the previous arithmetic, saturation and NaN handling. Every valid
RI8 scale is a normal double. The extended arithmetic regression compares
4,308,992 lane results across all 256 immediates, edge cases and random inputs
with zero differences from the previous host expressions. This preserves the
existing host behavior; it does not establish complete SPU accuracy.

Each build below has two one-minute hidden-window starting-town samples with
the same profiling/readback conditions as above and no movement or camera input:

| Build | First 60-second window | Second 60-second window |
| --- | --- | --- |
| 60 patches, optimized Debug CRT (`/MDd`) | 601 frames / 10.02 fps | 595 frames / 9.92 fps |
| 60 patches, isolated Release CRT (`/MD`) | 605 frames / 10.08 fps | 584 frames / 9.73 fps |
| 61 patches, context-owned MFC (`/MD`) | 786 frames / 13.10 fps | 689 frames / 11.48 fps |

The 60-patch Debug-CRT average is 9.97 fps, about 5% above the previous 59-patch
samples. Live NPC work varies, so this is a limited diagnostic comparison.
Rebuilding matching dependencies and the host runtime for Release CRT gives
9.91 fps and shows no meaningful town gain. Both builds use `/O2 /Ob2 /arch:AVX2`;
all 51 Windows checks pass and native regression assertions remain active.
The public build workflow retains its existing Debug CRT configuration.

The next bottleneck was repeated MFC engine lookup misses. Read-only inspection
of the 60-patch town process found all eight registry slots occupied by earlier
contexts, while subsequent jobs used a shared fallback engine. Each miss took
the global claim lock; unrelated contexts could also share DMA queues and tag
completion state. Patch 0061 embeds that state in each SPU context, removing
the fixed logical-context limit and global lookup. The engine survives a
sequential host-thread handoff and resets with its context, including the
selective SPURS job reset. One context still requires one executing owner.

The extended DMA regression isolates stalled lists across twelve contexts,
resumes real transfers, reuses the same context address and hands a context
to another host thread. The isolation assertion fails against patch 0060
and passes with 0061. A separate twelve-context concurrent SDK fixture passes
1,193 checks over 32 rounds. All 51 Windows Vulkan checks pass in normal Debug
and optimized Release-CRT builds. The host context layout changes, requiring
every SPU translation unit to be rebuilt. The patch stack upgrades from 60
with six files changed and reapplies with no changes. A clean SDK checkout
accepts all 61 patches (58 files updated).

The native 61-patch Release-CRT run reaches the initial WKC1 town view.
Its two hidden-window one-minute samples average 12.29 fps combined. Short movement and
camera rotation respond afterward, with the long hair/clothing strips absent
in the inspected views. Full rendering/physics fidelity and sustained gameplay
remain unverified. The sampler and five-second readbacks remain enabled; these
are hidden-window diagnostic samples, not a visible or profiler-off gameplay benchmark.

### Producer wakes and SPU helper overhead

The next experiment wakes the renderer when completed mapped SPU DMA overlaps
the exact self-JUMP command on which the FIFO walker is parked. It preserves
the four-millisecond bounded idle wait, guest clocks, command consumption and
completion labels. The initial, unfiltered 62-patch candidate produced about
600 overlap signals per presented town frame. Its two one-minute windows
presented 791 and 705 frames, averaging 12.47 fps combined, versus 12.29 fps
for patch 0061. With moving NPCs and these profiling conditions, this does not
establish a convincing improvement.

An overlapping copy can leave the self-JUMP unchanged, so overlap alone is
insufficient evidence that the walker can advance. The final patch 0062
publishes the parked address and expected command together in one atomic
descriptor and signals only when a completed overlapping PUT sees a changed
word. Registration rereads the command, and both sides retain the memory
ordering needed to avoid a missed producer update. The notification precedes
the existing block-coalescing early return. Optional aggregate counters record
registrations, changed-word signals, immediate retries and unchanged overlaps;
unset profiling leaves them zero. Guest command reads retain the runtime's
existing VM concurrency model. Atomic SPU commits and PPU writers outside this
ordinary DMA hook retain the bounded-timeout fallback.

The synthetic production-FIFO regression checks unchanged copies staying
quiet, actual replacement copies waking the walker, coalesced transfers,
updates before registration and between registration and reread, disabled
kicks and unset profiling. GET and the completion label advance only when the
walker consumes the replacement commands. Removing only the changed-word
filter makes the unchanged-copy assertion fail. The unfiltered native samples
above do not validate the filtered version's performance.

Patch 0063 replaces `CNTB`'s per-byte scalar population-count loop with SSE2
integer operations on supported x86 hosts, retaining the scalar fallback
elsewhere. An independent fixed-eight-bit reference checks 1,114,112 byte
results with no differences, including every byte value at every position and
deterministic random vectors. Both baseline SSE2 and AVX2 builds pass. Eight
private image-19 partial job replays produce 176 byte-identical pairs of
local-store and register snapshots. Instrumenting one replay confirms that
the helper actually executes 33 times. These replays stop before GPU output
allocation; they verify the sampled intermediate results, not a complete job
pipeline or a native frame-rate gain.

Patch 0064 moves the host-recursion guard's depth-limit cache, recent-site
history and report budget into host-thread-local storage. This removes shared
diagnostic writes from each guard call and keeps concurrent workers' histories
separate. The default limit of 2000, environment parsing and every threshold
halt remain unchanged; reports are now limited once per executing host thread.
The history follows that host thread when it runs different contexts, while
the checked `host_depth` remains context state. A deterministic two-worker
regression checks separate histories, just-below-limit execution, repeated
halts at the limit, and default, hexadecimal, zero and invalid settings. The
unmodified guard fails the mixed-history assertion.

All 59 Windows Vulkan regressions pass in normal Debug and optimized
Release-CRT builds, including the three producer-wake cases, four recursion
guard cases and the `CNTB` regression.

The native 64-patch Release-CRT build reaches the same initial town view.
Two hidden-window one-minute samples present 785 and 751 frames (13.08 and 12.52 fps),
averaging 12.80 fps combined. The counters report 0 unchanged
overlaps suppressed and 940,821 successful changed-word signals.
No unchanged overlaps were observed, so these samples do not support
false-wake suppression as the cause of a timing difference.
These are separate hidden-window live NPC samples with the sampler, producer
counters and five-second GPU readbacks enabled; they do not measure visible gameplay. The combined run does not isolate each
patch's contribution. Short movement and camera rotation respond afterward;
the long hair/clothing strips remain absent in inspected views. Full rendering
fidelity, sustained gameplay and a steady 30 fps remain unverified.

### Next CPU target from the full sampler data

The short profiler report retains only forty instruction addresses per thread.
Reading the sampler's full current-window maps through a private read-only tool
reveals dispersed costs that this ranking hides. Actual executable instruction
pointers are grouped by their PDB symbols; guessed callers from DLL stack scans
are excluded. Two short town snapshots put image 19's skinning routine at
`0xCE08` at about 20-24% of one critical worker's executable samples. This is
neither a share of whole-frame time nor a whole-process CPU percentage.

Its generated body contains frequent register-state moves, stack spills and
byte shuffles alongside the matrix arithmetic. The next investigation is
register lifetimes and selecting arithmetic mode outside the inner work,
while retaining the existing exact/audit paths. A replacement for this body
has not been implemented or validated. Raw maps, generated source, process
identifiers and local paths remain private.

## Secondary-bone physics and town stretching

Tracing the stretched vertices back through skinning located the bad values
before rendering. The SPU geometry job correctly skins the supplied vertices;
its input bone palette already contains large translations. The PPU palette
builder also matches an independent matrix multiplication of its inputs.

A consistent guest-memory snapshot and private replay of the secondary-bone
update isolate the source: the physics getter returns identity matrices for
enabled bones. Converting these from world space to model space introduces
the inverse character position, stretching the affected vertices across town.
The matrix operations themselves are consistent with their inputs.

The physics update submits a 21,408-byte SPU job through
`_cellSpursJobQueuePortCopyPushBody` (NID `46C3FB5A`). That import, queue
creation and synchronization were unresolved. The runtime returned success
without running the physics job, leaving its initialized output matrices in
place. Patch 0058 adds the missing queue/port path and a defined CRT manager
handoff; the raw job is now extracted from the baseline executable and
statically compiled as image 24, retaining the earlier image IDs.

Running the captured physics job through the native runner returns to the job
manager with result zero. The affected getter outputs change from identity
to world transforms, and the subsequent model-space conversion returns to
normal bone offsets. This private replay establishes the missing operation's
effect on sampled bones; native town verification is recorded separately.

The native 58-patch Vulkan run (`20261017-202420`) reaches the same WKC1
starting-town view with the long strips removed. Hair and coat stay attached
to Leonard after short analog movement and camera rotation. Two inspected
GPU readbacks are published at the top of the [gallery](GALLERY.md).

Private replay of 128 captured image-19 geometry jobs completes successfully.
Using the actual index buffers to exclude padding, none has a referenced
position component above 100 model-space units; the maximum is 17.19.
The preceding 128-job town sample had 21 jobs above that diagnostic threshold,
with a maximum of 1644.82. These are samples from separate animated moments,
not identical-input or whole-game comparisons. Full physics and rendering
fidelity, sustained traversal, combat and save/load remain unverified.

The full Windows Vulkan build passes all 51 runtime checks with 58 patches.
The new queue regression tests descriptor ownership, ordered execution, output
visibility, bounds and persistent missing-image/staging errors. The existing
SPURS ABI fixture now verifies initialized manager storage across repeated
jobs and multiple descriptor sizes. Patch application from the 57-patch
prefix updates five SDK files; reapplication changes none.

These inferred roles are retained for future source reconstruction. The
addresses apply to the pinned USA 01.00 executable; names describe observed
behavior and are not recovered original symbols.

| Address | Observed role | Evidence |
| --- | --- | --- |
| PPU `0015D9EC` | Apply secondary-bone physics results to the posed skeleton | Consistent snapshot replay through getter, coordinate conversion and final pose writes |
| PPU `0019F2C4` | Fetch a mapped physics bone's world matrix | Copies the selected matrix and returns its availability |
| PPU `00A7B0EC` | Build skinning palettes from posed bones and inverse bind matrices | Independent multiplication matches sampled outputs |
| PPU `00B0FA18` | Copy pose data | Source and destination tracing; ordinary vector-copy loop |
| SPU image 19 | Geometry decoding and skinning | Decoded inputs are normal; skinning consumes the already bad palette |
| SPU image 24 | Secondary-bone physics job | Queue caller, descriptor staging and observed matrix output writes |

Generated game code, memory snapshots and raw captures remain private.

## Frame pacing and game speed

Earlier creator counter readings around 37-60 counted host presentations,
including frames with repeated animation state. They did not establish 60
unique animation updates per second or complete creator smoothness. The earlier
avatar-turn comparisons are limited observations, not proof of full game-speed
or RPCS3 equivalence. Patch 0070 makes the counter's unit explicit: `Present/s`.

Vblanks now use the high-resolution host counter at 59.94 Hz, and pending flips
wait for a vblank. Patch 0068 adds a further required distinction: resetting
flip polling status does not itself request a display or justify a completion
callback. An actual consumed FIFO flip marker (`FEAD`) or immediate display
request must exist before the timer can deliver a completion.

The candidate also enables `GCM_SEMA_ACQUIRE=1`, honoring the game's own
semaphore gate. The guest releases that gate after two vblanks; the creator
candidate consequently presents around 30 times per second. This is guest
hardware-style pacing, with no host 30 fps limiter or division of the displayed
counter. Presentation rate still does not prove distinct animation updates or
rotation smoothness.

Patch 0066 avoids depth readbacks for supported GPU depth snapshots. Patch 0067
corrects its opt-in depth-byte compute path: explicit descriptor bindings 0/1,
the high-byte mask at the depth-1.0 rounding edge and staging-buffer reuse order.
A private RTX 3090 test compares all 16,777,216 D24 depths against the CPU result
with `TEX_RGBA=0` and `TEX_RGBA=1`; both modes have zero mismatches. The later
73-patch D32 snapshot path repeats this exhaustive comparison on both an RTX 3090
and an Intel UHD 770, with zero mismatches in all four device/mode runs. Other
devices and full native depth-of-field fidelity remain unverified. Patch 0069
also keeps disabled PPU memory-watch diagnostics off the hot path.

The 70-patch normal Windows Vulkan build passed all 61 tests in 25.52 seconds.
Its new regressions check production compute/graphics descriptor bindings and
real flip completion, including the guest's two-vblank acquire. The earlier
73-patch stack passed all 63 tests: 22.79 seconds in normal Debug and
20.46 seconds in optimized Release. The historical 76-patch stack passed all 64 in
**25.31 seconds (normal Debug)** and **22.91 seconds (optimized Release)**.
The 73-patch suite added worker-lifetime and depth-comparison shader tests;
patch 0074 extends the latter with all comparison-direction mappings. Patch
0076 adds the Vulkan arena reservation regression.
Reapplying the earlier 73-patch stack changed no SDK files.

A separate **visible-window 66-patch baseline** reaches the starting town after
the party and Travelogue dialogs are dismissed. It records 737 guest-boundary
presentations in 60.000045 seconds: **12.283324 presentations/s**. The 2 ms
sampling profiler and five-second readbacks were enabled, so this remains a
diagnostic sample. Earlier patch-58-64 timing records used a hidden window and
are not a directly matched visible-play comparison.

The visible 70-patch run uses the same starting view with `GCM_SEMA_ACQUIRE=1`
and `RSX_ENGINE_ZTEX_GPU=1`. Its first quiet minute presents 994 frames in
60.000396 seconds: **16.566557 presentations/s**, about 34.9% above the 66-patch
sample. The next minute falls to 759 frames in 60.000218 seconds:
**12.649954 presentations/s**. With no movement input, it later falls to about
7 presentations/s. These combined changes have not established a sustained
gain, and the first minute must not be presented as stable town performance.

A read-only thread census near the end finds 2,702 live host threads, of which
2,636 start in the asynchronous SPU worker entry. This accumulation is an
observed runtime fault, separate from a static low frame rate. Patch 0072 adds
explicit ownership before host-thread creation and retains it through runner
and stack cleanup. A post-fix native soak is required before calling the
progressive slowdown fixed across wider gameplay. In the later ten-minute
73-patch town test, worker accumulation is absent. Raw thread records and host
addresses remain private.

The user reports that the selected cutscenes are now in sync, while the shadow
at Leonard's feet still flickers. This is useful route-specific evidence, not
proof of complete in-engine voice synchronization. The audio device consumes
samples on its own clock; the PAMF decoder comparisons do not establish full
real-time mixer or scene timing fidelity.

Patch 0071 also fixes stale depth snapshots after their source is rewritten and
another depth target is bound. Snapshot reuse now requires the source generation
to match for every sampled depth target. Four synthetic regression assertions
fail without the change and pass with it. The later 73-patch native run includes this change but
still shows foot-shadow faults; this snapshot change alone is not a verified
shadow fix.

Patch 0072 keys asynchronous task workers by their guest taskset and slot, and
workload workers by their SPURS owner and workload ID. Claims occur before
host-thread creation; repeated submissions for an active owner coalesce instead
of creating another worker. Generation/claim tickets keep stale cleanup from
retiring a later task. The registry regression exercises 16 competing threads,
10,000 repeated active submissions, reuse and duplicate cleanup. The production
dispatch regression also checks claim order and delayed cleanup. Moving a claim
after thread creation or allowing stale cleanup fails its negative controls.
Reusing or removing an active owner returns `BUSY`; cancellation and arbitrary
unowned diagnostic dispatch are outside that ownership guarantee.

Patch 0073 uses native depth-comparison sampling for supported single-level 2D
depth textures, preserving the guest comparison function. Comparison happens
before linear filtering, as required for percentage-closer filtering (PCF),
rather than comparing one already-filtered depth. Projective sampling divides
the reference depth and coordinates by w. D32 depth snapshots and separate
ordinary/comparison bindings support shaders that use both kinds of sampler.
Multilevel and cube textures, and backends without comparison capability, retain
their existing path.

The public `wkc2_shadow_compare` test checks actual SPIR-V from synthetic
TEX/TXP/TXB/TXL instructions, mixed sampler bindings, coordinate scaling and
legacy compatibility. The initial private production Vulkan fixtures pass
37/37 checks on each of the RTX 3090 and Intel UHD 770, but assume the wrong
operand direction for ordered comparisons. The native 73-patch town run exposes
the resulting bright foot-shadow regression. Those initial comparison results
are superseded by the corrected patch-0074 oracle below. An engine-only negative
control independently fails three assertions when the old sampled-unit parser
is restored.

The **73-patch ten-minute idle test** presents 8,552 frames in 600.000104 seconds:
**14.253331 presentations/s**. Host thread count stays at 58-59; asynchronous
workers start and end the interval with one active, with sampled active counts
of 1-2 and a process-lifetime peak of 3. It completes 17,104 task runs during
the interval. Private committed bytes are 1,462,337,536 at the start and
1,460,867,072 at the end. No worker accumulation is observed in that tested
window. This does not establish cancellation, later-game lifetime correctness
or a steady 30 fps.

This visible run has the sampling profiler disabled, unlike the 66/70-patch
timing samples. Five-second GPU readbacks remain enabled, and a 24-frame capture
burst occurs within the interval. Its average includes that capture work; it is
not a pure performance benchmark or a matched code-only speedup comparison.

Patch 0074 corrects **texture-comparison operand order**. RSX compares the stored
texel depth against the reference, while a host comparison sampler compares the
reference against the stored texel. The ordered functions must be reversed when
creating that sampler; equality functions retain their direction. The change
uses a texture-specific helper, leaving framebuffer depth/stencil tests unchanged.
This distinction was checked against the actually used
[RPCS3 Vulkan texture-sampler call](https://github.com/RPCS3/rpcs3/blob/e38d27157e3e683da4341bf530bdbbd41eb9239c/rpcs3/Emu/RSX/VK/VKDraw.cpp#L409),
which requests the reversed comparison direction for texture ZFUNC.

The corrected private production GPU oracle passes **37/37 checks on each of
the RTX 3090 and Intel UHD 770**. The preceding 73-patch sampler fails 14 checks
on each device under that oracle. The public regression also checks all eight
comparison mappings. Both full 74-patch Windows suites pass all 63 checks.
The user confirms normal shadows in the 74-patch native town: the bright/inverted
shadow is gone. Black flicker at Leonard's feet remains unresolved. This confirms
the polarity correction in this view, not complete shadow or rendering fidelity.

## Update 1.01 and DLC integration

Planned scope includes the USA 1.01 update and matching DLC. Local package
inspection confirms update TITLE_ID BLUS30784 and APP_VER 01.01. It contains a
replacement executable, patch settings and an approximately 199 MB patch archive.
The USA DLC metadata also targets BLUS30784; a separate European package targets
BCES01085. Its regional differences are part of the planned DLC investigation.

A byte comparison of the supplied DLC packages finds 21 USA entries and 22 EU
entries under `USRDIR/cm`. The EU-only entry is `c1267f66.edat` (320 bytes).
Nine shared files are byte-identical; twelve differ. Package-level differences
can include licensing/container metadata, so this does not yet identify an extra
item, quest or other playable feature. Determine what the extra entry unlocks,
compare the differing shared entries, and test any relevant content against the
USA build before claiming compatibility.

The Japanese game, DLC and ten update packages are available for future
investigation. The DLC metadata identifies BCJS30042; update filenames indicate
a chain from 1.00 to 1.15. Updated executables and content have not been validated.
The long-term goal is to integrate all Japanese-exclusive content with English
translations, plus optional Japanese voiceovers. This requires identifying the
regional differences, translating relevant text and validating integration and
save compatibility. It does not change the immediate priority of baseline 1.00
3D rendering and gameplay. Files and local source locations remain private.

Later update/DLC work will analyze the updated executable, integrate the patch data and
DLC discovery, and validate content access plus gameplay/save compatibility.
The current baseline remains disc v01.00. Neither 1.01 execution nor DLC content
has been verified in the native port. Package bytes and local source locations
are not published.

## Investigation history

This narrative log was previously kept in the README. It records the order in
which problems were found and fixed; its counts and failure descriptions refer
to those earlier checkpoints. The verified table and physics recheck above
hold the current status.

### Progress summary

The title menu now renders with New Game, Continue and Options over an animated
background. LPCM startup logos and AC-3 title/attract movies play through the native
movie pipeline. See the [newest-first gallery](GALLERY.md).
Controller input opens New Game and selects White Knight Chronicles I. Description
and dialog text now render correctly. WKC1 loading passes the guard-registration
failure and dispatches previously skipped geometry jobs. A native binding for the
standard SPURS processor-ID query prevents those jobs from reading executable
bytes as kernel data. Loading advances to a further animation program, now lifted
and observed dispatching and returning. Fixing vertex-shader cache identities now
reveals the **Customize Avatar** screen. Correcting indexed triangle strips and
primitive restart now renders the **3D room with normal depth testing**. Some
avatar pieces appeared first; correcting SPU extended-range FNMS arithmetic now
reveals the **avatar body geometry**, including arms, hands, trousers and boots.
Correcting packed vertex normals and fragment source-range modifiers now also
restores **face shading and textured clothing**. Native text entry and character-name
conversion now retain the name through Quick Start to the **final avatar Review**.
Avatar confirmation now enters the **WKC1 opening movie**; completing GPU batches
at guest flip boundaries prevents presentation from stalling at that transition.
Later Vulkan runs reached the town, verified pause/resume and short movement
and camera response, and exposed geometry and NPC material faults. NV0039
support then improved the inspected NPC surfaces. Patch 0058 now executes the
missing secondary-bone physics job, removing the long hair/clothing strips in
the inspected town views, including after movement and camera rotation.
Sustained traversal, combat and save/load remain unverified.

Vulkan is the primary Windows renderer. It now renders the avatar and room
without its initial red tint after correcting colour-target feedback. D3D12
remains available as an optional comparison/fallback build. Both use the
host-compiled hybrid build qualified above. Full-game rendering remains unverified.

### Runtime investigation

The first native run spun on invalid FIOS conditions. `lwcond.cpp` corrects the
8-byte guest layout and implements waits/signals with portable C++ synchronization.
Its regression test checks adjacent memory, timeout/reacquisition and a real wakeup.

The runtime patches fix Windows offsets above 2 GB, reject partial SDAT caches,
avoid duplicate synthetic completions after real SPU events, and make job-chain
shutdown/join wait for actual walker termination. They also implement syscall
350 (allocation from a memory container), track container capacity and frees,
correct the container-size query ABI, and accept heap-backed SPU job binaries
instead of rejecting every binary above 0x10000000. Changes stay in `patches/` and
are applied idempotently by the build script.

`tools/prepare_sdat.py` prepares the three large disc archives with accelerated
AES and authenticates every block. It writes atomic caches beside the extracted
files; the original ISO is unchanged.

The pool-28 allocation failure is resolved: the runtime previously acknowledged
syscall 350 without allocating its requested 24 MB. The settings file now loads
and initialization advances. Both the lightweight-condition and memory-container
regression tests pass.

`lfqueue.cpp` implements import 0x8A85674D (`_cellSpursLFQueuePushBody`) and the
guest queue initializer used by WKC2. It serializes producers with the SPU atomic
line lock, publishes copied ring entries, and signals registered task waiters.
The queue regression test covers capacity, sequence wrap, waiter routing and
guest layout. All three regression tests pass.

Eight real asset blocks decoded by the lifted SPU match independent Python zlib
raw-DEFLATE output byte for byte (256 bytes and seven 64 KB blocks). The hashes
and validation method are in `decompression-verification.json`; private game
bytes remain under `work/`. Temporary SPU capture instrumentation was removed.

The heap-binary fix exposed two raw jobs (fingerprints
687D4BB18564813A, 54,544 bytes; 164B23483A68CBEA, 1,968 bytes). Both have now
been lifted and execute natively. The renderer worker passes its previous wait.
Those jobs and four later discoveries have now been located byte-for-byte in
the baseline executable and added to `tools/extract_raw_jobs.py`. They no longer
require runtime captures for regeneration. `tools/import_spu_misses.py` validates
newly dumped job fingerprints during further scene research. All original game
bytes and generated game code remain in the private workspace.

The D3D12 fragment path now binds all 16 RSX texture/sampler units. WKC2's UI
shader uses units 4 and 7; the old four-unit root signature rejected its pipeline.
It also uses the current buffered-constant decompiler API, snapshots exact inline
constant bits for each draw, honors the export-width control, and uses the API's
cubemap, alpha-test and unnormalized-coordinate handling. The shader cache includes
the static alpha comparison and coordinate dimensions. The old screen-size texture
substitution heuristic is opt-in (`SCREEN_AS_TEX=1`); it replaced real render-target
textures with a black previous-frame copy during WKC2 startup.

The runner consumes each pending flip once, removing a duplicate presentation
path. A 120-second probe observed 6,171 guest flips and 6,171 presentation calls.
GPU readback, rather than those counters, establishes the visible UI milestone.
Original GPU captures are retained in the private workspace. The gallery publishes
lossless PNG versions with optional metadata removed, alongside verification notes.

Queue creation publishes its host wait primitives before the queue becomes active,
and SPURS completion sends hold table ownership through enqueue, avoiding the
observed send-versus-create crash. A fourth regression test concurrently sends
while creating/destroying the same queue ID 2,000 times, then checks capacity and
payload order.

The FIFO walker preserves self-jumps while the SPU command producer fills its
segment. Previously it skipped to PUT and discarded commands and completion labels;
that caused a graphics-fence stall after language selection. Intentional producer
and semaphore waits are excluded from the stalled-GET recovery heuristic. SPURS
guard waits now last until notification or shutdown instead of killing an idle
chain after 20 seconds.

The later startup stall was traced to a dropped installation request: a background
check was still pending when the request arrived. `tools/apply_port_hooks.py`
installs synchronization before the original guest request. The guest then performs
the actual installation and starts loading the first logo movie. All three large
installed archives (4,364,470,879 bytes total) match their disc data by SHA-256;
see `verification/installation.json`.

The nine imported PAMF reader APIs now use the guest ABI, big-endian output and
actual stream descriptors. A fifth regression check covers synthetic video/audio
headers, 64-bit sizes, copied readers and invalid input. An optional private fixture
parses the first logo's two streams and 1280x720 video. Native startup now passes
reader initialization and reaches movie demux/decoder threads.

`dmux.cpp` now supplies memory queries, real MPEG program-stream/access-unit
parsing, guest callbacks, AU lifetimes and queue backpressure. Independent decoding
matches every pixel in 3,806 extracted video frames across the logo and opening
movie; extracted AC-3 also decodes. See [verification](../verification/movie-demux.json).
The native guest receives actual video and LPCM units through its decoder calls.
Callback stacks are isolated across host threads and nesting.

The imported video-postprocessor interface now has correct guest fields and actual
progressive YUV conversion, cropping and bilinear resizing. The build enables the
SDK's FFmpeg decoder with pinned 8.1 headers and a separately supplied matching
library. A 210-frame decoder check matches every frame through the guest video API
on two sequence passes, including reordering and corruption/drop negative controls.
Twenty checks passed at this checkpoint. The title screen and its menu rendered
natively; the current suite has 64 passing checks.

Packed A4R4G4B4 font textures were excluded by the D3D12 binding filter, leaving
previous artwork selected for text draws. The backend now records the current
texture, decodes through the shared RSX format/row-pitch path and creates the SRV
with the uploaded resource's format. New Game description and confirmation text
are readable. The suite also runs the SDK's texture layout, packed-format and
component-remap checks.

White Knight Chronicles I is the starting point for gameplay validation. The old
32-entry guard table filled while the game created several guards per live chain.
An unknown guard then let an unprepared job run. Guards now use allocated entries,
notifications/reset/waits are synchronized, and joining releases their owning
chain's entries. Unknown guards stop before dispatch. Regression checks cover
100 chain lifetimes and 128 simultaneous guards. The native run passes that
failure. A further 11,856-byte SPU program has been statically translated, and
eight targeted native dispatches return successfully. Ordered job-list execution
also reaches geometry work previously omitted by the SDK. The standard SPURS
processor-ID query now reads native handoff metadata, preserving job code at the
kernel's original local-store address. Loading advances to another 16,912-byte
animation program; eight dispatch/return pairs have been observed after lifting
it. FIFO consumption continues through a later timeout. A subsequent vertex-shader
cache fix revealed avatar-customization menus. At this checkpoint, the model
was still missing. Later SPU arithmetic and renderer fixes restored the avatar.
The historical 76-patch build loads the verified save into town; the earlier
64-patch checkpoint records a short movement and camera recheck, with the long
secondary-bone strips removed in inspected views. The 70-patch idle run exposes progressive slowdown; the 73-patch
ten-minute idle test does not reproduce its accumulating SPU workers. Full rendering
fidelity, broader gameplay, wider save compatibility and imports needed on untested
paths remain unverified. Starting-town local save/reload was verified at patch 0075 and used again for patch 0076.
