# White Knight Chronicles II Recomp

A native C++ port of **White Knight Chronicles II** built by recompiling
the PS3 game code. Development starts with **White Knight Chronicles I**, 
which is included on the disc.

[![White Knight Chronicles I town running in the native Vulkan build](verification/screenshots/2026-10-19-native-town-fps-unlocked.png)](docs/GALLERY.md)

<sub>Native Vulkan town with the FPS overlay, 19 October 2026. Select the image
for capture settings and the progress gallery, newest first.</sub>

<p><a href="docs/GALLERY.md#2026-10-18---native-graphics-settings"><img src="verification/screenshots/2026-10-18-native-graphics-settings.png" width="150" alt="Graphics Settings, 18 Oct" title="Graphics Settings, 18 Oct"></a> <a href="docs/GALLERY.md#2026-10-17--town-stretching-fixed-secondary-bone-physics"><img src="verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics.png" width="150" alt="Town, 17 Oct" title="Town, 17 Oct"></a> <a href="docs/GALLERY.md#2026-10-16--vulkan-avatar-colours-restored"><img src="verification/screenshots/2026-10-16-native-vulkan-avatar-colours.png" width="150" alt="Avatar creation, 16 Oct" title="Avatar creation, 16 Oct"></a> <a href="docs/GALLERY.md#2026-10-16--title-menu"><img src="verification/screenshots/2026-10-16-title-menu.png" width="150" alt="Title menu, 16 Oct" title="Title menu, 16 Oct"></a> <a href="docs/GALLERY.md#2026-10-15--language-selection"><img src="verification/screenshots/2026-10-15-language-selection.png" width="150" alt="First screen, 15 Oct" title="First screen, 15 Oct"></a></p>

<sub>Newest first, back to the first native screen: the milestones are in the
<a href="docs/GALLERY.md#milestones-at-a-glance">gallery timeline</a>.</sub>

## Status

Early development build. The native Windows build reaches the WKC1 starting town and
can resume from a local save. The long-term SPU target is complete replacement
with readable project-written C++; current coverage is partial.

**Verified**

- Startup screens, title menus, movie playback and avatar creation.
- Town rendering, short character movement, camera control and pause/resume.
- Logic Stone targeting, saving one starting-town slot and loading it through
  Continue after restarting.
- A dedicated **Triangle → System → Graphics Settings** panel with persistent
  preferences, the game's menu frame and a scrollbar.
- The town-to-Balastor Plain transition after Continue, with short movement and
  camera control. DirectX foreground clipping is fixed on that route, and
  leaf-shaped foliage shadow cutouts are restored on both renderers.
- **119 Windows regression checks** pass in both normal Debug and optimized Release.

**Graphics settings:** windowed, borderless and fullscreen modes; named output
resolutions; VSync; anisotropic filtering from Off to 16×; Off/FXAA anti-aliasing;
30/40/60/120/144/Unlocked frame limits; and an optional transparent FPS overlay.
Vulkan is the primary renderer. DirectX 12 is an experimental, restart-selected
renderer. Window mode, output size, VSync, filtering, FXAA and the FPS overlay are
integrated. Full rendering and transition parity remain in development.

**Frame rate:** use the 30 FPS baseline. Higher limits can exceed 30 presentations/s,
but area transitions, cutscene skips and walking currently have reported faults
above 30. Window titles show only the game name. The optional in-game FPS
overlay measures presentations and can include repeated animation states.

Desktop launches disable inherited profilers, traces, input injection and GPU
captures before runtime initialization. Normal play writes no diagnostic logs;
the optional in-game FPS overlay remains your choice.

## Known issues

- **Performance and game timing:** movement still stutters, and loading and cutscene
  timing need further comparison with RPCS3. Consistent frame pacing is a priority.
- **Rendering:** black flicker at Leonard's feet and the rotating Logic Stone effect
  remain open. DirectX shadow fidelity and darker grass are under investigation,
  and moving cloud shade and apparent grass density need matched comparisons with
  RPCS3. Earlier stretched hair/clothing strips and inverted shadows are fixed in
  inspected Vulkan views.
- **Controls:** analogue walk/run conversion has improved, but physical-controller
  consistency, especially at angles, still needs comparison.
- **Gameplay coverage:** sustained traversal, combat, a complete playthrough and
  broader save compatibility remain unverified.

Evidence, measurements and investigation history are in the findings log; remaining
runtime implementation gaps are tracked in the runtime audit.

## Roadmap

- **Features:** draw distance and fog controls, removal of menu background blur, and
  an in-game keyboard navigable with a gamepad.
- **USA update 1.01 and DLC:** integrate the updated executable and content, then
  validate loading, gameplay and save compatibility.
- **European DLC:** investigate content differences and compatibility with the USA
  build. An EU-only file entry has been identified; its purpose and compatibility
  remain unverified.
- **Japanese release:** the game, DLC and all ten update packages are available for
  research. The long-term goal is to add **all Japanese-exclusive content with
  English translations**, plus **optional Japanese voiceovers**.
