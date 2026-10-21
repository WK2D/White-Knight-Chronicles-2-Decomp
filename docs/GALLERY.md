# Native port progress gallery

Screenshots captured from native Windows x86-64 execution with D3D12 or Vulkan,
using USA BLUS30784 v01.00. These are game-frame GPU readbacks, losslessly encoded
as PNG. All optional metadata has been removed. Images contain the game frame only.

These captures come from a host-compiled recompilation build with a PS3 runtime
layer. Earlier runs observed SPU interpreter fallback; newer strict routes have
zero entries. Neither establishes full-game interpreter-free execution; see
[architecture](ARCHITECTURE.md).

Current source: 130 SDK patches / 120 Windows checks in normal Debug and optimized
Release. The newest capture shows the crowded town street after pipelined
geometry jobs and faster SPU transfers, with the FPS counter on: 21.3 FPS in
an unlocked DirectX run, where the last capture on this street read 8.5 at a
30 FPS cap. The street still lags. Earlier captures show the connected
geometry and visibility C++ runtime and authored extended negative-product
arithmetic; their diagnostic routes verify visibility modes and palette rows.
A classifier-only A/B improves stationary views; the later arithmetic run has
an unpaired 13.164 FPS sample and still stutters. Earlier captures follow the complete supported
C++ skinning routine on a strict DirectX town-to-plains route. Earlier captures
show leaf-shaped shadow cutouts after correcting
alpha-reference formats. These verify inspected frames, not performance gains.
Complete replacement of every game SPU job remains the target; no complete image
has been replaced yet. The opt-in supported whole-job runtime is documented in
[native work](NATIVE_CODE.md#opt-in-live-geometry-job); these screenshots
do not establish its performance or wider layout coverage.
The earlier captures show
the DirectX near/far clipping fix in Balastor Plain after a town transition,
walking and camera rotation, plus the earlier broken view. The foreground no
longer incorrectly covers Leonard's lower body in these inspected views.
Full rendering, shadow/flicker and performance fidelity remain open. Static SPU
coverage comes from strict entry guards and state replays, not these screenshots.
Fresh DirectX portrait capture is verified; same-session menu/reload remains open.
Earlier town captures use the native FPS overlay after Continue in the 104-patch
Vulkan build, with 30 and Unlocked frame limits; those are final swapchain
readbacks after the overlay. The earlier depth-range captures use
99 patches and the menu captures use 83 patches, with their original settings.
The earlier LOD comparison is captured before window scaling. Saving and Continue
reload work; normal shadow polarity was restored in inspected Vulkan views.
Full DirectX character-shadow fidelity remains unresolved. Foot/Logic Stone flicker,
incomplete rendering state and town lag remain open. Still images do not prove
smooth animation, performance, full gameplay or interpreter-free execution.
Historical entries retain their original test conditions and patch counts.

The dated entries after the two overview tables appear newest first; new captures go above the latest entry.
Each dated entry records what was verified at that checkpoint; later entries
supersede earlier limitations when further testing establishes progress.

## Milestones at a glance

Newest first: from the first native screen to a saved town and subsequent runtime work.
Select a row for the full entry and its limits.

| Date | Milestone | Capture |
| --- | --- | --- |
| 2026-10-21 | [Pipelined geometry, faster SPU code](#2026-10-21--pipelined-geometry-and-faster-spu-code) | <a href="#2026-10-21--pipelined-geometry-and-faster-spu-code"><img src="../verification/screenshots/2026-10-21-native-faster-spu-street.png" width="200" alt="Crowded town street with the FPS overlay at 21.3"></a> |
| 2026-10-21 | [Owned extended arithmetic](#2026-10-21--owned-extended-arithmetic) | <a href="#2026-10-21--owned-extended-arithmetic"><img src="../verification/screenshots/2026-10-21-native-cpp-negative-product-town.png" width="200" alt="Authored extended arithmetic in the crowded town"></a> |
| 2026-10-21 | [Frustum and interval selection](#2026-10-21--frustum-and-interval-selection) | <a href="#2026-10-21--frustum-and-interval-selection"><img src="../verification/screenshots/2026-10-21-native-cpp-frustum-town.png" width="200" alt="Authored selection jobs in the crowded town"></a> |
| 2026-10-21 | [Native C++ visibility jobs](#2026-10-21--native-c-visibility-jobs) | <a href="#2026-10-21--native-c-visibility-jobs"><img src="../verification/screenshots/2026-10-21-native-cpp-visibility-town.png" width="200" alt="Authored C++ geometry and visibility in the crowded town"></a> |
| 2026-10-20 | [Expanded native geometry inputs](#2026-10-20--expanded-native-geometry-inputs) | <a href="#2026-10-20--expanded-native-geometry-inputs"><img src="../verification/screenshots/2026-10-20-native-cpp-expanded-geometry-town.png" width="200" alt="Expanded native geometry inputs in the town"></a> |
| 2026-10-20 | [Native C++ host arithmetic](#2026-10-20--native-c-host-arithmetic) | <a href="#2026-10-20--native-c-host-arithmetic"><img src="../verification/screenshots/2026-10-20-native-cpp-host-math-town.png" width="200" alt="Authored C++ host arithmetic in the crowded town"></a> |
| 2026-10-20 | [Native C++ whole-job town](#2026-10-20--native-c-whole-job-town) | <a href="#2026-10-20--native-c-whole-job-town"><img src="../verification/screenshots/2026-10-20-native-cpp-whole-job-town.png" width="200" alt="Native C++ whole geometry job in town"></a> |
| 2026-10-20 | [Native C++ skinning routine](#2026-10-20--native-c-skinning-routine) | <a href="#2026-10-20--native-c-skinning-routine"><img src="../verification/screenshots/2026-10-20-native-cpp-skinning-stream.png" width="200" alt="Native C++ skinning routine in Balastor Plain"></a> |
| 2026-10-20 | [Native C++ normal output](#2026-10-20--native-c-normal-output) | <a href="#2026-10-20--native-c-normal-output"><img src="../verification/screenshots/2026-10-20-native-cpp-normal-output.png" width="200" alt="Native C++ final-normal output route"></a> |
| 2026-10-20 | [Leaf shadow cutouts](#2026-10-20--leaf-shadow-cutouts) | <a href="#2026-10-20--leaf-shadow-cutouts"><img src="../verification/screenshots/2026-10-20-native-foliage-alpha-shadows.png" width="200" alt="Leaf-shaped foliage shadows"></a> |
| 2026-10-19 | [Native C++ geometry positions](#2026-10-19--native-c-geometry-positions) | <a href="#2026-10-19--native-c-geometry-positions"><img src="../verification/screenshots/2026-10-19-native-cpp-geometry-positions.png" width="200" alt="Native C++ geometry positions"></a> |
| 2026-10-19 | [DirectX plains clipping fixed](#2026-10-19--directx-plains-clipping-fixed) | <a href="#2026-10-19--directx-plains-clipping-fixed"><img src="../verification/screenshots/2026-10-19-native-directx-plains-clipping-fixed-front.png" width="200" alt="DirectX plains clipping fixed"></a> |
| 2026-10-19 | [DirectX Off/FXAA](#2026-10-19--directx-fxaa) | <a href="#2026-10-19--directx-fxaa"><img src="../verification/screenshots/2026-10-19-native-directx-fxaa-on.png" width="200" alt="DirectX native FXAA enabled"></a> |
| 2026-10-19 | [DirectX FPS Counter On/Off](#2026-10-19--directx-fps-counter) | <a href="#2026-10-19--directx-fps-counter"><img src="../verification/screenshots/2026-10-19-native-directx-fps-on.png" width="200" alt="DirectX in-game counter enabled"></a> |
| 2026-10-19 | [DirectX display and filtering controls](#2026-10-19--directx-graphics-settings) | <a href="#2026-10-19--directx-graphics-settings"><img src="../verification/screenshots/2026-10-19-native-directx-graphics-settings.png" width="200" alt="DirectX graphics settings"></a> |
| 2026-10-19 | [Town FPS checkpoint](#2026-10-19---town-fps-checkpoint) | <a href="#2026-10-19---town-fps-checkpoint"><img src="../verification/screenshots/2026-10-19-native-town-fps-unlocked.png" width="200" alt="Town with native FPS overlay"></a> |
| 2026-10-19 | [Depth range check](#2026-10-19---window-depth-clip-range-check) | <a href="#2026-10-19---window-depth-clip-range-check"><img src="../verification/screenshots/2026-10-19-native-depth-range-front.png" width="200" alt="Depth range check"></a> |
| 2026-10-18 | [Graphics Settings](#2026-10-18---native-graphics-settings) | <a href="#2026-10-18---native-graphics-settings"><img src="../verification/screenshots/2026-10-18-native-graphics-settings.png" width="200" alt="Graphics Settings"></a> |
| 2026-10-18 | [Save and Continue](#2026-10-18---logic-stone-saving-and-restartcontinue-reload) | <a href="#2026-10-18---logic-stone-saving-and-restartcontinue-reload"><img src="../verification/screenshots/2026-10-18-native-wkc1-save-reloaded.png" width="200" alt="Save and Continue"></a> |
| 2026-10-17 | [Stretching fixed](#2026-10-17--town-stretching-fixed-secondary-bone-physics) | <a href="#2026-10-17--town-stretching-fixed-secondary-bone-physics"><img src="../verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics.png" width="200" alt="Stretching fixed"></a> |
| 2026-10-17 | [Walking and camera](#2026-10-17--town-movement-and-camera-response) | <a href="#2026-10-17--town-movement-and-camera-response"><img src="../verification/screenshots/2026-10-17-native-wkc1-town-controls.png" width="200" alt="Walking and camera"></a> |
| 2026-10-16 | [Town reached](#2026-10-16--wkc1-town-world-view) | <a href="#2026-10-16--wkc1-town-world-view"><img src="../verification/screenshots/2026-10-16-native-wkc1-town.png" width="200" alt="Town reached"></a> |
| 2026-10-16 | [Avatar creation](#2026-10-16--vulkan-avatar-colours-restored) | <a href="#2026-10-16--vulkan-avatar-colours-restored"><img src="../verification/screenshots/2026-10-16-native-vulkan-avatar-colours.png" width="200" alt="Avatar creation"></a> |
| 2026-10-16 | [First 3D room](#2026-10-16--3d-room-with-normal-depth) | <a href="#2026-10-16--3d-room-with-normal-depth"><img src="../verification/screenshots/2026-10-16-native-3d-room.png" width="200" alt="First 3D room"></a> |
| 2026-10-16 | [Title menu](#2026-10-16--title-menu) | <a href="#2026-10-16--title-menu"><img src="../verification/screenshots/2026-10-16-title-menu.png" width="200" alt="Title menu"></a> |
| 2026-10-16 | [Startup logos play](#2026-10-16--d3publisher-logo-playback) | <a href="#2026-10-16--d3publisher-logo-playback"><img src="../verification/screenshots/2026-10-16-d3publisher-logo.png" width="200" alt="Startup logos play"></a> |
| 2026-10-15 | [First native screen](#2026-10-15--language-selection) | <a href="#2026-10-15--language-selection"><img src="../verification/screenshots/2026-10-15-language-selection.png" width="200" alt="First native screen"></a> |

## Before and after

Fixes with a visible difference, newest first. Each entry below gives the test conditions.

| Change | Before | After |
| --- | --- | --- |
| [Foot shadow](#2026-10-18---earlier-shadow-regression-polarity-fixed-in-patch-0074)<br><sub>Different builds and camera positions.</sub> | <img src="../verification/screenshots/2026-10-18-native-wkc1-town-shadow-regression.png" width="320" alt="Bright patch under Leonard (73 patches)"><br><sub>Bright patch under Leonard (73 patches)</sub> | <img src="../verification/screenshots/2026-10-19-native-depth-range-front.png" width="320" alt="Polarity fixed in patch 0074 (99 patches)"><br><sub>Polarity fixed in patch 0074 (99 patches)</sub> |
| [Texture detail (patch 0079)](#2026-10-18---requested-texture-detail-restored)<br><sub>Same executable, save and camera.</sub> | <img src="../verification/screenshots/2026-10-18-native-wkc1-lod-before.png" width="320" alt="Bias forced to zero"><br><sub>Bias forced to zero</sub> | <img src="../verification/screenshots/2026-10-18-native-wkc1-lod-after.png" width="320" alt="Requested LOD bias applied"><br><sub>Requested LOD bias applied</sub> |
| [Town stretching](#2026-10-17--town-stretching-fixed-secondary-bone-physics)<br><sub>Different builds and runs.</sub> | <img src="../verification/screenshots/2026-10-17-native-wkc1-nv0039-npc-surfaces.png" width="320" alt="Strips at Leonard (52 patches)"><br><sub>Strips at Leonard (52 patches)</sub> | <img src="../verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics.png" width="320" alt="Secondary-bone physics job restored (58 patches)"><br><sub>Secondary-bone physics job restored (58 patches)</sub> |


## 2026-10-21 — Pipelined geometry and faster SPU code

![Crowded town street in the native DirectX build, FPS overlay reading 21.3](../verification/screenshots/2026-10-21-native-faster-spu-street.png)

The crowded street, the slowest scene so far, after two rounds of lag work.
Supported image-19 geometry jobs are now prepared on worker threads and
published in order ([pipelined geometry jobs](PATCHES.md#pipelined-geometry-jobs-and-sub-millisecond-sleeps-patches-0127-0128)).
Lifted SPU code now jumps directly between functions when no runtime hook
needs to run ([patch 0130](PATCHES.md#faster-spu-calls-and-transfers-patch-0130)).
Captured render-command jobs replay byte for byte.

This frame comes from the installed desktop build (commit `c5a2372`), relinked
only so its DirectX window stays hidden. The run used Continue, then a
twelve-move walk with camera turns, with an isolated copied save, sound muted,
the frame limit unlocked and the FPS overlay on. This frame reads **21.3 FPS**.
Across the walk the overlay read 16.7–21.7 FPS facing the crowd and 47.8–58.0
facing walls. The last capture on this street read 8.5 FPS, at a 30 FPS cap
and before both changes. Vulkan timelines on the same street record about
23–41 presentations per second.

The street still lags, and the render-command job (image 20) is now its largest
cost. A still frame does not prove smooth traversal. See
[image 20](NATIVE_CODE.md#next-critical-path-image-20-the-spu-render-command-builder)
for what remains.

## 2026-10-21 — Owned extended arithmetic

![Authored extended negative-product arithmetic in the native town](../verification/screenshots/2026-10-21-native-cpp-negative-product-town.png)

The [project-written extended negative-product operation](NATIVE_CODE.md#owned-extended-negative-product-arithmetic)
now runs in supported geometry and visibility jobs, with portable and AVX2
implementations. Original/native whole-job outputs match, and all 119 Windows
checks pass in Debug and Release. No complete SPU image has been removed.

This is an unmodified DirectX GPU frame after Continue, a camera turn and two
short town walks, with the FPS overlay enabled. The inspected frame reads
**8.5 FPS**; a separate quiet 12-second window in this run records about
**8.42 presentations/s**. No paired FPS gain is established, and the street lag
remains. Hidden/muted copied save, 30 FPS cap and strict fallback guard;
no arithmetic counters, job census or CPU profiler. The metadata-free image
matches every pixel of its private source readback.

## 2026-10-21 — Frustum and interval selection

![Authored C++ selection jobs in town, FPS overlay on](../verification/screenshots/2026-10-21-native-cpp-frustum-town.png)

All four known visibility modes now have connected authored algorithms for
admitted inputs. Town modes 1/2/3 execute; mode 4 has controlled original/native
comparisons. This separate quiet DirectX12 run uses Continue, a camera turn
and two short walks, isolated save, 30 FPS cap and strict fallback guard.
No job census or CPU profiler; GPU readback is armed for the screenshot.
The instantaneous counter reads 8.6 FPS: crowded-town lag remains. No paired
speed gain or full-image replacement is claimed. See [proof and limits](NATIVE_CODE.md#frustum-and-interval-selection).

## 2026-10-21 — Native C++ visibility jobs

![Native C++ geometry and visibility jobs in town, FPS counter on](../verification/screenshots/2026-10-21-native-cpp-visibility-town.png)

The opt-in runtime now executes two whole visibility-request modes through
authored C++: box occlusion and mask filtering. Fourteen complete real
requests and 200 synthetic whole-original dispatches match their outputs;
128 independent two-worker dispatches also pass. A separate diagnostic
town route observes both modes completing, alongside the new palette rows.

This frame comes from a quiet DirectX12 run after Continue, a camera turn
and two short walks, with a copied save, 30 FPS limit and strict fallback
guard. Diagnostics and CPU profiling are disabled; GPU readback is armed
for the screenshot. The instantaneous counter reads 8.6 FPS in this
NPC-heavy view: town lag remains. No paired speed gain, smooth animation
or complete SPU-image replacement is claimed. Modes 1/4 and unsupported
interfaces retain compiled execution. See [coverage and limits](NATIVE_CODE.md#owned-visibility-jobs).

## 2026-10-20 — Expanded native geometry inputs

![Expanded native C++ geometry inputs in the town, FPS counter on](../verification/screenshots/2026-10-20-native-cpp-expanded-geometry-town.png)

Expanded packed-position layouts now run through the owned C++ job. Fresh
original/native comparisons match 84 complete jobs, including allocator and
command output. A separate diagnostic town run records 7,757 new-format native
completions. This frame uses the public player code with the census and CPU
profiler disabled: DirectX 12, 30 FPS limit, Continue, short town movement,
isolated save, hidden/muted execution and strict fallback guard. The counter
is instantaneous and the readback is armed for this capture; no FPS gain or
smooth animation is proven. Palette/layout cases and other SPU jobs remain.
See [coverage and verification](NATIVE_CODE.md#expanded-quantized-position-inputs).

## 2026-10-20 — Native C++ host arithmetic

![Authored C++ arithmetic connected in the crowded town, FPS counter on](../verification/screenshots/2026-10-20-native-cpp-host-math-town.png)

Seven arithmetic/comparison operations now use authored C++ in the supported
whole geometry job. Integer NaN checks and direct final-packet construction are
also connected. Both Windows suites pass 117 checks; 389,120 arithmetic vectors
per configuration and all 32 actual Release VM replays match.

This frame follows Continue and a short walk toward the NPC street in DirectX
run `20261020-162847`. It uses a copied save, hidden/muted window, 30 FPS cap,
normal log suppression and strict fallback guard. The lossless GPU readback is
outside timing and has no optional PNG metadata. Its **12.0 FPS** overlay is an
instant; the separate twelve-second sample averages **13.164 presentations/s**
with a **393.468 ms** maximum interval. The sample has no paired baseline, and
lag remains. Other layouts/images and special numeric helpers remain outside
this replacement. See [arithmetic evidence and limits](NATIVE_CODE.md#authored-host-arithmetic-connected).

## 2026-10-20 — Native C++ whole-job town

![DirectX town with the supported whole C++ geometry job enabled](../verification/screenshots/2026-10-20-native-cpp-whole-job-town.png)

The opt-in public C++ runtime now handles the complete supported image-19
geometry job, from owned original inputs through allocation, coherent transfers
and command publication. All 32 original-input replays match byte for byte through
the real SDK backend. A separate logging-on profile/census records at least
131,072 completed native jobs and zero fatal errors; it is not the timing run.
Other layouts retain compiled jobs, SDK math helpers remain required, and no
complete SPU image has been replaced.

This DirectX frame comes from quiet run `20261020-144524`, with an isolated save,
hidden/muted window, 30 FPS cap, normal log suppression and the FPS counter on.
It is an unmodified final-swapchain GPU readback with optional PNG metadata
removed, manually armed outside the measured intervals. The displayed 16.2 is
an instant, not the benchmark average. The separate same-executable comparison
is near parity in two stationary town views, with no FPS gain established.
Movement segments are excluded because they end at the Leave area prompt;
NPC-heavy traversal and area-transition coverage remain unverified. See the
[measurements and limits](NATIVE_CODE.md#quiet-two-view-comparison).

## 2026-10-20 — Native C++ skinning routine

![DirectX plains with the readable complete supported skinning routine](../verification/screenshots/2026-10-20-native-cpp-skinning-stream.png)

Readable C++ now handles the supported skinning routine's setup, loops and drain.
All 32 recorded calls and 99,417 accepted randomized comparisons match complete
state, including unusual rounding and denormal settings. Both strict renderer
routes reach the plains and pass sampled live comparisons. Both Windows suites
pass 113 checks. [Evidence and limits](NATIVE_CODE.md#readable-whole-skinning-stream).

This DirectX capture is an unmodified final-swapchain readback, with optional PNG
metadata removed. Its 19.1 FPS is an instrumented instant in a hidden, muted,
isolated-save run with strict guards, tracing, sampled verification and automatic
captures. No performance gain is claimed. Whole SPURS jobs, DMA/scheduling,
complete image replacement and full rendering fidelity remain open.

## 2026-10-20 — Native C++ normal output

![Native DirectX plains after the C++ final-normal replacement](../verification/screenshots/2026-10-20-native-cpp-normal-output.png)

The final two four-normal batches in image 19 now use readable project-written
C++. All 32 captured contexts and 96,000 host/exact/audit comparisons match
complete state, including local-store outputs and continuation. Strict DirectX
`20261020-032618` and Vulkan `20261020-033037` runs confirm the plains transition,
walking and camera rotation with zero fallback entries. Both full Windows suites
pass 113 checks. [Details and remaining work](NATIVE_CODE.md#readable-final-normal-output).

This is a DirectX final-swapchain GPU readback with the in-game counter. The run
is hidden/muted and uses an isolated save, strict guard, tracing and automatic
captures. Its displayed 16.7 is an instrumented instant, not evidence of an FPS
gain. Kernel execution is established by traces and state comparisons, not this
still image. Whole jobs, DMA/scheduling equivalence, full SPU replacement and
remaining rendering differences are unverified. No complete SPU image is replaced.

## 2026-10-20 — Leaf shadow cutouts

The shared alpha-reference decoder mistook integer ABGR/XBGR surface formats
for floating point. The foliage depth pass's `0x59` reference now decodes to
`89/255`. Leaf-shaped gaps replace the broad solid polygons in this inspected
DirectX plains view. See [the diagnosis and limits](DIRECTX_PARITY.md#foliage-cloud-shade-and-grass-comparison).

![DirectX plains with leaf-shaped shadow cutouts](../verification/screenshots/2026-10-20-native-foliage-alpha-shadows.png)

| Before, 120 patches | After, 121 patches |
| --- | --- |
| ![Solid foliage shadows before correction](../verification/screenshots/2026-10-20-native-foliage-before-alpha.png) | ![Leaf cutouts after correction](../verification/screenshots/2026-10-20-native-foliage-alpha-shadows.png) |

Both captures are unmodified GPU frames from hidden, muted tests using isolated
save copies. Cameras and scene/animation timing differ. The after image has the
native FPS counter enabled; 17.4 is an instrumented instant with tracing and
automatic GPU snapshots, not a speed comparison. The before image has the
counter off. All 113 Windows checks pass in Debug and Release. Moving cloud
shade, matched grass density, remaining flicker and full rendering fidelity are
not established by these images. No desktop or optional image metadata is included.

## 2026-10-19 — Native C++ geometry positions

The native C++ matrix blend and position transforms execute through Continue,
a town-to-plains transition, walking and camera rotation on both DirectX and
Vulkan. Both strict routes record zero interpreter/microstep entries; trace
markers show at least 16,777,216 transformed blocks per run. All 111 Windows
checks pass in Debug and Release. Full-state replays and 8,000 fuzz cases are
recorded in [native execution work](NATIVE_CODE.md#readable-c-geometry-prefix).

![DirectX plains with native C++ geometry positions](../verification/screenshots/2026-10-19-native-cpp-geometry-positions.png)

Capture: 19 October, published 20 October. Unmodified final swap-chain pixels,
optional metadata removed. The FPS overlay reads 18.4 in this diagnostic frame;
logging, tracing and GPU captures were enabled. This is not an uninstrumented
performance comparison. Normal processing and job/output handling remain
compiled, and broader gameplay/CPU coverage and rendering parity remain open.

## 2026-10-19 — DirectX plains clipping fixed

The 118-patch DirectX build enables ordinary near/far rasterizer clipping. A
zero-initialized descriptor had disabled it, allowing foreground ground to cover
Leonard's lower body after entering Balastor Plain. The issue also reproduced on
the older 115-patch renderer; Vulkan showed the body on the corresponding route.

The corrected route loads Continue, leaves town, walks farther into the plains
and turns the camera. Both fatal SPU fallback guards remain enabled with zero
entries. All 107 Windows checks pass in Debug and Release; eight WARP cases
exercise clip boundaries, homogeneous W and the old disabled controls.

![DirectX plains after camera rotation](../verification/screenshots/2026-10-19-native-directx-plains-clipping-fixed-front.png)

![DirectX plains after short movement](../verification/screenshots/2026-10-19-native-directx-plains-clipping-fixed-walk.png)

Earlier 117-patch view, before this fix:

![Ground incorrectly covering Leonard's lower body](../verification/screenshots/2026-10-19-native-directx-plains-clipping-before.png)

Unmodified final backbuffer pixels, optional metadata removed. The views differ
in position and animation; these images establish the inspected visual result,
not an FPS gain, complete rendering parity or full-game CPU coverage.


## 2026-10-19 — DirectX FXAA

DirectX now applies the same project-written FXAA algorithm as Vulkan to a
separate completed image. The FPS text is added afterward. Native Off/FXAA
changes were checked against the applied renderer settings and actual pipeline
and output resources, then the run returned to the world for a camera turn and
short movement at the 30 FPS setting.

![DirectX native FXAA selection](../verification/screenshots/2026-10-19-native-directx-fxaa-on.png)

![DirectX anti-aliasing Off](../verification/screenshots/2026-10-19-native-directx-fxaa-off.png)

![DirectX world with FXAA enabled](../verification/screenshots/2026-10-19-native-directx-fxaa-world.png)

Source checkpoint: 110 SDK patches / 99 Windows checks in Debug and Release.
These unmodified final-backbuffer GPU readbacks include FXAA and the counter,
before DXGI presentation and window scaling. The window is 720p and the guest
scanout is 1920x1080. Moments differ between captures; these are functional
checks, not a performance comparison. The eight-case WARP test independently
checks filtered pixels, exact alpha and unchanged source contents. Full movie,
fresh avatar portrait, fullscreen and area-transition parity remain open.

## 2026-10-19 — DirectX FPS Counter

The native Graphics Settings panel now switches the DirectX counter On and Off.
The foreground text has no background box. These images show that control in
the same hidden, muted saved-scene run at the 30 FPS setting.

![DirectX FPS Counter On](../verification/screenshots/2026-10-19-native-directx-fps-on.png)

![DirectX FPS Counter Off](../verification/screenshots/2026-10-19-native-directx-fps-off.png)

Source checkpoint: 109 SDK patches / 98 Windows checks in Debug and Release.
These are completed final-backbuffer GPU readbacks, including the overlay,
before DXGI Present and output scaling. The window is 720p while the guest
scanout is 1920x1080. The counter measures accepted presentations; the stills
do not establish smooth gameplay, unique animation speed or a performance gain.
FXAA, fresh DirectX avatar capture and complete transition parity remain open.

## 2026-10-19 — DirectX graphics settings

DirectX now receives the display and filtering preferences from the native
Graphics Settings panel. This run changed 720p to 1080p, toggled VSync and
selected 16x filtering at the stable 30 FPS limit. The live renderer's applied
settings and actual window client size were read back independently.

![DirectX 12 Graphics Settings with 1080p and 16x filtering](../verification/screenshots/2026-10-19-native-directx-graphics-settings.png)

Source checkpoint: 107 SDK patches / 96 Windows checks in Debug and Release.
This is a completed GPU surface readback, before DXGI output scaling; the private
capture build hides its Win32 host window. Fullscreen transitions are covered by
recording-API tests and still need an interactive check. FXAA, the in-game FPS
overlay and full rendering/transition parity remain open. No performance claim
is made from this still image.

## 2026-10-19 - Town FPS checkpoint

The 104-patch Vulkan build reloads the verified WKC1 save through Continue.
These fresh captures show the native FPS counter in town with the frame limit
set to Unlocked and 30. All 92 Windows checks pass in Debug and Release.

**Unlocked: the counter shows 33.4 completed presentations/s at capture.**

![Town with the native FPS counter showing 33.4 and the frame limit unlocked](../verification/screenshots/2026-10-19-native-town-fps-unlocked.png)

**30 limit: the counter shows 29.5 completed presentations/s at capture.**

![Town with the native FPS counter showing 29.5 and the frame limit set to 30](../verification/screenshots/2026-10-19-native-town-fps-30.png)

Both images are unmodified 1280x720 final swapchain GPU readbacks, copied after
the unchanged native FPS overlay and GPU completion. FXAA is off. The hidden
runs had audio muting verified and used a short camera turn after loading.
Their camera angles differ, so these are not a controlled performance comparison.
The counter records presentations at capture; still images do not establish a
sustained frame rate, smooth animation or unique game updates. Foot/Logic Stone
flicker and wider rendering fidelity remain open.

## 2026-10-19 - Window-depth clip range check

Patch 0098 pairs requested depth intervals with vertex normalization. The final
99-patch candidate also initializes timezone and retail GPIO query outputs. All
84 Windows checks pass in Debug and Release. It loads Continue and moves briefly
in town in a hidden/muted run.

These unmodified final readbacks show the save-point and ground views after
the 99-patch reload. An earlier 98-patch candidate supplied the off/on comparison. No clear foot/Logic Stone flicker reduction is
established. Still images do not prove smoothness, performance or full fidelity.

![Town with paired depth-range handling](../verification/screenshots/2026-10-19-native-depth-range-front.png)

![Overhead ground view with paired depth-range handling](../verification/screenshots/2026-10-19-native-depth-range-ground.png)

## 2026-10-18 - Fragment input and request coverage check

The 96-patch candidate reloads Continue and moves through town after coordinate
override, perspective correction and conditional shader-write fixes. All 82
Windows checks pass in Debug and Release, including synthetic GPU shader checks.
Diagnostic probes now list known active rendering gaps automatically.

This unmodified final swapchain frame comes from the hidden/muted candidate after
short movement. It checks scene continuity, not a visible improvement, smoothness
or FPS gain. Foot/Logic Stone flicker and incomplete rendering fidelity remain open.

![Saved town after fragment input and request coverage checks](../verification/screenshots/2026-10-18-native-fragment-request-check.png)

## 2026-10-18 - Vulkan polygon offset check

Patch 0094 forwards the game's fill-polygon offset to Vulkan. All 80 Windows
checks pass in Debug and Release, including synthetic GPU depth readbacks.
The hidden/muted native candidate reloads Continue and moves in town.

These unmodified final swapchain readbacks come from the same executable and
camera with requested offset **enabled** (first) and forced **off** (second).
Idle animation, NPC poses and the rotating stone effect differ between frames.
The short sequences show no clear flicker reduction; foot/Logic Stone flicker
remain open. These are correctness-check captures, not an FPS comparison.

![Requested polygon offset enabled](../verification/screenshots/2026-10-18-native-polygon-offset-on.png)

![Polygon offset disabled for comparison](../verification/screenshots/2026-10-18-native-polygon-offset-off.png)

## 2026-10-18 - Task scheduling and SPU byte-operation checks

The 93-patch Vulkan build reloads the save and reaches this town view after
short movement. Task completion now follows reservation locking, and task ELF
flag bits are masked. Byte shifts/rotates have a tested SIMD implementation
with a scalar fallback. All 79 checks pass in Debug and Release.

This final swapchain readback comes from the hidden, muted diagnostic build
with SIMD selected. The mixed native timing comparison does not establish a
sustained FPS gain; the still image proves neither speed nor a rendering fix.

![Saved town after task scheduling and SPU byte-operation changes](../verification/screenshots/2026-10-18-native-task-byte-check.png)

## 2026-10-18 - Runtime correctness and controller input follow-up

The 90-patch Vulkan build reloads the verified town save. This game-only frame
comes from a hidden, muted synthetic input check, after a partial raw stick tilt.
The new regressions cover SPU instructions, PPU saturating subtraction, diagonal
stick range, concurrent report publication and complete pad API registration.
All 77 checks pass in Debug and Release. Physical-controller walk/run feel and
any performance gain remain unverified; this is not evidence of a visual fix.

![Saved town during the runtime and input check](../verification/screenshots/2026-10-18-native-runtime-input-check.png)

## 2026-10-18 - Full Graphics Settings panel and window close

Graphics Settings now uses the game's full settings box, with its native frame,
scrollbar, selection arrows and highlight. All eleven options and Back fit in
one panel. Up/Down selects, Left/Right or X changes a value, and Circle or Back
returns to System. The normal Game Settings page retains its original controls.

These final Vulkan swapchain readbacks show the panel and the FPS toggle after
moving through its rows. The window close event now exits normally; title and
saved-town settings integration tests end with code 0 without forced termination.
All 72 Windows checks pass in Debug and Release with 83 SDK patches.
Distance/fog controls and menu background blur removal remain pending.
Diagnostic screenshots and their counters are not performance benchmarks.

![Full native Graphics Settings panel](../verification/screenshots/2026-10-18-native-graphics-panel.png)

![FPS counter switched off in the full panel](../verification/screenshots/2026-10-18-native-graphics-panel-fps.png)

## 2026-10-18 - One scrolling Graphics Settings list

Historical layout, superseded by the full panel above.

All graphics controls now share one scrolling list. The original eight visible
rows show a moving window over twelve items, including Back. Up/Down scrolls
at the edges and wraps between first and last; Circle returns directly to System.
The heading shows the visible range. There is no More Settings page.

These final Vulkan swapchain readbacks show the top and bottom of the list.
Native checks cover scrolling, selection alignment and changing the FPS overlay
after scrolling. All 72 tests pass in Debug and Release. The 82 SDK patches,
rendering options and timing implementation are unchanged. The display is still
blurred behind the menu; world/NPC distance and fog remain inactive and labelled
Pending. Neither these screenshots nor their counters prove game performance.

![Top of the single Graphics Settings list](../verification/screenshots/2026-10-18-native-graphics-scroll-top.png)

![Bottom of the same scrolling list, including the FPS counter and Back](../verification/screenshots/2026-10-18-native-graphics-scroll-bottom.png)

## 2026-10-18 - Native Graphics Settings

The native System menu now has a Graphics Settings entry. These 82-patch captures
show actual game widgets after Continue loaded the save, with FXAA enabled.
All 72 Windows checks pass in Debug and Release. The frame-rate control now
changes guest pacing as well as the host limit; short saved-scene samples exceed
30 completed frames/s with normal sampled game-clock speed. These images do not
prove a frame-rate gain. See [measurements and limits](FINDINGS.md#native-graphics-settings-and-frame-rate-control).

Primary page: windowed/borderless/fullscreen, named output resolution, VSync,
anisotropic filtering, Off/FXAA and 30/40/60/120/144/Unlocked limits:

![Native Graphics Settings page](../verification/screenshots/2026-10-18-native-graphics-settings.png)

More Settings contains the renderer choice, draw-distance submenu and FPS toggle.
The asterisk means DirectX 12 has been selected for the next restart; this capture
is still Vulkan. DirectX 12 also reaches the save and menu in a separate test, but
at this 0082 checkpoint had visible differences and lacked the Vulkan display
preferences, FXAA and overlay. Later 0107/0109 entries supersede the display and
overlay limitations. Vulkan remains the default:

![Native API selector with the restart requirement](../verification/screenshots/2026-10-18-native-graphics-api.png)

The separate entry lives under System:

![Graphics Settings entry under System](../verification/screenshots/2026-10-18-native-system-graphics.png)

World/NPC distance and fog are placeholders, visibly pending and inactive.
The game's background blur remains in all these pages:

![Inactive draw-distance controls labelled pending](../verification/screenshots/2026-10-18-native-draw-distance-pending.png)

## 2026-10-18 - Requested texture detail restored

Patch 0079 honors the game's negative texture LOD bias instead of sampling
blurrier mip levels. All 67 Windows checks pass in Debug and Release.
One hidden, muted executable switched bias off/on at the same loaded save and
camera. These GPU readbacks precede window scaling; idle animation and the
rotating stone differ. No render-resolution increase or FPS gain is claimed.
Private A/B controls are absent from the published patch. See the
[rendering-state audit](FINDINGS.md#texture-detail-and-rendering-state-audit-patch-0079)
for the remaining omissions.

Requested bias applied:

![Native WKC1 clothing, stonework and foliage with the requested texture LOD bias](../verification/screenshots/2026-10-18-native-wkc1-lod-after.png)

Bias forced to zero, reproducing the previous behavior in the same executable:

![Same native camera with texture LOD bias disabled](../verification/screenshots/2026-10-18-native-wkc1-lod-before.png)

## 2026-10-18 - Vulkan batch scheduling in the saved town

Patch 0076 avoids pool-only submissions and submits small batches when the spare
slot is ready. One-minute stationary samples average 27.18 presentations/s before
and 28.03 after with file logging and sampling off. A locked 30 fps and full game-speed
fidelity remain unverified; see [conditions and limits](FINDINGS.md#vulkan-batch-scheduling-patch-0076).
The 76-patch build passes all 64 Windows checks in Debug and Release.

The native frame below was captured after Continue loaded the verified town save.
It documents the scene, not the measured frame rate or flicker cadence.

![Native WKC1 saved town with Logic Stone selected after the Vulkan batch scheduling change](../verification/screenshots/2026-10-18-native-wkc1-town-batching.png)

## 2026-10-18 - Logic Stone saving and restart/Continue reload

Patch 0075 delivers previously dropped FIFO user-command interrupts to the
registered native callback. The Logic Stone now highlights on approach and
opens with X. Save writes a new local slot; after fully restarting the program,
Continue loads it and returns to town. Both Windows suites pass all 63 checks.
This covers one starting-town save on the same build; wider save compatibility
is unverified. Lag, analogue walk/run inconsistency and remaining flicker are open.

At this checkpoint, the later of the two captures shows the reloaded town
and selected Logic Stone:

![Native WKC1 town after restart and Continue, with Logic Stone selected](../verification/screenshots/2026-10-18-native-wkc1-save-reloaded.png)

The earlier capture shows its normal menu, opened with X before choosing Save:

![Native Logic Stone menu with Save selected](../verification/screenshots/2026-10-18-native-wkc1-logic-stone-save.png)

## 2026-10-18 - Earlier shadow regression (polarity fixed in patch 0074)

The 73-patch native Vulkan build reaches the WKC1 town after character
creation and the opening cutscenes. It passes 63 Windows regression checks.
The bright patch beneath Leonard is a known rendering regression in the new
depth-comparison path. Black flickering at his feet is also observed in motion.
This unchanged game-frame readback records the fault; it does not establish
correct shadows, sustained frame rate or complete gameplay.

![Native WKC1 town with the bright foot-shadow regression beneath Leonard](../verification/screenshots/2026-10-18-native-wkc1-town-shadow-regression.png)

## 2026-10-17 — Town stretching fixed: secondary-bone physics

The 58-patch native Vulkan build executes the previously missing SPURS queue physics
job, statically translated to C/C++. Leonard's hair and clothing remain on
the model instead of stretching across the street. The long strips are absent
in the inspected starting-town views, including after short movement and
camera rotation. All 51 Windows runtime checks pass.

Private replay of 128 captured geometry jobs finds normal referenced vertex
bounds throughout; 21 of 128 samples before the fix had extreme offsets.
See the [trace and limits](FINDINGS.md#secondary-bone-physics-and-town-stretching).
At that checkpoint, full rendering fidelity, sustained traversal, combat and
save/load were unverified. Later entries above record local saving.

![Native WKC1 town after restoring secondary-bone physics](../verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics.png)

![Native WKC1 town after movement and camera rotation, without the long strips](../verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics-camera.png)

## 2026-10-17 — NPC surfaces after NV0039 transfers

The 52-patch native Vulkan build implements NV0039 byte copies and routes
their methods away from 3D state. Previously speckled NPC surfaces look
coherent in the inspected town views, including after short movement and
camera rotation. The run verifies at least 65,536 copies without mismatch
or rejection diagnostics. Full material fidelity remains unverified.

**The stretched strips at Leonard were still present at this checkpoint.**
The newer physics milestone above removes them in the inspected town views.
This capture records the earlier NPC surface improvement; it does not establish
complete rendering correctness. Sustained traversal, combat and save/load
were unverified at this checkpoint; local saving was verified later.

![Native WKC1 town with improved NPC surfaces and remaining stretched geometry](../verification/screenshots/2026-10-17-native-wkc1-nv0039-npc-surfaces.png)

## 2026-10-17 — Town movement and camera response

The full 42-patch native build reaches the town and responds to left-stick
movement and right-stick camera input. Successive GPU readbacks show walking,
facing changes, a changed minimap position and a rotated view of the street.
The SPU census was active throughout this run. This is limited starting-town
input validation; sustained traversal, combat and save/load were unverified
at this checkpoint. Local saving was verified later.
Stretched geometry and corrupt NPC surfaces were still visible at this checkpoint;
the later fixes are recorded above.

![Native WKC1 town after analog movement and camera rotation](../verification/screenshots/2026-10-17-native-wkc1-town-controls.png)

## 2026-10-16 — WKC1 town world view

This earlier native Vulkan run reached the town after avatar acceptance and the
opening cutscenes. Leonard, the surrounding buildings, NPC names, minimap and
save crystal appear in live 3D. Pause/resume input was checked separately in
the same run. Fixing a runtime cutoff that halted valid SPU completion-label
polling lets this run pass the previous loading stall.

Stretched triangles and corrupt NPC surfaces are visible in this unchanged
readback. Movement and camera control were unchecked in this run and were
verified in limited town probes on 2026-10-17. Sustained traversal, combat and
save/load were unverified in this run; later entries record local saving.
The 1920×1080 display readback contains the game's 1280×720 scene.

![Native WKC1 town showing Leonard, NPCs, minimap and save crystal, with visible geometry faults](../verification/screenshots/2026-10-16-native-wkc1-town.png)

## 2026-10-16 — Vulkan avatar colours restored

The initial Vulkan colour-feedback fix shows the WKC1 avatar and room without the
red tint seen in its first build. A post-processing draw reads the colour target
it is updating; preserving the preceding pixels in a separate GPU image restores
the missing colour channels. The entered name is retained. This is live 3D
rendering from the hybrid C/C++ build with the shared RSX draw engine.
The 1920×1080 display readback contains the game's 1280×720 scene. Full visual
fidelity was not established by this capture. Later town rendering and limited
controls and local save/reload were verified later, as recorded above. macOS
execution remains unverified.

![Native Vulkan WKC1 avatar and room after colour-feedback correction](../verification/screenshots/2026-10-16-native-vulkan-avatar-colours.png)

## 2026-10-16 — WKC1 opening movie after avatar confirmation

At this checkpoint, the native executable accepted the avatar and entered the
WKC1 opening movie. Presenting completed batches at guest FIFO flips prevents
small movie batches from being withheld by the earlier scene threshold. This
capture shows prerecorded footage, using freshly regenerated PPU and SPU code.
Live town rendering and limited controls were verified later, as recorded above.

![Native WKC1 opening movie after avatar confirmation](../verification/screenshots/2026-10-16-native-wkc1-opening-movie.png)

## 2026-10-16 — Character name retained through avatar review

This checkpoint verified name retention through Quick Start to final Review,
with the avatar, face shading and textured clothing visible under normal depth
testing and culling. Avatar acceptance and town entry were verified later.
A gamepad-operated in-game keyboard is planned; the Windows text-entry window
is temporary.

![Native Character name retained through avatar review](../verification/screenshots/2026-10-16-native-avatar-review.png)

## 2026-10-16 — Avatar face and clothing shading

Correcting packed vertex normals and fragment source-range modifiers revealed
the shaded face and textured clothing of the default male avatar, with normal
depth testing and culling. This version 1.00 capture predates avatar acceptance
and town validation. Full visual fidelity remains unverified.

![Native Avatar face and clothing shading](../verification/screenshots/2026-10-16-native-avatar-shading.png)

## 2026-10-16 — Avatar body geometry

Correcting SPU FNMS extended-range arithmetic revealed the previously missing
arms, hands, trousers and boots, with normal depth and culling. Face shading and
clothing materials were still incorrect in this version 1.00 capture; later
fixes and town progress are recorded above.

![Native Avatar body geometry](../verification/screenshots/2026-10-16-native-avatar-body.png)

## 2026-10-16 — 3D room with normal depth

Correcting indexed triangle strips and primitive restart made the WKC1
character-creation room render with normal depth testing. Some avatar pieces
appeared, but the body was incomplete in this version 1.00 capture. Later
entries record the complete default avatar, avatar acceptance and town progress.

![Native 3D room with normal depth](../verification/screenshots/2026-10-16-native-3d-room.png)

## 2026-10-16 — Avatar customization UI

This version 1.00 checkpoint reached Customize Avatar and dismissed its
introductory notice through controller input. Text and menu controls rendered,
but the character model was missing at the time. Later entries record the
avatar rendering fixes and progression into the town.

![Native Avatar customization UI](../verification/screenshots/2026-10-16-avatar-customization.png)

## 2026-10-16 — White Knight Chronicles I selected

This checkpoint selected the first White Knight Chronicles through native
controller input, with readable description and dialog text. Loading then
stopped at a job with an empty SPU binary source. That failure was resolved
before the later avatar and town milestones above.

![Native White Knight Chronicles I selected](../verification/screenshots/2026-10-16-wkc1-selection.png)

## 2026-10-16 — Readable New Game description

Fixing packed font texture bindings made the native New Game description
readable. This capture verifies that menu text; later progress is recorded above.

![Native Readable New Game description](../verification/screenshots/2026-10-16-readable-new-game.png)

## 2026-10-16 — New Game submenu

At this checkpoint, live controller input moved the title highlight and opened
New Game, showing its book artwork, labels and cursor. Description and dialog
text still appeared as blocks; the later font fix is recorded above.

![Native New Game submenu](../verification/screenshots/2026-10-16-new-game-menu.png)

## 2026-10-16 — Title menu

The native title menu displays New Game, Continue and Options over the animated
background. Successive captures show the title artwork changing. With no input,
the guest subsequently opens its attract movie. This capture proves menu rendering;
menu response, avatar acceptance and limited town controls were validated later.

![Native White Knight Chronicles II title menu](../verification/screenshots/2026-10-16-title-menu.png)

## 2026-10-16 — Startup advances to Havok / Dolby

The same native run reaches the Havok and Dolby Digital screen after the studio
logos. This is a later startup stage, before the AC-3 title background was supported.

![Native Havok and Dolby Digital startup screen](../verification/screenshots/2026-10-16-havok-dolby.png)

## 2026-10-16 — Japan Studio

Japan Studio appears as startup proceeds beyond the earlier movie stall.

![Native Japan Studio logo](../verification/screenshots/2026-10-16-japan-studio.png)

## 2026-10-16 — Sony presents

Startup advances to the Sony Computer Entertainment presentation screen.

![Native Sony Computer Entertainment presents screen](../verification/screenshots/2026-10-16-sony-presents.png)

## 2026-10-16 — D3Publisher logo playback

The first logo movie now animates through to the formed D3Publisher logo.
Successive GPU readbacks show different movie frames, and the guest then closes
and reopens the decoders for the next startup movie without a skip input.

![Native D3Publisher logo after its animation](../verification/screenshots/2026-10-16-d3publisher-logo.png)

## 2026-10-16 — First decoded logo frame

The startup movie now passes through real MPEG-2 decoding, YUV-to-RGBA processing
and the native renderer. This image is a GPU readback of the first logo sequence.
At this milestone, later captures held this image and continuous animation was unverified.
Audio output was still a placeholder at this milestone. This establishes visible decoded video,
not completed playback or title-screen rendering.

![Decoded startup logo image rendered natively](../verification/screenshots/2026-10-16-first-logo-frame.png)

## 2026-10-15 — Autosave notice

The autosave notice renders at a later startup stage. This screenshot establishes
that the notice displays; it does not establish that saving or loading works.

![Native autosave notice with warning icon and text](../verification/screenshots/2026-10-15-autosave-notice.png)

## 2026-10-15 — Startup warning

The warning icon, panel artwork and instruction-manual notice render after language
selection. This establishes another visible startup stage.

![Native startup warning with icon, panel artwork and text](../verification/screenshots/2026-10-15-startup-warning.png)

## 2026-10-15 — Language selection

The background, cursor, highlighted English option and other language options render.
A simulated CROSS press through the controller path selects English and advances startup.

![Native language selection showing English, Français and Español](../verification/screenshots/2026-10-15-language-selection.png)

## Evidence and updates

D3D12 captures are 1280 × 720; Vulkan display captures are 1920 × 1080 and
contain the game's 1280 × 720 scene. Every published pixel was compared with
the original BMP or PPM GPU readback before publication. The [capture manifest](../verification/gallery.json) records
source and PNG hashes. Full probe logs and original captures remain private.

New verified visual milestones will be added here as development progresses.
Captions state the observed behavior; screenshots alone do not prove unrelated
features or full-game compatibility.

Game artwork and text belong to their respective rights holders; the project's
source-code license does not relicense game content shown in these screenshots.
