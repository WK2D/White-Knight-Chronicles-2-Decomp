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
Use a project identity and a GitHub no-reply email for public commits when
personal attribution is not desired. Configure them for this repository only.

## Regression checks and evidence

The runtime regressions run through `tools/build.ps1`; the current Windows Vulkan
configuration passes 84 checks with 99 SDK patches in Debug and Release. See
[BUILD.md](BUILD.md) for
the pinned dependencies and regeneration workflow. When reporting a
rendering improvement, inspect a real GPU readback; frame counters alone do not
establish correct rendering. Inspect the default probe RSX audit and resolve or
explicitly document active missing/approximate/unverified features before claiming
coverage. Check the probe's `unimplemented_syscall_ids` too; an empty list does
not establish correctness of registered handlers. Use `-NoRsxAudit` for controlled timing probes. An empty feature list is
not proof of complete command support. Keep full logs private and share only reviewed excerpts.

## Milestones and screenshots

For each new visible milestone, add a dated entry at the top of `docs/GALLERY.md`,
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

Keep section headings unchanged once published: other pages link to them by anchor.
