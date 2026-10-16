# White Knight Chronicles II native-port research

Target: USA BLUS30784, disc version 01.00. The goal is native Windows execution
using binary recompilation to C/C++ and a PS3 compatibility runtime, followed by
Apple Silicon portability testing.

**The current build is hybrid:** PPU and SPU code is compiled ahead of time, but
an SPU interpreter fallback is also present and has executed in observed runs.
This is not a complete source-level decompilation or a verified interpreter-free
port. See [execution architecture and modding scope](docs/ARCHITECTURE.md).

**This is not a playable port yet.** Analysis, compilation, boot, rendering and
gameplay are distinct milestones. Passing an analysis or build step does not
establish that the game works.

- [Build instructions](docs/BUILD.md)
- [Contribution workflow](docs/CONTRIBUTING.md)
- [Findings log](docs/FINDINGS.md): verified results, open issues and next steps
- [Progress gallery](docs/GALLERY.md): newest first

## Progress

[![Native Vulkan WKC1 town with visible geometry faults](verification/screenshots/2026-10-16-native-wkc1-town.png)](docs/GALLERY.md)

The latest image shows the live WKC1 town after the opening cutscenes, with
Leonard, NPCs, the minimap and a save crystal visible. Stretched geometry and
corrupt NPC surfaces remain. Earlier captures are in the [gallery](docs/GALLERY.md).

So far the native build:

- renders the title menu, with New Game, Continue and Options, over an animated background;
- plays LPCM startup logos and AC-3 title/attract movies through the native movie pipeline;
- accepts controller input to open New Game and select White Knight Chronicles I,
  with description and dialog text rendering correctly;
- reaches the WKC1 **Customize Avatar** screen, rendering the **3D room with normal
  depth testing** and the **avatar body geometry**, including arms, hands, trousers
  and boots, with **face shading and textured clothing**;
- retains a name entered through native text entry through Quick Start to the
  **final avatar Review**;
- accepts the avatar and enters the **WKC1 opening movie**;
- advances through the opening cutscenes to the **live WKC1 town**, with NPCs,
  the minimap and a save crystal visible, and responds to pause/resume input.

Town rendering still has obvious geometry and material faults. Movement, camera
control, combat and save/load remain unverified; this is not yet a playable port.

Vulkan is the primary Windows renderer. It now renders the avatar and room
without its initial red tint after correcting colour-target feedback. D3D12
remains available as an optional comparison/fallback build. Both use the
host-compiled hybrid build described above. Full-game rendering remains unverified.

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
  five embedded SPU ELFs and 18 executable-backed raw SPURS jobs; 23 registered SPU
  images. Clean extraction and static lifting reproduce all 23 image inputs from the
  baseline executable without runtime captures.
- **Native executable build:** passes on Windows using the pinned SDK, Clang 23.1.3,
  local Windows SDK/MSVC headers and DirectX-Headers. `tools/build.ps1` reproduces the
  build from this workspace's prepared inputs; `-Regenerate` also runs the lift.
- **Native startup:** executes the game, its SPU tasks/jobs, filesystem operations,
  graphics initialization and audio initialization. GPU readback confirms the
  language-selection screen, copyright page and warning screen render natively.
- **Initial UI input:** a simulated CROSS press selects English and the screen fades
  into the next startup stage. Further input and gameplay remain under test.
- **Title screen and menu input:** verified on Windows.
- **Readable New Game and WKC1 selection dialogs:** verified on Windows.
- **WKC1 avatar-customization UI:** renders and dismisses its introductory notice; the
  default male character is visible with face shading and textured clothing. Name entry
  is retained through Quick Start to final Review and the start-game confirmation. Full
  visual fidelity remains unverified. Avatar acceptance and entry into the town
  have now been observed.
- **Avatar confirmation enters the WKC1 opening movie:** verified with freshly
  regenerated PPU/SPU code. This does not establish interactive 3D gameplay.
- **WKC1 town world view and pause/resume:** observed on the native Vulkan build
  after the opening cutscenes. Geometry and NPC materials remain visibly corrupt.
- **Movement, camera control, combat and save/load verification:** pending.
- **In-game keyboard with gamepad navigation, shared across Windows and macOS:**
  planned. The current Windows text-entry window is temporary.
- **Apple Silicon build and testing:** pending.
- **GeoNet compatibility:** separate later research; not established.

## Current investigation

The detailed investigation log, covering runtime fixes, SPU work, movie decoding and
renderer changes in the order they were made, is in the
[findings log](docs/FINDINGS.md#investigation-history). Diagnostic probe options are in
[BUILD.md](docs/BUILD.md#diagnostic-probes).
