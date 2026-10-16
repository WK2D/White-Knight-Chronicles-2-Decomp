# Native port progress gallery

Screenshots captured from native Windows x86-64 execution with D3D12 or Vulkan,
using USA BLUS30784 v01.00. These are game-frame GPU readbacks, losslessly encoded
as PNG. All optional metadata has been removed. Images contain the game frame only.

These captures come from a host-compiled recompilation build with a PS3 runtime
layer and observed SPU interpreter fallback. They do not establish
interpreter-free execution; see [architecture](ARCHITECTURE.md).

**Current status:** the native Vulkan build accepts the WKC1 avatar, advances
through the opening cutscenes and reaches the live town view. Leonard, NPCs,
the minimap and a save crystal are visible; pause/resume responds to input.
Stretched geometry and corrupt NPC surfaces remain. Movement, camera control,
combat and save/load have not been verified. Complete audio fidelity and timing
against original hardware also remain unverified.

Newest milestones appear first. Future captures will be added at the top of the gallery.
Each dated entry records what was verified at that checkpoint; later entries
supersede earlier limitations when further testing establishes progress.

## 2026-10-16 — WKC1 town world view

The native Vulkan build now reaches the town after avatar acceptance and the
opening cutscenes. Leonard, the surrounding buildings, NPC names, minimap and
save crystal appear in live 3D. Pause/resume input was checked separately in
the same run. Fixing a runtime cutoff that halted valid SPU completion-label
polling lets this run pass the previous loading stall.

There are still obvious stretched triangles and corrupt NPC surfaces, visible
in this unchanged readback. Movement, camera control, combat and save/load
remain unverified. This is a world-view milestone, not a playable-port claim.
The 1920×1080 display readback contains the game's 1280×720 scene.

![Native WKC1 town showing Leonard, NPCs, minimap and save crystal, with visible geometry faults](../verification/screenshots/2026-10-16-native-wkc1-town.png)

## 2026-10-16 — Vulkan avatar colours restored

The initial Vulkan colour-feedback fix shows the WKC1 avatar and room without the
red tint seen in its first build. A post-processing draw reads the colour target
it is updating; preserving the preceding pixels in a separate GPU image restores
the missing colour channels. The entered name is retained. This is live 3D
rendering from the hybrid C/C++ build with the shared RSX draw engine.
The 1920×1080 display readback contains the game's 1280×720 scene. Full visual
fidelity, interactive gameplay, save/load and macOS execution remain unverified.

![Native Vulkan WKC1 avatar and room after colour-feedback correction](../verification/screenshots/2026-10-16-native-vulkan-avatar-colours.png)

## 2026-10-16 — WKC1 opening movie after avatar confirmation

The native executable now accepts the avatar and enters the WKC1 opening movie. Presenting completed batches at guest FIFO flips prevents the small movie batches from being withheld by the earlier scene threshold. This capture shows prerecorded movie footage. Interactive 3D gameplay and save/load remain unverified. The run uses freshly regenerated PPU and SPU code from the baseline executable.

![Native WKC1 opening movie after avatar confirmation](../verification/screenshots/2026-10-16-native-wkc1-opening-movie.png)

## 2026-10-16 — Character name retained through avatar review

Native WKC1 character creation now retains the entered name and reaches the final Review page through Quick Start. The avatar, face shading and textured clothing render with normal depth testing and culling. This capture verifies name retention and review navigation; entering gameplay and save/load remain unverified. A gamepad-operated in-game keyboard is planned; the Windows text-entry window is temporary.

![Native Character name retained through avatar review](../verification/screenshots/2026-10-16-native-avatar-review.png)

## 2026-10-16 — Avatar face and clothing shading

The native WKC1 character creator now shows a shaded face and textured clothing after correcting packed vertex normals and fragment source-range modifiers. The complete default male avatar is visible with normal depth testing and culling. Captured on the original version 1.00 baseline. Full visual fidelity, completed character creation, gameplay and save/load remain unverified.

![Native Avatar face and clothing shading](../verification/screenshots/2026-10-16-native-avatar-shading.png)

## 2026-10-16 — Avatar body geometry

The avatar body now appears in the native WKC1 character creator after correcting SPU FNMS extended-range arithmetic. Previously missing arms, hands, trousers and boots are visible with normal depth and culling. Face shading and clothing materials remain incorrect; completed character creation, gameplay and save/load are still unverified. Captured on the original version 1.00 baseline.

![Native Avatar body geometry](../verification/screenshots/2026-10-16-native-avatar-body.png)

## 2026-10-16 — 3D room with normal depth

The character-creation room now renders with normal depth testing after correcting indexed triangle strips and primitive restart. Some avatar pieces appear, but the body is incomplete. Captured from native C/C++ execution on the original version 1.00 baseline after selecting White Knight Chronicles I. Completed character creation, gameplay and save/load remain unverified.

![Native 3D room with normal depth](../verification/screenshots/2026-10-16-native-3d-room.png)

## 2026-10-16 — Avatar customization UI

Selecting White Knight Chronicles I now reaches Customize Avatar. Controller input dismisses the introductory notice. Text and menu controls render, but the character model is missing. Gameplay, completed character creation and save/load remain unverified. Captured on the original version 1.00 baseline.

![Native Avatar customization UI](../verification/screenshots/2026-10-16-avatar-customization.png)

## 2026-10-16 — White Knight Chronicles I selected

The first White Knight Chronicles is selected through native controller input. Its description and selection dialog render correctly. Confirming this choice reaches a job with an empty SPU binary source during loading; no playable scene is verified.

![Native White Knight Chronicles I selected](../verification/screenshots/2026-10-16-wkc1-selection.png)

## 2026-10-16 — Readable New Game description

The native New Game description is readable after fixing packed font texture bindings. This verifies this menu text; gameplay and save/load remain unverified.

![Native Readable New Game description](../verification/screenshots/2026-10-16-readable-new-game.png)

## 2026-10-16 — New Game submenu

Live controller input moves the title highlight and opens New Game. Its submenu renders the book artwork, labels and cursor. Description text is visibly corrupted into blocks. A further confirm opens a dialog with the same glyph issue; starting gameplay remains unverified.

![Native New Game submenu](../verification/screenshots/2026-10-16-new-game-menu.png)

## 2026-10-16 — Title menu

The native title menu displays New Game, Continue and Options over the animated
background. Successive captures show the title artwork changing. With no input,
the guest subsequently opens its attract movie. This capture proves menu rendering;
menu response was validated later; gameplay remains unverified.

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

D3D12 captures are 1280 × 720; Vulkan display captures are 1920 × 1080 and
contain the game's 1280 × 720 scene. Every published pixel was compared with
the original BMP or PPM GPU readback before publication. The [capture manifest](../verification/gallery.json) records
source and PNG hashes. Full probe logs and original captures remain private.

New verified visual milestones will be added here as development progresses.
Captions state the observed behavior; screenshots alone do not prove unrelated
features or full-game compatibility.

Game artwork and text belong to their respective rights holders; the project's
source-code license does not relicense game content shown in these screenshots.
