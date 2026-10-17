# White Knight Chronicles II native-port research

Target: USA BLUS30784, disc version 01.00. The goal is native Windows execution
through **static recompilation**, followed by Apple Silicon portability testing.
The game's PS3 machine code is translated to C/C++ ahead of time and compiled
into a normal PC program. A runtime reimplements the PS3 system services the
game calls, such as threads, files, graphics and movie playback.

Static recompilation is one kind of binary recompilation. Emulators such as
RPCS3 instead translate or interpret the game's code while it runs.

The current build compiles 56,580 main-CPU (PPU) functions and 24 known SPU
image/job inputs to native code. Some remaining SPU paths still reach an
interpreter fallback, so these counts do not establish complete static coverage.
See
[execution architecture and modding scope](docs/ARCHITECTURE.md).

**Current milestone (2026-10-17): the native Vulkan build runs the missing
secondary-bone physics job, removing the long stretched strips in the tested
WKC1 town views. Short movement and camera rotation are rechecked.**
Full material and rendering fidelity remain unverified. Sustained traversal,
combat and save/load remain unverified, so this is not a playable port yet.

- [Build instructions](docs/BUILD.md)
- [Contribution workflow](docs/CONTRIBUTING.md)
- [Findings log](docs/FINDINGS.md): verified results, open issues and next steps
- [Progress gallery](docs/GALLERY.md): newest first

## Progress

[![Native WKC1 town after restoring secondary-bone physics; the long strips are gone](verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics.png)](docs/GALLERY.md)

The latest image shows Leonard with his hair and clothing attached to the
model, after restoring native execution of the secondary-bone physics job.
Earlier captures and a rotated view are in the [gallery](docs/GALLERY.md).

