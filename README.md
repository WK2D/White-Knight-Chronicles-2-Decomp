# White Knight Chronicles II native-port research

**2026-10-18:** Vulkan batch scheduling improves the saved-town sample to
**28.03 presentations/s**, compared with 27.18 in the repeated baseline.
A locked 30 fps and RPCS3-equivalent smoothness remain goals. Logic Stone saving
and Continue reload work; normal shadows are restored, while black foot flicker remains.

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

**Local save milestone (2026-10-18).** Approaching the
Logic Stone highlights it, X opens its menu, and Save writes a new slot. After
restarting the program, Continue loads that slot and returns to town. This
covers the starting-town save on the same native build; other scenes/slots,
cross-build compatibility and PS3/RPCS3 save interchange remain unverified.

The earlier ten-minute town idle test keeps 58–59 host threads without worker
accumulation and averages 14.25 presentations/s under its recorded diagnostic
conditions. A steady 30 fps and smooth game timing remain goals. The user reports
slower loading and cutscenes, less smooth motion than RPCS3, and inconsistent
analogue walk/run response. See [measurements and limits](docs/FINDINGS.md#frame-pacing-and-game-speed).
Full rendering fidelity, sustained traversal and combat remain unverified.

- [Build instructions](docs/BUILD.md)
- [Contribution workflow](docs/CONTRIBUTING.md)
- [Findings log](docs/FINDINGS.md): verified results, open issues and next steps
- [Progress gallery](docs/GALLERY.md): newest first

The source stack contains **76 SDK patches** and passes **64 Windows regression
checks** in normal Debug and optimized Release. Patch 0076 fixes descriptor-pool
submission churn and feeds small batches to the GPU when the spare slot is ready.
It keeps the game's two-vblank pacing and the unchanged **Present/s** counter.
The counter counts accepted presentations, including repeated animation states.
See [batch scheduling and its measurement limits](docs/FINDINGS.md#vulkan-batch-scheduling-patch-0076).

## Progress

[![Native WKC1 town after restarting and loading the Logic Stone save](verification/screenshots/2026-10-18-native-wkc1-town-batching.png)](docs/GALLERY.md)

The latest capture shows the 76-patch build in the town loaded from the verified
Logic Stone save. The performance change is documented by timed counter samples;
a still image cannot establish smoothness. Black foot flicker remains open.
Earlier captures are in the [gallery](docs/GALLERY.md).

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

- filters unchanged FIFO producer writes, accelerates SPU byte counts and
  separates workers' recursion diagnostics (patches 0062-0064);
- removes repeated SPU conversion-scale calls and shared MFC engine lookups
  (patches 0060-0061); their hidden-window town samples measured 11.5-13.1 fps;
- improves the hidden-window starting-town diagnostic benchmark by about 15%
  by caching launch-time diagnostic settings (patch 0059); visible gameplay
  performance still needs measurement;
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
- passes **64 Windows runtime regression checks** with **76 SDK patches** applied.

Short movement, facing changes, walking animation and camera rotation now
respond to analog input in the starting town. The long stretched strips are
removed in the inspected views; full material fidelity is unverified.
The starting-town local save/reload is verified. Sustained traversal and combat remain unverified. This is not yet
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
  rotation first observed on the 42-patch build and rechecked with all 64 patches.
- **Local save/reload:** starting-town Logic Stone save and restart/Continue verified.
- **Sustained traversal and combat:** pending.
- **In-game keyboard with gamepad navigation, shared across Windows and macOS:**
  planned. The current Windows text-entry window is temporary.
- **Apple Silicon build and testing:** pending.
- **GeoNet compatibility:** separate later research; not established.

## Current investigation

Town performance is the immediate focus: a steady 30 fps first, followed by
validated 60 fps support. The earlier 70-patch visible run slowed progressively while
idle, with thousands of asynchronous SPU workers still alive. The 73-patch
ten-minute idle test shows no worker accumulation. Texture-comparison operand
order is corrected in patch 0074, with normal shadow polarity confirmed in the native town; black foot flicker remains.
The completed passes avoid repeated SPU conversion-scale
CRT calls and replace the fixed MFC registry with independent context-owned
DMA state. They also filter unchanged producer writes, accelerate byte counts
and keep recursion history per host thread. The window-title counter now shows
average presentation rate and long intervals in the visible native window.
New restarts can load the verified town save through Continue. The next investigation
compares update timing, frame pacing, loading and cutscene speed with RPCS3.
Frame pacing and distinct animation updates need further verification. The findings
retain the measured frame counts and comparison limits.

The long town strips came from secondary-bone physics jobs that the runtime
never executed. Patch 0058 implements their queue/port calls and CRT handoff;
the newly extracted job is compiled to native C/C++. The town recheck shows
normal hair and clothing after short movement and camera rotation.
Remaining work includes material fidelity, sustained traversal, combat and
wider save compatibility. The SPU census also identifies static-entry candidates whose
behavior still needs validation.

See [the latest performance findings](docs/FINDINGS.md#town-performance-after-the-physics-fix)
and [the physics trace](docs/FINDINGS.md#secondary-bone-physics-and-town-stretching).
The [full findings log](docs/FINDINGS.md) retains earlier investigations;
[diagnostic probes](docs/BUILD.md#diagnostic-probes) describe reproduction options.
