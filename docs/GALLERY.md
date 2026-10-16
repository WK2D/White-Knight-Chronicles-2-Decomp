# Native port progress gallery

Screenshots captured from native Windows x86-64 execution with the D3D12 backend,
using USA BLUS30784 v01.00. These are game-frame GPU readbacks, losslessly encoded
as PNG. All optional metadata has been removed. Images contain the game frame only.

**Current status:** startup UI renders and language selection responds to controller
input. A decoded frame from the first logo movie now reaches the renderer.
Continuous movie playback stalls; correct audio, the title screen, gameplay and
save/load are still unverified.

## 2026-10-15 — Language selection

The background, cursor, highlighted English option and other language options render.
A simulated CROSS press through the controller path selects English and advances startup.

![Native language selection showing English, Français and Español](../verification/screenshots/2026-10-15-language-selection.png)

## 2026-10-15 — Startup warning

The warning icon, panel artwork and instruction-manual notice render after language
selection. This establishes another visible startup stage.

![Native startup warning with icon, panel artwork and text](../verification/screenshots/2026-10-15-startup-warning.png)

## 2026-10-15 — Autosave notice

The autosave notice renders at a later startup stage. This screenshot establishes
that the notice displays; it does not establish that saving or loading works.

![Native autosave notice with warning icon and text](../verification/screenshots/2026-10-15-autosave-notice.png)

## 2026-10-16 — First decoded logo frame

The startup movie now passes through real MPEG-2 decoding, YUV-to-RGBA processing
and the native renderer. This image is a GPU readback of the first logo sequence.
Later captures hold this image, so continuous animation remains unverified.
Audio output is still a placeholder. This establishes visible decoded video,
not completed playback or title-screen rendering.

![Decoded startup logo image rendered natively](../verification/screenshots/2026-10-16-first-logo-frame.png)

## Evidence and updates

All images are 1280 × 720. Every pixel was compared with the original BMP readback
before publication. The [capture manifest](../verification/gallery.json) records
source and PNG hashes. Full probe logs and original captures remain private.

New verified visual milestones will be added here as development progresses.
Captions state the observed behavior; screenshots alone do not prove unrelated
features or full-game compatibility.

Game artwork and text belong to their respective rights holders; the project's
source-code license does not relicense game content shown in these screenshots.
