# Findings

Updated 2026-10-16. Target: White Knight Chronicles II, USA BLUS30784 v01.00.

## Goal and reporting

Develop native Windows C/C++ execution through the title screen, gameplay and
verified save/load, with an architecture suitable for later Apple Silicon work.
Completion remains unproven until those behaviors have been tested.
Start gameplay validation with White Knight Chronicles I, included on the disc.

Maintain this log as research progresses. Publish checked code, concise findings
and new verified screenshots together when a meaningful milestone is reached.
Keep full traces, generated game code, assets and local paths in the private workspace.

## Verified

| Result | Evidence | Limit |
| --- | --- | --- |
| Native Windows build succeeds | Clang build and twenty runtime regression checks | Compilation alone does not verify gameplay |
| WKC1 reaches Customize Avatar | Readable UI and controller dismissal of the introductory notice; [capture](GALLERY.md) | Character model missing; completed character creation, gameplay and save/load unverified |
| Language-selection screen renders | GPU readback in the [gallery](GALLERY.md) | Startup UI only |
| Language selection accepts input | CROSS through the controller path advances startup | General gameplay controls remain untested |
| Warning and autosave notices render | Separate GPU readbacks in the gallery | The autosave notice does not verify saving |
| Lifted SPU decoder produces correct output for sampled assets | Eight outputs match independent raw-DEFLATE decoding byte for byte; [record](../decompression-verification.json) | Sampled blocks, not all assets |
| Native guest installer writes the large data archives | Three installed archives, totaling 4,364,470,879 bytes, hash-identical to their disc data; [record](../verification/installation.json) | Installation progress, not gameplay or save/load |
| First movie header parses through the guest ABI | Real header reports two streams and 1280x720 video; native execution advances to demux/decoder startup | Header parsing scope; later playback is recorded below |
| MPEG-2 access-unit extraction matches independent decoding | All 210 logo frames and 3,596 opening-movie frames match FFmpeg's direct demux/decode pixel for pixel; [record](../verification/movie-demux.json) | Offline validation, not native playback |
| Extracted opening-movie AC-3 decodes | 3,750 audio frames / 5,760,000 samples per channel at 48 kHz | Elementary-stream decode; native API comparison is recorded below |
| Native demux bridge delivers actual access units | Guest video decoder receives the first 19,356-byte unit; audio decoder receives a 960-byte LPCM unit | Decoder delivery evidence; later native playback is recorded below |
| Real video decoding matches reference pixels through the guest API | All 210 logo frames match on two sequence passes, including B-frame reordering; [record](../verification/movie-demux.json) | Standalone decoder validation, not continuous in-game playback |
| Decoded first-logo image renders natively | D3D12 GPU readback in the [gallery](GALLERY.md) | First-image milestone; later playback is recorded below |
| Two LPCM startup movies advance natively | Successive readbacks, decoder lifecycle and later Havok/Dolby capture in one 120-second probe with no input after movie start | Startup-clip evidence; title rendering is recorded below |
| Native LPCM sample conversion matches FFmpeg | All 336,480 stereo sample frames from the first logo match; [record](../verification/movie-demux.json) | Assembled elementary input; independent audio demux and final mix fidelity unverified |
| Native title menu renders over an animated background | GPU readback shows New Game, Continue and Options; idle execution proceeds to the attract movie | Controller navigation is recorded below; gameplay remains unverified |
| AC-3 samples match the reference through the guest API | Opening: 3,750 frames in stereo and 5.1; title: 938 frames in 5.1; two sequence passes each | Assembled elementary input; original-console mix fidelity unverified |
| Guest flips continue during the later black screen | A 120-second probe observed 6,171 flips and 6,171 presentation calls | Counters do not establish visible rendering |

## Implemented fixes

- Correct lightweight-condition layout and wait/signal behavior.
- Allocate and account for memory-container allocations instead of acknowledging
  allocation without providing memory.
- Support large archive offsets and authenticated, atomic SDAT caches.
- Lift observed heap-backed SPU jobs and avoid duplicate synthetic completions.
- Preserve SPU-produced FIFO segments while their producer fills them.
- Bind all 16 fragment texture units, preserve inline constants, and use the
  current shader-decompiler interfaces.
