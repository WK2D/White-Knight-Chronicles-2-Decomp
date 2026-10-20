# Native execution work

The target is **100% replacement of the game's generated SPU code with
project-written, readable, publicly available C++**, across all 24 currently
identified images and any additional code discovered during validation.
This includes job interfaces, outputs, DMA visibility and scheduling behavior.
Generated instruction-by-instruction C and its register/local-store ABI are
temporary references, not the final implementation. Native compilation and
interpreter-free sampled routes alone do not satisfy this target.

No complete SPU image has yet been replaced. The current readable geometry
kernels are intermediate work. Completion requires a player build that links
no generated SPU game code, uses no SPU interpreter, and has validated native
interfaces and behavior throughout the game. Build-only reference replays may
retain private generated code for differential verification.

Python is currently used to
extract inputs, generate C/C++, prepare fixtures and analyze captures. These
tools run outside the game. Rewriting them in C++ is not an FPS optimization
and does not remove the SPU interpreter fallback.

The current program compiles 56,580 generated PPU functions and 24 SPU image/job
inputs. Its PS3 memory/register conventions and compatibility services still
exist. It is not yet an interpreter-free port, nor a reconstruction of the
original readable source. Static image registration alone is not full coverage.

## Current execution evidence

The 115-patch Continue probe recorded six image-17 fallback rows. The next
reference run also found four image-19 rows. Five extra static entry points now
cover those observed paths: image 17 `0x7D60`, `0x7E38`, `0x7E78`; image 19
`0x49B8`, `0x4AE0`. Complete code-region checks and 40 private replay pairs match
the full context, 128 registers, 256 KiB local store, PC/status and continuation.
The replay covers these functions, not complete job/DMA scheduling.

The 117-patch DirectX probe `20261019-213333` completes a 420-second route-control
window after Continue, including movement, camera rotation and the Balastor Plain
exit prompt. With interpreter and microstep entrances forbidden, the table stays
at zero and neither fatal guard fires. The exit prompt is not a confirmed area
transition. A separate instrumented run confirms execution of all five new entries.

A separate strict DirectX route `20261019-215032` confirms the actual town-to-
Balastor Plain transition with zero fallback. The ground incorrectly occludes
Leonard’s lower body there. That fault reproduces on the older 115-patch DirectX
build; Vulkan 117 shows the lower body on the corresponding route. This remains
a DirectX rendering issue fixed in the inspected route by patch 0118; it was
not caused by the static SPU entries. Production probe `20261019-222607` also
completes transition, short movement and camera rotation with zero strict fallback.

## Readable C++ geometry prefix

The image-19 `0xCE08` block now calls a project-written C++ four-influence matrix
blend and a complete supported post-blend batch operation in `native_skinning.h`, through
`native_skinning_bridge.cpp`. The core uses
named vertex, weight and cyclic affine-matrix types; it has no guest registers,
addresses or DMA interface. The bridge decodes packed indices and local-store
palettes, preserves the original multiply/FMA order and exports the required
register boundary. The batch operation transforms four positions, forms their
normal-cofactor matrices and first normal products, writes positions to local
store, reloads the next batch and return address, and prepares the next weights
and stack records. It exports all live and scratch registers before the wrapper
dispatches the continuation. Later blocks still accumulate/pack normals and
complete the geometry job; this is one block, not a complete job replacement.

The earlier cofactor bridge computes 12 rounded products and 12 fused subtract results
into private storage without changing the context. The hook materializes each
value at the original register write, preserving intervening stack stores and
register uses. Noncanonical controls keep the compiled arithmetic. Expanded fuzzing
also exposed a missing xyz-copy mask guard in the position bridge; that guard now
rejects unsupported layouts before changing state.

The current batch bridge replaces that instruction-by-instruction tail on
supported layouts. All inputs are validated before architectural writes.
Position stores precede the next-input and return-address reads; stack stores
retain their order. This matters when output overlaps the stack or read-ahead
buffer, including wrapped and unaligned addresses. Host and audit modes retain
the compiled C tail for nonfinite arithmetic inputs: host IEEE math leaves NaN
payload selection sensitive to compiler operand exchanges. Exact-SPU mode has
no value restriction. This preserves compatibility rather than asserting a
portable host NaN-payload contract.

`tools/apply_native_geometry.py` checks the complete original block hash, retains
its compiled reference and installs the native calls idempotently. An existing
blend-only, blend/position or cofactor hook upgrades only after its exact retained reference and wrapper
match. Unknown image or weight masks decline without changing state and use
that compiled reference.
`WKC_NATIVE_SKINNING=0` selects the reference for research comparisons. No game
function body or capture bytes are published.

