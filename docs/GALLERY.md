# Native port progress gallery

Screenshots captured from native Windows x86-64 execution with D3D12 or Vulkan,
using USA BLUS30784 v01.00. These are game-frame GPU readbacks, losslessly encoded
as PNG. All optional metadata has been removed. Images contain the game frame only.

These captures come from a host-compiled recompilation build with a PS3 runtime
layer and observed SPU interpreter fallback. They do not establish
interpreter-free execution; see [architecture](ARCHITECTURE.md).

**Current status:** the 76-patch native Windows Vulkan build verifies Logic Stone
proximity targeting, X opening its menu, Save writing a new local slot, and
Continue returning to town after a full restart. All 64 Windows tests pass in
normal Debug and optimized Release. Other NPC/object coverage and wider save
compatibility remain unverified. The patch-0075 slot also loads on patch 0076.

The latest clean one-minute saved-town sample averages 28.03 presentations/s;
a locked 30 fps remains unverified. The long hair/clothing strips are removed
in inspected views, and normal shadow polarity is restored. Black foot flicker and the rotating Logic Stone light
remain open. Lag is the next priority: presentation counts alone do not establish
smooth animation, accurate cutscene speed or loading performance. The prior
73-patch ten-minute town test averages 14.25 presentations/s without worker
accumulation; [findings](FINDINGS.md) retain its conditions and limits.
Full rendering fidelity, sustained traversal, combat, Linux and macOS remain unverified.

Newest milestones appear first. Future captures will be added at the top of the gallery.
Each dated entry records what was verified at that checkpoint; later entries
supersede earlier limitations when further testing establishes progress.

## 2026-10-18 - Vulkan batch scheduling in the saved town

