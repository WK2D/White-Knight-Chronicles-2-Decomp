# Contributing

This repository contains the host runtime integration, SDK patches, diagnostic
tools and regression checks for BLUS30784 v01.00. It is a native recompilation
research project; it is not yet a playable port or a completed matching decompilation.

Keep original disc files, decrypted executables, generated game sources, saves,
raw captures, logs, analysis databases and local configuration outside this repository.
Commit changes to the pinned SDK as numbered patches under `patches/` and retain
the upstream license notices. Document observable behavior separately from theories.

Before publishing:

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

The runtime regressions run through `tools/build.ps1`. When reporting a
rendering improvement, inspect a real GPU readback; frame counters alone do not
establish correct rendering. Keep full logs private and share only reviewed excerpts.

For each new visible milestone, add a dated entry to `docs/GALLERY.md`, a lossless
capture under `verification/screenshots/`, and provenance hashes in
`verification/gallery.json`. Use game-frame readbacks and inspect the image before
publication. Remove all optional PNG metadata; the staged-tree check accepts only
IHDR, IDAT and IEND chunks. Record what the image establishes and keep unverified
features marked pending. Commit and push checked milestone updates as work progresses.
