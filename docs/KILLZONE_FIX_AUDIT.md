# Killzone 3 runtime-fix comparison

Reviewed 2026-10-18 against this project's **84-patch SDK stack**, pinned to
`a2c20108af45174dbb7d3e2f87f6acaaef09c6b4`, and port commit `d01f01d`.

Source: [undecuple's Killzone 3 fix catalogue](https://gist.github.com/undecuple/2845432bf13e485634de122084d3fd56),
revision `8d91ce0675154fcc00644879598919b6d40b6d29` (API update 2026-10-13).
All **62 entries, P1 through T2**, were compared with the patched SDK, runner,
generated registration table and relevant private WKC analysis. The catalogue's
upstream-status statements describe its author's earlier audit; this is not a
new check of every upstream PR.

The original 84-patch review was read-only. A follow-up implementation batch
is recorded below; the current stack has **93 patches / 79 Windows tests**. The
catalogue's tests and Killzone measurements have not been rerun here. A matching
source defect does not establish the cause of WKC's lag or flicker.

## Highest-value follow-ups

| Priority | Entries | Local finding and next check |
|---|---|---|
| 1 | S2 | Address scale fixed by 0085; independent guarded load/store regression passes. Saved-town reload works; execution coverage and any scene effect remain unmeasured. |
| 2 | S6, P1 | 0086/0089 fix the accumulator and four missing integer cases; 0090 replaces four WKC PPU `vsubuws` TODOs. Tests pass; remaining VMX/special-register semantics and native instruction coverage need work. |
| 3 | S7, S4 | 0093 adds tested byte-shift/rotate SIMD and scalar fallback; helper benchmark improves, native timing is mixed. Scalar host `fma`/`fms`, inlining and remaining shuffle work stay open; exact `fnms` is retained. |
| 4 | S8, R1 | 0091 makes completion/scan updates coherent with SPU reservations; other producer RMW helpers remain to review. Global lock redesign/contention is separate. |
| 5 | P8, P9 | HLE lookup is linear and per-read HOTREAD TLS diagnostics remain. Measure their current clang-cl cost and test indexed lookup equivalence. |
| 6 | G3, V11 | Reports stay host-side and ZPASS is fixed-visible. Town logs contain type-1 GET_REPORT requests. Capture DMA context/render-enable use before implementing Vulkan queries. |
| 7 | V1-V3, V5 | Shared fragment-shader gaps and missing AA sample-resolution handling can affect Vulkan. Use small shader/GPU tests and a WKC feature census. FXAA does not implement RSX AA memory layout. |
| 8 | P6, G1, G5 | TLS sizing/exhaustion, 16 IO-mapping slots/count-based reuse, and ownership-blind FIFO wrapping remain. Add allocation/reuse/context regressions. |

Generated SPU sources contain 18,216 byte-shift/rotate helper call sites and
15,230 `shufb` sites, plus 3,577 `fma`, 193 `fms` and 906 `fnms` sites. These
are static source-site counts, **not execution frequencies or profile shares**.

## PPU and build tools

Paths below refer to the patched SDK unless identified as port files.
Covered means an existing equivalent addresses the described issue; partial
means some pieces exist. Missing does not imply execution in the current scene.

| ID | Status | Evidence and limits |
|---|---|---|
| P1 | Partial | Old VMX lane/pack bodies remain in `tools/ppu_lifter.py`; patch 0090 replaces all four generated WKC `vsubuws` TODOs and tests decoded/lifted unsigned saturation, aliases and sticky VSCR. Other reported operations need independent semantic tests; not all ten are proven wrong here. |
| P2 | Missing; coroutine-dependent | `ppu_loader.cpp` keeps `s_depth`/`g_vcall_sp` thread-local, without a coroutine-swappable slot. WKC host-fiber use/depth leakage is unproven. |
| P3 | Missing tooling; workaround exists | No lifter `--overrides`; port `tools/apply_port_hooks.py` wraps selected generated functions. |
| P4 | Covered for Windows port | Port `lwcond.cpp` supplies the layout/wait/signal behavior through context registration. Other-platform parity is separate work. |
| P5 | Missing portability fix | Some TLS macros select `__declspec(thread)` under `_WIN32`. Current clang-cl supports it; GCC/MinGW needs a compiler-based guard. |
| P6 | Missing | `ppu_tls_new_block` includes the `0x7000` TP bias in every allocation. Exhaustion returns zero and thread entry substitutes main TLS. Check positive-TP accesses and thread churn before changing sizing. |
| P7 | Partial tooling | Line-count chunks exist; address-stable chunks, separate override declarations and general write-only-if-changed output do not. Rebuild speed, not game FPS. |
| P8 | Missing | Both `ps3_nid_table_find` and context-handler dispatch scan linearly. Any index must preserve precedence, duplicate registration and dynamic overrides. |
| P9 | Partial | Patch 0069 improves disabled memory watches; HOTREAD TLS tracking still runs per read. The catalogue's MinGW cost is not a measured WKC clang-cl cost. |

## SPU execution and SPURS

| ID | Status | Evidence and limits |
|---|---|---|
| S1 | Covered | Disassembler already decodes `orbi`, `xorhi`, `xorbi`. |
| S2 | Fixed by 0085 | SMC absolute loads/stores now use `I16 << 2`, matching the normal interpreter. Independent address/guard regression fails before the change and passes after. |
| S3 | Superseded | Patch 0061 gives each `spu_context` an MFC engine. Enlarging the old shared table would be weaker. |
| S4 | Partial | Ordinary `static inline`, policy-mode LS probe calls outside the relevant range, and scalar host FMA remain. FI already avoids common `ldexpf` work; exact `fnms` is ours. Preserve extended SPU semantics. |
| S5 | Partial | Normal dispatch tries resident overlays; interpreter rejoin's `spu_lifted_lookup` only checks `ctx->image_id`. WKC overlay usage needs measurement. |
| S6 | Partly addressed by 0086/0089 | RR accumulator destination and `mpyhhau` fixed; `bgx`, `mpyui`, `rothmi`, `rotmahi` execute. Floating/special-register gaps remain, including no-op `mfspr`/FSCR. |
| S7 | Partly addressed by 0093 | Nine byte-shift/rotate forms use SSSE3 with a scalar fallback. Both pass independent byte-order/count tests. Helper speed improves; sustained town FPS gain unproven. Remaining `shufb`/FMA work stays open; exact AVX2 FNMS is retained. |
| S8 | Missing larger design | Global lock, no striped/seqlock reservation reads. MSVC/x64 already pauses; non-MSVC has a bare spin. Ordering/invalidation require stress tests. |
| S9 | Partial; mainly portability/performance | Dispatch/history fields remain TLS; host depth and MFC are already context-owned. Native Windows TLS cost differs from MinGW emulated TLS. |
| S10 | Missing log cleanup | `RESUME-ELEMS` is ungated and PM poll logs restart with each run counter. Normal play redirects logs to NUL; catalogue file-write speedups do not transfer directly. |
| R1 | Completion/scan fixed by 0091; producers pending | Completion and pending-bit clears share the reservation lock and notify LR. Snapshot/descriptor reads are protected. Regression models the compare/commit window with production completion and real coherence. Shared bitset-builder RMW helpers still need review. |
| R2 | Partly addressed by 0072/0092 | Host lifetime claims are serialized; scan ELF flag bits are masked and all seven combinations tested. Full SPU/PPU task-creation publication remains separate. |
| R3 | Missing | Taskset2/Task2 exports absent. Not found among current named WKC direct imports; indirect/guest-library use remains possible. |
| R4 | Missing | No complete Taskset2 event-flag/exit-code machinery. Generic tasksets and async-lifetime fixes are different facilities. |
| R5 | Missing PM-lane optimization | Multi-lane PM runs still spawn/join per pass and sleep at loop entry. Lane count clamps to six rather than `si->nspus`. Patch 0072's reusable async task workers are a separate path. Measure WKC PM usage. |
| R6 | Missing; experimental proposal | Select-workload intercept returns zero. The catalogue reports a regression/freeze and disables its replacement by default. Do not enable blindly. |

## Rendering and command processing

| ID | Status | Evidence and limits |
|---|---|---|
| G1 | Missing | Mapping capacity is 16; syscall IO-map insertion uses count instead of a free slot. Test holes/reuse and failure allocation, not just capacity. |
| G2 | Covered differently | Patches 0006/0062 wait at producer self-jumps and wake on changed guest memory. A timed skip could discard WKC work. |
| G3 | Missing | GET_REPORT updates host `s_report_data`; main-memory report publication/DMA context handling is absent. Town reports observed; their requested location needs capture. |
| G4 | Covered | Patch 0075 routes HLE user callbacks; regression and native save-interaction evidence exist. |
| G5 | Missing | `gcm_flip_into_fifo` wraps supplied contexts without verifying runtime ownership. Native WKC use of this HLE insertion path is unproven. |
| G6 | Covered, coherence caveats | Patch 0052 implements NV0039 transfers/invalidation. Catalogue D3D12 surface writeback is additional coherence work, not a missing copy opcode. |
| G7 | Missing docs fix | SDK comments/template name the wrong SetFlipMode NID. Current generated registration hashes names correctly; no dispatch failure established. |
| V1 | Missing | Pack/unpack constants lack emission cases; PKG/UPG helpers absent in shared FP decompiler. Needs shader tests and WKC usage census. |
| V2 | Missing | Predicated writes still emit `lerp`. Our CMP instruction's select fix is separate; raw-bit/NaN-safe predication remains. Shared with Vulkan. |
| V3 | Missing/partial | No FP `SV_Depth` export. D3D12 has depth snapshots but not the complete export/key/coordinate corrections. |
| V4 | Partial; D3D12 | Per-zeta snapshots exist; their creation-time sizing remains. Growth, alias-preference and self-sampling cases are not independently covered. Vulkan has separate fixes. |
| V5 | Missing shared semantics | Surface extraction ignores AA sample factors; corresponding viewport/scissor/sample-memory handling is missing. Post-process FXAA does not solve this. |
| V6 | Partial | Host-write invalidation/resolve and D3D12 coherence work exist. Full main-memory surface writeback/batched readback model is not established; needs byte-level tests. |
| V7 | Partial; D3D12 | Float vertex textures and FP16 targets exist. Ordinary fragment FP16 texture decoding still clamps to RGBA8; full float preservation is absent. Vulkan formats are separate. |
| V8 | Partial | Vulkan shadow compare/depth re-encoding have extensive fixes. Shared FP declarations support 2D/cube, not 3D; D3D12 lacks the full feature set. |
| V9 | Missing | No complete per-channel texture-gamma decode from address bits in the reviewed sampling path. Capture WKC requests before attributing colour differences. |
| V10 | Covered Vulkan; missing D3D12 | Patch 0079 compares decoded Vulkan sampler fields. Live-draw's 32-bit packed LOD key still truncates bits. |
| V11 | Missing | Constant visible ZPASS, no real query/predication flow. Town requests reports; conditional-draw usage and cost remain unmeasured. |
| V12 | Partial; backend-dependent | Vulkan already has async batches, arena reservation and program-cache fixes. D3D12 retains synchronous submits and lacks this particular chunk-submit/PSO memo design. |
| V13 | Covered shared engine; missing D3D12 | Shared clear distinguishes Z16/Z24; live-draw still decodes all clears as Z24. |
| V14 | Missing D3D12 optimization | Synchronous list/fence model remains. Catalogue two-frame design admits incomplete shutdown cleanup; not a ready-made Vulkan patch. |

## Files, services, identity and future content

| ID | Status | Evidence and limits |
|---|---|---|
| L1 | Missing ABI path | UserAutoSave uses eight-argument generic dispatch; only UserAutoLoad has a context wrapper, with registration unwired in runner. Verified Logic Stone saves use another path. |
| L2 | Partial | Generated registration already correctly hashes GetFreeSize/Chmod. Utime/SdataOpenByFd absent. Not all four catalogue omissions apply here. |
| L3 | Missing | Existing EDAT key candidates are supported, but no guest-supplied klicensee registration is wired from IsAvailable. Future DLC/regional relevance; no keys belong in Git. |
| L4 | Missing EDAT feature | EDAT cache remains beside source. Patch 0001's SDAT cache is separate. |
| L5 | Missing; WKC direct import | Commerce CreateCtx takes three arguments/writes the third rather than the described five-argument ABI. Trace actual invocation/caller. Additional later-SDK stubs need separate coverage review. |
| L6 | Covered | Patch 0001 uses Windows 64-bit stat/seek/tell. Do not reapply. |
| L7 | Missing | Fixed trophy maximum, invalid platinum output and incomplete TRP/per-user/BE-info behavior. Not an established town-FPS cause. |
| N1 | Missing feature | No unified strict network policy/name map/egress layer. Online restoration remains future work. |
| N2 | Missing | HTTP SendRequest reaches resolver/socket operations without the sys_net online gate. WKC imports HTTP; actual network activity was not measured. |
| N3 | Missing latent identity fix | Online fallback in `np_psnr.c` can use OS username; offline uses PS3Player. No current identity transmission is claimed. Use neutral default before online tests. |
| N4 | Missing feature | No strict-policy local UDP DNS answers. Depends on policy layer; not needed for offline town rendering. |
| N5 | Missing | Basic APIs check their own init flag, not broader NP state. No demonstrated WKC boot blocker. |
| N6 | Missing layout fix | HTTP URI path is read at +8 rather than +16. Direct HTTP imports exist; execution/impact of this route is not established. |
| N7 | Partial; concrete unsafe body path | Some header/status support exists, but named ResponseGet exports are absent. Buffered overflow uses guest translation; subsequent socket recv still uses raw guest pointer. Needs a loopback multi-read test. |

## Extraction and lift coverage

| ID | Status | Evidence and limits |
|---|---|---|
| T1 | Partial tooling | Immediate, continuation and pointer-table seeding already exist. First-executable-segment selection remains, but all 30 extracted WKC SPU ELFs examined have one executable PT_LOAD. Not a demonstrated omission in these inputs. |
| T2 | Partial | Import stubs beyond code-end are restored specially. Other out-of-range calls can still become non-code comments; build uses an explicit bound. Audit ordinary dropped targets rather than assuming imports are broken again. |

## Implementation order and verification

Start with S2's guarded round trip, S6's RR accumulator case and P1's missing
WKC opcode. Evaluate S7 on captured jobs, then HLE/diagnostic costs under a
matched native profile. Establish taskset/reservation safety before optimizing locks.

For rendering, establish feature usage before integrating queries, AA sizing,
gamma or shader opcodes. Keep shared/Vulkan changes separate from experimental
D3D12 work. Fixed-visible queries are conservative; an incorrect replacement
can make objects disappear.

Killzone FPS gains are not predictions for WKC. Several large entries describe
changes without full diffs, and R6/V14 disclose unresolved problems. Each adopted
fix needs its own regression and appropriate native capture/save/performance check.


## First implementation batch: 0085-0090

S2, the concrete S6 accumulator fault and four missing integer interpreter cases
now have Windows regressions and fixes. P1's four WKC `vsubuws` TODOs are
replaced with tested native translation. Patches 0087-0088 separately follow up
the user's physical-stick report with diagonal range expansion, an outer margin
and serialized report publication. Native synthetic input reaches the game
steadily; physical feel is still unverified. All 77 Debug and Release checks
pass, including regressions that fail on the earlier implementations.

This is the first batch, not completion of the entire catalogue. SIMD/HLE
performance, reservation/taskset concurrency, reports/conditional rendering,
fragment/AA semantics, TLS/IO/context allocation and service correctness remain
in the queue above. Optimizations need equivalent-result tests and measurements;
Killzone-specific scheduling policies are not enabled wholesale. No FPS gain is
claimed for this batch.


## Second implementation batch: 0091-0093

Completion/scan coherence and task ELF flag handling now have failing-before
regressions. Byte-shift/rotate SIMD matches the architectural oracle, with a
separately tested scalar fallback. All 79 Windows checks pass in Debug and
Release. A hidden/muted saved-town comparison uses the exact old helper bodies
and SIMD in one diagnostic build: early samples improve, but both paths slow
later. Isolated helper speed improves; sustained town FPS gain remains unproven.
No global reservation-lock redesign or wholesale SPURS scheduling replacement
is included. See [implementation and evidence](FINDINGS.md#spurs-completion-coherence-and-spu-byte-operations-patches-0091-0093).