- Publish event queues only after host synchronization objects are initialized.
- Wait for a pending background content check before entering the original
  installation request, preventing that request from being dropped.
- Implement the nine imported PAMF reader APIs with guest-endian fields,
  64-bit file sizes, actual stream descriptors and copyable guest reader state.
- Parse MPEG program-stream packets incrementally and assemble MPEG-2, LPCM and
  AC-3 access units across packet and input-buffer boundaries.
- Supply demux work-memory queries, guest-resident AU records and payloads,
  guest callback messages, queue backpressure, flush and release lifetimes.
- Give callback threads separate guest stack slots, with separate slices for
  nested callbacks and slot reuse when host threads exit.
- Implement imported Vpost open/query/close/exec interfaces, with the actual
  argument/field layout and progressive YUV420 crop, bilinear resize and RGBA output.
- Enable real FFmpeg MPEG-2 decoding using pinned public headers and a separately
  supplied library. The optional decoder check reports a skip if no library loads.

Regression checks cover real video decoding, sparse video timestamps, LPCM sample
conversion/concurrent queues, stereo/surround AC-3, Vpost conversion/windows, callback
stacks, demux ABI/lifetimes, packet/access-unit parsing, PAMF reader ABI, lightweight conditions, memory containers, LFQueue and
event-queue recycling. PAMF checks use synthetic video/audio descriptors,
including declared data larger than 4 GiB, copied readers and malformed headers.
An optional private-header check also passes against the first logo file.
Their scope is narrower than full-game compatibility.

## Startup investigation

Later startup frames are black. A captured late command batch contains a black
clear and the final composite, without title-scene draws. The update dispatcher
continues executing its callbacks, so a global pause of the update loop does not
explain this sample.

Tracing then found an installation request arriving while a check-only request
was still pending. The guest helper rejected it, leaving startup waiting for
installation work that had never begun. An isolated empty emulated HDD alone
reproduced the same wait.

Synchronization before the original install request lets the check finish first.
The original guest installer then allocates its buffer, copies the data archives
and advances startup. Whole-file hashes verify the three large installed archives. A second run from
an empty isolated emulated HDD, with detailed tracing disabled, reproduces the
installation and all three matching archive hashes.
No completion result or installation progress was fabricated.

## Current startup status

AC-3 decoding removes the audio-open failure after the startup logos. The file
trace identifies the previously blocked clip as `title02.pam`, the animated title
background. The native title screen displays New Game, Continue and Options.
With no controller input after the startup notices, it enters `wk2rush.pam`, the
attract movie. The [gallery](GALLERY.md) includes the rendered title menu.

The asynchronous video bridge separates pending compressed inputs from decoded
output capacity, preventing the earlier busy/callback deadlock. Both LPCM and AC-3
use real samples, per-output guest records and sample-count-based timestamps.
The game requests six-channel AC-3 output in the observed run. Synthetic stereo
and 5.1 checks pass; all samples from the opening stream and title background
match an independent FFmpeg elementary-stream decode across two sequence starts.
See the [sample counts and hashes](../verification/movie-demux.json).

The AC-3 comparisons cover sample conversion, requested downmix/channel order,
input copies, output backpressure and reset. They do not independently validate
container audio demux or prove identical output to original console hardware.
Live controller input changes the title highlight and opens New Game. Description,
game-selection and confirmation text now render correctly. The font atlas is a
2048x1024 linear A4R4G4B4 texture, with a 4096-byte row pitch. The earlier binding
filter excluded this format, so text draws sampled the previous artwork. Recording
the actual texture and using the shared decoder plus the uploaded DXGI format fixes
the observed menus. The build now runs the SDK texture-layout tests as well.

White Knight Chronicles I was selected and confirmed through the controller path.
The previous loading attempt dispatched a 258,688-byte all-zero image because
its guard had never registered: the fixed host table was full and an unknown
guard incorrectly released the chain. The importer rejects that empty capture.
The runtime now allocates guard entries, tracks the owning chain, synchronizes
notifications/reset/waits and releases entries after joining. It does not dispatch
past an unknown guard. A cleanup-only fix still hit the 32-guard cap, confirming
that simultaneous capacity mattered as well as lifetime.

