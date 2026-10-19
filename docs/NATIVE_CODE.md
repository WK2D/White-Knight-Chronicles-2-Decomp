# Native execution work

The target is host-compiled C/C++ gameplay and platform services, with no guest
CPU interpreter required on supported routes. Python is currently used to
extract inputs, generate C/C++, prepare fixtures and analyze captures. These
tools run outside the game. Rewriting them in C++ is not an FPS optimization
and does not remove the SPU interpreter fallback.

The current program compiles 56,580 generated PPU functions and 24 SPU image/job
inputs. Its PS3 memory/register conventions and compatibility services still
exist. It is not yet an interpreter-free port, nor a reconstruction of the
original readable source. Static image registration alone is not full coverage.

## Current execution evidence

The 115-patch Continue probe recorded six image-17 fallback rows. The next
reference run also found four image-19 rows. Five extra static entry points now
cover those observed paths: image 17 `0x7D60`, `0x7E38`, `0x7E78`; image 19
`0x49B8`, `0x4AE0`. Complete code-region checks and 40 private replay pairs match
the full context, 128 registers, 256 KiB local store, PC/status and continuation.
The replay covers these functions, not complete job/DMA scheduling.

The 117-patch DirectX probe `20261019-213333` completes a 420-second route-control
window after Continue, including movement, camera rotation and the Balastor Plain
exit prompt. With interpreter and microstep entrances forbidden, the table stays
at zero and neither fatal guard fires. The exit prompt is not a confirmed area
transition. A separate instrumented run confirms execution of all five new entries.

A separate strict DirectX route `20261019-215032` confirms the actual town-to-
Balastor Plain transition with zero fallback. The ground incorrectly occludes
Leonard’s lower body there. That fault reproduces on the older 115-patch DirectX
build; Vulkan 117 shows the lower body on the corresponding route. This remains
a DirectX rendering issue fixed in the inspected route by patch 0118; it was
not caused by the static SPU entries. Production probe `20261019-222607` also
completes transition, short movement and camera rotation with zero strict fallback.

## Readable C++ geometry prefix

The image-19 `0xCE08` block now calls a project-written C++ four-influence matrix
blend and four position transforms in `native_skinning.h`, through
`native_skinning_bridge.cpp`. The core uses
named vertex, weight and cyclic affine-matrix types; it has no guest registers,
addresses or DMA interface. The bridge decodes packed indices and local-store
palettes, preserves the original multiply/FMA order and exports the required
register boundary. The position bridge checks affine masks and cyclic input
layout before writing five registers; unsupported layouts keep the compiled tail.
The remaining compiled tail still handles normal processing and local-store
stores. These are parts of one block, not a complete geometry job.

`tools/apply_native_geometry.py` checks the complete original block hash, retains
its compiled reference and installs the native calls idempotently. An existing
blend-only hook upgrades only after its exact retained reference and wrapper
match. Unknown image or weight masks decline without changing state and use
that compiled reference.
`WKC_NATIVE_SKINNING=0` selects the reference for research comparisons. No game
function body or capture bytes are published.

Validation on Windows x86-64:

- Eight initial captures only exercised one-bone weights. A second capture set
  contains 64 distinct blocks with mixed weights in all four vertices, using
  bone indices up to 35. All 72 complete block replays match the reference's
  context, all registers, local store, PC/status and continuation.
- A separate recovered position operation matches all 256 positions from the
  64 mixed-weight captures. The position operation is now installed; all 72
  full-block capture replays still match the complete state and continuation.
- 8,000 full-state fuzz cases pass against the retained compiled reference:
  4,000 host-mode and 4,000 exact-SPU-mode cases, including arbitrary weight and
  position float bits, 16-bit index wrap, overlapping output/stack addresses,
  unsupported masks and inconsistent late-vertex cyclic layouts.
- The portable arithmetic test checks 32,000 finite cases against a lane oracle.
  A further 32,000 finite cases match the optional x86 FMA policy bit for bit.
  Live execution keeps SDK float helpers to preserve host/exact/audit semantics;
  the finite host-FMA test does not establish exceptional-value SPU equivalence.
- A separate portable cofactor operation matches all 864 matrix vectors from the
  72 captures. It passes 64 independent signed-minor matrix tests and 32,000
  finite scalar/x86 FMA comparisons. It is not yet installed in live dispatch;
  the compiled tail still owns its register-write timing and scaled normal work.
- Strict DirectX route `20261019-230123` and Vulkan route `20261019-231203` load
  Continue, enter Balastor Plain, walk and rotate the camera with zero fallback
  entries. Trace markers establish at least 16,777,216 native blend calls per
  run. Inspected final frames retain Leonard's lower body and the surrounding
  scene; these instrumented runs are not performance benchmarks.