Validation on Windows x86-64:

- Eight initial captures only exercised one-bone weights. A second capture set
  contains 64 distinct blocks with mixed weights in all four vertices, using
  bone indices up to 35. All 72 complete block replays match the reference's
  context, all registers, local store, PC/status and continuation.
- A separate recovered position operation matches all 256 positions from the
  64 mixed-weight captures. The position operation is now installed; all 72
  full-block capture replays still match the complete state and continuation.
- Two 20,000-case full-state fuzz runs pass against the retained compiled
  reference, each split equally between host and exact modes. One uses arbitrary
  arithmetic bit patterns; one supplies finite weights, palettes, normals and
  positions. Both cover wrapped 16-bit indices, output/stack aliasing, wrapped
  and unaligned addresses, unsupported masks and inconsistent cyclic layouts.
  The finite run accepts 6,134 native batches in each mode; its remaining cases
  intentionally exercise compiled compatibility paths. The arbitrary-bit run
  accepts 623 host and 6,134 exact batches; nonfinite host inputs retain the
  compiled tail. These totals include compatibility cases, not 40,000 native accepts.
- The portable arithmetic test checks 32,000 finite cases against a lane oracle.
  A further 32,000 finite cases match the optional x86 FMA policy bit for bit.
  Another 50,000 wide finite bit-pattern cases check blend and complete geometry
  stages, including overflow, subnormals and nonfinite intermediates, with no
  NaN-payload or signed-zero tolerance.
  Live execution keeps SDK float helpers to preserve host/exact/audit semantics;
  the finite host-FMA test does not establish exceptional-value SPU equivalence.
- The portable cofactor operation matches all 768 matrix vectors from the
  64 mixed-weight captures. It passes 64 independent signed-minor matrix tests and 32,000
  finite scalar/x86 FMA comparisons of both products and results. It is now
  installed in live dispatch; all 72 complete block replays still match. The
  current complete batch operation also owns the stores and first normal products.
  Of all 72 complete-state replays, 71 accept the new operation and one retains
  the compiled tail for nonfinite host inputs. The separate finite cofactor
  comparison does not assert NaN-payload equivalence for that exceptional capture.
- Strict DirectX route `20261019-230123` and Vulkan route `20261019-231203` load
  Continue, enter Balastor Plain, walk and rotate the camera with zero fallback
  entries. Trace markers establish at least 16,777,216 native blend calls per
  run. Inspected final frames retain Leonard's lower body and the surrounding
  scene; these instrumented runs are not performance benchmarks.

New strict routes `20261019-234935` (DirectX) and `20261019-235407` (Vulkan)
complete the same transition/walking/camera path with the position replacement,
zero fallback and at least 16,777,216 transformed blocks in each run. Both final
frames were inspected. The Vulkan run additionally samples CPU time; profiling,
tracing and GPU captures make it unsuitable as an FPS benchmark.

Both Windows suites now pass 113 checks with 122 SDK patches. No FPS gain,
whole-job/DMA scheduling equivalence, complete static coverage or ARM execution
is claimed.

The cofactor replacement completes strict DirectX route `20261020-010450` and
Vulkan route `20261020-010805`, each through Continue, the plains transition,
walking and camera rotation. Both report zero interpreter/microstep entries and
at least 16,777,216 native cofactor blocks. Final frames were inspected. The
hidden, muted runs use isolated save copies, tracing and automatic captures;
they are not performance benchmarks. The broad solid foliage-shadow edges
remain visible. The full suites pass in 26.73 seconds (Debug) and 23.10 seconds
(Release).

The complete supported batch operation passes strict route `20261020-014206`
(DirectX) through the plains transition, walking and camera rotation. Vulkan
`20261020-014512` stays in town after movement and camera inputs; its attempted
plains transition was not reached. Both record zero fallback entries and at least
16,777,216 completed native batches. Both final frames were inspected. The same hidden, muted,
isolated and instrumented limits apply. All 112 checks pass in Debug (26.70
seconds) and Release (22.75 seconds). This is still one geometry block; the
DirectX foliage shadow remains solid in its pre-alpha-reference-fix view.

After the independent renderer correction in patch 0121, DirectX route
`20261020-015412` and adaptive Vulkan route `20261020-015927` both confirm the
plains transition, walking and camera rotation with zero strict fallback entries.
The geometry implementation is unchanged. These routes also show the separate
leaf-cutout shadow improvement; cloud shade, grass density, whole-job coverage
and performance remain open.

## Work order

1. **Refresh runtime evidence.** Capture fallback sessions and sampled CPU time
   on Continue, NPC-heavy movement, area transitions and cutscenes. Record which
   build and routes were measured. Distinguish waiting from executing code and
   per-worker samples from whole-frame cost.
