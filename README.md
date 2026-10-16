# White Knight Chronicles II native-port research

Target: USA BLUS30784, disc version 01.00. The goal is native Windows execution
using static C/C++ recompilation, followed by Apple Silicon portability testing.

**This is not a playable port yet.** Analysis, compilation, boot, rendering and
gameplay are distinct milestones. Passing an analysis or build step does not
establish that the game works.

See [build instructions](docs/BUILD.md) and [contribution workflow](docs/CONTRIBUTING.md).
The [findings log](docs/FINDINGS.md) tracks verified results, open issues and next steps.

## Update 1.01 and DLC plans

We will add support for the USA **1.01 update and DLC** as part of this port.
Development currently uses the original BLUS30784 v01.00 disc executable as its
baseline, with gameplay testing starting in White Knight Chronicles I.

The supplied update's metadata confirms BLUS30784, APP_VER 01.01. A matching USA
DLC package has also been identified. Integration will cover the updated
executable and patch data, DLC discovery/loading, and gameplay/save compatibility.
**Update 1.01 and DLC are planned work; runtime support is not yet verified.**
We will also compare the European DLC for content absent from the USA package
and investigate compatibility with BLUS30784. Initial file comparison finds one
EU-only entry; what it unlocks and whether it works in the USA build are unverified.
Original packages, their contents and local source locations remain private.

## Progress gallery

The title menu now renders with New Game, Continue and Options over an animated
background. LPCM startup logos and AC-3 title/attract movies play through the native
movie pipeline. See the [newest-first gallery](docs/GALLERY.md).
Controller input opens New Game and selects White Knight Chronicles I. Description
and dialog text now render correctly. WKC1 loading now passes the guard-registration failure and reaches a further SPU
job that now runs and returns. Ordered job-list dispatch now reaches four further
geometry jobs that previously were skipped. Their output and the subsequent
graphics/FIFO stall still need investigation; gameplay and save/load remain
unverified.

[![Native WKC1 selection](verification/screenshots/2026-10-16-wkc1-selection.png)](docs/GALLERY.md)

## Workspace layout

The source project is here. Private game files, generated game code, build
directories, downloaded tools and dependency checkouts live under a separate
private workspace's `work/` directory. Original game files and generated game code
are kept there; reviewed screenshots are published separately in the gallery.

