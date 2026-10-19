# Contributing

This repository contains the host runtime integration, SDK patches, diagnostic
tools and regression checks for BLUS30784 v01.00. The Windows Vulkan prototype
reaches the WKC1 starting town, with short movement and camera control verified.
It remains an early native recompilation project: one starting-town Logic Stone
save and restart/Continue reload are verified; sustained gameplay, combat, wider
save compatibility and full rendering fidelity are unverified. The remaining
SPU interpreter fallback is documented in [ARCHITECTURE.md](ARCHITECTURE.md).
It is not a completed source or matching decompilation.

## What stays private

Keep original disc files, decrypted executables, generated game sources, saves,
raw captures, logs, analysis databases and local configuration outside this repository.
Commit changes to the pinned SDK as numbered patches under `patches/` and retain
the upstream license notices. Document observable behavior separately from theories.

## Before publishing

```powershell
git add .
python tools/check_public_tree.py
git diff --cached --stat
git diff --cached --check
```

The scanner checks staged contents and an explicit file allowlist. Review the
staged diff as well: pattern checks cannot recognize every possible secret.
It also validates recorded SHA-256 fields and checks gallery/rendering image
hashes against the staged PNG files. Private executable/source hashes still
need comparison with their actual local artifacts before publication.
Use a project identity and a GitHub no-reply email for public commits when
personal attribution is not desired. Configure them for this repository only.

## Regression checks and evidence

The runtime regressions run through `tools/build.ps1`; the current Windows Vulkan
configuration passes 95 checks with 105 SDK patches in Debug and Release. See
[BUILD.md](BUILD.md) for
the pinned dependencies and regeneration workflow. When reporting a
rendering improvement, inspect a real GPU readback; frame counters alone do not
establish correct rendering. Inspect the default probe RSX audit and resolve or
explicitly document active missing/approximate/unverified features before claiming
coverage. Check the probe's `unimplemented_syscall_ids` too; an empty list does
not establish correctness of registered handlers. Use `-NoRsxAudit` for controlled timing probes. An empty feature list is
not proof of complete command support. Keep full logs private and share only reviewed excerpts.

## Milestones and screenshots

For each new visible milestone, add a dated entry above the latest one in `docs/GALLERY.md`
(and a row in its milestone table for a major step),
a lossless capture under `verification/screenshots/`, and provenance hashes in
`verification/gallery.json`. Use game-frame readbacks and inspect the image before
publication. Remove all optional PNG metadata; the staged-tree check accepts only
IHDR, IDAT and IEND chunks. Record what the image establishes and keep unverified
features marked pending. Update the current status in `README.md`, `project.json`
and the verification records together, while retaining earlier results as dated
history. Keep speculative fixes and future content support separate from verified
native behavior. Commit and push checked milestone updates as work progresses.

## Where notes go

| Note | Page |
| --- | --- |
| How to build, run or diagnose | [BUILD.md](BUILD.md) |
| What a new SDK patch changes and how it was checked | [PATCHES.md](PATCHES.md): add a section at the top of the notes and a row at the top of the index table |
| How a larger runtime subsystem works | [SUBSYSTEMS.md](SUBSYSTEMS.md) |
| Measurements, verified results and investigations | [FINDINGS.md](FINDINGS.md): add the section after the status sections and list it under its topic in the contents |
| A new screenshot | [GALLERY.md](GALLERY.md), newest first |
| A general lesson or a symptom with a known cause | [LESSONS.md](LESSONS.md): add a row to the symptom index when the symptom is recognizable |
| A term, environment variable or game address | [REFERENCE.md](REFERENCE.md) |

Keep section headings unchanged once published: other pages link to them by anchor.

## Findings template

New findings sections use the same five parts, so that people and tools can
compare them. Omit a part only when it does not apply, and say so.

```markdown
## Short description of the result (patch NNNN)

**Problem.** What was observed, with the scene, build and settings.

**Cause.** What the evidence shows. Mark theories as theories.

**Fix.** What changed, with the patch or file names.

**Evidence.** Measurements, readbacks, check results or screenshots, with dates.

**Limits.** What this does not establish and what remains unverified.
```

Naming the patch in the heading as `(patch NNNN)` or `(patches NNNN-NNNN)` links
the section from `patches/index.json`.

## Generated files and counts

After adding a patch or a patch note, regenerate the patch index:

```powershell
python tools/gen_patch_index.py
```

`patches/index.json` lists each patch with its description, the SDK files it
touches, its subsystems and the notes and findings that cover it. Do not edit it
by hand.

The current patch and check counts appear in several pages. After a change to
`patches/` or to `current_build.runtime_checks_passed` in `project.json`, update
them all with:

```powershell
python tools/sync_status_counts.py
```

Both tools accept `--check`, which reports drift without changing files. Dated
measurements keep the counts they were recorded with.
