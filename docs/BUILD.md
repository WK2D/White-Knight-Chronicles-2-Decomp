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
SDAT archives. The build applies twelve numbered runtime patches idempotently
and runs seventeen checks: real video decoding, sparse video timing, asynchronous video queues, LPCM queues/conversion, stereo/surround AC-3,
Vpost conversion/windows, callback stacks,
demux ABI/lifetimes, MPEG packet/access-unit parsing, PAMF reader, lightweight
conditions, memory containers, LFQueue, event queues, RSX texture formats and
SPURS guard lifetime/notification behavior.

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