The port uses the [PS3Recomp SDK](https://github.com/sp00nznet/ps3recomp).
Its pinned revision is recorded in `project.json`; its license is retained in
`LICENSE.ps3recomp`. Runtime changes must be
recorded as patches rather than silently updating that revision.

## Analysis

Using a Python environment with the SDK requirements installed:

```text
python tools/analyze.py --sdk <ps3recomp-checkout> --elf <EBOOT.elf> --output <analysis-directory>
```

This runs the SDK loader, bounds candidate functions to executable sections,
resolves firmware import names and inventories generated HLE handlers. The
coverage report distinguishes handler presence from verified implementation.

## Milestones

- Disc metadata and executable identified: complete.
- Retail SELF decrypted locally using RPCS3 CLI: complete.
- Function/import/SPU inventory: initial pass complete (56,580 lifted PPU functions;
  five embedded SPU ELFs, 12 executable-backed raw SPURS jobs and four observed
  asset-backed jobs; 21 registered SPU images).
- Native executable build: passes on Windows using the pinned SDK, Clang 23.1.3,
  local Windows SDK/MSVC headers and DirectX-Headers. `tools/build.ps1` reproduces
  the build from this workspace's prepared inputs; `-Regenerate` also runs the lift.
- Native startup: executes the game, its SPU tasks/jobs, filesystem operations,
  graphics initialization and audio initialization. GPU readback confirms the
  language-selection screen, copyright page and warning screen render natively.
- Initial UI input: a simulated CROSS press selects English and the screen fades
  into the next startup stage. Further input and gameplay remain under test.
- Title screen and menu input: verified on Windows.
- Readable New Game and WKC1 selection dialogs: verified on Windows.
- Scene, combat and save/load verification: pending.
- Apple Silicon build and testing: pending.
- GeoNet compatibility: separate later research; not established.

## Current investigation

The first native run spun on invalid FIOS conditions. `lwcond.cpp` corrects the
8-byte guest layout and implements waits/signals with portable C++ synchronization.
Its regression test checks adjacent memory, timeout/reacquisition and a real wakeup.

The runtime patches fix Windows offsets above 2 GB, reject partial SDAT caches,
avoid duplicate synthetic completions after real SPU events, and make job-chain
shutdown/join wait for actual walker termination. They also implement syscall
350 (allocation from a memory container), track container capacity and frees,
correct the container-size query ABI, and accept heap-backed SPU job binaries
instead of rejecting every binary above 0x10000000. Changes stay in `patches/` and
are applied idempotently by the build script.

`tools/prepare_sdat.py` prepares the three large disc archives with accelerated
AES and authenticates every block. It writes atomic caches beside the extracted
files; the original ISO is unchanged.

The pool-28 allocation failure is resolved: the runtime previously acknowledged
syscall 350 without allocating its requested 24 MB. The settings file now loads
and initialization advances. Both the lightweight-condition and memory-container
regression tests pass.

`lfqueue.cpp` implements import 0x8A85674D (`_cellSpursLFQueuePushBody`) and the
guest queue initializer used by WKC2. It serializes producers with the SPU atomic
line lock, publishes copied ring entries, and signals registered task waiters.
The queue regression test covers capacity, sequence wrap, waiter routing and
guest layout. All three regression tests pass.

Eight real asset blocks decoded by the lifted SPU match independent Python zlib
raw-DEFLATE output byte for byte (256 bytes and seven 64 KB blocks). The hashes
and validation method are in `decompression-verification.json`; private game
bytes remain under `work/`. Temporary SPU capture instrumentation was removed.

The heap-binary fix exposed two asset-backed jobs (fingerprints
687D4BB18564813A, 54,544 bytes; 164B23483A68CBEA, 1,968 bytes). Both have now
been lifted and execute natively. The renderer worker passes its previous wait.
`tools/import_spu_misses.py` validates newly dumped job fingerprints and wraps
them in private ELF inputs for `build_spu_workloads.py`. Prepared image inputs,
including these asset-backed captures, remain under `work/` for regeneration.

The D3D12 fragment path now binds all 16 RSX texture/sampler units. WKC2's UI
shader uses units 4 and 7; the old four-unit root signature rejected its pipeline.
It also uses the current buffered-constant decompiler API, snapshots exact inline
constant bits for each draw, honors the export-width control, and uses the API's
cubemap, alpha-test and unnormalized-coordinate handling. The shader cache includes
the static alpha comparison and coordinate dimensions. The old screen-size texture
substitution heuristic is opt-in (`SCREEN_AS_TEX=1`); it replaced real render-target
textures with a black previous-frame copy during WKC2 startup.

The runner consumes each pending flip once, removing a duplicate presentation
path. A 120-second probe observed 6,171 guest flips and 6,171 presentation calls.
GPU readback, rather than those counters, establishes the visible UI milestone.
Original GPU captures are retained in the private workspace. The gallery publishes
lossless PNG versions with optional metadata removed, alongside verification notes.

Queue creation publishes its host wait primitives before the queue becomes active,
and SPURS completion sends hold table ownership through enqueue, avoiding the
observed send-versus-create crash. A fourth regression test concurrently sends
while creating/destroying the same queue ID 2,000 times, then checks capacity and
payload order.

The FIFO walker preserves self-jumps while the SPU command producer fills its
segment. Previously it skipped to PUT and discarded commands and completion labels;
that caused a graphics-fence stall after language selection. Intentional producer
and semaphore waits are excluded from the stalled-GET recovery heuristic. SPURS
guard waits now last until notification or shutdown instead of killing an idle
chain after 20 seconds.

The later startup stall was traced to a dropped installation request: a background
check was still pending when the request arrived. `tools/apply_port_hooks.py`
installs synchronization before the original guest request. The guest then performs
the actual installation and starts loading the first logo movie. All three large
installed archives (4,364,470,879 bytes total) match their disc data by SHA-256;
see `verification/installation.json`.

The nine imported PAMF reader APIs now use the guest ABI, big-endian output and
actual stream descriptors. A fifth regression check covers synthetic video/audio
headers, 64-bit sizes, copied readers and invalid input. An optional private fixture
parses the first logo's two streams and 1280x720 video. Native startup now passes
reader initialization and reaches movie demux/decoder threads.

`dmux.cpp` now supplies memory queries, real MPEG program-stream/access-unit
parsing, guest callbacks, AU lifetimes and queue backpressure. Independent decoding
matches every pixel in 3,806 extracted video frames across the logo and opening
movie; extracted AC-3 also decodes. See [verification](verification/movie-demux.json).
The native guest receives actual video and LPCM units through its decoder calls.
Callback stacks are isolated across host threads and nesting.

The imported video-postprocessor interface now has correct guest fields and actual
progressive YUV conversion, cropping and bilinear resizing. The build enables the
SDK's FFmpeg decoder with pinned 8.1 headers and a separately supplied matching
library. A 210-frame decoder check matches every frame through the guest video API
on two sequence passes, including reordering and corruption/drop negative controls.
Seventeen checks pass. The title screen and its menu render natively; see the gallery.

Packed A4R4G4B4 font textures were excluded by the D3D12 binding filter, leaving
previous artwork selected for text draws. The backend now records the current
texture, decodes through the shared RSX format/row-pitch path and creates the SRV
with the uploaded resource's format. New Game description and confirmation text
are readable. The suite also runs the SDK's texture layout, packed-format and
component-remap checks.

White Knight Chronicles I is the starting point for gameplay validation. The old
32-entry guard table filled while the game created several guards per live chain.
An unknown guard then let an unprepared job run. Guards now use allocated entries,
notifications/reset/waits are synchronized, and joining releases their owning
chain's entries. Unknown guards stop before dispatch. Regression checks cover
100 chain lifetimes and 128 simultaneous guards. The native run passes that
failure. A further 11,856-byte SPU program has been statically translated, and
eight targeted native dispatches return successfully. Its output correctness and
the following graphics/FIFO stall still need investigation.
Gameplay, save/load and missing imports remain open.

Run `tools/probe.ps1 -Seconds 15` for a bounded diagnostic attempt. Logs and a
result JSON go into `work/probes/`; unrecognized SPU jobs go into the private
analysis directory. Probes record the last observed guest flip and presented-frame
counts every five seconds; these counters alone do not verify visible content.
`-CaptureFrames` saves four backend BMP readbacks by default in a private probe
subdirectory. `-CaptureFrameCount`, `-CaptureEvery` and `-CaptureSkip` select a
longer sampling interval; probes accept durations up to 600 seconds, with each
host wait bounded to five seconds. `PAD_SCRIPT` can supply timed controller pulses
through the SDK's controller path; the probe records that script in its result.
Capture suppresses the runner's pre-flip placeholder presents, changing
presentation timing for diagnostics. `WKC2_THREADS=1` adds guest thread register
snapshots at each progress report.
`tools/trace_calls.py` optionally adds opt-in return/argument
tracing to selected generated functions. Those diagnostics are disabled unless
`WKC2_TRACE_CALLS=1` is set.
