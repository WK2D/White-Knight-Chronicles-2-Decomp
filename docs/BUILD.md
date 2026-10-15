# Current Windows build workflow

The source checkout and private working directory can be separate. Pass the
private workspace with `-Workspace`; the build script gets project sources from
its own location. Its default workspace layout is three parent directories above
the script and is intended for the original development layout.

Required tools: PowerShell, Git, Python 3.11+, Clang/clang-cl, CMake, Ninja,
MSVC Build Tools headers/libraries and the Windows 10 SDK. Install the Python
dependencies listed by the pinned PS3Recomp SDK, plus PyCryptodome. Keep downloaded
tools and dependency repositories in the private workspace.

The current `windows-env.ps1` locates Clang in `work/tools/llvm/bin` and uses
MSVC 14.29.30133 / Windows SDK 10.0.19041.0. Adjust these tool locations for a
different installation. They are build prerequisites, not runtime requirements.

Private workspace layout:

```text
work/
  deps/ps3recomp/          # commit a2c20108af45174dbb7d3e2f87f6acaaef09c6b4
  deps/DirectX-Headers/   # commit 461ef5138bd30922b44fbad77c4e47662812d2d5
  tools/llvm/bin/
  venv/Scripts/python.exe
  wkc2-inspection/PS3_GAME/USRDIR/EBOOT.elf
  wkc2-disc/              # privately extracted disc tree used by the VFS
  wkc2-analysis/
  wkc2-generated/ppu/
  wkc2-generated/spu/
```

Supply your own decrypted BLUS30784 v01.00 executable and extracted disc tree.
The analyzed ELF is 20,455,904 bytes, SHA-256
`333400e0b0915e3008a22f29daf95ed2c9b03f3c2a2787cf5705222d73a5ceeb`.
No executable, asset or generated game source is distributed here.

```powershell
./tools/build.ps1 -Workspace <private-workspace> -Regenerate
./tools/probe.ps1 -Workspace <private-workspace> -Seconds 15
```

`-Regenerate` analyzes and lifts the PPU plus embedded/executable-backed SPU jobs.
Two additional asset-backed SPU jobs were captured during development; reproducing
the most advanced boot currently requires private captures and the import workflow
in `tools/import_spu_misses.py`, followed by `build_spu_workloads.py`. A fresh disc-only
build is therefore not yet a complete reproduction of the latest verified UI boot.

`tools/prepare_sdat.py <private-USRDIR>` authenticates and caches the supported
SDAT archives. The build applies the six numbered runtime patches idempotently
and runs the PAMF reader, lightweight-condition, memory-container, LFQueue and
event-queue tests.

`pamf.cpp` supplies project-local context handlers for the nine imported PAMF APIs
before SDK fallback registration. The reader test uses synthetic headers by default.
`wkc2_pamf_test <private-movie-file>` additionally checks a local 2048-byte header;
the file is not copied into the source tree or test fixtures. This covers header
reading, not demuxing or movie playback.

Set `WKC2_UPDATE_DIAGNOSTICS=1` for dispatcher/scene state samples. The generated
update hook is applied by the build script and is disabled by default at runtime.
Probe logs and GPU captures go to the private workspace.

`tools/apply_port_hooks.py` installs synchronization before the original guest
installation request: a pending background content check must finish first.
This prevents a faster native caller from losing the install request to the
guest helper's pending-request guard. Allocation, copying and completion checks
remain in the original guest path. The hook is reapplied after regeneration.
`tools/trace_calls.py --env WKC2_CONTENT_TRACE` supports a separate trace switch
for focused content diagnostics, without enabling other instrumented functions.

`Wkc2Decompile.java` is a Ghidra 12.1.4 headless script for selected-function analysis
using `PowerPC:BE:64:64-32addr`. It takes a private TSV inventory (hex start, exclusive
end, TOC), a private output directory, and hex function addresses. It is an analysis
aid; generated pseudocode is not source distributed by this repository.