The regression test covers 100 create/run/shutdown/join cycles, two-notification
waits, reset, queued notifications, 128 simultaneous guards and invalid guards.
The previous implementation fails this test. The native WKC1 attempt now registers
the formerly missing guards and reaches a real 11,856-byte SPU program
(fingerprint 6AEA19F8490A2656). Its private static lift finds 123 functions and
98.2% code coverage, with seven undecoded words. A subsequent native WKC1 run
traces eight dispatches of this program, all returning `rc=0`. No unregistered-job
warning appears in that run. This verifies dispatch/return, not output correctness:
the game subsequently reports a graphics/FIFO error and stalls. Inspect the
produced command buffers and FIFO consumption next. Playable scenes and save/load
remain unverified.

A read-only observer at the game's graphics timeout captures three matching
samples: the completion label is one submission behind the expected value, and
GET points at a JUMP to its own address. That command is a producer wait, so the
next task is to identify the producer responsible for replacing it and verify
its output. Advancing GET or inventing a label completion would skip pending
work and would not establish correct gameplay.

The next WKC1 probe records write provenance for the mapped command-buffer
allocation. All three timeout samples identify SPU image 19 (fingerprint
687D4BB18564813A), at the DMA-writing block beginning at PC 0xB00, as the last
observed writer of the parked command. That transfer writes 704 bytes, beginning
with the self-jump. The previous observed contents at that address were also
written by image 19. This narrows the investigation to its command generation
and the work that should link the resulting segments; it does not establish a
root cause or validate the job's output. The observer covers lifted PPU stores,
their atomic path and completed SPU DMA PUTs, with two records per word. Host
bulk copies and other unobserved write paths remain outside its coverage.
This probe was stopped after the repeated stall was captured. No new playable
scene or screenshot milestone was obtained.

The game also builds `JOBLIST` commands for this rendering pipeline. The SDK
walker treated them as no-ops, so the jobs in those arrays never ran. Patch 0014
dispatches the array entries in order using the list's descriptor size, with
validation of nonempty descriptor sizes and address ranges. Tests cover guarded
publication, 128/256-byte entries, order, a following single job, empty lists and
invalid ranges/sizes. The previous walker fails the added list-dispatch check;
the implementation passes all seventeen regression checks.

A native WKC1 run with this fix reaches a list containing four 256-byte job
descriptors. They reference a previously unregistered 67,200-byte SPU program
(fingerprint 38DCC9863DD4E49F), now captured privately and statically lifted.
Its strings reference Edge vertex processing. The lift discovers 562 functions,
with 93.3% coverage and 98 undecoded words; those numbers alone do not verify
execution or correct output. The next native WKC1 run records all four dispatches
and returns. The graphics wait persists, still one submission behind, and the
last observed writer of the parked command remains fingerprint 687D4BB18564813A.
Job output correctness, character creation, gameplay and save/load remain
unverified.

The geometry program calls the canonical `cellSpursGetSpuId` routine. It reads
the kernel's SPU number at local-store address 0x1C8. Native job staging loads
the job at address zero, where that location holds an instruction (0x18275E25).
The job interprets it as an invalid processor ID and takes its error/stop path.
Patch 0015 recognizes the standard three-instruction library routine during
lifting and binds it to native job-manager metadata. The serial walker exposes
virtual SPU 0; task/raw contexts keep their original local-store reads. No job
instruction bytes are overwritten. A generated-instruction regression exercises
the actual staging code, preserved bytes, raw/task behavior and a similar routine
that must remain unbound. Without the binding the new regression fails.
All eighteen runtime checks pass.

