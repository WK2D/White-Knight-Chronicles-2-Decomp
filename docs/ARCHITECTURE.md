# Execution architecture and modding scope

This project uses static recompilation: the game's PPU and SPU machine code is
translated into C/C++ ahead of time and compiled into the host program, together
with a PS3 compatibility runtime. Static recompilation is a form of binary
recompilation. It differs from dynamic recompilation and interpretation, which
translate or execute code while the game runs.

Execution is not yet fully static. SPU code that the recompiler does not cover
still reaches an interpreter fallback, so the current build is a static
recompilation with an observed interpreter fallback. It is not a verified
interpreter-free port or a recovered, readable version of the original source.

## What executes

- The PPU lifter generates C++ functions from the executable. These sources are
  compiled and linked into the host program; the loader dispatches their function
  pointers. The current generation registers 56,580 PPU functions.
- The SPU build extracts and lifts 24 image/job inputs, compiles their generated
  C, and registers compiled entries by image fingerprint. These include the
  secondary-bone physics job added for the town stretching fix.
- Firmware SPURS job-queue/port calls now execute registered jobs and pass the
  control block required by the job's C runtime startup. The current implementation
  runs jobs synchronously in submission order within each queue. This restores
  the physics output used by Leonard's hair and clothing in the inspected WKC1
  town views; it does not establish full-game scheduling or physics fidelity.
- The SDK also contains an SPU interpreter and a runtime-code microstep path.
  Native census runs record six fallback entry fingerprints in image 17,
  including calls from the microstep path.
  This is observed execution, not merely an unused interpreter in the source tree.
  Registering all 24 inputs does not prove every executed instruction was lifted.
  The proportion of execution using this fallback has not been measured.
- The runtime retains guest memory/register conventions and reimplements PS3
  system interfaces, including threads, files and media services. This is a
  compatibility/emulation layer; claiming there is no emulation would be inaccurate.
- Graphics commands and shaders are translated for the host GPU. Vulkan is the
  primary Windows renderer; D3D12 is an optional fallback. Linux and the shared
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

The [launch-time diagnostic caches](../patches/0059-cache-launch-diagnostics.patch)
reduce host overhead while retaining the same guest jobs and rendering work.
[Conversion scales](../patches/0060-spu-conversion-scales.patch) now avoid repeated
CRT calls, and [context-owned MFC state](../patches/0061-context-owned-mfc.patch)
keeps DMA queues and completion tags independent across logical SPU contexts.
This removes the fixed registry and shared overflow engine; it does not change
the one-owner execution requirement for each context.
[Producer notifications](../patches/0062-gcm-producer-wake.patch) now distinguish
changed commands from unchanged overlapping writes.
[Byte counts](../patches/0063-spu-cntb-sse2.patch) use SSE2 on supported x86 hosts,
with the scalar path retained elsewhere;
[recursion diagnostics](../patches/0064-spu-depth-history-tls.patch) follow the
executing host thread while the depth value remains in its SPU context.
The [Vulkan window-title counter](BUILD.md#vulkan-window-title-counter-patch-0065)
reports accepted queue-presentation rate and the maximum interval. It measures
host presentation timing; unique guest animation frames and display scanout are
not counted. The current label is `Present/s`. Windows compilation and all 63
checks pass with 75 patches in normal Debug and optimized Release; initial
counter display validation is retained at its original
65-patch scope. The [pacing changes](FINDINGS.md#frame-pacing-and-game-speed) honor
real flip requests and the game's two-vblank semaphore gate; they do not divide
the counter or add a host 30 fps cap. The visible 70-patch town run initially
reaches 16.57 presentations/s, then slows to about 7 while idle as asynchronous
SPU workers accumulate. Patch 0072 claims each asynchronous task/workload by
its guest owner before creating a host thread and retains ownership through
cleanup, so duplicate active submissions coalesce. Active owner reuse/removal
returns `BUSY`; cancellation and arbitrary unowned diagnostic dispatch are not
implemented by this change. The 73-patch ten-minute idle test keeps host thread
count at 58–59 while completing 17,104 task workers; no worker accumulation is
observed. Its 14.25 presentations/s average is not a matched speedup comparison
because profiler settings differ and capture work remains enabled. Patches 0071
and 0073 add current-generation depth snapshots and native depth comparison.
The native run exposes a bright foot-shadow fault; patch 0074 corrects comparison
operand order and restores normal shadow polarity in the observed town. Black
foot flicker remains. Patch 0075 routes consumed user interrupts to the registered
native HLE callback. Logic Stone interaction, a local save write and
restart/Continue reload into town are verified; other save compatibility is unverified.
The earlier [starting-town timing comparison](FINDINGS.md#town-performance-after-the-physics-fix)
uses hidden-window diagnostic runs; a separate visible 66-patch baseline is
recorded with its own profiling limits.
The immediate 30 fps target and later 60 fps support remain unverified.

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

The intended interpreter-free path requires covering missing SPU entries and
any runtime-generated code with validated static or specific native implementations.
The first census finds six entry fingerprints in image 17 matching the
corresponding 64-byte windows in the extracted SPU image. That match is not a
whole-function comparison. These are candidates for improving static entry
discovery. The three primary entry hints produce a Windows-compilable
private lift; its behavior and native execution have not been verified.
Disabling the interpreter before validating replacements would break execution.

Validation must measure fallback use along tested routes. Boot, rendering or
gameplay milestones must not be presented as proof of fully static CPU coverage.
The census counts completed fallback sessions rather than all executed
instructions, so it cannot establish a fallback percentage. An empty table alone
would not prove interpreter-free execution. See the
[census findings](FINDINGS.md#spu-fallback-census-and-host-runtime-review)
for the measured route and limits.
