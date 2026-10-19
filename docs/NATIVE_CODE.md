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

Fresh 115-patch DirectX Continue probe `20261019-204445` records 661 completed fallback sessions and 2,868 steps across the same six image-17 fingerprints. A 12.000-second actual-IP sample finds 222 hits at compiled geometry entry `0xCE08` among 4,779 sampling slots on that worker. The in-process profiler and framebuffer capture are enabled, and sampling briefly suspends threads; these are prioritization evidence, not a benchmark or whole-frame percentage. No native replacement is integrated by this checkpoint.

## Work order

1. **Refresh runtime evidence.** Capture fallback sessions and sampled CPU time
   on Continue, NPC-heavy movement, area transitions and cutscenes. Record which
   build and routes were measured. Distinguish waiting from executing code and
   per-worker samples from whole-frame cost.
2. **Complete image-17 static coverage.** The retained census has six entry
   fingerprints. Three primary entry hints (`0x7D60`, `0x7E38`, `0x7E78`) already
   produce a private compiling lift. Verify complete code regions and all
   reachable targets, then compare outputs, registers, local store, DMA effects
   and exits against the existing implementation. The existing 64-byte matches
   do not establish whole-function equivalence. No replacement is promoted yet.
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
   diagnostic builds. Only make fallback fatal for release routes after broader
   coverage. An empty completed-session table alone is insufficient evidence.

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