Patch 0076 avoids pool-only submissions and submits small batches when the spare
slot is ready. One-minute stationary samples average 27.18 presentations/s before
and 28.03 after with file logging and sampling off. A locked 30 fps and full game-speed
fidelity remain unverified; see [conditions and limits](FINDINGS.md#vulkan-batch-scheduling-patch-0076).
The 76-patch build passes all 64 Windows checks in Debug and Release.

The native frame below was captured after Continue loaded the verified town save.
It documents the scene, not the measured frame rate or flicker cadence.

![Native WKC1 saved town with Logic Stone selected after the Vulkan batch scheduling change](../verification/screenshots/2026-10-18-native-wkc1-town-batching.png)

## 2026-10-18 - Logic Stone saving and restart/Continue reload

Patch 0075 delivers previously dropped FIFO user-command interrupts to the
registered native callback. The Logic Stone now highlights on approach and
opens with X. Save writes a new local slot; after fully restarting the program,
Continue loads it and returns to town. Both Windows suites pass all 63 checks.
This covers one starting-town save on the same build; wider save compatibility
is unverified. Lag, analogue walk/run inconsistency and remaining flicker are open.

At this checkpoint, the later of the two captures shows the reloaded town
and selected Logic Stone:

![Native WKC1 town after restart and Continue, with Logic Stone selected](../verification/screenshots/2026-10-18-native-wkc1-save-reloaded.png)

The earlier capture shows its normal menu, opened with X before choosing Save:

![Native Logic Stone menu with Save selected](../verification/screenshots/2026-10-18-native-wkc1-logic-stone-save.png)

## 2026-10-18 - Earlier shadow regression (polarity fixed in patch 0074)

The 73-patch native Vulkan build reaches the WKC1 town after character
creation and the opening cutscenes. It passes 63 Windows regression checks.
The bright patch beneath Leonard is a known rendering regression in the new
depth-comparison path. Black flickering at his feet is also observed in motion.
This unchanged game-frame readback records the fault; it does not establish
correct shadows, sustained frame rate or complete gameplay.

![Native WKC1 town with the bright foot-shadow regression beneath Leonard](../verification/screenshots/2026-10-18-native-wkc1-town-shadow-regression.png)

## 2026-10-17 — Town stretching fixed: secondary-bone physics

The 58-patch native Vulkan build executes the previously missing SPURS queue physics
job, statically translated to C/C++. Leonard's hair and clothing remain on
the model instead of stretching across the street. The long strips are absent
in the inspected starting-town views, including after short movement and
camera rotation. All 51 Windows runtime checks pass.

Private replay of 128 captured geometry jobs finds normal referenced vertex
bounds throughout; 21 of 128 samples before the fix had extreme offsets.
See the [trace and limits](FINDINGS.md#secondary-bone-physics-and-town-stretching).
At that checkpoint, full rendering fidelity, sustained traversal, combat and
save/load were unverified. Later entries above record local saving.

![Native WKC1 town after restoring secondary-bone physics](../verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics.png)

![Native WKC1 town after movement and camera rotation, without the long strips](../verification/screenshots/2026-10-17-native-wkc1-secondary-bone-physics-camera.png)

## 2026-10-17 — NPC surfaces after NV0039 transfers

The 52-patch native Vulkan build implements NV0039 byte copies and routes
their methods away from 3D state. Previously speckled NPC surfaces look
coherent in the inspected town views, including after short movement and
camera rotation. The run verifies at least 65,536 copies without mismatch
or rejection diagnostics. Full material fidelity remains unverified.

**The stretched strips at Leonard were still present at this checkpoint.**
The newer physics milestone above removes them in the inspected town views.
This capture records the earlier NPC surface improvement; it does not establish
complete rendering correctness. Sustained traversal, combat and save/load
were unverified at this checkpoint; local saving was verified later.

![Native WKC1 town with improved NPC surfaces and remaining stretched geometry](../verification/screenshots/2026-10-17-native-wkc1-nv0039-npc-surfaces.png)

## 2026-10-17 — Town movement and camera response

The full 42-patch native build reaches the town and responds to left-stick
movement and right-stick camera input. Successive GPU readbacks show walking,
facing changes, a changed minimap position and a rotated view of the street.
The SPU census was active throughout this run. This is limited starting-town
input validation; sustained traversal, combat and save/load were unverified
at this checkpoint. Local saving was verified later.
Stretched geometry and corrupt NPC surfaces were still visible at this checkpoint;
the later fixes are recorded above.

![Native WKC1 town after analog movement and camera rotation](../verification/screenshots/2026-10-17-native-wkc1-town-controls.png)

## 2026-10-16 — WKC1 town world view

This earlier native Vulkan run reached the town after avatar acceptance and the
opening cutscenes. Leonard, the surrounding buildings, NPC names, minimap and
save crystal appear in live 3D. Pause/resume input was checked separately in
the same run. Fixing a runtime cutoff that halted valid SPU completion-label
polling lets this run pass the previous loading stall.

Stretched triangles and corrupt NPC surfaces are visible in this unchanged
readback. Movement and camera control were unchecked in this run and were
verified in limited town probes on 2026-10-17. Sustained traversal, combat and
save/load were unverified in this run; later entries record local saving.
The 1920×1080 display readback contains the game's 1280×720 scene.

![Native WKC1 town showing Leonard, NPCs, minimap and save crystal, with visible geometry faults](../verification/screenshots/2026-10-16-native-wkc1-town.png)

## 2026-10-16 — Vulkan avatar colours restored

The initial Vulkan colour-feedback fix shows the WKC1 avatar and room without the
red tint seen in its first build. A post-processing draw reads the colour target
it is updating; preserving the preceding pixels in a separate GPU image restores
the missing colour channels. The entered name is retained. This is live 3D
rendering from the hybrid C/C++ build with the shared RSX draw engine.
The 1920×1080 display readback contains the game's 1280×720 scene. Full visual
fidelity was not established by this capture. Later town rendering and limited
controls and local save/reload were verified later, as recorded above. macOS
execution remains unverified.

![Native Vulkan WKC1 avatar and room after colour-feedback correction](../verification/screenshots/2026-10-16-native-vulkan-avatar-colours.png)

## 2026-10-16 — WKC1 opening movie after avatar confirmation

At this checkpoint, the native executable accepted the avatar and entered the
WKC1 opening movie. Presenting completed batches at guest FIFO flips prevents
small movie batches from being withheld by the earlier scene threshold. This
capture shows prerecorded footage, using freshly regenerated PPU and SPU code.
Live town rendering and limited controls were verified later, as recorded above.

![Native WKC1 opening movie after avatar confirmation](../verification/screenshots/2026-10-16-native-wkc1-opening-movie.png)

## 2026-10-16 — Character name retained through avatar review

This checkpoint verified name retention through Quick Start to final Review,
with the avatar, face shading and textured clothing visible under normal depth
testing and culling. Avatar acceptance and town entry were verified later.
A gamepad-operated in-game keyboard is planned; the Windows text-entry window
is temporary.

![Native Character name retained through avatar review](../verification/screenshots/2026-10-16-native-avatar-review.png)

## 2026-10-16 — Avatar face and clothing shading

Correcting packed vertex normals and fragment source-range modifiers revealed
the shaded face and textured clothing of the default male avatar, with normal
depth testing and culling. This version 1.00 capture predates avatar acceptance
and town validation. Full visual fidelity remains unverified.

![Native Avatar face and clothing shading](../verification/screenshots/2026-10-16-native-avatar-shading.png)

## 2026-10-16 — Avatar body geometry

Correcting SPU FNMS extended-range arithmetic revealed the previously missing
arms, hands, trousers and boots, with normal depth and culling. Face shading and
clothing materials were still incorrect in this version 1.00 capture; later
fixes and town progress are recorded above.

![Native Avatar body geometry](../verification/screenshots/2026-10-16-native-avatar-body.png)

## 2026-10-16 — 3D room with normal depth

Correcting indexed triangle strips and primitive restart made the WKC1
character-creation room render with normal depth testing. Some avatar pieces
appeared, but the body was incomplete in this version 1.00 capture. Later
entries record the complete default avatar, avatar acceptance and town progress.

![Native 3D room with normal depth](../verification/screenshots/2026-10-16-native-3d-room.png)

## 2026-10-16 — Avatar customization UI

This version 1.00 checkpoint reached Customize Avatar and dismissed its
introductory notice through controller input. Text and menu controls rendered,
but the character model was missing at the time. Later entries record the
avatar rendering fixes and progression into the town.

![Native Avatar customization UI](../verification/screenshots/2026-10-16-avatar-customization.png)

## 2026-10-16 — White Knight Chronicles I selected

This checkpoint selected the first White Knight Chronicles through native
controller input, with readable description and dialog text. Loading then
stopped at a job with an empty SPU binary source. That failure was resolved
before the later avatar and town milestones above.

![Native White Knight Chronicles I selected](../verification/screenshots/2026-10-16-wkc1-selection.png)

## 2026-10-16 — Readable New Game description

Fixing packed font texture bindings made the native New Game description
readable. This capture verifies that menu text; later progress is recorded above.

![Native Readable New Game description](../verification/screenshots/2026-10-16-readable-new-game.png)

## 2026-10-16 — New Game submenu

At this checkpoint, live controller input moved the title highlight and opened
New Game, showing its book artwork, labels and cursor. Description and dialog
text still appeared as blocks; the later font fix is recorded above.

![Native New Game submenu](../verification/screenshots/2026-10-16-new-game-menu.png)

## 2026-10-16 — Title menu

The native title menu displays New Game, Continue and Options over the animated
background. Successive captures show the title artwork changing. With no input,
the guest subsequently opens its attract movie. This capture proves menu rendering;
menu response, avatar acceptance and limited town controls were validated later.

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
