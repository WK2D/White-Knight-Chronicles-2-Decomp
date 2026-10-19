# Documentation

Start with the [project overview](../README.md). Pages in this folder:

| Page | What it covers |
| --- | --- |
| [BUILD.md](BUILD.md) | Requirements, private workspace layout, building, regression checks, play and diagnostic options |
| [FINDINGS.md](FINDINGS.md) | Verified results, measurements, open issues and investigation history, grouped by topic |
| [PATCHES.md](PATCHES.md) | What each numbered SDK patch changes and how it was checked, newest first |
| [SUBSYSTEMS.md](SUBSYSTEMS.md) | Renderer, movie decoding, guest frame presentation and native text entry |
| [LESSONS.md](LESSONS.md) | Symptom index and reusable lessons on timing, rendering, SPU jobs and runtime services |
| [REFERENCE.md](REFERENCE.md) | Glossary, environment variables and known game addresses |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Execution model, remaining SPU fallback and modding scope |
| [GALLERY.md](GALLERY.md) | Dated screenshots, newest first |
| [KILLZONE_FIX_AUDIT.md](KILLZONE_FIX_AUDIT.md) | Runtime-fix comparison and remaining implementation gaps |
| [CODE_REVIEW.md](CODE_REVIEW.md) | Review findings, fixes and the checks that were run |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contribution workflow and where new notes belong |
| [ICON.md](ICON.md) | Icon used by Windows builds |

For other recompilation projects, start with [LESSONS.md](LESSONS.md): its
symptom index maps a visible problem to the cause found here. [PATCHES.md](PATCHES.md)
and [SUBSYSTEMS.md](SUBSYSTEMS.md) then say what was wrong in the runtime, what
changed, and how it was verified. Tools can read the same patch history from
[`patches/index.json`](../patches/index.json).
