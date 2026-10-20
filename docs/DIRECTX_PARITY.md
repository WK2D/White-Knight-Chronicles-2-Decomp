# DirectX and Vulkan parity audit

Source review: 20 October 2026. DirectX uses `rsx_live_draw.c`; Vulkan uses
`rsx_draw_engine.c` and `rsx_vulkan_backend.c`. A fix in the shared engine does
not automatically reach live DirectX. Shared PPU/SPU runtime changes do reach
both. This table records implementation gaps, not established causes of every
visual or performance difference.

| Feature | Vulkan | Live DirectX | Next validation |
| --- | --- | --- | --- |
| Shadow comparison and operand order | Patches 0073–0074 | Comparison shader/sampler path added in 0113; 144 synthetic WARP results pass. Native shadow appearance remains unresolved. | Compare the depth producer, sampled snapshot and final character shadow. |
| Leaf-cutout shadows, moving cloud shadows and vegetation density | Not yet compared on matched views | User comparison shows broad foliage-shadow shapes and reports missing moving cloud shade plus less grass | Match camera/FOV, resolution, save, graphics settings and elapsed scene time; inspect alpha-tested depth producers, ground shader samples and vegetation visibility separately. |
| Partial color clears | Honors individual RGBA channels | 0112 adds per-channel clears; 128 WARP cases cover RGBA8/FP16, rectangles and depth/stencil preservation. | The sampled town trace contains full-color and depth clears; no visual fix is attributed to partial clears. |
| Fill polygon offset | 0094 forwards scale/bias and applies host depth bias | 0114 decodes fill state, keys applied values and sets D32 rasterizer bias; sixteen WARP depth cases pass | Native town invokes the implementation, but a visible character-shadow fix is not established. Fractional constants round to D3D's integer field; other depth precision remains unverified. |
| TC0–7 coordinate override | 0096 forwards the mask; 0120 shares its decoder | 0120 forwards the same request mask in buffered/literal modes and keys PSOs on it | 64 DirectX shader compilations cover all eight inputs, perspective modes and mask controls; native verification and visual effect recorded below. |
| Window-depth interval | 0098 pairs vertex normalization and host viewport limits | Live viewport remains 0..1 without the paired transform | Implement valid D3D-supported intervals; explicitly audit unsupported ranges. Never change only one half. |
| Request census | 0095–0098 inspect draw state and compiled shaders | Live renderer does not call the census | Add an independent live entry point with accurate capability flags; a dispatch-table classification alone is insufficient. |
| Depth snapshot freshness | 0071 serializes snapshots against target generations; clear-only targets supported | Live path uses `had_write`/`snapshot_valid`, invalidates explicit clears, excludes the currently bound depth target and rejects clear-only captures | Test clear-only, bound-target sampling, zeta switches and logical-size reuse. Different code is a review target, not proof of a race. |
| CPU-visible surface reads | Shared engine handles its own color/depth coherence paths | 0111 supports exact, unambiguous linear ARGB8 color reads; general depth/overlap handling remains absent | Test actual consumers before extending support; fresh portrait capture is verified, same-session menu/reload is not. |
| Texture/render-target aliasing | Shared engine has its own freshness and feedback logic | Live color lookup prefers dimensions, then draw generation, and excludes depth for comparison textures | Capture binding/resource generations across area changes. Stale portrait and empty geometry after cutscene skips also reproduce before 0111. |
| Batched storage and submission | 0036 batches draws/uploads/clears using separate arenas; 0057 overlaps full batches through a spare slot | Live DirectX already records multiple draws and uses upload/descriptor rings and fences | Measure each path. Neither is accurately described as one GPU wait per draw. Vulkan still waits at present/readbacks; full frame overlap is separate. |
| Constant upload reuse | 0103 reuses unchanged uploads within a live batch, invalidated at retirement | Live path copies the complete vertex block per draw and allocates fragment constants separately | Profile NPC-heavy scenes before changing ownership or reuse. |
| Display, sampling controls and FXAA | Implemented | 0107/0109/0110 implement output/window/VSync/filtering, counter and shared FXAA math | Broader fullscreen/movie/transition checks remain open. |
| Core shader fixes | Shared VP/FP decompilers include co-issue, precision and arithmetic-predication fixes | Same shared files are compiled into DirectX | Audit call-site inputs separately; shared source does not establish all state is forwarded. |

