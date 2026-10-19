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

The geometry block `0xCE08` has eight captured input/output pairs and eight
passing reference replays. It has 12 local-store stores and no channel calls
within this block. This validates the replay harness, not a rewritten kernel or
whole-job equivalence. No FPS gain is claimed. Both Windows suites pass 106 checks.

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
   geometry code, including entry `0xCE08`, as a useful candidate. Recover its
   input/output layout and behavior, name the operation once established, and
   implement a readable C/C++ kernel beside a reference path. Preserve SPU float
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
