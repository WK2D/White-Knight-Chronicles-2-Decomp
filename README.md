# White Knight Chronicles II Recomp

A native C++ port of **White Knight Chronicles II** built by statically recompiling
the PS3 game code. Development targets **Windows with Vulkan** and starts with
**White Knight Chronicles I**, which is included on the disc.

![White Knight Chronicles I town running in the native Vulkan build](verification/screenshots/2026-10-19-native-depth-range-front.png)

<sub>Native Windows town capture, 19 October 2026. More screenshots, including the
Graphics Settings menu, are in the progress gallery.</sub>

## Status

Early development build. The native Windows build reaches the WKC1 starting town and
can resume from a local save.

**Verified**

- Startup screens, title menus, movie playback and avatar creation.
- Town rendering, short character movement, camera control and pause/resume.
- Logic Stone targeting, saving one starting-town slot and loading it through
  Continue after restarting.
- A dedicated **Triangle → System → Graphics Settings** panel with persistent
  preferences, the game's menu frame and a scrollbar.
- **84 Windows regression checks** pass in both Debug and optimized Release. The
  latest native check loads Continue and moves briefly in town.

**Graphics settings:** windowed, borderless and fullscreen modes; named output
resolutions; VSync; anisotropic filtering from Off to 16×; Off/FXAA anti-aliasing;
30/40/60/120/144/Unlocked frame limits; and an optional transparent FPS overlay.
Vulkan is the primary renderer. DirectX 12 is an experimental, restart-selected
comparison path with incomplete settings and rendering parity.

**Frame rate:** higher frame limits can exceed 30 completed frames/s in short probes,
but smooth, sustained 60 FPS gameplay has not been established. The window-title
`Present/s` counter measures presentations and can include repeated animation states.

## Known issues

- **Performance and game timing:** movement still stutters, and loading and cutscene
  timing need further comparison with RPCS3. Consistent frame pacing is a priority.
- **Rendering:** black flicker at Leonard's feet and the rotating Logic Stone effect
  remain open. The earlier stretched hair/clothing strips and inverted shadows are
  fixed in the inspected town views; full visual fidelity still needs work.
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
implements the PS3 services it uses. Some SPU paths still use an interpreter
fallback, so this is not yet a fully static port, and it is not a recovered version
of the original source. The architecture page explains the execution model and the
remaining work.

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
| [Architecture](docs/ARCHITECTURE.md) | Execution model, remaining SPU fallback and modding scope |
| [Progress gallery](docs/GALLERY.md) | Dated screenshots, newest first |
| [Runtime audit](docs/KILLZONE_FIX_AUDIT.md) | Killzone 3 runtime-fix comparison and remaining implementation gaps |
| [Code review history](docs/CODE_REVIEW.md) | Review findings, fixes and the checks that were run |
| [Contributing](docs/CONTRIBUTING.md) | Contribution workflow and where new notes belong |

## Credits and license

Built on the [PS3Recomp SDK](https://github.com/sp00nznet/ps3recomp). The pinned SDK
revision is recorded in [project.json](project.json), its license is retained in
[LICENSE.ps3recomp](LICENSE.ps3recomp), and local runtime changes are maintained as
numbered patches.
