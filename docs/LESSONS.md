# Lessons for other recompilation projects

Runtime problems found while porting White Knight Chronicles II that are likely to
affect other PS3 static recompilations. Each lesson gives the symptom, the cause
and the fix, and links to the full evidence. Start from the symptom index if you
are debugging something you can see.

Terms such as RSX, GCM, FIFO, SPURS and MFC are defined in the
[reference](REFERENCE.md#glossary).

## Symptom index

| What you see | Likely cause | Lesson |
| --- | --- | --- |
| The game runs in slow motion although the frame counter is high | Flip completion reported before the frame is shown, so the game renders flat out and its logic falls behind | [Report a flip as complete only when it is shown](#report-a-flip-as-complete-only-when-it-is-shown) |
| Fades and animation step unevenly; a small hitch every few frames | Vblanks timed with a coarse clock or the wrong period | [Time vblanks with a high-resolution clock](#time-vblanks-with-a-high-resolution-clock) |
| The FPS counter reads high but motion looks slow | The counter counts presentations, not new game frames | [A presentation counter is not a frame rate](#a-presentation-counter-is-not-a-frame-rate) |
| Red flashes or a red close-up in cutscenes | A depth-only pass bound to the display buffer, or movie frames sampled from a stale GPU copy | [Depth-only passes must not replace a live surface](#depth-only-passes-must-not-replace-a-live-surface) |
| A whole scene is tinted one colour | A post-processing draw samples the target it is writing | [Snapshot a target before sampling it](#snapshot-a-target-before-sampling-it) |
| Shadows are inverted or a character's feet glow | Texture depth-comparison operands reversed on the host sampler | [Reverse ordered depth comparisons for host samplers](#reverse-ordered-depth-comparisons-for-host-samplers) |
| Shadows from an earlier pass, or corrupted depth | A stale depth snapshot, or two snapshots racing on a shared buffer | [Keep depth snapshots current and ordered](#keep-depth-snapshots-current-and-ordered) |
| Long stretched strips from hair or clothing across the world | A job queue import returned success without running the physics jobs | [Unimplemented imports that return success hide missing work](#unimplemented-imports-that-return-success-hide-missing-work) |
| Speckled or wrong NPC surfaces | Memory-to-memory copies (NV0039) not implemented, or their methods treated as 3D state | [Implement memory-to-memory copies on their own subchannel](#implement-memory-to-memory-copies-on-their-own-subchannel) |
| Non-finite or black shading on faces | Packed 11/11/10 normals decoded as floats | [Decode packed vertex normals](#decode-packed-vertex-normals) |
| A small scene such as a movie stops presenting after a large one | A clear-based presentation heuristic keeps the larger scene's draw threshold | [Present at the guest's flips, not by heuristic](#present-at-the-guests-flips-not-by-heuristic) |
| Missing post-processing or the final composite | A fixed-size target or draw-record table overflowed silently | [Report capacity exhaustion](#report-capacity-exhaustion) |
| Loading stalls with geometry jobs stopped | An automatic cutoff halted a job that was validly polling a GPU label | [A long poll is not a hang](#a-long-poll-is-not-a-hang) |
| Many threads spinning during loading | SPUs re-reading an unchanged reservation line without yielding | [Yield on an unchanged reservation and wake waiters](#yield-on-an-unchanged-reservation-and-wake-waiters) |
| One SPU's DMA wait satisfied by another SPU's transfer | Several contexts sharing one MFC engine | [Give each SPU context its own MFC state](#give-each-spu-context-its-own-mfc-state) |
| NPCs or objects cannot be interacted with | User-command interrupts not delivered to the game's registered handler | [Deliver user-command interrupts to the registered handler](#deliver-user-command-interrupts-to-the-registered-handler) |
| Walking diagonally is slower than walking straight | Deadzone applied to each stick axis separately | [Apply the deadzone to the stick vector](#apply-the-deadzone-to-the-stick-vector) |
| Startup waits forever on installation | An install request issued while a check request was still pending | [Let pending service requests finish in order](#let-pending-service-requests-finish-in-order) |
| Closing the window leaves the process running | The window-close event was not treated as exit | [Exit cleanly when the window closes](#exit-cleanly-when-the-window-closes) |

## Timing and frame pacing

### Report a flip as complete only when it is shown

WKC2 paces itself on flip completion. When the runtime called the game's flip
handler as soon as a flip was requested, the game believed every frame was already
on screen, rendered as fast as it could, and its logic ran about four times too
slowly: the avatar turned at 49 degrees per second against RPCS3's 200. Complete a
flip only after an actual flip request, deliver the callback at the vblank that
shows it, and honour the game's own two-vblank semaphore gate. See
[frame pacing and game speed](FINDINGS.md#frame-pacing-and-game-speed) and
[patches 0067-0070](PATCHES.md#depth-sampling-and-real-flip-completion-patches-0067-0070).

### Time vblanks with a high-resolution clock

A vblank clock driven by `GetTickCount64` (about 15.6 ms steps) and a flat 16 ms
period (62.5 Hz) delivered ticks in pairs or not at all. A game that advances by
the vblanks it counts then steps unevenly. Use the high-resolution counter at
59.94 Hz. See [frame pacing and game speed](FINDINGS.md#frame-pacing-and-game-speed).

### A presentation counter is not a frame rate

Counting host presentations includes repeated animation states, so a counter can
read 60 while the game finishes far fewer distinct frames. Label the unit
(`Present/s`), inspect the longest interval as well as the average, and measure
game speed directly. See the [window-title counter](PATCHES.md#vulkan-window-title-counter-patch-0065)
and [presentation timing](FINDINGS.md#window-title-presentation-timing).

### Measure speed against a reference

To tell slow rendering from slow logic, time a repeatable action such as one full
avatar turn with the stick held, in both the port and RPCS3, from captured frames.
Frame-rate readings alone could not show that the game ran four times too slowly.
See [frame pacing and game speed](FINDINGS.md#frame-pacing-and-game-speed).

### Hidden windows do not time like visible ones

Profiling runs with a hidden window produced different timing from visible play.
Record window visibility with every measurement and compare like with like. See
the [measurement correction](FINDINGS.md#town-performance-after-the-physics-fix).

### Keep fence wakeups separate from frame limiting

Another port's runner wakes on fences independently of its frame limiter; that
separation keeps the command consumer responsive while the frame rate is capped.
See the [Drakengard 3 comparison](FINDINGS.md#drakengard-3-performance-and-settings-comparison).

## Rendering

### Depth-only passes must not replace a live surface

A shadow pass sets the colour target to NONE but leaves colour offset 0, which is a
display buffer. Binding a colour target there replaced the visible surface and
produced red flashes. Give depth-only passes a scratch colour target, and reload
GPU copies when the host writes guest memory (for example `cellVpost` movie
frames). See [cutscene red flash and frame rate](FINDINGS.md#cutscene-red-flash-and-frame-rate)
and [patches 0053-0057](PATCHES.md#renderer-coherence-and-execution-overhead-patches-0053-0057).

### Snapshot a target before sampling it

A post-processing draw that samples the target it is updating read stale guest
memory and tinted the scene red. Copy the target on the GPU first and sample the
copy. See [shared renderer and colour feedback](FINDINGS.md#shared-renderer-and-colour-feedback).

### Reverse ordered depth comparisons for host samplers

RSX ZFUNC compares the stored texel depth against the reference; host comparison
samplers reverse those operands, so ordered operators must be mirrored. Getting
this wrong inverts shadows. See [patch 0074](PATCHES.md#texture-comparison-direction-patch-0074)
and [patch 0073](PATCHES.md#vulkan-depth-comparison-patch-0073).

### Keep depth snapshots current and ordered

A sampled depth snapshot must match its target's latest write or clear, including
after a pass binds a different target. Snapshots that share a staging buffer need
a barrier, or two in one batch corrupt each other. See
[patch 0071](PATCHES.md#depth-snapshot-generations-patch-0071) and
[patches 0067-0070](PATCHES.md#depth-sampling-and-real-flip-completion-patches-0067-0070).

### Implement memory-to-memory copies on their own subchannel

WKC binds the NV0039 copy object on subchannel 1. Without those copies, and with
their methods interpreted as 3D state, NPC surfaces were wrong. Consume copy
methods before the 3D renderer sees them. See
[NV0039 memory transfers](FINDINGS.md#nv0039-memory-transfers).

### Decode packed vertex normals

Type-6 CMP attributes are signed 11/11/10 values. Decoding them as floats produced
a near-zero tangent frame and non-finite shading. See
[avatar normals and fragment shading](FINDINGS.md#avatar-normals-and-fragment-shading).

### Present at the guest's flips, not by heuristic

A clear-triggered presentation heuristic kept a draw-count threshold from a large
scene, so a smaller movie batch was never presented. Present complete batches at
the guest's flips. See [guest flip presentation](FINDINGS.md#guest-flip-presentation-through-scene-changes)
and [guest frame presentation](SUBSYSTEMS.md#guest-frame-presentation).

### Report capacity exhaustion

A 16-entry target table and a 2048-entry draw-record table both overflowed in WKC1
scenes, silently dropping post-processing targets and draws. Size tables from
observed scenes and report when they fill. See
[render targets](FINDINGS.md#scene-render-targets-and-descriptor-ranges) and
[draw-record capacity](FINDINGS.md#scene-draw-record-capacity).

### Honour texture LOD bias

The guest's signed 4.8 LOD bias selects texture detail; ignoring it changes
sharpness. See [patch 0079](PATCHES.md#vulkan-texture-lod-bias-patch-0079).

## SPU and SPURS

### Unimplemented imports that return success hide missing work

The physics update submitted jobs through a SPURS job queue whose imports were
unresolved; the runtime returned success and the jobs never ran. The physics
getter then returned identity matrices, which skinning turned into long strips
across the town. Trace a visual fault backwards through the data before assuming
a rendering bug. See [secondary-bone physics](FINDINGS.md#secondary-bone-physics-and-town-stretching)
and [patch 0058](PATCHES.md#spurs-job-queue-physics-patch-0058).

### A long poll is not a hang

Geometry jobs validly re-read a GPU completion label until command-buffer space
frees up. An automatic cutoff after 256 identical DMA transfers stopped them and
stalled loading. Only cut off repeated reads on explicit request. See
[SPU completion-label polling](FINDINGS.md#wkc1-town-and-spu-completion-label-polling).

### Yield on an unchanged reservation and wake waiters

SPUs spinning on an unchanged reservation line starved the holder and piled up
threads. Yield after repeated unchanged reads, and wake job-guard waiters with a
condition variable. See [patches 0053-0057](PATCHES.md#renderer-coherence-and-execution-overhead-patches-0053-0057).

### Give each SPU context its own MFC state

An eight-slot registry handed later contexts one shared MFC engine, so one SPU's
tag wait could be satisfied by another's transfer. Tie MFC queue and tag state to
the context's lifetime. See [patches 0060-0061](PATCHES.md#spu-conversion-scales-and-dma-ownership-patches-0060-0061).

### Claim asynchronous work before starting host threads

Claim each guest task or workload before creating its host thread and hold the
claim through cleanup, so duplicate submissions coalesce and thread counts stay
bounded. See [patch 0072](PATCHES.md#asynchronous-spu-worker-ownership-patch-0072).

### Keep task completion coherent with SPU atomics

Task completion's bit updates must stay coherent with SPU atomic commits. See
[patches 0091-0093](PATCHES.md#task-completion-and-spu-byte-operations-patches-0091-0093).

### Extract SPU jobs statically

Jobs first seen in runtime captures were all present byte for byte in the
executable. Extracting them from the executable makes a clean build reproducible
without captures. See [reproducing the SPU inputs](FINDINGS.md#reproducing-the-spu-inputs-from-the-executable).

## Runtime services and input

### Deliver user-command interrupts to the registered handler

The FIFO walker sent user-command method `0xEB00` to a low-level interrupt queue
that did not exist, so the game's registered callback never ran and its
interaction targets stayed marked occluded. See
[patch 0075](PATCHES.md#fifo-user-command-delivery-patch-0075) and
[proximity interaction](FINDINGS.md#proximity-interaction-and-saving-patch-0075).

### Apply the deadzone to the stick vector

A per-axis deadzone reduced a full diagonal to about 87% of straight-ahead
magnitude, changing walk/run behaviour by direction. Convert the stick as a vector
before quantizing. See [patch 0084](PATCHES.md#windows-analogue-response-patch-0084)
and [patches 0085-0090](PATCHES.md#runtime-and-input-follow-up-patches-0085-0090).

### Report every missing syscall

A global warning budget hid later unimplemented syscalls. Report each missing ID
once. See [patch 0099](PATCHES.md#platform-query-outputs-and-syscall-warnings-patch-0099).

### Let pending service requests finish in order

An installation request arrived while a check-only request was still pending; the
guest rejected it and startup waited for work that never began. See the
[startup investigation](FINDINGS.md#startup-investigation).

### Exit cleanly when the window closes

Treating window close as "disable rendering" left guest threads waiting on work
that could never complete. See [patch 0083](PATCHES.md#window-close-patch-0083-and-native-runner).

## Working practices

- **Check rendering with GPU readbacks.** Frame counters do not show whether the
  picture is right; see [contributing](CONTRIBUTING.md#regression-checks-and-evidence).
- **Trace backwards from the symptom.** The stretched strips were traced from the
  vertices through skinning to the physics getter before any rendering code changed.
- **Keep diagnostics off by default.** Switches are environment-gated and listed in
  the [reference](REFERENCE.md#environment-variables).
- **Record measurement conditions.** Window visibility, logging and profilers all
  change timing; see [performance records](../verification/performance.json).
