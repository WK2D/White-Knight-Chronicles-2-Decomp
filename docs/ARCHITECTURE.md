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
The [starting-town timing comparison](FINDINGS.md#town-performance-after-the-physics-fix)
remains below the immediate 30 fps target; 60 fps support is a later validation goal.

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