A native WKC1 probe records eight bound queries, with the displaced instruction
word preserved, and advances beyond the earlier self-jump wait to a missing
16,912-byte SPU program (fingerprint 66E2457D0BEDAE24). Its strings refer to
animation jobs. The private static lift finds 176 functions, 94.9% coverage and
20 undecoded words. The next native run records eight dispatch/return pairs for
that program, with 22 SPU images registered. Output correctness remains unverified.
The capture is black and three later graphics-timeout samples agree: the label
is still one submission behind, but the published GET now points at a normal
method header rather than a self-jump. The next observation must distinguish
in-flight command consumption from missing producer output. No new gallery image
or playable-scene milestone is claimed.

Patch 0016 observes the FIFO worker inside a drain using optional host atomics.
The next ten-minute WKC1 probe captures two timeout samples with the same
published GET, while the in-flight packet moves and the packet count increases
by 3,694. Recent branch records include calls into generated command segments
and returns. Further drains complete and presentation resumes, with a later
capture containing 623 recorded operations. The earlier timeout therefore does
not establish that command consumption stopped. A native thread sample also
finds the worker in vertex-attribute fetch, through vertex upload and drawing.
These observations narrow the next investigation to slow rendering and the
remaining black output; they do not prove a root cause. Both the backbuffer and
the main scene render target remain black. Character creation, gameplay and
save/load remain unverified. The observation changes diagnostic timing but does
not write guest completion values or advance the queue.

Two vertex-shader cache defects explain a later rendering failure. The backend
shifted a 16-entry array when full, but queued draws retained the old indices.
It also hashed unrelated resident instructions after the current program's END,
causing false cache misses. Patch 0017 retains stable IDs until backend shutdown,
compares the actual program bytes after hashing and excludes that trailing store
content. A regression covers 128 retained IDs, copied instructions, deliberate
hash collisions, bytes after END, invalid inputs and release lifetime. A negative
control reproducing the moving 16-slot policy fails; all nineteen checks pass.

The next native WKC1 run reaches Customize Avatar with readable menu controls.
Controller input dismisses the introductory downloadable-content notice. A scene
capture contains 21 distinct vertex-shader IDs (0 through 20), confirming that
this path exceeds the previous cache capacity. The newest gallery image shows
the UI after the notice is dismissed. The character model is still missing, and
performance remains slow. This verifies the customization screen and that input
response, not completed character creation, gameplay, saving or DLC integration.

Patch 0018 extends private frame captures with exact vertex-program bytes,
constant banks and up to sixteen uploaded vertices per draw. Offline evaluation
of sampled avatar body vertices produces finite clip coordinates, positive W
and positions inside the view. Working UI draws produce their expected screen
positions. This rules out wholly empty/offscreen inputs for those samples; it
does not verify every vertex, GPU rasterization or fragment output.

The backend also forced depth testing and writes for all guest fragment-program
draws. Patch 0019 retains the guest's depth enable, mask and comparison per draw
and includes them in pipeline identity. A regression covers all eight comparison
functions and a UI-before-model occlusion case. An unconditional-depth negative
control fails; all twenty checks pass. The native capture records UI draws with
depth disabled, geometry with depth writes enabled and overlay passes with writes
disabled. The customization UI still renders, but the avatar is still missing.
This corrects depth-state handling without establishing the remaining root cause;
fragment output, culling and other raster state remain under investigation.

Patch 0020 captures a render target immediately after selected GPU draws, before
later passes can overwrite it. A known-good startup-logo readback validates the
copy path. In the customization scene, snapshots after the background and selected
avatar draws have byte-identical RGB pixels, including after the last model draw.
Those draws use guest fragment pipelines with culling disabled. Post-processing
subsequently changes the image. The missing model therefore already fails to
affect the captured color target before post-processing; this does not identify
whether depth rejection, fragment output or another earlier stage is responsible.
These private diagnostic images add no gallery milestone.

A separate run with the existing `DEPTH_OFF=1` diagnostic shows the room and
avatar. Intermediate copies also show nonzero color changes after the sampled
body draws. The override disables both depth testing and writes; this establishes
that those stages affect the missing geometry, not that normal rendering works.
No image from that override is published as a gallery milestone.