Framebuffer sRGB output, fragment depth export, window-origin/offset semantics,
visibility reports and other request-census warnings also need work in the
shared path. They must not be advertised as solved Vulkan features that can
simply be copied into DirectX.

The user reports higher DirectX speed in empty areas, lower speed with NPCs,
and darker grass. These are observations awaiting matched scenes/settings and
CPU/GPU profiles. There is no established single cause or measured speedup from
this audit. Continue tests use isolated save copies and the 30 FPS setting.
The separately confirmed above-30 game-timing regression remains deferred.

## Plains transition follow-up

At 30 FPS, the town-to-Balastor Plain route incorrectly occludes Leonard’s lower
body with foreground ground in DirectX 117 (`20261019-215032`). It also reproduces
in DirectX 115 (`20261019-215530`), before the added static SPU entries. Vulkan 117
(`20261019-215842`) shows the lower body on the corresponding route. The strict
117 runs reach the plains with zero interpreter/microstep entries. This narrows
the visual failure to the renderer path without establishing its exact cause.
Patch 0118 fixes this inspected artifact by explicitly enabling DirectX near/far
rasterizer clipping. The zero-initialized descriptor had disabled it. Production
probe `20261019-222607` verifies the transition, short movement and camera rotation
with zero strict fallback entries. Eight WARP cases cover the correction and old
state. Non-default RSX clamp modes and the other parity gaps remain open.

## Foliage, cloud shade and grass comparison

On 20 October the user supplied an RPCS3/native plains comparison and reports:

- RPCS3 tree shadows preserve leaf silhouettes and gaps; the native tree shadow
  has broad solid shapes in the inspected image.
- Moving clouds cast moving shade on the ground in RPCS3. A still image cannot
  establish the corresponding animation in the native build.
- RPCS3 appears to draw denser grass. The supplied views differ in camera,
  framing and scene timing, so missing vegetation geometry is not yet proven.

Treat these as three open rendering checks. Do not substitute a darker ground
texture or a higher grass count for reproducing the original rendering behavior.
A comparison needs matching camera/FOV, output settings, scene/save and time.

Live DirectX already emits fragment discard and alpha tests. The first private
plains trace (`20261020-001523`) records a 2560×2560 depth producer with alpha-test
requests; selected frame 2897 contains 4,661 executed draw rows, including 930
in that shadow pass. The renderer reports zero dropped groups and texture decode
failures in this window. This does not prove correct sampling, clipping,
vegetation counts or final pixels. The heavy trace hit a capture timeout before
the final route step, so it is not a completed route or performance measurement.
The bounded one-frame follow-up `20261020-002430` completes the native route.
Frame 2855 records 2,448 draws, including 538 shadow-pass draws across nine actual
cached pipelines. All nine sample a texture, and the six with non-ALWAYS alpha
comparisons emit discard. These generated-shader checks do not establish correct
bound texture bytes, rasterized cutouts or final shadow detail.

That frame contains nonzero TC0–7 input override masks on 2,079 draws. Live
DirectX previously passed mask zero to the fragment decompiler, unlike Vulkan.
Patch 0120 forwards the shared decoded mask in buffered and literal modes and
includes it in PSO identity. This corrects a verified ignored state request; its
effect on leaf silhouettes, cloud movement or grass density remains unproven.

The corrected native route `20261020-003932` completes Continue, the plains
transition, short movement and camera rotation with zero strict fallback entries.
A bounded frame contains 2,450 draw rows and 104 pipeline variants. Forty-four
variants consume requested overrides, and their actual generated shaders include
the override expressions. The inspected final view still has the broad solid
foliage shadow, so this state correction does not resolve that visual fault.
Moving cloud shade and matched grass density remain unverified. All 112 checks
pass in Debug and Release. These instrumented routes establish no FPS gain.

Raw shaders, draw records and the supplied comparison remain private. The
comparison includes desktop content and must not be copied into the public gallery.

A subsequent control-state census (`20261020-005556`, 2,448 draws in one complete
frame) finds zero texture-level alpha-kill requests across the enabled texture
units. Missing texture alpha-kill is therefore not supported as the cause in
this frame. This does not exclude other alpha, texture, mip or depth errors, nor
establish behavior outside the sample. The leaf-shadow, cloud and grass checks
remain open.
