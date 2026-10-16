# Native port progress gallery

Screenshots captured from native Windows x86-64 execution with the D3D12 backend,
using USA BLUS30784 v01.00. These are game-frame GPU readbacks, losslessly encoded
as PNG. All optional metadata has been removed. Images contain the game frame only.

**Current status:** the native title screen renders with New Game, Continue,
Options and an animated background. AC-3 and LPCM audio decode, and idle startup
enters the attract movie. Menu interaction, gameplay and save/load remain unverified;
complete audio fidelity and timing against original hardware remain unverified.

Newest milestones appear first. Future captures will be added at the top of the gallery.

## 2026-10-16 — Title menu

The native title menu displays New Game, Continue and Options over the animated
background. Successive captures show the title artwork changing. With no input,
the guest subsequently opens its attract movie. This capture proves menu rendering;
menu response and gameplay need separate validation.

![Native White Knight Chronicles II title menu](../verification/screenshots/2026-10-16-title-menu.png)

## 2026-10-16 — Startup advances to Havok / Dolby

The same native run reaches the Havok and Dolby Digital screen after the studio
logos. This is a later startup stage, before the AC-3 title background was supported.

![Native Havok and Dolby Digital startup screen](../verification/screenshots/2026-10-16-havok-dolby.png)

## 2026-10-16 — Japan Studio

Japan Studio appears as startup proceeds beyond the earlier movie stall.

![Native Japan Studio logo](../verification/screenshots/2026-10-16-japan-studio.png)

## 2026-10-16 — Sony presents

Startup advances to the Sony Computer Entertainment presentation screen.

![Native Sony Computer Entertainment presents screen](../verification/screenshots/2026-10-16-sony-presents.png)

## 2026-10-16 — D3Publisher logo playback

The first logo movie now animates through to the formed D3Publisher logo.
Successive GPU readbacks show different movie frames, and the guest then closes
and reopens the decoders for the next startup movie without a skip input.

![Native D3Publisher logo after its animation](../verification/screenshots/2026-10-16-d3publisher-logo.png)

## 2026-10-16 — First decoded logo frame

The startup movie now passes through real MPEG-2 decoding, YUV-to-RGBA processing
and the native renderer. This image is a GPU readback of the first logo sequence.
At this milestone, later captures held this image and continuous animation was unverified.
Audio output was still a placeholder at this milestone. This establishes visible decoded video,
not completed playback or title-screen rendering.

![Decoded startup logo image rendered natively](../verification/screenshots/2026-10-16-first-logo-frame.png)

## 2026-10-15 — Autosave notice

The autosave notice renders at a later startup stage. This screenshot establishes
that the notice displays; it does not establish that saving or loading works.

![Native autosave notice with warning icon and text](../verification/screenshots/2026-10-15-autosave-notice.png)

## 2026-10-15 — Startup warning

The warning icon, panel artwork and instruction-manual notice render after language
selection. This establishes another visible startup stage.

![Native startup warning with icon, panel artwork and text](../verification/screenshots/2026-10-15-startup-warning.png)

## 2026-10-15 — Language selection

The background, cursor, highlighted English option and other language options render.
A simulated CROSS press through the controller path selects English and advances startup.

![Native language selection showing English, Français and Español](../verification/screenshots/2026-10-15-language-selection.png)

## Evidence and updates

All images are 1280 × 720. Every pixel was compared with the original BMP readback
before publication. The [capture manifest](../verification/gallery.json) records
source and PNG hashes. Full probe logs and original captures remain private.

New verified visual milestones will be added here as development progresses.
Captions state the observed behavior; screenshots alone do not prove unrelated
features or full-game compatibility.

Game artwork and text belong to their respective rights holders; the project's
source-code license does not relicense game content shown in these screenshots.