The guest fragment pipelines also left D3D12 depth clipping disabled through
zero-initialization. Patch 0021 decodes the guest clipping/clamping control and
retains it in the raster pipeline key. A command-processor regression covers
reset, disable/restore, clamping and deferred state; the previous no-clipping
policy fails it. All twenty-one checks pass. A further native run confirms
clipping-enabled guest pipelines with normal depth testing, but the avatar is
still missing. Clipping control is corrected without establishing the remaining
depth failure. The next diagnostic needs the depth target itself and its writers.

Intermediate depth-plane copies now show that the clear contains 1.0. Room
geometry subsequently fills the sampled avatar positions with nearer depth:
one sample is approximately 0.785 before the body draw and remains unchanged
afterward. Sampled body vertices request depth near 0.9964. A separate D3D11
stream-output replay of the captured vertex shader, constants and attributes
agrees with the offline transform calculation. This verifies those replayed
inputs, not the live D3D12 shader bindings or all mesh vertices. The next work
is to validate the earlier geometry and the depth state around the room/avatar
transition. Normal-depth avatar rendering remains unverified.

The next native input capture identifies a concrete assembly error: the game
enables primitive restart with value 65535, while the indexed strip path treated
that marker as a vertex index. In the captured room packets these reads produce
zero-position vertices, joining separate strips into false triangles. The path
also omitted alternating strip winding and lost continuity between command
packets. Patch 0022 corrects those behaviors. The C++ regression fails when
either restart handling or alternating winding is independently removed.
[RPCS3's index handling](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Common/BufferUtils.cpp)
was used to check restart semantics, including full-width marker comparison for
16-bit indices.

The native capture contains 708 restart markers across 32 room command packets
(5,128 indices). With patch 0022, the room renders with normal guest depth and
culling enabled. At the previously observed body pixel, room depth is now about
0.99853 and the following model draw writes about 0.99640 in front of it, instead
of being rejected behind false room geometry. The gallery records this room
milestone. Some avatar pieces render, but the body remains incomplete; this does
not verify full character rendering, completed character creation or gameplay.
All twenty-two regression checks pass. The next rendering investigation must
distinguish missing mesh geometry, depth-prepass agreement and shader output.

## Update 1.01 and DLC integration

Planned scope includes the USA 1.01 update and matching DLC. Local package
inspection confirms update TITLE_ID BLUS30784 and APP_VER 01.01. It contains a
replacement executable, patch settings and an approximately 199 MB patch archive.
The USA DLC metadata also targets BLUS30784; a separate European package targets
BCES01085. Its regional differences are part of the planned DLC investigation.

A byte comparison of the supplied DLC packages finds 21 USA entries and 22 EU
entries under `USRDIR/cm`. The EU-only entry is `c1267f66.edat` (320 bytes).
Nine shared files are byte-identical; twelve differ. Package-level differences
can include licensing/container metadata, so this does not yet identify an extra
item, quest or other playable feature. Determine what the extra entry unlocks,
compare the differing shared entries, and test any relevant content against the
USA build before claiming compatibility.

The Japanese game, DLC and ten update packages are available for future
investigation. The DLC metadata identifies BCJS30042; update filenames indicate
a chain from 1.00 to 1.15. Updated executables and content have not been validated.
The long-term goal is to integrate all Japanese-exclusive content with English
translations, plus optional Japanese voiceovers. This requires identifying the
regional differences, translating relevant text and validating integration and
save compatibility. It does not change the immediate priority of baseline 1.00
3D rendering and gameplay. Files and local source locations remain private.

Next steps are to analyze the updated executable, integrate the patch data and
DLC discovery, and validate content access plus gameplay/save compatibility.
The current baseline remains disc v01.00. Neither 1.01 execution nor DLC content
has been verified in the native port. Package bytes and local source locations
are not published.

## Still unverified

Open audio issue: audible distortion in character-creation music. Device buffer
starvation, guest mixer scheduling and source decoding have not yet been
distinguished. Decoder comparisons do not establish real-time mixer fidelity.

Playable scenes, combat, save/load, a complete fresh-disc
reproduction of the latest boot, macOS execution and online functionality.

See the [build limitations](BUILD.md) and [capture manifest](../verification/gallery.json)
for reproduction requirements and screenshot provenance.
