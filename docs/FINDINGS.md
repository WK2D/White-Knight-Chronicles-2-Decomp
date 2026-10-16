# Findings

Updated 2026-10-16. Target: White Knight Chronicles II, USA BLUS30784 v01.00.

## Goal and reporting

Develop native Windows C/C++ execution through the title screen, gameplay and
verified save/load, with an architecture suitable for later Apple Silicon work.
Completion remains unproven until those behaviors have been tested.

Maintain this log as research progresses. Publish checked code, concise findings
and new verified screenshots together when a meaningful milestone is reached.
Keep full traces, generated game code, assets and local paths in the private workspace.

## Verified

| Result | Evidence | Limit |
| --- | --- | --- |
| Native Windows build succeeds | Clang build and ten runtime regression checks | Compilation alone does not verify gameplay |
| Language-selection screen renders | GPU readback in the [gallery](GALLERY.md) | Startup UI only |
| Language selection accepts input | CROSS through the controller path advances startup | General gameplay controls remain untested |
| Warning and autosave notices render | Separate GPU readbacks in the gallery | The autosave notice does not verify saving |
| Lifted SPU decoder produces correct output for sampled assets | Eight outputs match independent raw-DEFLATE decoding byte for byte; [record](../decompression-verification.json) | Sampled blocks, not all assets |
| Native guest installer writes the large data archives | Three installed archives, totaling 4,364,470,879 bytes, hash-identical to their disc data; [record](../verification/installation.json) | Installation progress, not gameplay or save/load |
| First movie header parses through the guest ABI | Real header reports two streams and 1280x720 video; native execution advances to demux/decoder startup | Movie playback remains unverified |
| MPEG-2 access-unit extraction matches independent decoding | All 210 logo frames and 3,596 opening-movie frames match FFmpeg's direct demux/decode pixel for pixel; [record](../verification/movie-demux.json) | Offline validation, not native playback |
| Extracted opening-movie AC-3 decodes | 3,750 audio frames / 5,760,000 samples per channel at 48 kHz | LPCM has not yet had independent audio comparison |
| Native demux bridge delivers actual access units | Guest video decoder receives the first 19,356-byte unit; audio decoder receives a 960-byte LPCM unit | Decoder delivery evidence; later native playback is recorded below |
| Real video decoding matches reference pixels through the guest API | All 210 logo frames match on two sequence passes, including B-frame reordering; [record](../verification/movie-demux.json) | Standalone decoder validation, not continuous in-game playback |
| Decoded first-logo image renders natively | D3D12 GPU readback in the [gallery](GALLERY.md) | First-image milestone; later playback is recorded below |
| Two LPCM startup movies advance natively | Successive readbacks, decoder lifecycle and later Havok/Dolby capture in one 120-second probe with no input after movie start | AC-3 opening movie remains unsupported; title screen unverified |
| Native LPCM sample conversion matches FFmpeg | All 336,480 stereo sample frames from the first logo match; [record](../verification/movie-demux.json) | Assembled elementary input; independent audio demux and final mix fidelity unverified |
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
conversion/concurrent queues, Vpost conversion/windows, callback
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

## Current blocker

A 120-second native probe now advances through the first two LPCM startup movies
and reaches the Havok/Dolby screen. Successive movie readbacks differ, and the
guest closes and reopens the decoders between movies. No controller input occurs
after movie playback begins. The earlier single-frame stall is resolved for these
startup clips.

The next opening movie uses AC-3. Its audio decoder open returns `0x80610003`
because the project audio bridge currently supports LPCM only. Implementing real
AC-3 decoding is the next step; the title screen and gameplay remain unverified.

The asynchronous video bridge owns copies of accepted compressed inputs, separates
input command capacity from decoded output capacity, and wakes pending work when
the game consumes pictures. This avoids the SDK's output-count busy deadlock,
where every accepted input had already completed and the guest waited for a
callback that could never arrive. Missing presentation timestamps follow the
decoded rational rate after B-frame reordering.

The LPCM bridge uses distinct output slots, correct guest resource/record layouts,
real packed samples and sample-count-based timestamps. All 336,480 stereo sample
frames from the first logo match FFmpeg PCM conversion through the native API.
This comparison starts from assembled elementary samples; independent audio demux
and the game's final mix are not established. The native mix is finite and nonzero,
with a peak of 0.98865, but complete audio fidelity remains unverified.

Fabricated SPURS completion broadcasts were removed. Actual lifted SPU event
instructions deliver notifications, and the subsequent probe has no unresolved
audio-worker calls. Missing imports remain open. Vpost currently supports
progressive bilinear processing; field deinterlacing and other filters return errors.

## Still unverified

Title-screen rendering, playable scenes, combat, save/load, a complete fresh-disc
reproduction of the latest boot, macOS execution and online functionality.

See the [build limitations](BUILD.md) and [capture manifest](../verification/gallery.json)
for reproduction requirements and screenshot provenance.