New strict routes `20261019-234935` (DirectX) and `20261019-235407` (Vulkan)
complete the same transition/walking/camera path with the position replacement,
zero fallback and at least 16,777,216 transformed blocks in each run. Both final
frames were inspected. The Vulkan run additionally samples CPU time; profiling,
tracing and GPU captures make it unsuitable as an FPS benchmark.

Both Windows suites now pass 111 checks with 119 SDK patches. No FPS gain,
whole-job/DMA scheduling equivalence, complete static coverage or ARM execution
is claimed.

## Work order

1. **Refresh runtime evidence.** Capture fallback sessions and sampled CPU time
   on Continue, NPC-heavy movement, area transitions and cutscenes. Record which
   build and routes were measured. Distinguish waiting from executing code and
   per-worker samples from whole-frame cost.
2. **Extend verified static coverage.** The five image-17/image-19 entries above
   are integrated through `tools/build_spu.py`, with pinned image hashes and a
   stable 24-image registry. Keep testing NPC-heavy movement, confirmed area
   transitions and cutscenes with `SPU_FALLBACK_STRICT=1`; investigate any new
   entry before promoting a wider interpreter-free claim.
3. **Take semantic control of hot jobs.** An earlier profile identifies image-19
   geometry code, including entry `0xCE08`, as a useful candidate. Matrix blending
   and position transforms are integrated beside compiled references. Recover
   the remaining normal processing/output packing and establish job boundaries
   before replacing more of the compiled tail. Preserve SPU float
   behavior, byte order, alignment, aliasing, DMA visibility and job ordering.
   The older profile is a lead; a current NPC-heavy profile must confirm priority.
4. **Optimize validated kernels.** Use portable scalar C++ as the specification,
   with tested x86 SIMD and future ARM/NEON implementations where useful. SIMD
   must preserve required SPU rounding, denormal, saturation and fused-operation
   semantics. Compiler-generated C alone is not evidence of efficient execution.
5. **Remove reliance on fallback.** After each replacement passes differential
   tests, route native calls to it and log unexpected interpreter entries in
   diagnostic builds. Keep the fatal guard opt-in until broader
   coverage is established. An empty completed-session table alone is insufficient evidence.

## Acceptance for each native replacement

- A documented interface, input bounds, ownership and known behavior.
- Independent arithmetic/boundary tests, plus private real-job comparisons of
  complete outputs and side effects. Partial replays must be labelled partial.
- A native route that actually executes the replacement, including NPC/transition
  cases relevant to it, with remaining fallback calls reported.
- Matched performance measurements before claiming an improvement. Removing a
  small fallback may improve coverage without producing measurable FPS gains.
- Public source contains project-written code and metadata only. Game binaries,
  extracted bytes, generated game source and captures of guest memory stay private.

The final player build should not require Python or development scripts to run.
Build-time generation may continue to use Python. Later replacement of build
tools can be justified by packaging, maintenance or reproducibility needs.

See [execution architecture](ARCHITECTURE.md), the
[fallback census](FINDINGS.md#spu-fallback-census-and-host-runtime-review), and the
[DirectX audit](DIRECTX_PARITY.md). The DirectX shadow investigation and this CPU
work are separate; neither is a claim of completed native coverage.

## SPU reference cross-check

The [SPU overview](https://www.psdevwiki.com/ps3/Synergistic_Processing_Unit_(SPU))
and [documented channels](https://www.psdevwiki.com/ps3/Documented_SPU_Channels)
help check ABI, channel and DMA boundaries while recovering kernels. The
[undocumented channel table](https://www.psdevwiki.com/ps3/Undocumented_SPU_Channels)
mostly covers isolation, loader, persistence, RNG and performance-monitor
facilities. A static scan of all 24 generated images finds no channel calls in
its 64–74 range. Two `rchcnt 113` sites occur beside undecoded words in images
1 and 2; their reachability and meaning are unverified, so they are not treated
as confirmed runtime requests. It does not establish that WKC uses a missing channel. Add behavior
only after an actual decoded instruction or runtime request establishes the need;
do not substitute guessed channel results for correct native execution.

## Refreshed CPU sample limits

The final ten-second Vulkan plains window in `20261019-235407` records one
thread using about 96% of one CPU core. Most of its samples stop inside Windows
calls whose nearest game-side caller is the short `sys_timer_usleep` yield/QPC
loop. Native instructions confirm the two-call deadline loop. This is active
polling overhead, not evidence of SPU arithmetic time or an NPC-spawn cause.
Other samples include guest-memory helpers and renderer conversion/texture work.
DLL-side caller attribution uses a stack scan and is approximate. A controlled
NPC-heavy town profile and matched timing comparison remain required before
choosing a performance fix or assigning whole-frame percentages.
