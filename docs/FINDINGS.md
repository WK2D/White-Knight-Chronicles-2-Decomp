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
| Native Windows build succeeds | Clang build and four runtime regression checks | Compilation alone does not verify gameplay |
| Language-selection screen renders | GPU readback in the [gallery](GALLERY.md) | Startup UI only |
| Language selection accepts input | CROSS through the controller path advances startup | General gameplay controls remain untested |
| Warning and autosave notices render | Separate GPU readbacks in the gallery | The autosave notice does not verify saving |
| Lifted SPU decoder produces correct output for sampled assets | Eight outputs match independent raw-DEFLATE decoding byte for byte; [record](../decompression-verification.json) | Sampled blocks, not all assets |
| Native guest installer writes the large data archives | Three installed archives, totaling 4,364,470,879 bytes, hash-identical to their disc data; [record](../verification/installation.json) | Installation progress, not gameplay or save/load |
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

Regression checks cover lightweight conditions, memory containers, LFQueue and
event-queue recycling. Their scope is narrower than full-game compatibility.

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

Startup now reaches the first logo movie. The PAMF reader rejects initialization
and the guest aborts with exit code 1. Movie-reader ABI/header handling is the next
investigation. The title screen, playback, gameplay and save/load remain unverified.
Missing imports and unresolved audio-worker calls remain separate open issues.

## Still unverified

Title-screen rendering, playable scenes, combat, save/load, a complete fresh-disc
reproduction of the latest boot, macOS execution and online functionality.

See the [build limitations](BUILD.md) and [capture manifest](../verification/gallery.json)
for reproduction requirements and screenshot provenance.
