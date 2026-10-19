# Runtime subsystem notes

Implementation notes for the larger runtime subsystems. Patch-by-patch changes are in
the [runtime patch notes](PATCHES.md); build and run instructions are in the
[build guide](BUILD.md). These notes were previously part of the build guide.

- [Windows renderer](#windows-renderer)
- [Movie decoding](#movie-decoding)
- [Guest frame presentation](#guest-frame-presentation)
- [Native text entry](#native-text-entry)

## Windows renderer

Vulkan is the default build and drives the shared RSX draw engine, which also
has a Metal backend. This is native host GPU rendering
alongside the statically recompiled C/C++ game code. Apple Silicon execution has
not been tested.

```powershell
./tools/build.ps1 -Workspace <private-workspace> -Renderer Vulkan
./tools/probe.ps1 -Workspace <private-workspace> -Renderer Vulkan -Seconds 600
```

This uses a separate `work/build/wkc2-vulkan` directory. The build invokes
`tools/prepare-vulkan.ps1`, which checks out and builds pinned public dependencies
inside the private workspace: SDL2 2.32.10, glslang 16.6.0 and its SPIR-V tools,
plus Vulkan headers 1.4.328. Exact revisions are in that script. Existing source
changes or different revisions cause an error; the helper does not reset them.
The standard preparation script builds optimized dependencies with the Debug
CRT and C++ ABI used by `build.ps1`. The measured private Release build uses
separate matching Release-CRT dependencies; the two ABIs must not be mixed.
A Vulkan-capable driver and its loader are runtime requirements.

The normal Vulkan-enabled Windows build also offers an experimental DirectX 12
path through Graphics Settings -> Graphics API. Save the
selection, exit, and restart. It uses the SDK live-draw engine. Window/output
controls, VSync, filtering, FXAA and the FPS overlay are integrated. The menu remains
accessible to switch back. This is a comparison path, with visible differences
and incomplete parity.

A separate legacy D3D12-only comparison build can still be requested:

```powershell
./tools/build.ps1 -Workspace <private-workspace> -Renderer D3D12
./tools/probe.ps1 -Workspace <private-workspace> -Renderer D3D12 -Seconds 600
```

For direct CMake configuration, `-DPS3RECOMP_RSX_VULKAN=OFF` selects D3D12.
The choice of primary renderer does not establish complete game compatibility.
Linux can reuse the Vulkan renderer in future; Linux platform integration and
execution remain untested. macOS can use the shared engine's Metal backend.

The SDK patch helper reconstructs the complete stack before updating source.
It accepts an unchanged SDK or any known earlier prefix, including one already
fully patched. Overlapping later patches do not make earlier patches appear
missing. Unrecognized edits to affected files cause an error before any SDK
source is changed.

Reconstruction fixes Git's line-ending policy and tolerates whitespace in patch
context for the upstream mixed CRLF/LF files. Known-prefix comparison still
rejects unrelated source edits, including indentation changes. The full stack
was checked with both line-ending settings and produced the same source after
CRLF normalization.

For periodic native Vulkan GPU readbacks, create a private directory and set
`RSX_ENGINE_SNAP_DIR` to it and `RSX_ENGINE_SNAP_EVERY_S` to the interval in seconds
before launching. This produces PPM files. The probe's `-CaptureFrames` controls
select the separate D3D12 capture path.

Patches 0035 and 0036 use the shared-engine and Windows Vulkan work from
[Soulcalibur2HD-recomp](https://github.com/n0madic/Soulcalibur2HD-recomp/tree/88e6cc842a7853e093e8dbab8ac053ac1ad51cc7).
The toolkit patches retain the SDK's MIT license; see the retained
`LICENSE.ps3recomp`. Patch 0038 implements colour-target snapshots after inspecting
`off_rt_snapshot` in the MIT-licensed
[Demon's Souls SDK fork](https://github.com/JonathanDC64/ps3recomp/blob/61909373a2cb59f8679d8b96382e2899d656e4d0/libs/video/rsx_d3d12_backend.c).
These are implementation sources actually used by this port.

## Movie decoding

`tools/prepare_ffmpeg.py` obtains FFmpeg 8.1 public headers from the official source
archive, checks SHA-256 `b072aed6871998cce9b36e7774033105ca29e33632be5b6347f3206898e0756a`,
and writes the local public header configuration. The build enables the SDK's
runtime-loaded decoder. A matching libavcodec 62 / libavutil 60 is needed to decode
video; no FFmpeg binary is included here. The tested Windows installation uses
PyAV 18.1.0 in the private venv. `tools/media-env.ps1`, loaded by build and probe,
locates its DLLs and dependencies. Alternatively, set `PS3RECOMP_FFMPEG_AVCODEC`
and `PS3RECOMP_FFMPEG_AVUTIL` to matching library paths and provide their dependencies
on the host loader path. A different-major library is rejected.

The automatic decoder check uses eight original synthetic frames, including
B-frame reordering, with a decoded pixel reference. It exercises the SDK's existing
guest API test, two sequence passes, a changed pixel and a dropped-unit negative
control. It skips when the runtime library cannot load; a skip does not verify
decoding. The separate private logo check covered all 210 frames at 1280x720.
Patch 0008 fixes that SDK test's 64-bit seen-set limit so sequences above 64 frames
are checked without undefined shifts.

`pamf.cpp` supplies project-local context handlers for the nine imported PAMF APIs
before SDK fallback registration. The reader test uses synthetic headers by default.
`wkc2_pamf_test <private-movie-file>` additionally checks a local 2048-byte header;
the file is not copied into the source tree or test fixtures. This covers header
reading, not demuxing or movie playback.

`dmux.cpp` supplies project-local demux handlers, including QueryAttr and QueryEsAttr,
which were absent from the SDK library's handler surface. Work sizes describe the
project implementation's buffers. The worker queues actual units in guest resources
and calls guest OPDs; it waits when resources fill until the guest releases units.
No completion or timestamps are synthesized to skip movie content.

Offline private media validation (PyAV is an optional verification dependency):

```text
wkc2_mpegps_test <private-movie-file> <private-output-prefix>
python tools/verify_movie_units.py <private-movie-file> <private-output-prefix> --record <private-record.json>
```

The test exports elementary video/audio plus video-unit sizes to the private prefix.
The verifier decodes those video units and compares every frame against FFmpeg's
independent MPEG demux/decode of the original movie. It also decodes exported AC-3
when present. Records contain hashes and counts, never game bytes or host paths.
The checked samples are in `verification/movie-demux.json`. Native runs separately
verify continuous playback of startup logos, the title background and observed
attract-movie playback. The offline checks do not establish complete movie
coverage or the game's final audio mix.

`vpost.cpp` implements the four imported video-postprocessor APIs with guest field
layouts and real progressive YUV420 processing. Tested paths include legal-range
black/white, cropped/scaled RGBA output, planar output, guest metadata and output
window guards. Field deinterlacing and non-bilinear filters are not implemented.

Set `WKC2_UPDATE_DIAGNOSTICS=1` for dispatcher/scene state samples. The generated
update hook is applied by the build script and is disabled by default at runtime.
Probe logs and GPU captures go to the private workspace.

`tools/apply_port_hooks.py` installs synchronization before the original guest
installation request: a pending background content check must finish first.
This prevents a faster native caller from losing the install request to the
guest helper's pending-request guard. Allocation, copying and completion checks
remain in the original guest path. The hook is reapplied after regeneration.
`tools/trace_calls.py --env WKC2_CONTENT_TRACE` supports a separate trace switch
for focused content diagnostics, without enabling other instrumented functions.

`Wkc2Decompile.java` is a Ghidra 12.1.4 headless script for selected-function analysis
using `PowerPC:BE:64:64-32addr`. It takes a private TSV inventory (hex start, exclusive
end, TOC), a private output directory, and hex function addresses. It is an analysis
aid; generated pseudocode is not source distributed by this repository.

`adec.cpp` implements eight guest audio decoder APIs for PAMF LPCM and AC-3. It owns output
slots inside the supplied guest work resource and keeps metadata/samples until
consumption. Supported packed depths are 16/20/24 bits, with dummy-channel removal,
channel reordering and sample-count-based missing timestamps. The AC-3 path uses FFmpeg, channel-pointer mapping, requested mono/stereo downmix
or full 5.1, PCM scaling and line/RF dynamic range modes. Unsupported custom
compression, forced stereo/dual-mono and karaoke modes return an argument error.

The LPCM test exercises backpressure, distinct metadata and output queues, 16/24-bit
samples, mono dummy channels, 5.1 reordering and 1,000 concurrent submissions.
An optional private test compares every first-logo sample with FFmpeg PCM decoding:

```text
wkc2_adec_test <private-s16be-input> <private-f32be-output>
python tools/verify_lpcm.py <private-s16be-input> <private-f32be-output> --record <private-record.json>
wkc2_vdec_decode_test timing <private-mpeg2-stream> 30000 1001
```

The PCM comparison starts from assembled elementary samples; it does not independently
verify demuxing or the game's final audio mix. Sparse video timing is tested after
B-frame reordering and across two sequence starts. Patch 0009 removes fabricated
job-completion broadcasts; lifted SPU event instructions retain event delivery.
Patch 0010 supplies decoded video rate/colour/field metadata and derives missing
presentation timestamps at the decoded rational frame rate.

`vdec.cpp` supplies asynchronous context handlers for the eight imported video
APIs and uses the SDK's real FFmpeg backend for pixels. It copies accepted input
records/data, bounds pending input separately from decoded pictures, and delays
work when output fills. Consumption wakes the decoder so pending inputs can
complete. This replaces the SDK's synchronous output-count busy path, which can
leave the guest waiting after all accepted inputs already reported completion.
The asynchronous test checks pixels, sparse timestamps, callback counts, sequence
reset and immediate reuse of the submitted input buffers. The private logo test
also fills the decoded-output queue before it starts consuming frames.

AC-3 checks use original synthetic six-channel tones with independent float
references for stereo and 5.1. Every sample is compared across two sequence starts.
The optional private verifier generates a reference and runs the same native API
check against a complete elementary stream:

```text
python tools/verify_ac3.py <private-ac3-stream> <private-reference.f32be> --channels 6 --decoder-test <wkc2_adec_test.exe> --record <private-record.json>
```

The first-logo LPCM and opening/title AC-3 records are in
`verification/movie-demux.json`. Audio comparisons begin with assembled elementary
streams; independent container audio demux and original-console mix fidelity are
not established. Patch 0011 resolves FFmpeg's option setter for decoder downmix/DRC.
Matching runtime libraries are required; unavailable AC-3 decoders fail explicitly.

For a focused private job trace, set `SPURS_JOB_TRACE_FP` to a hexadecimal SPU
binary fingerprint before launching the probe. Patch 0012 logs dispatch and return
for at most eight matching jobs without enabling global per-job verbose output.
A successful return alone does not validate the job's output.

`WKC2_FIFO_TRACE=1` enables a read-only observation at the original game's graphics
wait timeout. It records at most three expected/current completion-label samples
and command words around GET/PUT in the private probe log. It does not advance
the command queue or change completion values.

With that observation enabled, `WKC2_FIFO_REGION=<hex-EA>:<hex-byte-length>`
also retains the two most recent observed writers for each word in the selected
command-buffer allocation. Patch 0013 supplies separate PPU-store and SPU-DMA
observation hooks. Timeout reports identify the PPU function/thread or SPU
image/PC that wrote the parked command and nearby words. Tracking is opt-in,
uses host memory proportional to the selected region, and can affect timing.
Host bulk copies and other unobserved write paths are outside its coverage.

Patch 0014 executes `JOBLIST` chain commands, using the list's guest-endian count,
descriptor size and array address. Every descriptor runs through the existing
SPU job dispatcher before the walker advances. The SPURS check covers 128-byte
and 256-byte lists, guarded publication, list order, a following single job,
empty lists and invalid sizes/ranges. The previous walker fails the list test.

Patch 0015 recognizes the canonical `cellSpursGetSpuId` library routine during
SPU lifting and binds it to job-manager handoff metadata while running a native
job. The serial chain walker currently exposes virtual SPU 0. Task/raw contexts
retain their original LS reads. This avoids overwriting executable job bytes
with a kernel structure. `SPURS_JOB_ABI_TRACE=1` reports the first eight bound
queries per host thread. Existing private SPU lifts need regeneration to acquire the binding.
The ABI test lifts synthetic instructions, runs the query through the actual
job staging code, checks preserved LS bytes and tests a similar unbound routine.

Patch 0016 adds `GCM_FIFO_PROGRESS=1` for a read-only view inside FIFO drains.
At the game's observed timeout it reports an approximate in-flight packet,
phase, packet count and the last sixteen branch words. Individual fields use
host atomics; the sample is not a coherent memory snapshot. This distinguishes
in-flight work from the GET value published only after a drain completes.
The trace changes timing and does not advance GET, consume commands or release
labels. Raw words and addresses remain in private logs.

Patch 0017 keeps vertex-program cache IDs stable for deferred draws and keys each
entry on the actual program through END, with bytecode comparison after hashing.
The cache grows as distinct programs appear and releases retained shader blobs at
backend shutdown. Its regression covers 128 queued IDs, copied bytecode, deliberate
hash collisions, unrelated bytes after END, invalid input and single release of
each compiled object. A negative control with the old moving 16-slot policy fails.
This fixed the customization UI at that checkpoint; later patches restore
avatar geometry, shading and the town view described in the gallery.

Patch 0018 adds `RSX_CAP_GEOMETRY=1` to completed-frame private captures. It
records the exact cached vertex programs, each draw's vertex constants and the
first sixteen uploaded vertices (all sixteen float4 attributes). The geometry
manifest records offsets and counts. Data is read after frame completion from
the retained parity buffers; source bytes remain private.

Patch 0019 records the guest depth-test enable, write mask and comparison with
each draw, includes that state in fragment-pipeline identity and applies it to
the D3D12 pipeline. It supports the eight GL comparisons and the runtime's
initializer ordinal. The helper regression checks comparison mapping, deferred
state identity and a UI-quad/model occlusion scenario. Restoring unconditional
depth testing/writes fails that scenario. Primary fallback pipelines still have
fixed depth behavior; this change covers the guest fragment-program pipelines.
The avatar model was still missing in that probe; subsequent fixes are recorded
in the gallery and findings log.

Patch 0020 adds optional intermediate GPU readbacks. Set `RSX_DRAW_SNAP_FILE`
to a private request file containing up to eight decimal draw-operation indices,
separated by spaces. The backend consumes the request on the next rendered frame,
queues a copy after each selected draw and maps the copies after GPU completion.
BMP files go to `RSX_CAP_DIR`; logs identify the actual guest pipeline and its
depth, culling and alpha state. Clear operations and unsubmitted draws produce
no image. This changes diagnostic timing without replacing shaders, render state
or guest memory. Use a completed-frame manifest to select operation indices.
Also set `RSX_DRAW_SNAP_DEPTH=1` to retain the depth plane after those draws.
Each `.depth.bin` begins with four little-endian uint32 values (width, height,
row pitch and device footprint format), followed by the original plane bytes.
The current D24 source uses its low 24 bits for normalized depth. The device
may describe the copy footprint as `R32_TYPELESS`; the stored samples remain
packed depth. These buffers, like shader and geometry captures, remain private.

Patch 0021 decodes `SET_ZMIN_MAX_CONTROL`, including its reset value, and retains
the depth clipping/clamping selection in each draw's raster-state cache key.
Bit 3 of the logged `cull` key now means depth clipping; bits 0-2 still describe
face culling. Guest fragment pipelines set D3D12 `DepthClipEnable` from that key.
A command-processor regression covers the reset, disable/restore transitions,
clamping, dirty-state notification and retained draw keys. Nonstandard ignore-W
behavior and primary fallback-pipeline clipping remain outside this change.

Patch 0022 decodes primitive-restart enable/value commands and assembles indexed
triangle strips and fans with restart boundaries and consistent winding. The
cursor survives contiguous command packets within one BEGIN/END stream, including
packets too short to produce a triangle. A C++ regression checks winding,
degenerate triangles, packet boundaries, repeated markers, disabled restart,
32-bit markers and fan anchors. Independently removing restart or winding
handling makes the regression fail.

Optional `RSX_INDEX_TRACE=N` captures up to 64 indexed strip/fan input packets in
`RSX_CAP_DIR`. Each contains the original indices, converted vertex attributes
and input-state description. These files include game data and remain private.

Patch 0023 extends private geometry captures with each draw's actual fragment
constant buffer (`op_NNN.fragment-constants.bin`) and a state description in
`fragment-inputs.txt`. The current host buffer stride is 2,304 bytes, including
unused capacity. Set `RSX_CAP_GEOMETRY_FULL=1` alongside `RSX_CAP_GEOMETRY=1` to
retain every converted vertex in `op_NNN.vertices-full.bin`; the original
16-vertex sample files remain available. This observes completed-frame inputs
without changing shaders, render states or draw order. Complete vertex buffers
and fragment constants contain game data and remain private.

Patch 0024 adds optional `RSX_DRAW_AUDIT=1` observation to private frame captures.
`draw-inputs.txt` records up to 8,192 draw callbacks per frame, with an explicit
omitted count, resource availability and geometry/record counters before and
after each original callback. Completed-frame counter and parity accompany the
records. The option also logs each unhandled RSX method once. It does not alter
the callback's inputs or resulting draws.

Patch 0025 adds optional `SPURS_JOB_EXIT_TRACE=1` observation. It logs up to 64
distinct image/status/stop/PC combinations per host thread after job execution.
The return register is included in each sample but is not part of the distinct
key. Reaching the native manager's return sentinel does not verify job outputs.
Logs and captures remain private because they can contain game data.

Patch 0026 makes the SDK's legacy YDKJ CRI task behavior select an image through
`SPU_CRI_IMAGE`, retaining 22 as the SDK default. This runner sets it to -1 before
starting SPU threads: WKC's image ordinals must not activate another title's
bootstrap, argument or context handling. The selection follows the MIT-licensed
[runtime used by Drakengard 3](https://github.com/soyjxck/ps3recomp/blob/de08397c0a485cd7705c049ca7532dd90fadc704/runtime/spu/spu_context.h).
Three regression cases execute the linked SPU task EXIT path with CRI handling
disabled or assigned to image 22 or 23. They check normal task termination and
the selected task's first-return/subsequent-exit behavior. Running the disabled
case with the old image-22 selection fails the actual return-path assertion.

Patch 0027 exposes an optional read-only SPURS job observer before and after the
same job. The port's observer is enabled only when `WKC2_JOB_CAPTURE_FILE` and
`WKC2_JOB_CAPTURE_DIR` name private request/output locations. Write three decimal
integers to the request file: an increasing request ID, a job count from 1 to 32,
and the image ID. For example, `1 4 19` selects the next four jobs of image 19.
Up to 128 jobs can be captured in one run. Each capture contains the full local
store and register file before/after execution, plus up to 512 ordinary DMA PUT
records and 1 MiB of their payload. Limits, nested jobs and I/O errors are
reported explicitly; atomic commits are outside this observer's scope. The
existing command-buffer writer observer remains connected when both are enabled.
These files contain game code/data and must stay in the private workspace.

Patch 0028 implements FNMS (`c - a*b`) using integer significands for SPU
single-precision extended-range values. Exponent 255 is finite; exponent-zero
inputs/results flush to zero, overflow saturates and results truncate toward
zero. In particular, zero multiplied by an extended value does not become a
host NaN. A separate unbounded-integer oracle generates 7,099 synthetic cases
covering extended operands, zero, cancellation, exponent boundaries and random
values. The native check compares all four lanes, totaling 28,396 results.
This patch covers FNMS result bits; it does not add FPSCR exception flags or
change the other floating-point operations.

Patch 0029 decodes RSX type-6 CMP vertex attributes as one big-endian word
containing signed X11/Y11/Z10 values. The fields expand by 5/5/6 bits and divide
by 32767, with W=1, including when the format's nominal size is one packed word.
The regression checks signed boundaries, distinct components, byte order,
stride, frequency selection and disabled constant attributes. Optional
`RSX_CMP_TRACE=1` logs at most four fetched samples per attribute; it observes
the actual format, source word and converted vector without changing them.

Patch 0030 restores the three fragment source-range modifiers encoded in DWORD2.
The supported ranges are full, half-range, [-2,2], [-1,1] and [0,1]; mode 5
remains a no-op. Saturation precedes ABS, signed range clamps follow ABS and
negation is last. The third source's ABS bit is also decoded. These are range
operations; this patch does not implement complete fp16 register arithmetic.
The Windows regression executes 72 synthetic source/modifier programs through
D3D11 WARP and compares float readbacks with independently calculated values.
It also checks a negative lighting term through SAT, LG2, EX2 and ADD, where
ignoring saturation previously generated a NaN. No game assets are used.

Patch 0031 records surface clears in submission order, including display clears
when guest-flip accumulation is enabled. It preserves the depth/stencil values,
aspect mask and scissor bounds at recording time. A depth-only clear no longer
becomes a colour clear. Captures include immutable surface dimensions, zeta
offset/pitch and clear parameters for pass diagnosis.

The Windows `wkc2_surface_clear_gpu` check runs production clear helpers on D3D12
WARP. It checks every pixel of synthetic colour, depth and stencil attachments
for independent aspect masks, exact depth values, clipped/empty scissors and
surface clip bounds. It creates no window and uses no game inputs. Partial
colour-component masks remain unsupported and are reported instead of clearing
unrequested components. Separate depth attachments, stencil write masks and
depth-texture sampling remain outside this patch.

Patch 0033 raises the deferred draw-record limit from 2048 to 4096. A native
WKC1 cutscene transition reached the old limit before the final display draw.
The follow-up retains 2149 operations, including that final composite, with
2246 audited callbacks and none omitted. Its vertex upload remains within
the existing capacity. This verifies record retention, not a correct scene.

Patch 0032 increases the offscreen colour-target table from 16 to 32 entries.
A captured later WKC1 frame submits 22 distinct targets; six post-processing
targets were absent from the host table. Capacity failures now emit a bounded
diagnostic instead of silently returning an unavailable target.

Shader descriptor ranges are also separated: atlas at 0, uploaded textures at
1..96, persistent targets at 97..128, and per-draw texture windows from 129.
The previous upload range overlapped both persistent-target descriptors and
the first draw windows. Uploading a later texture could overwrite an earlier
draw's descriptor before command submission.

## Guest frame presentation

The runner sets `RSX_ACCUM_FRAME=1` before graphics initialization. Its existing
pending-FIFO-flip path presents complete batches; display-clear heuristics can
otherwise retain a larger scene's threshold and stop presenting a subsequent
small movie batch. This selects frame completion policy and leaves the guest's
shaders, depth and culling state intact.

On Windows the idle wait uses the SDK's FIFO kick event, signalled by guest
fence polling and the changed parked-word producer notifications from patch
0062. The wait is bounded by four milliseconds or the next vblank deadline.
This allows a kick to wake the consumer promptly while retaining presentation
at guest flips. The
POSIX path keeps its existing wait; macOS execution remains unverified.

## Native text entry

`osk.cpp` replaces the imported keyboard handlers and integrates their events
with the guest's sysutil callback polling. It reads guest pointers and UTF-16
explicitly, preserves the caller's output capacity, and supports ordinary and
continuous entry/cancel/abort lifetimes. `osk_win32.cpp` supplies a Windows editor;
the platform boundary is declared in `osk.hpp`. A macOS editor is not implemented.

One regression uses synthetic guest memory and a controlled UI backend. It covers
Unicode boundaries, output canaries, callback order, duplicate/stale completion,
cancellation, abort, platform-open failure and continuous-mode text retrieval.
It also exercises optional scripted UTF-8 input through the same completion path.
Set `WKC2_OSK_INPUT_FILE` to a private request file for this path: `submit <text>`
submits a line and `cancel` requests cancellation. Requests are consumed only while
the keyboard is open. Malformed UTF-8 and embedded NUL bytes are rejected. No
default player name is supplied. Entered text is not written to the diagnostic log.
A second Windows regression exercises the production edit control and window
procedure with hidden test windows. It checks Unicode text, OK, Cancel, closure,
reuse and orderly shutdown.

`l10n.cpp` registers the UCS2stoUTF8s and UTF8stoUCS2s imports used around name
entry. Its synthetic regression checks guest byte order, exact counts (including
embedded NUL), output canaries, size queries and partial-conversion errors.
These UCS-2 APIs reject supplementary characters; the keyboard protocol itself
supports UTF-16 surrogate pairs. Shift-JIS conversion is not implemented here.

The intended replacement for the temporary Windows editor is an in-game keyboard
with complete gamepad navigation, acceptance and cancellation, sharing the same
guest protocol on Windows and macOS. That interface remains planned work.
