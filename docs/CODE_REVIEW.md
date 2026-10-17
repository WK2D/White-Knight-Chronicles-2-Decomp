# Code review suggestions

## Windows follow-up

The original review below describes `1865c88`. The current changes address
findings 1–3: audio sequences can end and restart after a fatal decode error,
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

## Checked in a Linux container

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

## Findings

Ordered by priority. None of them blocks current progress.

### 1. Audio decoder cannot restart after an AC-3 decode failure

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

### 2. Demuxer can wait forever on an AU that fits the size check but not the arena

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

### 3. The two new self-tests only compile on Windows

`tests/pad_file_test.c` calls `_putenv_s`, which the SDK's `win32_compat.h`
does not provide off Windows. `VirtualAlloc` is provided, so
`tests/spu_dma_poll_test.c` should be portable. Both are built into the main
executable whenever `WKC2_BUILD_TESTS` is on, so a future Linux or macOS build
will fail to compile there. One option is to add a small `setenv` wrapper
under `#ifndef _WIN32`, as `main.cpp` already does for `SPU_CRI_IMAGE`.

### 4. Minor: horizontal chroma siting in scaled YUV-to-RGBA conversion

`vpost.cpp:307`. The scaled path samples chroma at `(sx-0.5)/2`
horizontally, which assumes centre-sited chroma. MPEG-2's default 4:2:0
siting is co-sited with the left luma sample horizontally, `sx/2`, and
midway vertically, `(sy-0.5)/2`, which the code already uses. The unscaled
path picks nearest samples, so the two paths differ by a quarter of a chroma
sample. This is a subpixel shift and probably invisible in movie playback.
Change it only if a pixel comparison test calls for it.

## Areas reviewed with no issues found

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
