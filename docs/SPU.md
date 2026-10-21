# SPU jobs and their replacement

This page is the one place to start for anything about the game's SPU code: what
the SPU jobs are, how the port runs them, the problems found and fixed, how a
job is replaced with readable C++, and what's left. It's written for people and
for AI agents picking up the work. Detailed evidence stays in the linked pages;
this page summarises it and says where to look.

**At a glance (2026-10-21)**

- 24 SPU programs ("images") are extracted from the executable, lifted to C and
  compiled into the port. None has been fully replaced yet.
- Readable C++ runs the supported image-19 geometry jobs, pipelined across
  worker threads, and all four image-23 visibility modes. Other inputs fall
  back to the compiled code before any output is published.
- The largest remaining SPU cost on the crowded street is image 20, the
  render-command builder: 18–21 ms per frame, and it has to run serially.
- Captured jobs can be replayed byte for byte (`WKC_JOB_REPLAY`), so every
  change to a job can be checked without playing the game.

## Contents

- [Background: SPUs, SPURS and job chains](#background-spus-spurs-and-job-chains)
- [How the port runs SPU code](#how-the-port-runs-spu-code)
- [The 24 images](#the-24-images)
- [Replacement status](#replacement-status)
- [Problems found and fixed](#problems-found-and-fixed)
- [What limits the frame rate](#what-limits-the-frame-rate)
- [Replacing a job, step by step](#replacing-a-job-step-by-step)
- [Switches and tools](#switches-and-tools)
- [Pitfalls](#pitfalls)
- [Where the details live](#where-the-details-live)

## Background: SPUs, SPURS and job chains

The PS3's Cell processor has one general-purpose core (the PPU) and six usable
SPUs. Each SPU runs code from its own 256 KB local store and reaches main memory
only by DMA. It programs the DMA engine (the MFC) through numbered channels:
local address, effective address, size, tag, command, then a tag-status wait.

WKC2 hands most per-frame number crunching to SPUs through **SPURS**, Sony's
SPU task system, mostly as **job chains**:

- A chain is a list of commands: run a job, sync, guard, jump, call, run a job
  list, end. The PPU builds it and SPURS walks it.
- Each job has a descriptor: binary address and size, an input DMA list
  (staged into local store before the job starts), an optional cache list,
  and stack and scratch sizes.
- A chain created with `maxCont=1` runs on one SPU, so its jobs are strictly
  ordered on the PS3 too. A `SYNC` waits for every earlier job.
- Jobs signal completion through event flags, guards and memory the PPU
  polls. Some use atomic reservations (`GETLLAR`/`PUTLLC`) on shared lines.

## How the port runs SPU code

1. **Extraction.** `tools/extract_raw_jobs.py` and the SDK's image extractor
   pull the five embedded SPU ELFs and 19 raw job binaries from the executable.
   All 24 come from the executable itself; no runtime capture is needed.
   See [reproducing the SPU inputs](FINDINGS.md#reproducing-the-spu-inputs-from-the-executable).
2. **Lifting.** `tools/build_spu.py` runs the SDK's `spu_lifter.py` with
   validated extra entry points. Each SPU function becomes C functions over an
   `spu_context`: registers are 128-bit values in `ctx->gpr[]`, and local-store
   accesses byte-swap through `spu_ls_read128`/`spu_ls_write128`.
3. **Control flow.** A branch to another lifted function sets `ctx->pc` and a
   thread-local trampoline, then returns to a drain loop that runs per-transfer
   hooks (interrupts, lockstep, launch checks) and calls the target. Since
   [patch 0130](PATCHES.md#faster-spu-calls-and-transfers-patch-0130),
   `SPU_JUMP` tail-calls the target directly when no hook would act. A
   `brsl` call becomes a nested host call plus `spu_drain_call`.
4. **Post-processing.** `tools/apply_native_geometry.py`, `apply_native_normals.py`
   and `apply_native_stream.py` install native hooks at hash-pinned
   boundaries. `apply_spu_direct_jumps.py` then spells branches as `SPU_JUMP`
   outside those regions.
5. **Dispatch.** The SDK's chain walker (`cellSpurs.c`, `jc_execute`) runs each
   chain on a host thread. A job's binary is fingerprinted to find its image,
   and the native hook ([patch 0124](PATCHES.md#original-input-native-spurs-dispatch-patch-0124))
   is offered the job first. If it declines, the job is staged into a reused
   context and the compiled code runs. Scope callbacks bracket both paths.
6. **Fallback.** The SDK still links an SPU interpreter and a microstep path for
   code that wasn't lifted. `SPU_FALLBACK_STRICT=1` makes any such entry fatal;
   tested routes run with zero entries.

The [architecture page](ARCHITECTURE.md#what-executes) describes the whole
execution model, including the PPU side.

## The 24 images

Image numbers are the registry order in `tools/build_spu.py`. Street costs are
SPU job time per presented frame in a ten-second unlocked Vulkan timeline of
the crowded street with the current build (2026-10-21, 251 frames). They vary
with the scene. Native C++ work doesn't appear in these spans.

| Image | Input | What it does | Status | Street cost |
| --- | --- | --- | --- | --- |
| 1–5 | `spu_0000`–`spu_0004` (embedded ELFs) | System and middleware SPU programs; not yet analysed individually | Compiled | 4: 0.1 ms |
| 6–16 | Raw jobs `0121D800`–`0126DA00` | Not yet identified individually | Compiled | 6: 1.1 ms, 10: 0.6 ms, 11: 2.3 ms, 12: 3.7 ms |
| 17 | Raw job `01272B80` | Runtime-code path that used to reach interpreter fallback; three static entries added | Compiled | 1.5 ms |
| 18 | Raw job `0127FB00` | Not yet identified | Compiled | 3.8 ms |
| **19** | Raw job `01283D80` | **Edge geometry**: skinning, normals, filtering, vertex export and RSX command publication | **C++ for supported inputs, pipelined** | 1.8 ms on the chain thread; preparation runs on workers |
| **20** | Raw job `01294400` | **Render-command builder**: interprets display lists into RSX commands, shader microcode and constant uploads | Compiled; replay harness ready | **18.3 ms** |
| 21 | Raw job `012A1980` | Short jobs that commit completion flags the PPU waits on | Compiled | 0.4 ms |
| 22 | Raw job `012A2180` | Not yet identified | Compiled | 0.1 ms |
| **23** | Raw job `012A6B00` | **Visibility requests**: frustum, occlusion, mask and projected-interval selection | **C++ for all four modes, admitted inputs** | Native |
| 24 | Queue job `0118FA00` | Secondary-bone physics (hair and clothing), run through the SPURS job queue | Compiled | 0.2 ms |

Image 20 is the clearest next target. Its strings include "unknown cmd %d",
"prim error", "fp ucode size over!" and "spu skin matirx num over %d/%d!". A
captured job makes about 450 small reads and 28 writes, mostly into RSX local
memory. See [image 20](NATIVE_CODE.md#next-critical-path-image-20-the-spu-render-command-builder).

## Replacement status

The goal is **100% project-written, readable C++** for every SPU job, with no
generated SPU code or interpreter linked into the player. The acceptance rules
are in [native work](NATIVE_CODE.md#acceptance-for-each-native-replacement).

**Done, for supported inputs:**

- **Image 19, whole job.** Owned typed inputs, decoding, skinning, normals,
  projection and filtering, allocation, export and command publication
  ([opt-in live geometry job](NATIVE_CODE.md#opt-in-live-geometry-job)). Jobs
  are prepared on worker threads and committed in chain order, with a write
  log that refreshes stale inputs
  ([pipelined geometry jobs](PATCHES.md#pipelined-geometry-jobs-and-sub-millisecond-sleeps-patches-0127-0128)).
- **Image 23, modes 1–4.** Complete requests in C++, records copied in source
  order, header published last
  ([visibility jobs](NATIVE_CODE.md#owned-visibility-jobs),
  [frustum and interval](NATIVE_CODE.md#frustum-and-interval-selection)).
- **Arithmetic.** Seven host operations and the extended negative-product
  operation are authored C++ with portable and AVX2 versions
  ([details](NATIVE_CODE.md#owned-extended-negative-product-arithmetic)).

**Still compiled:** every other image; image-19 and image-23 inputs outside
the admitted constructor and layout domains; reciprocal estimates,
interpolation and integer conversions (SDK adapters).

**Not yet verified:** full-game static coverage, cutscenes and combat with the
native jobs, ARM hosts, and complete scheduling equivalence.

## Problems found and fixed

Each row is a real fault found in this port. The patch notes have the evidence.

**Jobs that didn't run, or ran wrong**

| Symptom | Cause | Fix |
| --- | --- | --- |
| Long stretched strips on Leonard's hair and clothing | The SPURS job-queue imports returned success without running the physics jobs | [0058](PATCHES.md#spurs-job-queue-physics-patch-0058) runs the queue and passes the job's control block |
| A 67 KB Edge geometry program never ran | `JOBLIST` chain commands were skipped, so the jobs they listed never dispatched | 0014 runs job lists ([notes](SUBSYSTEMS.md)) |
| A job's own code bytes overwritten | `cellSpursGetSpuId` read a kernel structure at LS `0x1C8` inside the job | 0015 binds it to handoff metadata |
| First cache input quadword corrupted | The job stack started 16 bytes below the top, so the entry's link-register save landed on the cache data | [0122](PATCHES.md#spurs-job-caller-frame-patch-0122) reserves the full 32-byte frame |
| Interrupts re-enabled when they should stay off | `SPU_RdMachStat` reported running state instead of the interrupt bit | [0123](PATCHES.md#spu-machine-status-interrupt-bit-patch-0123) |
| Jobs started as stopped after a reset | Running status was only set by the interpreter | [0116](PATCHES.md#spu-running-status-and-fallback-entry-guards-patches-0116-0117) |
| Guards closed after one release | `cellSpursJobGuardReset` didn't restore the notify count | 0048 |
| Interpreter fallback in images 17 and 19 | Entry points the lifter couldn't see | Five hash-checked static entries; [0117](PATCHES.md#spu-running-status-and-fallback-entry-guards-patches-0116-0117) makes fallback fatal for testing |
| Taskset completion races | Completion bits and SPU atomic commits weren't coherent | [0091–0092](PATCHES.md#task-completion-and-spu-byte-operations-patches-0091-0093) |
| Unrelated jobs sharing DMA state | A fixed eight-slot MFC registry handed later contexts one shared engine | [0061](PATCHES.md#spu-conversion-scales-and-dma-ownership-patches-0060-0061) gives each context its own |
| Duplicate submissions and stale cleanup of SPU worker threads | Asynchronous workers had no owner until after their host thread started | [0072](PATCHES.md#asynchronous-spu-worker-ownership-patch-0072) claims each one first |

**Arithmetic and byte order**

| Problem | Fix |
| --- | --- |
| SPU single precision has no NaN or infinity and an extended range; host floats differ | `SPU_FLOAT_MODE` exact rules; exact FNMS (0028, [0078](PATCHES.md#exact-avx2-spu-fnms-patch-0078)); NaN-payload checks in every native replacement |
| Conversion scales built with `exp2`/`ldexp` | [0060](PATCHES.md#spu-conversion-scales-and-dma-ownership-patches-0060-0061) builds exact powers of two from exponent bits |
| Interpreter multiply-accumulate and integer gaps | [0085, 0086, 0089](PATCHES.md#runtime-and-input-follow-up-patches-0085-0090) |
| SHUFB swapped every operand into SPU byte order and back | [0104](PATCHES.md#host-layout-spu-byte-shuffle-patch-0104) selects host bytes directly; 0093 vectorises byte shifts |

**Overhead**

| Problem | Fix |
| --- | --- |
| Hashing up to 67 KB of job binary on every dispatch; a 256 KB local-store clear per job | 0055 caches fingerprints; 0056 reuses one context per thread and clears only what earlier jobs wrote ([notes](PATCHES.md#renderer-coherence-and-execution-overhead-patches-0053-0057)) |
| Guard waits polled every millisecond | 0054 uses a condition variable and yields on unchanged reservations |
| Image-19 jobs ran one at a time on the chain thread | Pipelined preparation with in-order commit ([0127–0128](PATCHES.md#pipelined-geometry-jobs-and-sub-millisecond-sleeps-patches-0127-0128)); street 15.5 → 26.5 FPS |
| About a fifth of compiled job time spent in transfer bookkeeping | [0130](PATCHES.md#faster-spu-calls-and-transfers-patch-0130); image-20 job 311 → 280 µs |
| Per-job log lines cost about a third of a replayed job | Keep `PS3_VERBOSE=0` when timing |

## What limits the frame rate

The crowded town street is the slowest scene. Findings in order:

1. **Earlier town profile (2026-10-17).** Jobs waited on the GPU, not the other
   way round: the Edge chain polled for output-ring space the renderer freed
   late. Patch [0057](PATCHES.md#renderer-coherence-and-execution-overhead-patches-0053-0057)
   then removed the renderer's mid-frame waits.
2. **Street diagnosis (2026-10-21).** Slowing one thread at a time showed the
   image-19 geometry thread was the critical path: halving its speed cost about
   40% of the frame rate, while slowing the renderer or PPU threads cost nothing.
   Pipelining image 19 took the street from about 15.5 to 26.5 FPS.
3. **Now: image 20.** Frame timelines put compiled image 20 at 18–21 ms per
   frame (about 54 jobs per frame on the street; 21.2 ms before patch 0130,
   18.3 ms after, in separate runs). Every job reads a shared table and
   render-state block that the previous job rewrote, with different bytes each
   time, so the jobs can't overlap. They have to get faster instead.

**Tried and set aside**

- **Running Edge-chain jobs in parallel.** The chain keeps one output ring per
  SPU; reporting every job as SPU 0 corrupted it, and other SPU numbers stalled
  the GPU at the title screen.
- **Running image-20 jobs on worker threads.** The `SYNC` after each job drains
  them anyway, and the data flow is serial.
- **Asynchronous presentation.** No FPS difference; the present wait wasn't on
  the critical path.
- **Disabling the stack cookie (`/GS-`) for lifted code.** No measurable effect.

## Replacing a job, step by step

This is the workflow that produced the image-19 and image-23 replacements, and
the one set up for image 20.

1. **Capture real jobs.** Run the game from the Continue save with
   `WKC_JOB_CAPTURE=<dir>` and `WKC_JOB_CAPTURE_IMAGE=<id>`, then create
   `<dir>/arm` when the scene is right. Each job is saved with its descriptor,
   staged inputs (`S`), every DMA read (`G`) and every DMA write (`P`).
2. **Check the baseline replays.** `WKC_JOB_REPLAY=<dir>` with
   `WKC_JOB_REPLAY_RUNS=<n>` and `PS3_VERBOSE=0` reruns each capture against
   the loaded EBOOT and compares every write. The compiled job must match 100%
   before anything changes.
3. **Recover what the job does.** Use its strings, DMA pattern, descriptor
   layout and lifted functions. Write down the input and output contract
   before writing code.
4. **Write the C++ against owned inputs.** No `spu_context` or local store
   inside the algorithm. Decline unsupported inputs *before* any external
   effect, so the compiled job can still run.
5. **Hook it in** through the native job hook, keeping scope callbacks
   balanced. Publish outputs in the original order, headers last.
6. **Compare.** Replay the captures, run randomized and edge-case comparisons
   against the compiled reference (including NaN payloads and rounding modes),
   then run strict routes with `SPU_FALLBACK_STRICT=1`.
7. **Measure** with a frame timeline (`WKC_TRACE_GATE`/`WKC_TRACE_OUT`) and
   same-process A/B where possible. FPS varies a lot between separate runs.

## Switches and tools

| Switch | Purpose |
| --- | --- |
| `WKC_NATIVE_GEOMETRY_JOB=1` | Enable the C++ image-19 and image-23 routes (the desktop build sets it) |
| `WKC_GEOM_PIPELINE=<n>`, `WKC_GEOM_PIPELINE_VERIFY=1` | Pipeline worker count (0 = serial); verify mode re-runs every 64th job serially |
| `WKC_JOB_CAPTURE`, `_IMAGE`, `_COUNT` | Record complete job transactions for one image |
| `WKC_JOB_REPLAY`, `WKC_JOB_REPLAY_RUNS` | Replay captures, compare writes, report mean job time |
| `SPU_FALLBACK_STRICT=1` | Make any interpreter or microstep entry fatal |
| `SPU_FLOAT_MODE` | Host floats, an `audit` count, or exact SPU rules |
| `WKC_TRACE_GATE`, `WKC_TRACE_OUT` | Ten-second frame timeline, including SPU job spans per image |
| `SPU_HOST_DEPTH_MAX` | Host recursion limit for lifted calls (default 2000) |
| `PS3_VERBOSE=0` | Silence per-job log lines; required for timing |

The full list is in the [reference](REFERENCE.md#spu-and-spurs). Every
diagnostic switch is cleared in player mode.

## Pitfalls

- **Byte order.** Registers are stored as host words with the SPU's preferred
  slot in word 0; SPU byte `P` lives at host byte `P ^ 3`. Local store holds
  guest big-endian bytes.
- **Floats.** SPU single precision flushes denormals, has no NaN or infinity,
  and saturates. Never let the compiler reorder or fuse float operations in a
  replacement without proving the results match; one rejected prototype changed
  NaN payloads.
- **Job exit.** A finished job branches to local-store address 0. Don't treat
  that address as an ordinary branch target.
- **Ordering.** `maxCont=1`, `SYNC` and shared state make a chain serial. Check
  the data flow (the capture tools show it) before trying any parallelism.
- **Generated code edits.** The hook installers hash the lifted text they
  replace. Run anything that rewrites generated code after them, and skip
  their marked regions.
- **Two generated trees.** The Debug build compiles `work/wkc2-generated`; a
  Release tree may use a different private copy. Apply post-processing to the
  tree you build.
- **Private data.** Captures, replays and generated code contain game bytes.
  They stay in the private workspace; only tools and results are published.

## Where the details live

| Topic | Page |
| --- | --- |
| Evidence for every native replacement, work order, acceptance rules | [NATIVE_CODE.md](NATIVE_CODE.md) |
| What each SDK patch changes and how it was checked | [PATCHES.md](PATCHES.md) |
| Measurements and investigation history | [FINDINGS.md](FINDINGS.md) |
| Execution model and modding scope | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Reusable SPU lessons for other ports | [LESSONS.md](LESSONS.md#spu-and-spurs) |
| Machine-readable patch history | [`patches/index.json`](../patches/index.json) |