- **Platforms:** Linux and Apple Silicon support are longer-term goals.

Development continues on the original USA disc baseline, with WKC1 tested first.
These are planned additions; update, DLC and cross-region runtime support are not
verified yet. GeoNet compatibility is separate later research.

## How it works

The game code is translated to C/C++ ahead of time and compiled with a runtime that
implements the PS3 services it uses. Tested town-to-plains routes run with the SPU
interpreter fallback forbidden, after five missing SPU entries were added to the
static build. The fallback remains available for untested paths.

Readable, project-written C++ now handles supported whole geometry jobs and all
four known visibility-request modes for admitted inputs: frustum, occlusion,
mask and projected-interval selection. Retained records publish in source order. Enable the experimental route with
`tools/play.ps1 -NativeGeometryJob`; unsupported inputs retain compiled execution
before any native output is published. See the [native execution work](docs/NATIVE_CODE.md#owned-visibility-jobs)
for interfaces, original-job comparisons and live coverage.

Seven ordinary host arithmetic/comparison operations and the extended
negative-product operation use authored C++, with a portable implementation
and AVX2 acceleration for the latter. Reciprocal estimates, integer conversions
and other job/image paths still use the SDK and
generated code. No complete SPU image has been replaced, and the latest
visibility work has no measured FPS gain. Smooth 30 FPS traversal, full-game
static coverage and a readable reconstruction of the original source remain
unverified. Normal startup stays manual, with diagnostics disabled and the
30 FPS baseline recommended.

## Building and documentation

This repository contains project source, build tools, SDK patches and verification
records. Building requires privately prepared game files from the **USA BLUS30784
version 01.00 disc**. Game files and generated game sources are kept outside this
repository.

| Page | Contents |
| --- | --- |
| [Build guide](docs/BUILD.md) | Requirements, private workspace layout, building, regression checks, play and diagnostic options |
| [Findings log](docs/FINDINGS.md) | Verified results, measurements, open issues and investigation history, grouped by topic |
| [Runtime patch notes](docs/PATCHES.md) | What each numbered SDK patch changes and how it was checked, newest first |
| [Runtime subsystem notes](docs/SUBSYSTEMS.md) | Renderer, movie decoding, guest frame presentation and native text entry |
| [Lessons for recompilers](docs/LESSONS.md) | Symptom index and reusable lessons on timing, rendering, SPU jobs and runtime services |
| [Reference](docs/REFERENCE.md) | Glossary, environment variables and known game addresses |
| [Architecture](docs/ARCHITECTURE.md) | Execution model, remaining SPU fallback and modding scope |
| [Progress gallery](docs/GALLERY.md) | Dated screenshots, newest first |
| [Runtime audit](docs/KILLZONE_FIX_AUDIT.md) | Runtime-fix comparison and remaining implementation gaps |
| [Native C++ execution work](docs/NATIVE_CODE.md) | Replacing SPU code with readable C++: evidence, work order and acceptance rules |
| [DirectX parity audit](docs/DIRECTX_PARITY.md) | Vulkan fixes still missing from live DirectX, with evidence and next checks |
| [Code review history](docs/CODE_REVIEW.md) | Review findings, fixes and the checks that were run |
| [Contributing](docs/CONTRIBUTING.md) | Contribution workflow and where new notes belong |

## Credits and license

Built on the [PS3Recomp SDK](https://github.com/sp00nznet/ps3recomp). The pinned SDK
revision is recorded in [project.json](project.json), its license is retained in
[LICENSE.ps3recomp](LICENSE.ps3recomp), and local runtime changes are maintained as
numbered patches.

## Special thanks

Thank you to these projects for the ideas, references and improvements this
port has drawn on:

- [PS3Recomp](https://github.com/sp00nznet/ps3recomp) by sp00nznet and contributors
- [Drakengard 3 recomp](https://github.com/soyjxck/dod3-recomp) and [PS3Recomp fork](https://github.com/soyjxck/ps3recomp) by soyjxck
- [PS3Recomp fork](https://github.com/NoahBPeterson/ps3recomp) by NoahBPeterson
- [PS3Recomp fork](https://github.com/JonathanDC64/ps3recomp) by JonathanDC64
- [inFAMOUS 2 recomp](https://github.com/erikomis/infamous2-recomp) by erikomis
- [Killzone 3 fix catalogue](https://gist.github.com/undecuple/2845432bf13e485634de122084d3fd56) by undecuple
- [Soulcalibur II HD recomp](https://github.com/n0madic/Soulcalibur2HD-recomp) by n0madic
- [RPCS3](https://github.com/RPCS3/rpcs3)
- [envytools](https://github.com/envytools/envytools) and the [PS3 Developer Wiki](https://www.psdevwiki.com/ps3/)
- [FFmpeg](https://ffmpeg.org/) and [SDL](https://www.libsdl.org/)
