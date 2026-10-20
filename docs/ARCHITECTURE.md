# Execution architecture and modding scope

This project uses static recompilation: the game's PPU and SPU machine code is
translated into C/C++ ahead of time and compiled into the host program, together
with a PS3 compatibility runtime. Static recompilation is a form of binary
recompilation. It differs from dynamic recompilation and interpretation, which
translate or execute code while the game runs.

Full-game static coverage is not yet verified. Earlier probes reached SPU
interpreter fallback; five newly compiled entries remove the observed cases on a
strict saved-town route. Fallback remains available for untested paths. This is
not a recovered, readable version of the original source.

## What executes

- The PPU lifter generates C++ functions from the executable. These sources are
  compiled and linked into the host program; the loader dispatches their function
  pointers. The current generation registers 56,580 PPU functions.
- The SPU build extracts and lifts 24 image/job inputs, compiles their generated
  C, and registers compiled entries by image fingerprint. These include the
  secondary-bone physics job added for the town stretching fix.
- One image-19 geometry prefix now uses a readable C++ four-influence matrix
  blend and a complete supported batch boundary with a narrow guest-state bridge:
  positions, cofactors, first normal products, local-store output and read-ahead.
  Its retained
  compiled reference and
  full-state replay/fuzz checks are described in [native execution work](NATIVE_CODE.md).
  Later normal accumulation/output packing and the rest of the job remain
  compiled guest code. Host/audit nonfinite inputs retain the compiled C tail;
  exact-SPU mode accepts all arithmetic bit patterns.
- Firmware SPURS job-queue/port calls now execute registered jobs and pass the
  control block required by the job's C runtime startup. The current implementation
  runs jobs synchronously in submission order within each queue. This restores
  the physics output used by Leonard's hair and clothing in the inspected WKC1
  town views; it does not establish full-game scheduling or physics fidelity.
- The SDK also contains an SPU interpreter and a runtime-code microstep path.
  Reference runs recorded six fallback rows in image 17 and four in image 19.
  Five added static entries cover those observed cases. A seven-minute saved-town
  route with both fallback entrances made fatal completes with zero entries.
  Registering all 24 inputs still does not prove full-game coverage, and the
  earlier fallback proportion was not measured.
- The runtime retains guest memory/register conventions and reimplements PS3
  system interfaces, including threads, files and media services. This is a
  compatibility/emulation layer; claiming there is no emulation would be inaccurate.
- Graphics commands and shaders are translated for the host GPU. Vulkan is the
  primary Windows renderer; DirectX 12 is an experimental restart-selected
  comparison path with incomplete feature/rendering parity. Linux and the shared
  engine's Metal path on macOS still require integration and testing.

The build does not run through an RPCS3 executable. That fact alone does not
establish the absence of CPU interpretation or other emulation techniques.

Implementation points are the pinned SDK's
[PPU loader/dispatch](https://github.com/sp00nznet/ps3recomp/blob/a2c20108af45174dbb7d3e2f87f6acaaef09c6b4/runtime/ppu/ppu_loader.cpp),
[SPU interpreter interface](https://github.com/sp00nznet/ps3recomp/blob/a2c20108af45174dbb7d3e2f87f6acaaef09c6b4/runtime/spu/spu_interp.h)
and this project's [generated-code hook tool](../tools/apply_port_hooks.py).
The pinned SDK is built with the project's [runtime patches](BUILD.md),
including [SPURS queue/port execution](../patches/0058-spurs-job-queue-secondary-bones.patch)
and [fallback profiling](../patches/0042-spu-interp-profile.patch).

## Scheduling, rendering and timing

Each SPU context owns its DMA queues and completion tags. It requires one execution
owner at a time. Asynchronous task/workload ownership is retained through cleanup
so duplicate active submissions coalesce; active owner reuse/removal returns
`BUSY`. Cancellation and arbitrary unowned diagnostic dispatch are not implemented
by that ownership mechanism. The [worker lifetime investigation](FINDINGS.md#frame-pacing-and-game-speed)
records the fix and the ten-minute idle validation.

The runtime honors real flip requests and the game's vblank/semaphore pacing.
Graphics Settings adjusts both the host frame cap and the guest timing hooks;
raising a host cap alone does not unlock the game. Window captions remain static. The optional in-game FPS overlay measures
presentations; it does not count distinct guest animation updates or display scanout. See
[graphics settings and timing](PATCHES.md#native-graphics-settings-patches-0081-0082)
for the current controls and validation limits.

Vulkan translates the game's rendering state and shaders to the host GPU.
Visibility reports still use conservative placeholders rather than measured GPU
sample counts, and rendering coverage remains incomplete. The
[runtime audit](KILLZONE_FIX_AUDIT.md) tracks remaining gaps; the
[findings log](FINDINGS.md) retains the rendering fixes, performance comparisons
and their original test conditions. Smooth sustained gameplay and locked higher
frame rates remain unverified.

The runtime dispatches consumed user interrupts to registered native HLE callbacks.
This restores the route used by Logic Stone interaction and saving. One starting-town
slot has been saved locally and reloaded through Continue after restart; wider
save compatibility remains unverified.

Generated game sources and game inputs remain private. The public repository
contains the build tools, project code, runtime patches and verification records.

## What this means for mods

Binary recompilation does not inherently prevent modification. The project
already inserts a host function hook into a generated PPU function for content
installation synchronization. This demonstrates a code-integration mechanism,
not a general mod loader or proven gameplay mod support.

| Kind of modification | Work still needed |
| --- | --- |
| Textures, text, audio and models | Understand the relevant formats and add controlled asset overrides or repacking tools |
| Gameplay logic and balance | Identify functions/data, document them and provide stable hooks or replacements |
| Loadable community mods | Define a mod API, versioning, load order and compatibility rules |
| Larger engine/content changes | Recover enough behavior and data structures to implement and validate each change |

A source-level decompilation would make many changes easier to understand, but
it is not a prerequisite for all modding. None of the entries above is a claim
that a supported WKC modding framework currently exists.

## Remaining CPU work

The [native execution work plan](NATIVE_CODE.md) separates removal of interpreter
fallback from readable replacements for already compiled hot jobs. Python tools
perform build-time generation and diagnostics; they are not the gameplay CPU path.

Five additional entries in images 17 and 19 are integrated through a hash-checked
build manifest. Forty private replays match complete context, GPRs, local store
and continuation against the former fallback. Patch 0116 initializes running
status before compiled dispatch; 0117 can reject either fallback entrance before
it executes. The scoped native route and remaining coverage are recorded in
[native execution work](NATIVE_CODE.md).

An empty completed-session census alone would miss unfinished calls. Strict
entry guards supply stronger route evidence, with negative controls for both
interpreter and microstep paths. Broader NPC, transition and cutscene coverage,
readable hot-job kernels and complete scheduling validation remain required.
