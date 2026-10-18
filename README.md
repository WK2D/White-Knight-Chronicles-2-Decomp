# White Knight Chronicles II native-port research

**2026-10-18:** Triangle -> System now has its own **Graphics Settings** page:
window mode, named output resolution, VSync, anisotropic filtering, Off/FXAA,
and 30/40/60/120/144/Unlocked frame limits. All controls now appear together in the game's full settings panel, with its
native frame, selection arrows and scrollbar. This includes the FPS overlay and
experimental Vulkan / DirectX 12 selector (restart required).
The window close button now exits the process instead of leaving game threads waiting.
The previous 30 FPS timing gate is now adjustable: short saved-scene probes
reach roughly 37-38 completed frames/s with game time tracking real time.
This is not a locked 60 FPS or full-game compatibility claim. Vulkan remains
the supported renderer; its display settings are not yet implemented on DirectX 12.
Unlocked mode now schedules ticks at frame boundaries instead of flooding callbacks
while polling the FIFO. A follow-up Windows XInput change expands diagonal range,
adds a small outer calibration margin and prevents partially updated pad reports.
Physical walk/run consistency still needs user comparison. The first runtime audit
batch also fixes SPU instruction execution and four PPU subtract TODOs; see
[tests and remaining work](docs/FINDINGS.md#runtime-correctness-and-input-follow-up-patches-0085-0090).
Task completion now follows SPU reservation locking, and flagged task ELF
addresses are decoded correctly. Byte shifts/rotates have a tested SIMD path;
helper microbenchmarks improve, while the town comparison remains mixed. See
[task scheduling and byte operations](docs/FINDINGS.md#spurs-completion-coherence-and-spu-byte-operations-patches-0091-0093).
Vulkan now applies the requested fill-polygon offset, with GPU tests for
constant/slope bias and reset. Saved-town comparisons have not established a
foot/Logic Stone flicker improvement; see [offset validation](docs/FINDINGS.md#vulkan-polygon-offset-patch-0094).
The renderer now honors texture-coordinate overrides and perspective correction,
and shader predication preserves unselected values without arithmetic blending.
Diagnostic probes automatically report known active rendering gaps; see
[request coverage and shader checks](docs/FINDINGS.md#active-rsx-request-coverage-and-fragment-inputs-patches-0095-0096).
Draw distance, fog and removing menu background blur remain pending.

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
covers one starting-town slot, first saved and reloaded with patch 0075 and
subsequently loaded by patch 0076. Other scenes/slots, further cross-build
compatibility and PS3/RPCS3 save interchange remain unverified.

The earlier ten-minute town idle test keeps 58–59 host threads without worker
accumulation and averages 14.25 presentations/s under its recorded diagnostic
conditions. A steady 30 fps and smooth game timing remain goals. The user reports
slower loading and cutscenes and less smooth motion than RPCS3. The analogue
conversion has further changes under test; physical-controller comparison is pending. See [measurements and limits](docs/FINDINGS.md#frame-pacing-and-game-speed).
Full rendering fidelity, sustained traversal and combat remain unverified.

- [Build instructions](docs/BUILD.md)
- [Contribution workflow](docs/CONTRIBUTING.md)
- [Findings log](docs/FINDINGS.md): verified results, open issues and next steps
- [Progress gallery](docs/GALLERY.md): newest first
- [Project icon](docs/ICON.md): approved WKCII artwork used by Windows builds

The source stack contains **96 SDK patches** and passes **82 Windows regression
checks** in normal Debug and optimized Release. The menus, preferences, Vulkan
FXAA initialization and save loading have been exercised in native runs; the
experimental DirectX 12 path also reaches the saved scene and settings menu.
See [settings and validation limits](docs/BUILD.md#native-graphics-settings-patches-0081-0082).

Patch 0079 restores signed texture
LOD bias and distinct sampler-cache entries; see [rendering audit](docs/FINDINGS.md#texture-detail-and-rendering-state-audit-patch-0079).
Patch 0078 keeps exact arithmetic
while computing FNMS lanes together on AVX2; other builds keep the scalar path.
See [conditions and limits](docs/FINDINGS.md#exact-spu-vector-math-patch-0078).
Patch 0077 adds an optional Windows
frame timeline; movement remains uneven, and a controlled speed gain is unverified. Patch 0076 fixes descriptor-pool
submission churn and feeds small batches to the GPU when the spare slot is ready.
It keeps the game's two-vblank pacing and the unchanged **Present/s** counter.
The counter counts accepted presentations, including repeated animation states.
See [batch scheduling and its measurement limits](docs/FINDINGS.md#vulkan-batch-scheduling-patch-0076).

## Progress

[![Native in-game Graphics Settings menu](verification/screenshots/2026-10-18-native-graphics-panel.png)](docs/GALLERY.md)

The gallery includes the latest saved-town shader checks and the native Graphics
Settings panel. These stills do not establish animation smoothness or performance.
See the [newest-first gallery](docs/GALLERY.md) for the System entry, API selector
and explicitly pending draw-distance controls.

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

- adds the native Graphics Settings pages, persistent Vulkan display preferences,
  Off/FXAA, an optional transparent FPS overlay and experimental restart-only API choice;
- removes the hard 30 FPS gate for higher frame-limit options; short stationary
  probes exceed 30 completed frames/s while sampled game time stays close to wall time;

- improves Vulkan batch scheduling; the saved-town sample averages **28.03
  presentations/s** with logs and sampling off (patch 0076; a locked 30 fps is
  still the target);
- restores **Logic Stone targeting and local saving**; Continue reloads the
  verified slot after restart, including loading the patch-0075 save on patch 0076;
- restores **normal shadow polarity** (patch 0074); black foot flicker remains;
- prevents the observed accumulation of duplicate asynchronous SPU workers
  (patch 0072); the 73-patch ten-minute idle test observes no worker accumulation;
- filters unchanged FIFO producer writes, accelerates SPU byte counts and
  separates workers' recursion diagnostics (patches 0062-0064);
- removes repeated SPU conversion-scale calls and shared MFC engine lookups
  (patches 0060-0061); their hidden-window town samples measured 11.5-13.1 fps;
- improves the hidden-window starting-town diagnostic benchmark by about 15%
  by caching launch-time diagnostic settings (patch 0059); this historical
  measurement is separate from the latest visible saved-town comparison;
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
- passes **82 Windows runtime regression checks** with **96 SDK patches** applied.

Short movement, facing changes, walking animation and camera rotation now
respond to analog input in the starting town. The long stretched strips are
removed in the inspected views; full material fidelity is unverified.
The starting-town local save/reload is verified. This is an early prototype;
sustained traversal, combat and a complete playthrough remain unverified.

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
  rotation first observed on the 42-patch build and rechecked at the 64-patch
  checkpoint. The latest performance run is stationary, not a new traversal test.
- **Local save/reload:** starting-town Logic Stone save and restart/Continue verified.
- **Sustained traversal and combat:** pending.
- **In-game keyboard with gamepad navigation, shared across Windows and macOS:**
  planned. The current Windows text-entry window is temporary.
- **Apple Silicon build and testing:** pending.
- **GeoNet compatibility:** separate later research; not established.

## Current investigation

Town performance is the immediate focus: the latest clean saved-town sample
averages 28.03 presentations/s, with a steady 30 fps first and validated 60 fps
support later. Patch 0076 reduces Vulkan batch stalls; smooth motion, longer
frame intervals and matching RPCS3 game speed still need work. The earlier
70-patch visible run slowed progressively while idle, with thousands of asynchronous SPU workers still alive. The 73-patch
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

See [the latest performance findings](docs/FINDINGS.md#vulkan-batch-scheduling-patch-0076)
and [the physics trace](docs/FINDINGS.md#secondary-bone-physics-and-town-stretching).
The [full findings log](docs/FINDINGS.md) retains earlier investigations;
[diagnostic probes](docs/BUILD.md#diagnostic-probes) describe reproduction options.
