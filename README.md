# White Knight Chronicles II Recomp

A C++ native-port project for **White Knight Chronicles II**, using static
recompilation of the PS3 game code. Development currently targets **Windows with
Vulkan**, starting with **White Knight Chronicles I**, which is included on the disc.
Linux and Apple Silicon support are longer-term goals.

[![White Knight Chronicles I town running in the native Vulkan build](verification/screenshots/2026-10-19-native-depth-range-front.png)](docs/GALLERY.md)

*Native Windows town capture, 19 October 2026. [View the progress gallery](docs/GALLERY.md)
for more screenshots, including the Graphics Settings menu. Newest images appear first.*

## Current status

The native build reaches the WKC1 starting town and can resume from a local save.
It is an early development build, with the following verified:

- Startup screens, title menus, movie playback and avatar creation.
- Town rendering, short character movement, camera control and pause/resume.
- Logic Stone targeting, saving one starting-town slot and loading it through Continue
  after restarting.
- A dedicated **Triangle → System → Graphics Settings** panel with persistent
  preferences, the game's menu frame and a scrollbar.
- **84 Windows regression checks** passing in both Debug and optimized Release.
  The latest native check loads Continue and moves briefly in town.

The graphics panel includes windowed/borderless/fullscreen modes, named output
resolutions, VSync, anisotropic filtering from Off to 16×, Off/FXAA anti-aliasing,
30/40/60/120/144/Unlocked frame limits and an optional transparent FPS overlay.
Vulkan is the primary renderer. DirectX 12 is an experimental, restart-selected
comparison path with incomplete settings and rendering parity.

Higher frame limits can exceed 30 completed frames/s in short probes. Smooth,
sustained 60 FPS gameplay has not been established. The window-title `Present/s`
counter measures presentations and can include repeated animation states.

## What needs work

- **Performance and game timing:** movement still stutters, and loading and cutscene
  timing need further comparison with RPCS3. Consistent frame pacing is a priority.
- **Rendering:** black flicker at Leonard's feet and the rotating Logic Stone effect
  remain open. The earlier stretched hair/clothing strips and inverted shadows are
  fixed in the inspected town views; full visual fidelity still needs work.
- **Controls:** analogue walk/run conversion has improved, but physical-controller
  consistency, especially at angles, still needs comparison.
- **Gameplay coverage:** sustained traversal, combat, a complete playthrough and
  broader save compatibility remain unverified.
- **Planned features:** draw distance and fog controls, removal of menu background
  blur, and an in-game keyboard navigable with a gamepad.

See the [findings log](docs/FINDINGS.md) for evidence, measurements and investigation
history, and the [runtime audit](docs/KILLZONE_FIX_AUDIT.md) for remaining implementation gaps.

## Building and contributing

This repository contains project source, build tools, SDK patches and verification
records. Building requires privately prepared game files from the **USA BLUS30784
version 01.00 disc**. Game files and generated game sources are kept outside this
repository.

- [Build instructions and diagnostic tools](docs/BUILD.md)
- [Architecture, remaining SPU fallback and modding scope](docs/ARCHITECTURE.md)
- [Contribution workflow](docs/CONTRIBUTING.md)
- [Progress gallery](docs/GALLERY.md)
- [Project icon used by Windows builds](docs/ICON.md)

The game code is translated to C/C++ ahead of time and compiled with a runtime
that implements the PS3 services it uses. Some SPU paths still use an interpreter
fallback. This is not yet a fully static port or a recovered version of the
original source; the architecture page explains the execution model and remaining work.

The project uses the [PS3Recomp SDK](https://github.com/sp00nznet/ps3recomp).
Its pinned revision is recorded in [project.json](project.json), its license is
retained in [LICENSE.ps3recomp](LICENSE.ps3recomp), and local runtime changes are
maintained as patches. Engineering details and patch history live in the
[build documentation](docs/BUILD.md) and [findings log](docs/FINDINGS.md).

## Update 1.01 and DLC plans

Development continues on the original USA disc baseline, with WKC1 tested first.
Future content work includes:

- **USA update 1.01 and DLC:** integrate the updated executable and content, then
  validate loading, gameplay and save compatibility.
- **European DLC:** investigate content differences and compatibility with the USA
  build. An EU-only file entry has been identified; its purpose and compatibility
  remain unverified.
- **Japanese release:** the game, DLC and all ten update packages are available for
  future research. The long-term goal is to add **all Japanese-exclusive content
  with English translations**, plus **optional Japanese voiceovers**.

These are planned additions; update, DLC and cross-region runtime support are not
verified yet. GeoNet compatibility is separate later research.