2. **Extend verified static coverage.** The five image-17/image-19 entries above
   are integrated through `tools/build_spu.py`, with pinned image hashes and a
   stable 24-image registry. Keep testing NPC-heavy movement, confirmed area
   transitions and cutscenes with `SPU_FALLBACK_STRICT=1`; investigate any new
   entry before promoting a wider interpreter-free claim.
3. **Take semantic control of hot jobs.** An earlier profile identifies image-19
   geometry code, including entry `0xCE08`, as a useful candidate. Matrix blending
   and a supported complete post-blend batch boundary are integrated beside
   compiled references. The final normal-output batches are now replaced separately below. Recover
   the remaining normal-loop variants and establish whole-job boundaries. Preserve SPU float
   behavior, byte order, alignment, aliasing, DMA visibility and job ordering.
   The refreshed town profile below confirms work on this geometry worker,
   alongside channel waits, PPU polling and renderer waits; it does not establish
   that the recovered arithmetic determines the whole frame.
4. **Optimize validated kernels.** Use portable scalar C++ as the specification,
   with tested x86 SIMD and future ARM/NEON implementations where useful. SIMD
   must preserve required SPU rounding, denormal, saturation and fused-operation
   semantics. Compiler-generated C alone is not evidence of efficient execution.
5. **Remove reliance on fallback.** After each replacement passes differential
   tests, route native calls to it and log unexpected interpreter entries in
   diagnostic builds. Keep the fatal guard opt-in until broader
   coverage is established. An empty completed-session table alone is insufficient evidence.

## Acceptance for each native replacement

- A documented interface, input bounds, ownership and known behavior.
- Independent arithmetic/boundary tests, plus private real-job comparisons of
  complete outputs and side effects. Partial replays must be labelled partial.
- A native route that actually executes the replacement, including NPC/transition
  cases relevant to it, with remaining fallback calls reported.
- Matched performance measurements before claiming an improvement. Removing a
  small fallback may improve coverage without producing measurable FPS gains.
- Public source contains project-written code and metadata only. Game binaries,
  extracted bytes, generated game source and captures of guest memory stay private.

The final player build should not require Python or development scripts to run.
Build-time generation may continue to use Python. Later replacement of build
tools can be justified by packaging, maintenance or reproducibility needs.

