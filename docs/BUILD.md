# Current Windows build workflow

The source checkout and private working directory can be separate. Pass the
private workspace with `-Workspace`; the build script gets project sources from
its own location. Its default workspace layout is three parent directories above
the script and is intended for the original development layout.

Required tools: PowerShell, Git, Python 3.11+, Clang/clang-cl, CMake, Ninja,
MSVC Build Tools headers/libraries and the Windows 10 SDK. Install the Python
dependencies listed by the pinned PS3Recomp SDK, plus PyCryptodome. Keep downloaded
tools and dependency repositories in the private workspace.

The current `windows-env.ps1` locates Clang in `work/tools/llvm/bin` and uses
MSVC 14.29.30133 / Windows SDK 10.0.19041.0. Adjust these tool locations for a
different installation. They are build prerequisites, not runtime requirements.

Private workspace layout:

```text
work/
  deps/ps3recomp/          # commit a2c20108af45174dbb7d3e2f87f6acaaef09c6b4
  deps/DirectX-Headers/   # commit 461ef5138bd30922b44fbad77c4e47662812d2d5
  tools/llvm/bin/
  venv/Scripts/python.exe
  wkc2-inspection/PS3_GAME/USRDIR/EBOOT.elf
  wkc2-disc/              # privately extracted disc tree used by the VFS
  wkc2-analysis/
  wkc2-generated/ppu/
  wkc2-generated/spu/
```

Supply your own decrypted BLUS30784 v01.00 executable and extracted disc tree.
The analyzed ELF is 20,455,904 bytes, SHA-256
`333400e0b0915e3008a22f29daf95ed2c9b03f3c2a2787cf5705222d73a5ceeb`.
No executable, asset or generated game source is distributed here.

```powershell
./tools/build.ps1 -Workspace <private-workspace> -Regenerate
./tools/probe.ps1 -Workspace <private-workspace> -Seconds 15
```

`-Regenerate` analyzes and lifts the PPU plus embedded/executable-backed SPU jobs.
Three additional asset-backed SPU jobs were captured during development; reproducing
the most advanced boot currently requires private captures and the import workflow
in `tools/import_spu_misses.py`, followed by `build_spu_workloads.py`. A fresh disc-only
build is therefore not yet a complete reproduction of the latest verified UI boot.

`tools/prepare_sdat.py <private-USRDIR>` authenticates and caches the supported
SDAT archives. The build applies twenty-six numbered runtime patches idempotently
and runs twenty-five checks: real video decoding, sparse video timing, asynchronous video queues, LPCM queues/conversion, stereo/surround AC-3,
Vpost conversion/windows, callback stacks,
demux ABI/lifetimes, MPEG packet/access-unit parsing, PAMF reader, lightweight
conditions, memory containers, LFQueue, event queues, RSX texture formats and
SPURS guard lifetime/notification behavior, ordered job-list dispatch and the
native SPURS kernel-ID binding, plus vertex-program cache identity/lifetime and guest depth state.

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
The checked samples are in `verification/movie-demux.json`. Real decoded video now
also reaches the native renderer, but these checks do not establish continuous
in-game playback or correct audio.

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
This fixes the observed customization UI output; model rendering remains unverified.

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
The avatar model remains missing in the latest native probe.

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
Bit 3 of the logged `cull` key now means depth clipping; bits 0–2 still describe
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
