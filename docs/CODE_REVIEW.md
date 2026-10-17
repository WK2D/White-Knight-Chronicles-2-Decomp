# Code review suggestions

Later tracing located the long town strips in omitted secondary-bone physics
jobs. Patch 0058 restores their queue execution and CRT handoff; see the
[native recheck](FINDINGS.md#secondary-bone-physics-and-town-stretching).
The dated reviews below retain their original validation scope.

## Second review

Review of `main` at `b66c2aa` plus patch 0045, on the `claude-suggestions`
branch. It covers SDK patches 0001-0045, the host sources and tools not reviewed
before, and the test and CMake wiring. Two further reviewer passes read the
render patches 0031, 0035, 0036 and 0038. Their findings that were not traced
again here are marked *reported*.

The town captures that show stretched geometry and corrupt NPC surfaces are all
from Vulkan runs. Of the fixes below, 0046, 0050 and 0051 could affect those
faults. The 51-patch Windows suite passed and its native town run still showed
both faults; see [the recorded findings](FINDINGS.md#second-runtime-review).

### Reviewed fixes

| Patch | Defect | Check (fails against the earlier runtime) |
| --- | --- | --- |
| 0046 | HLE-to-guest callback stacks sat at fixed addresses 0xC7FF0000-0xCFFF0000, inside the 256 MiB of RSX local memory at 0xC0000000. Each callback (flip, vblank, sysutil and others) wrote its frames over whatever the title had placed below its slot's top. | `wkc2_callback_stack` |
| 0047 | `sys_ppu_thread_create` ignored `SYS_PPU_THREAD_CREATE_JOINABLE`, so a non-joinable thread kept its slot after it ended and the 64-entry table filled (EAGAIN). A detached thread ending in `sys_ppu_thread_exit` was freed and then marked finished again. Guest stacks were never reused (ENOMEM after about 250 threads). | `wkc2_ppu_thread_lifetime` |
| 0048 | `cellSpursJobGuardReset` left a guard without autoReset open after its release, so its job ran again without a notify. | `wkc2_spurs_job` |
| 0049 | `SPU_CRI_IMAGE=-1`, set by this runner, equals the pure-interpretation image id, so `RD_SPU_INTERP` threads got the CRI task's EXIT and syscall special cases. | `wkc2_spu_title_*` (native suite) |
| 0050 | NV3089 blits read the 12.4 `IMAGE_IN` point as 12.12, dropping source starts below 256 pixels, and skipped X8R8G8B8 sources. Both renderers use these copies. | `wkc2_nv3089_blit` |
| 0051 | The shared draw engine stored 2D-object methods (NV3062, NV308A, NV3089) in NV4097 registers: a blit's `SET_OPERATION` and `IMAGE_OUT_POINT` became the following draws' alpha-test and blend enables. Vulkan only. | `wkc2_draw_engine` |

Each patch is described at the end of
[Additional runtime fixes](BUILD.md#additional-runtime-fixes) in BUILD.md.

### Findings and remaining work

Rendering:

1. **NV0039 copies were missing; addressed by patch 0052.** The earlier
   `cellGcmSys.c` treated subchannel 1 as NV4097 unless `GCM_SUBCH1_2D` was
   set, and had no NV0039 handler either way. On that baseline, transfers on
   this binding never copied their data, and their offset, pitch and format
   words landed on NV4097 state: blend and colour mask on both renderers.
   A ten-second native town trace confirmed 33 complete transfer sequences.
   Patch 0052 adds byte copies and consumes their methods before both 3D
   renderers, with the WKC runner selecting the verified slot-1 binding.
   Its two new regression tests and extended draw-engine checks pass on
   Windows. The native run verifies copied bytes with no mismatch or rejection
   diagnostics. NPC surfaces improve in the inspected town views, while
   stretched geometry remains. See the
   [native results](FINDINGS.md#nv0039-memory-transfers).
2. **Render targets are matched to textures by address alone**
   (`rsx_draw_engine.c` `sink_bind_textures`; the D3D12 backend is reported to
   do the same), and are never retired when guest memory is reused. A texture streamed to the
   address of an old target samples the stale target image. A DXT texture can
   never be a render target, so excluding compressed formats would be a safe
   first step; detecting guest writes is the full fix.
3. Patch 0030's precision clamps use plain `clamp`, whose result for a NaN
   input is implementation-defined. RPCS3's `precision_clamp` maps NaN to 0
   first.
4. Restart-index handling in `rsx_commands.c` is inside `#ifndef NDEBUG`, so a
   Release D3D12 build ignores primitive restart.
5. *Reported:* patch 0038's feedback snapshot binds the whole target even when
   the texture unit names a smaller image or a different pitch at that address.
6. *Reported:* patch 0035's resolve detects supersampling from pitch alone,
   returns before a resolve reaches an engine surface at the destination, and
   writes pending resolves back lazily over newer guest bytes.
7. *Reported, D3D12 only:* patch 0031 decodes the depth clear value as 24-bit
   integer depth for every zeta format (the register's reset value is
   0xFFFFFF00, here 0), always clears the one shared depth buffer whichever
   zeta was named, clears colour with no colour target bound, and replays
   display clears after legacy line and point draws.
8. *Reported:* patch 0036's Vulkan arena positions advance before a draw can
   fail, and are not reset when no batch is open.

Runtime:

9. A `cellSpursRunJobChain` that arrives while the chain's walker is stopping
   for `cellSpursShutdownJobChain` is coalesced into that walker and lost.
10. Patch 0009 left `jc_signal_done`, `g_spurs_job_user_events`,
    `g_spurs_job_running` and the `lv2_register.c` hook unused.
11. In `sys_event.c` only `sys_event_queue_push_by_id` takes the table lock,
    and `sys_event_queue_destroy` deletes the queue's critical section while
    receivers may still be waiting on it.
12. Two threads opening the same uncached EDAT at once decrypt into the same
    `.partial` file, and one can publish it while the other is still writing.
13. Patches 0024 and 0025 call `getenv` on every unhandled method and every
    job.

Tools: `prepare_sdat.py` writes its receipt in place rather than atomically;
`prepare_ffmpeg.py`'s `tarfile` filter needs Python 3.11.4 or later; the `/w`
in `build.ps1` also hides warnings in the hand-written sources.

### Checks run

- The 51-patch stack applies to a clean `a2c20108` checkout with both
  line-ending settings, upgrades a 45-patch tree, and reapplies with no change.
- Linux, under AddressSanitizer and UBSan: `wkc2_callback_stack`,
  `wkc2_ppu_thread_lifetime`, `wkc2_nv3089_blit` and `wkc2_draw_engine`, also
  through CMake and CTest. The thread test also ran under ThreadSanitizer,
  which reported only the test's own polling.
- Wine with a MinGW build: `wkc2_ppu_thread_lifetime`, `wkc2_ppu_join`,
  `wkc2_spurs_job` and `wkc2_nv3089_blit`. MinGW ignores
  `__declspec(thread)`, so for these runs `PPU_TLS` in `sys_ppu_thread.c` was
  built as `__thread`; clang-cl and MSVC honour the original.
- `tools/check_public_tree.py` reports no findings.

Earlier Windows validation (51-patch baseline) on 2026-10-17: the Vulkan
clang-cl build and all 48 project tests passed with the 51-patch stack. This includes both new tests, the extended
checks above and all three SPU float modes. No MinGW TLS override was used.
The separately regenerated disc-input build also compiles with all 51 patches.
Native results are recorded in [FINDINGS.md](FINDINGS.md#second-runtime-review).
All four requested configurations reach town: default float arithmetic with
and without `RSX_ENGINE_ALL_SUBCHANNELS=1`, then SPU float audit and exact.
The stretched strip and NPC surface faults remain in all four. Native traces
confirmed the missing NV0039 transfers subsequently implemented in patch 0052.

### Native reproduction checks (completed above)

1. Run the town with all 51 patches and compare against the existing town
   captures, including the strip at Leonard and the NPC surfaces.
2. Search the run log for `[NV3089] SKIPPED`, `[GCM2D-UNH]` and
   `[GCM2D] inline write`; these name the 2D engines the title uses.
3. Create an empty private file, set `GCM_SUBCH_GATE` to its path, and stand in
   the town for a few seconds. `[subch] 1 0x0184`-`0x0328` lines mean NV0039
   transfers (finding 1, now addressed by patch 0052).
4. Repeat with `RSX_ENGINE_ALL_SUBCHANNELS=1`. A change in the faults between
   the two runs means 2D-engine methods were reaching the draw state.
5. Repeat with patch 0045's `SPU_FLOAT_MODE=audit` and `exact`; both native
   town runs are complete, with the target faults still visible.

## Windows follow-up

The original review below describes `1865c88`. Findings 1–3 were addressed in
[`e86ad08`](https://github.com/WK2D/White-Knight-Chronicles-2-Decomp/commit/e86ad0896059ba8d5fbcd2bcc2a701f3ed7d5852):
audio sequences can end and restart after a fatal decode error,
demux AU validation uses the actual aligned arena capacity, and the pad-file
test uses `setenv` on POSIX systems. Windows audio recovery and demux boundary
regressions fail against the old code and pass with the fixes. The POSIX branch
has not been run on Linux or macOS here.

Finding 4 is deferred until an independent pixel comparison establishes the
required chroma placement. No chroma-siting change is included.

## Original review

Review of `main` at `1865c88`, covering the host runtime sources, the three
newest SDK patches (0039 to 0041) and the publication checker. These are
suggestions only. No runtime code was changed, because the affected paths need
the private generated sources and FFmpeg to test, and native behaviour can only
be checked on the Windows build.

### Original checks in a Linux container

- All 41 patches apply to a clean SDK checkout of `a2c20108` with
  `tools/apply_sdk_patches.py`.
- `tools/check_public_tree.py` reports no findings (149 files).
- `tests/memory_test.c` builds without warnings (`-Wall -Wextra`) against the
  patched `sys_memory.c` and passes. Patch 0040 also reserves mapper ranges,
  which fixes a gap in the original allocator proposal: mapped pages could
  otherwise have been handed out again.
- Patches 0039 (DMA polling) and 0041 (pad-file sticks) were reviewed by
  reading only. Each looks correct: the repeat limit is per-thread and off by
  default, and malformed stick fields are rejected without clearing the
  request file.

Not checked: Windows builds, game regeneration, native boot and rendering.

### Original findings

Ordered by priority. None of them blocks current progress.

#### 1. Audio decoder cannot restart after an AC-3 decode failure

`adec.cpp:190`. On a failed AC-3 decode, the worker sets `failed=true` and
`accepting=false` but leaves `active=true`. After that:

- `cellAdecEndSeq` returns `seq` because `accepting` is false;
- `cellAdecStartSeq` returns `seq` because `active` is true.

The decoder can only be closed and reopened. `vdec.cpp` handles the same case
differently: it sets only `failed`, so EndSeq still drains, clears `active`,
and a later StartSeq resets `failed`. `decode()` already rejects new AUs once
`failed` is set, so leaving `accepting` alone keeps that rejection and makes
recovery match the video decoder:

```diff
-                {std::lock_guard l(d->mutex);d->used[c.slot]=false;d->failed=true;d->accepting=false;}
+                {std::lock_guard l(d->mutex);d->used[c.slot]=false;d->failed=true;}
```

Suggested regression: feed one corrupt AC-3 AU (valid sync word, damaged
body), expect the fatal error callback, then check that EndSeq returns 0, the
sequence-end callback fires and StartSeq succeeds. This only matters if the
game retries after a fatal audio error rather than closing the decoder.

#### 2. Demuxer can wait forever on an AU that fits the size check but not the arena

`dmux.cpp:87`. `publish()` rejects an AU only when it exceeds `es->size-128`.
The free arena starts at a 128-byte boundary at least 128 bytes into the
resource, and its length is rounded down to 128 bytes. For a base address
16 bytes past a 128-byte boundary, that leaves `size-256` bytes. An AU between
those two limits passes the check, but no free block is ever large enough, so
the worker waits in `changed.wait` until close.

Resources come from `cellDmuxQueryEsAttr` with a large margin, so this is
unlikely in practice. If you want it closed, record the arena size in
`enable_es` and compare against that:

```diff
 struct Elementary {
-    uint32_t handle=0,size=0,callback=0,user=0,major=0,minor=0;
+    uint32_t handle=0,size=0,capacity=0,callback=0,user=0,major=0,minor=0;
@@ publish
-    if(want>es->size-128) throw std::runtime_error("access unit exceeds guest ES resource");
+    if(want>es->capacity) throw std::runtime_error("access unit exceeds guest ES resource");
@@ enable_es
-    es->free.emplace(arena,(size-arena)&~127u);
+    es->capacity=(size-arena)&~127u;
+    es->free.emplace(arena,es->capacity);
```

#### 3. The two new self-tests only compile on Windows

`tests/pad_file_test.c` calls `_putenv_s`, which the SDK's `win32_compat.h`
does not provide off Windows. `VirtualAlloc` is provided, so
`tests/spu_dma_poll_test.c` should be portable. Both are built into the main
executable whenever `WKC2_BUILD_TESTS` is on, so a future Linux or macOS build
will fail to compile there. One option is to add a small `setenv` wrapper
under `#ifndef _WIN32`, as `main.cpp` already does for `SPU_CRI_IMAGE`.

#### 4. Minor: horizontal chroma siting in scaled YUV-to-RGBA conversion

`vpost.cpp:307`. The scaled path samples chroma at `(sx-0.5)/2`
horizontally, which assumes centre-sited chroma. MPEG-2's default 4:2:0
siting is co-sited with the left luma sample horizontally, `sx/2`, and
midway vertically, `(sy-0.5)/2`, which the code already uses. The unscaled
path picks nearest samples, so the two paths differ by a quarter of a chroma
sample. This is a subpixel shift and probably invisible in movie playback.
Change it only if a pixel comparison test calls for it.

### Original areas reviewed with no issues found

- `lwcond.cpp`: the guest mutex is released and the waiter enqueued under one
  lock, so a signal cannot be lost. Timed waits remove the waiter before
  unlocking.
- `lfqueue.cpp`: ring index bounds, the wait for an outstanding SPU producer
  commit, and waiter-cursor arithmetic.
- `mpegps.hpp`, `pamf.cpp`: PES header lengths are checked before every
  timestamp read; PAMF descriptor tables stay within the declared header.
- `vdec.cpp`: output slot accounting cannot run out (output is capped below
  the slot count); PTS interpolation keeps a remainder.
- `osk.cpp`, `l10n.cpp`: output capacity checks, surrogate handling, and
  rejection of overlong UTF-8.
- `tools/check_public_tree.py`: the allowlist is applied before contents are
  read, and PNG metadata chunks are rejected.