The cutscene's red flashes came from a depth-only pass replacing display
buffer 0 and from movie frames sampled through a stale GPU copy; see
[the findings](docs/FINDINGS.md#cutscene-red-flash-and-frame-rate), which also
cover the frame-rate work.

NV0039 memory copies now execute on the native build. During the town run, a
diagnostic re-read of each destination reported no mismatches; this confirms
the writes landed, not that the copy semantics are fully correct. Previously
speckled NPC surfaces look coherent in the inspected views. The remaining
long strips were traced to omitted
physics work and corrected by patch 0058. See the
[implementation and validation results](docs/FINDINGS.md#nv0039-memory-transfers).

Verified progress, with the latest milestone first:

- removes the long **stretched hair and clothing strips** in the tested town
  views by running the previously omitted SPURS queue physics job; all 128
  sampled geometry jobs now have normal referenced vertex bounds;
- removes the **red flashes** in the post-avatar cutscene (two shared-renderer
  faults); earlier optimized measurements improved cutscenes from about 11 to
  25-31 fps, with the town at about 7 fps;
- boots **straight to the title** with the optional `WKC2_SKIP_INTRO` switch;
- improves previously speckled **NPC surfaces** in the starting town after NV0039
  transfer support; full visual fidelity remains unverified;
- reaches the **live WKC1 town** after avatar acceptance and opening cutscene
  skips, showing Leonard, NPCs, the minimap and a save crystal;
- responds to short left-stick movement, facing changes and walking animation,
  right-stick camera rotation, and pause/resume input;
- retains a name through native text entry, Quick Start and **final avatar Review**,
  then accepts the avatar and enters the **WKC1 opening movie**;
- renders the WKC1 **Customize Avatar** screen, including the **3D room with normal
  depth testing** and the **avatar body geometry**, including arms, hands, trousers
  and boots, with **face shading and textured clothing**;
- accepts controller input to open New Game and select White Knight Chronicles I,
  with description and dialog text rendering correctly;
- renders the title menu over an animated background and plays LPCM startup logos
  and AC-3 title/attract movies through the native movie pipeline;
- passes **51 Windows runtime regression checks** with **58 SDK patches** applied.

Short movement, facing changes, walking animation and camera rotation now
respond to analog input in the starting town. The long stretched strips are
removed in the inspected views; full material fidelity is unverified.
Sustained traversal, combat and save/load remain unverified. This is not yet
a playable port.

Vulkan is the primary Windows renderer. It now renders the avatar and room
without its initial red tint after correcting colour-target feedback. D3D12
remains available as an optional comparison/fallback build. Both use the
statically recompiled build described above. Full-game rendering remains unverified.

## Update 1.01 and DLC plans

We will add support for the USA **1.01 update and DLC** as part of this port.
Development currently uses the original BLUS30784 v01.00 disc executable as its
baseline, with gameplay testing starting in White Knight Chronicles I.

- **Update 1.01:** the supplied update's metadata confirms BLUS30784, APP_VER 01.01.
- **USA DLC:** a matching package has been identified. Integration will cover the
  updated executable and patch data, DLC discovery/loading, and gameplay/save
  compatibility.
- **European DLC:** we will compare it for content absent from the USA package and
  investigate compatibility with BLUS30784. Initial file comparison finds one EU-only
  entry; what it unlocks and whether it works in the USA build are unverified.
- **Japanese release:** the game, DLC and ten update packages are available for future
  research. The long-term goal is to bring **all Japanese-exclusive content into the
  port with English translations**, and offer **optional Japanese voiceovers**.

**Update 1.01 and DLC are planned work; runtime support is not yet verified.**
Content differences, translations and cross-region integration still need
investigation and validation.

## Workspace layout

This repository holds the source project. Private game files, generated game code,
build directories, downloaded tools and dependency checkouts live in a separate
private workspace's `work/` directory. Reviewed screenshots are published separately
in the gallery.

The port uses the [PS3Recomp SDK](https://github.com/sp00nznet/ps3recomp). Its pinned
revision is recorded in `project.json`, and its license is retained in
`LICENSE.ps3recomp`. Runtime changes must be recorded as patches rather than silently
updating that revision.

## Analysis

Using a Python environment with the SDK requirements installed:

```text
python tools/analyze.py --sdk <ps3recomp-checkout> --elf <EBOOT.elf> --output <analysis-directory>
```

This runs the SDK loader, bounds candidate functions to executable sections,
resolves firmware import names and inventories generated HLE handlers. The
coverage report distinguishes handler presence from verified implementation.

## Milestones

- **Disc metadata and executable identified:** complete.
- **Retail SELF decrypted locally using RPCS3 CLI:** complete.
- **Function/import/SPU inventory:** initial pass complete. 56,580 lifted PPU functions;
  five embedded SPU ELFs and 19 executable-backed raw SPURS jobs; 24 registered SPU
  images. Clean extraction and static lifting reproduce all 24 image inputs from the
  baseline executable without runtime captures.
- **Native executable build:** passes on Windows using the pinned SDK, Clang 23.1.3,
  local Windows SDK/MSVC headers and DirectX-Headers. `tools/build.ps1` reproduces the
  build from this workspace's prepared inputs; `-Regenerate` also runs the lift.
- **Native startup:** executes the game, its SPU tasks/jobs, filesystem operations,
  graphics initialization and audio initialization. GPU readback confirms the
  language-selection screen, copyright page and warning screen render natively.
- **Initial UI input:** a simulated CROSS press selects English and advances startup.
  Later menu input, avatar acceptance and limited town controls are verified below.
- **Title screen and menu input:** verified on Windows.
- **Readable New Game and WKC1 selection dialogs:** verified on Windows.
- **WKC1 avatar-customization UI:** renders and dismisses its introductory notice; the
  default male character is visible with face shading and textured clothing. Name entry
  is retained through Quick Start to final Review and the start-game confirmation. Full
  visual fidelity remains unverified. Avatar acceptance and entry into the town
  have now been observed.
- **Avatar confirmation enters the WKC1 opening movie:** verified with freshly
  regenerated PPU/SPU code. This checkpoint covers prerecorded movie playback;
  live town rendering and limited controls are verified separately below.
- **WKC1 town world view and pause/resume:** observed on the native Vulkan build
  after the opening cutscenes. NPC surfaces improve with patch 0052; patch 0058
  removes the long strips in the inspected views.
- **Initial town controls:** short analog movement, facing changes and camera
  rotation first observed on the 42-patch build and rechecked with all 58 patches.
- **Sustained traversal, combat and save/load verification:** pending.
- **In-game keyboard with gamepad navigation, shared across Windows and macOS:**
  planned. The current Windows text-entry window is temporary.
- **Apple Silicon build and testing:** pending.
- **GeoNet compatibility:** separate later research; not established.

## Current investigation

The long town strips came from secondary-bone physics jobs that the runtime
never executed. Patch 0058 implements their queue/port calls and CRT handoff;
the newly extracted job is compiled to native C/C++. The town recheck shows
normal hair and clothing after short movement and camera rotation.
Remaining work includes material fidelity, sustained traversal, combat and
save/load. The SPU census also identifies static-entry candidates whose
behavior still needs validation.

See [the latest native findings](docs/FINDINGS.md#secondary-bone-physics-and-town-stretching)
for the physics trace and town comparison, [the full findings log](docs/FINDINGS.md) for
earlier investigations, and [diagnostic probes](docs/BUILD.md#diagnostic-probes)
for reproduction options.