See [execution architecture](ARCHITECTURE.md), the
[fallback census](FINDINGS.md#spu-fallback-census-and-host-runtime-review), and the
[DirectX audit](DIRECTX_PARITY.md). The DirectX shadow investigation and this CPU
work are separate; neither is a claim of completed native coverage.

## SPU reference cross-check

The [SPU overview](https://www.psdevwiki.com/ps3/Synergistic_Processing_Unit_(SPU))
and [documented channels](https://www.psdevwiki.com/ps3/Documented_SPU_Channels)
help check ABI, channel and DMA boundaries while recovering kernels. The
[undocumented channel table](https://www.psdevwiki.com/ps3/Undocumented_SPU_Channels)
mostly covers isolation, loader, persistence, RNG and performance-monitor
facilities. A static scan of all 24 generated images finds no channel calls in
its 64–74 range. Two `rchcnt 113` sites occur beside undecoded words in images
1 and 2; their reachability and meaning are unverified, so they are not treated
as confirmed runtime requests. It does not establish that WKC uses a missing channel. Add behavior
only after an actual decoded instruction or runtime request establishes the need;
do not substitute guessed channel results for correct native execution.

## Refreshed CPU sample limits

The final ten-second Vulkan plains window in `20261019-235407` records one
thread using about 96% of one CPU core. Most of its samples stop inside Windows
calls whose nearest game-side caller is the short `sys_timer_usleep` yield/QPC
loop. Native instructions confirm the two-call deadline loop. This is active
polling overhead, not evidence of SPU arithmetic time or an NPC-spawn cause.
Other samples include guest-memory helpers and renderer conversion/texture work.
DLL-side caller attribution uses a stack scan and is approximate. A controlled
NPC-heavy town profile was subsequently collected as described below. The
older stack scan is not used for the new offline-unwound caller evidence.

## NPC-facing cadence and rejected host-FMA trial

The 121-patch DirectX run `20261020-023115` loads the current save privately at
the 30 FPS limit. Three separate 12-second windows count renderer presentations:

| View | Presentations/s | Process CPU ms/presentation |
| --- | ---: | ---: |
| Facing the Logic Stone | 29.91 | 131.31 |
| Camera facing the street | 12.00 | 316.95 |
| After walking into the main road | 8.08 | 454.57 |

The window is hidden, audio is muted and the save is copied. Sampling, tracing,
diagnostic logs and automatic GPU snapshots are disabled during timing; the
capture counter confirms no readback in any window. The strict fallback guard
stays enabled and the interpreter/microstep census remains zero. Camera,
position and scene time change, so this is not proof that NPC spawning alone
causes the slowdown. These counters do not measure unique animation updates.

A separate CPU sample identifies image-19 geometry work on the active SPU worker,
including the recovered C++ functions and compiled routines. Offline unwinding
of 480 stack snapshots also identifies channel waits in `spu_rdch`, a PPU worker
in `sys_timer_usleep` yield/QPC polling, and renderer fence waits in `ld_flush`.
Maximum thread suspension was 246 microseconds; unwinding happened after resume.
These separate profiles are not whole-frame percentages or performance windows.

An experimental bridge selected x86 vector FMA for finite host-mode inputs,
retaining SDK exact/audit and exceptional-value behavior. It passed all 72
captured full-state comparisons and both 20,000-case differential fuzz runs.
Correctness did not translate into a measured speed improvement. Private run
`20261020-025524` switches the arithmetic policy in the same process, without
moving the camera between modes within either view. Six 12-second windows per
view use scalar/SIMD/SIMD/scalar/scalar/SIMD order, with two seconds to settle:

| View | Scalar presentations/s | SIMD presentations/s |
| --- | --- | --- |
| Street | 12.67, 12.33, 12.17 | 10.00, 10.33, 12.67 |
| After walking | 8.50, 8.67, 8.42 | 7.92, 8.08, 7.42 |

Scene time and NPC animation still vary; these are bounded live comparisons,
not deterministic replays. No GPU snapshots, diagnostic logging or sampling
occur in the timed windows. The strict census stays empty. The runtime change
was withdrawn before publication and was never installed in the desktop build.
The additional 50,000-case arithmetic regression remains. Both complete Windows
suites pass all 113 checks without media skips: Debug 26.90 seconds, Release
22.21 seconds. Town performance, later normal processing and whole-job ownership
remain open; the next optimization needs evidence from the critical path.

## Readable final normal output

The image-19 `0xD500` boundary now uses project-written C++ for its final two
batches of four normals. `normal_length_stages` transposes decoded components
and preserves the rounded x-square, fused z-square addition, then fused y-square
addition. `scale_skin_normals` applies four reciprocal lengths while multiplying
w by one. These core interfaces have no SPU registers, addresses or local store.

The narrow bridge retains SDK reciprocal-square-root estimate/interpolation and
host/exact/audit arithmetic. It does not substitute host `sqrt`. Eight output
stores retain their original order when streams overlap, wrap local store or
use unaligned pointers. Every scratch/live register is exported; the wrapper
owns PC/continuation dispatch. The block has no DMA or channel operations, so
this verifies no whole-job DMA/scheduling boundary. Other normal-loop variants
and most of the image remain generated code.

All 32 captured complete contexts from saved-game run `20261020-031202` match,
and all 32 take the new C++ path, including exceptional host inputs. Wider fuzzing
first found a two-NaN multiplication payload mismatch: LLVM commuted a product
after decoding native values. The host policy now explicitly gives the first
normal operand's quiet payload priority when both operands are NaNs. Exact/audit
continue through their SDK policies. This matches the validated Windows reference;
ARM behavior remains to be tested.

The corrected implementation passes 96,000 complete-context comparisons: 32,000
each in host, exact and audit mode, using the same random sequence in every mode.
They mix finite and arbitrary arithmetic bits, malformed control/packing,
wrapped/unaligned outputs and overlapping streams. Each mode accepts 25,272
native cases and declines 6,728 intentionally invalid layouts to its compiled
reference. There is no nonfinite-value guard on supported layouts. Comparisons
include all registers, local store, normalized full context and continuation.
Another 32,000 finite arithmetic tests match independent per-normal calculations
and optional x86 FMA; live SIMD remains disabled following the earlier slowdown.
Public tests also cover the NaN payload and rejection without state changes.

Strict DirectX route `20261020-032618` and Vulkan route `20261020-033037` load
Continue, confirm the exit prompt, enter Balastor Plain, walk and rotate the
camera. Both have zero interpreter/microstep entries and positive final-normal
trace counts; final frames were inspected. Hidden windows, mute, isolated saves,
tracing and GPU snapshots make these correctness checks unsuitable for FPS claims.
Both full Windows suites pass 113 checks without media skips: Debug 29.90 seconds,
Release 23.04 seconds. A hash-checked generation hook retains a private reference
and rejects altered inputs/installations. Full SPU replacement remains incomplete:
**zero of the 24 identified images have been completely replaced**.

## Readable whole skinning stream

The supported one- and two-direction modes of image-19 routine `0xC968` now
execute through project-written C++. `native_skinning_stream.h` supplies the
typed algorithm; `native_skinning_stream_bridge.cpp` handles the current ABI.
The core contains no SPU registers, local store, guest addresses or generated game
code. It accepts packed influences, decoded positions, direction streams, live
palette reads and ordered output callbacks, processing four vertices per group.

This replaces the supported complete skinning routine, including setup, iteration
and output drain. It does **not** replace the outer SPURS job, DMA/scheduling or
an entire image. No complete image is replaced. Unsupported interfaces retain the
private compiled reference; the earlier smaller hooks remain on that path.

The current interface is:

| Input | Observed role |
| --- | --- |
| r3 | Metadata; a big-endian 16-bit vertex count at byte 8 |
| r4 | Table of position and direction-stream pointers |
| r5 | Stream count including positions; supported values 2 and 3 |
| r6 | Eight interleaved weight/joint bytes per vertex |
| r7 | Cyclic matrix palette, 48 bytes per entry |
| r8 | Declared palette count |

These describe observed behavior, not original symbols. The adapter checks
alignment, bounds, nonzero counts divisible by four, disjoint output streams and
the supported stack/control context before writing. Interrupt/policy contexts
and other layouts remain compiled. It preserves the 2,224-byte saved-register
frame, cyclic read-ahead values and continuation bookkeeping: `4*g+1` steps for
one direction and `3*g+2` for two, with `g` the group count. These are runtime
dispatch counts, not game time. The routine has no decoded channel operations.

Live palette reads are essential. Some encoded indices exceed the declared
palette count and observe earlier output or frame writes. All four influences
are read, including zero weights. Decoding the palette once failed 14 of the
32 recorded calls. One direction stream delays output by two groups; two streams
delay it by one. Initial empty writes and the final drain must also be preserved:
the first batch multiplies zero by the initial reciprocal, and the second empty
batch in the two-group pipeline goes through reciprocal normalization. Those
writes can affect later aliased palette reads.

The earlier offline candidate exposed host NaN-payload and signed-zero
differences. Cofactor multiplication and multiply-subtract now explicitly preserve
the compiled host path's NaN selection, sign and quieting. Exact/audit arithmetic
continues to use the SDK policies. Under downward rounding, the SDK's unsigned
conversion can produce negative zero and the initial reciprocal can also be
negative zero. The adapter samples these two policies once per routine; all
nonzero byte weights remain exactly representable. No blanket first-NaN rule or
finite-only value guard is used by this whole-routine path.

### Complete-state evidence

Saved-town probe `20261020-035339` supplied 32 complete calls: 25 with one
direction stream and seven with two, totaling 14,392 position vectors and
20,688 direction vectors. The public bridge now matches all 32 restored compiled
reference calls, including all 128 registers, all 256 KiB of local store,
normalized complete context, return and runtime step count.

The final private differential runs call the actual public bridge against an
unhooked compiled reference:

| Dataset | Attempts | Accepted complete-state matches | Unchanged declines | Mismatches |
| --- | ---: | ---: | ---: | ---: |
| Recorded-LS and bounded finite palettes, three arithmetic modes | 30,000 | 27,609 | 2,391 | 0 |
| Additional arbitrary float-bit palettes, three modes | 30,000 | 27,552 | 2,448 | 0 |
| Four rounding modes × four FTZ/DAZ combinations × three arithmetic modes | 48,000 | 44,256 | 3,744 | 0 |
| Total | 108,000 | 99,417 | 8,583 | 0 |

The same random sequence is reused across arithmetic modes; these totals do not
claim 108,000 distinct inputs or exhaustive arithmetic coverage. Declined cases
leave the complete context unchanged. Public regressions also cover 128
independent row-major cases (4,352 vertices), eight live-palette/delay cases,
normalized-empty-batch aliasing, signed-zero conversion/initial writes,
unsupported ABI contexts and explicit NaN operand selection. Finite scalar/x86
FMA results agree. Both Windows configurations pass all 113 checks without media
skips; ARM execution remains untested.

### Live execution and remaining work

Strict DirectX route `20261020-061618` and Vulkan route `20261020-061926`
load Continue, enter Balastor Plain, walk and rotate the camera. Both are hidden,
muted, isolated-save correctness runs at a 30 FPS cap. Each logged at least
262,144 accepted native calls, zero declines and 1,056 sampled full-state
comparisons without mismatch. Both fallback censuses have zero sessions.
The optional verifier checks the first 32 attempts and then every 256th attempt;
it compares the native result with the private compiled reference and aborts on
a mismatch. These are sampled route results, not full-game proof.

`tools/apply_native_stream.py` installs the wrapper only after checking the
original entry body's SHA-256:
`98915a23b2309986a6963b5e921efccdc42199e802a444bb80413a3cf727a760`.
It retains and validates the private reference, refuses altered bodies/wrappers
and is idempotent. Generated bodies and raw comparisons remain private.

Normal play enables the routine without tracing, verification copies or counters.
The player environment guard clears the research switches before startup.
The verified Release executable is installed for the desktop shortcut; saves and
preferences are unchanged. A poisoned-environment test confirms diagnostic and
automated-input switches are cleared, with a research-mode negative control.
No FPS gain has been measured. Whole-job descriptors, DMA visibility, output
ownership and scheduling, other layouts, additional NPC-heavy/cutscene/combat
routes and replacement of all remaining generated SPU code are still required.

## Whole geometry job boundary

The next replacement target is the complete image-19 geometry job: staged SPURS
descriptor and input buffers in, published output buffers and completion out.
The current outer algorithm remains compiled guest code. A new private replay
now checks that entire boundary, rather than only a skinning routine.

Two hidden, muted, isolated-save DirectX probes at the 30 FPS baseline provide
32 complete jobs. Probe `20261020-065842` captures 16 position-only jobs;
`20261020-072615` adds eight one-direction and eight two-direction skinning jobs.
There are also 288 before/after pairs at nine processing stages per job. The
second set is selected using the original descriptor's request flags; part of
the packed input header is rewritten during setup and is unsuitable as an
unchanged job-class identifier.

Both the unhooked compiled reference and the current build's partial C++
integration reproduce all 32 jobs, comparing every register, every local-store
byte, normalized complete context, return and runtime step count. Trace evidence
confirms 16 accepted whole-skinning C++ calls, with no declines, in the second
set. The first set does not exercise that routine.

| Recorded observation | Count across the 32 jobs |
| --- | ---: |
| Channel events, including tag masks/status and returned values | 1,889 |
| Bulk DMA output writes | 99 |
| Bulk DMA output bytes | 270,080 |
| Atomic allocator-line writes | 31 |
| Atomic allocator-line bytes | 3,968 |

The replay checks the observed channel sequence and effective-memory outputs at
each command completion. It supplies recorded DMA/atomic read receipts and
write-destination preimages in private VM memory, then uses the actual channel
and DMA implementation. Deliberately corrupting one output byte or a restored
register is rejected for both capture sets. Raw contexts, LS, memory receipts,
generated references and the replay fixtures stay private.

These checks establish a reproducible **recorded-input whole-job reference**.
They do not establish asynchronous scheduling equivalence, rule out every
unobserved external read, or replace the whole job. Both live probes use the
strict fallback guard and end with zero interpreter/microstep sessions; that
coverage is limited to the recorded routes. Arithmetic here is host mode on
Windows x86-64; other arithmetic modes and ARM need separate whole-job checks.

The traces expose shared allocation/publication state: observed jobs use atomic
line operations, tag-controlled transfers and a final fenced PUT. Some jobs take
a different allocation branch, so one fixed command count is not a valid general
contract. Owning each job's private C++ buffers alone does not prove parallel jobs
safe. The shared allocator, command ordering, output visibility and lifetimes
must also be recovered and validated.

Next comes the complete semantic input/decode/transform/pack/publication algorithm
using owned host buffers. Only after that boundary is understood can a faster
path omit pipeline scratch writes shown to be private. The retained exact
reference remains the comparison target. Normal play makes no full-LS copy per
skinning call: those copies belong to opt-in verification. The native routine's
`4*g+1`/`3*g+2` additions preserve step bookkeeping without dispatching through
the old loop. Neither fact is an FPS measurement. This research leaves the
desktop executable unchanged; no CPU/FPS gain or complete SPU-image replacement
is claimed.

## Packed vertex decoding in owned buffers

The input side of the whole geometry job now has a project-written portable
C++ decoder in [`native_vertex_decode.h`](../native_vertex_decode.h). It accepts
a byte span, vertex count, format and three signed position biases, and writes
owned host position/direction arrays. There are no guest registers, local-store
addresses or DMA calls in the core. **This decoder is tested offline and is not
yet used by the desktop game.**

The two recovered layouts use nine or twelve bytes per vertex: three big-endian
unsigned 16-bit positions followed by one or two 24-bit directions. Positions add
the bias with 32-bit wraparound and convert from twelve fractional bits. Each
direction packs two ten-bit components, an axis selector, a reconstructed-component
sign and handedness. The decoder preserves the recovered fused and separately
rounded arithmetic steps. Its reciprocal and position-conversion policies are
explicit: substituting an ordinary square root is not a bitwise-equivalent policy.
Callers must disable implicit floating-point contraction; the regression build
does so. No optimized ARM execution has been checked yet.

The consumed input stays stable while decoding. Requested output spans must be
large enough, and consumed/written ranges must be disjoint; unsupported formats, short
buffers and overlap decline before any write. Empty direction spans suppress
unrequested outputs. This interface reconstructs decoded streams, not the old
routine's scratch/register side effects. Additional formats and parameter layouts
remain to be recovered.

Private differential tests compare the actual public decoder with the unhooked
compiled input stage from the whole-job captures. They retain the existing
host/exact/audit arithmetic helpers as policies for this comparison.

| Dataset | Decoder calls | Position vectors | Direction vectors | Mismatches |
| --- | ---: | ---: | ---: | ---: |
| Original 32 recorded stage calls | 32 | 11,584 | 16,352 | 0 |
| Random packed bytes and full-width biases, three arithmetic modes | 3,072 | 1,703,424 | 3,139,584 | 0 |
| Four rounding modes × four FTZ/DAZ settings × three arithmetic modes | 3,072 | 1,703,424 | 3,139,584 | 0 |

Random sequences repeat across arithmetic modes/settings. These are bounded
comparisons over the captured counts/layouts and selected output streams, not
6,144 distinct interfaces or complete input coverage. The recorded parameter
layout is retained while packed bytes and position biases vary. Deliberately
changing one output bit is rejected in both the position-only and direction
capture sets.

Public tests extend `wkc2_native_skinning` with 32,576 independent direction
property checks, 32,000 scalar/x86 FMA comparisons, distinct vertex/stream byte
wiring, zero/short/tail cases, wrapped biases and reject-before-write controls.
Both Windows suites pass all 113 checks. The decoder adds no new SDK patch.

The next stages still need owned-buffer implementations and complete-job output
comparison, including shared allocation, DMA ordering and publication. No whole
job/image replacement, live decoder integration, scheduling proof or performance
gain is claimed by these tests. The player executable, saves and preferences are
unchanged, and generated reference code and raw fixtures remain private.

## Compressed triangle-index decoding

The geometry stage at `0x78F0`, previously provisionally labeled as packing,
actually decompresses triangle indices. The new project-written
[`native_index_decode.h`](../native_index_decode.h) accepts stable packed bytes
and writes logical host `uint16_t` indices. It uses no guest registers, local
store, DMA or allocation. **It is tested offline, with no live game hook.**

The recovered header supplies an explicit-value count, delta bias, selector-byte
count and delta bit width. One-bit selectors choose sequential new indices or
explicit values. Explicit deltas use an eight-value prediction stride with
wrapping 16-bit arithmetic. Two-bit triangle commands either introduce three
tokens or reuse two corners of the previous triangle and consume one token.
The implementation streams these operations with an eight-element predictor.

All 32 captured calls match all **20,220 logical indices**. Those captures have
no explicit values and zero delta width, so they alone do not verify the other
branches. Private synthetic comparisons invoke the actual unhooked compiled
stage with randomized packed deltas, biases, selectors and all four commands:

| Dataset | Attempts | Accepted output matches | Unchanged declines | Checked indices | Mismatches |
| --- | ---: | ---: | ---: | ---: | ---: |
| Widths 1–15, 24–402 indices | 7,680 | 7,679 | 1 | 1,629,786 | 0 |
| Widths 0–16, including unsupported boundaries | 4,352 | 3,840 | 512 | 813,735 | 0 |

These sets reuse one deterministic seed and overlap in input classes; they are
not an exhaustive format proof. Deliberately changing one output bit is rejected
for both captured sets. The public regression adds 1,841 randomized logical
streams (116,535 indices), 79 unchanged scratch-alias declines, an independently
specified four-command stream, unaligned input, tails, truncation and unchanged
reject controls. Both Windows suites pass all 113 checks with 122 SDK patches.

There are explicit limits. Width 16 gives a different extraction result in the
reference, and zero-width explicit values depend on its scratch contents; both
currently decline. Layouts where the old token buffer can overwrite unread
explicit values also decline. This dependency was observed in tiny synthetic
jobs and a 24-index case; it must be reconstructed or its legal input domain
established before a complete replacement. Short streams, insufficient tokens
or explicit values, non-triangle counts and overlapping input/output ranges
decline before writes.

Only logical indices are covered. The reference also writes rounded tail
triangles containing previous arena data: the captured allocations contain 696
bytes beyond logical indices. This kernel does not recreate those writes,
register/stack effects or metadata publication. The whole-job comparison must
prove their external liveness or reproduce them, then validate shared allocation,
DMA visibility and scheduling. No whole-job/image replacement or performance
gain is claimed; the desktop executable, save and preferences are unchanged.

## Position-only skinning stream

The image-19 geometry job also has a position-only branch (`0xB1D0`), separate
from the supported live direction-skinning routine. Its project-written
`skin_positions_stream` in [`native_skinning_stream.h`](../native_skinning_stream.h)
now uses typed position, influence and palette storage callbacks. Palette
preparation uses a bit-preserving row-major-to-cyclic reorder in
[`native_skinning.h`](../native_skinning.h). **This branch is verified offline;
it is not installed as a new game hook.**

The core processes four vertices per group, reads all four influences even for
zero weights, preserves ordered blend/FMA operations, then commits positions in
order 3, 0, 1, 2. A live palette provider must see earlier output writes. The
declared palette count is not a joint-index bound in the original branch:
the 16 recorded position-only jobs include 143 reads beyond it. The native
interface requires defined backing storage for those reads rather than skipping
them or indexing past an ordinary C++ vector.

The recorded comparison matches all 2,712 positions, including 19 nonfinite
outputs, and all 64 declared palette entries after preparation. The reference
also reproduces all 16 captured complete stage states. The public C++ kernel
itself is compared at its decoded-stream boundary, not as a replacement for
that stage's registers, stack or local-store scratch.

| Differential dataset | Calls | Positions | Mismatches |
| --- | ---: | ---: | ---: |
| Random bounded finite declared palettes/positions and arbitrary influence bytes, three arithmetic modes | 3,072 | 520,704 | 0 |
| Arbitrary float-bit declared palettes/positions and influence bytes, three modes | 3,072 | 520,704 | 0 |
| Four rounding modes × four FTZ/DAZ settings × three arithmetic modes | 6,144 | 1,041,408 | 0 |

Counts/layouts and other backing bytes come from the captures. Random sequences
repeat across modes/settings, and an out-of-count palette read can still see
nonfinite ambient data in the bounded-finite dataset. This is not exhaustive
input or interface coverage. For these comparisons, the reference supplies its
loop-entry backing state, including ABI spill bytes accessible to arbitrary
palette indices. No transformed position or blended matrix result is supplied
to the native core. Palette preparation is checked separately before the core
runs. Future whole-job ownership must resolve these ambient dependencies rather
than relying on a reference invocation in the player.

Host fused operations need an explicit NaN selection policy for this branch;
compiler operand allocation alone changed six position payloads in an earlier
512-call arbitrary-bit comparison. `OrderedHostPositionStreamMath` preserves
the observed a/b/accumulator priority and quiets the selected payload. This
policy is local to this recovered host stream; exact/audit arithmetic continues
using its existing helpers. Weight conversion likewise supplies its policy's
signed zero instead of assuming that every host conversion yields positive zero.
Changing an output bit fails the recorded oracle as expected.

Public regressions cover 128 independent row-major cases (4,352 vertices),
scalar/x86 FMA comparison, four live-palette cases, zero-weight joint-255 reads,
commit order, invalid counts declining without storage access, palette layout
and explicit NaN selection. Both Windows suites pass 113 checks; ARM execution
is untested. There is no measured CPU/FPS gain, new complete SPU image or whole
job replacement, and the desktop executable remains unchanged.

## Recovered allocation and export contracts

The whole-job stage names were provisional. `0x6970` transforms/filters geometry;
it does not allocate or issue DMA. Channel events still tagged with that stage
number occur later in the root between its stage snapshots. Actual allocation
uses `0x2DB8`, with ring and global helpers `0x2BB8`/`0x2A78`. Vertex packing is
under `0x4FC0`, separately from payload transfer and final command publication.

The existing 32-job corpus covers **31 successful nonwrapping ring allocations,
all in slot 0**, with the consumer receipt equal to the old producer cursor.
The only changed word in each published 128-byte ring block is its allocation
cursor. The remaining job has zero surviving indices and skips allocation and
payload export. These captures provide no contention, wrap, atomic retry,
global fallback, blocking wait or other-slot scheduling coverage.

All 32 jobs publish a final fenced PUT after earlier same-tag payload PUTs, where
present. The final packet size matches metadata header byte 1 × 16. Native
replacement must retain payload-before-command visibility, shared state
ownership and retry behavior; an owned buffer per job is insufficient by itself.
The decoded allocator/export notes are research contracts, not new live C++
implementations. Broader synthetic branch and concurrency tests remain required.
