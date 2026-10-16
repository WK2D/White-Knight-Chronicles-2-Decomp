# Optional Windows renderer dependencies; all sources stay in the private workspace.
param(
    [string]$Workspace = (Resolve-Path "$PSScriptRoot/../../.."),
    [ValidateRange(1,64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/windows-env.ps1" -Workspace $Workspace
$deps = Join-Path $Workspace 'work/deps'
$prefix = Join-Path $deps 'vulkan-prefix'

function PinnedSource([string]$Directory, [string]$Url, [string]$Revision) {
    if (-not (Test-Path -LiteralPath (Join-Path $Directory '.git'))) {
        if (Test-Path -LiteralPath $Directory) {
            if (Get-ChildItem -LiteralPath $Directory -Force | Select-Object -First 1) {
                throw "Expected an empty dependency directory: $Directory"
            }
        } else { New-Item -ItemType Directory -Path $Directory -Force | Out-Null }
        git -C $Directory init --quiet
        if ($LASTEXITCODE -ne 0) { throw 'Dependency Git initialization failed' }
        git -C $Directory remote add origin $Url
        git -C $Directory fetch --quiet --depth 1 origin $Revision
        if ($LASTEXITCODE -ne 0) { throw "Dependency fetch failed: $Url" }
        git -C $Directory -c core.autocrlf=false checkout --quiet --detach FETCH_HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Dependency checkout failed' }
    }
    if ((git -C $Directory rev-parse HEAD) -ne $Revision) {
        throw "Dependency revision mismatch: $Directory"
    }
    # Nested dependency repositories are intentionally untracked by their parent.
    if (git -C $Directory status --porcelain --untracked-files=no) {
        throw "Dependency has local source changes: $Directory"
    }
}

PinnedSource "$deps/SDL2" 'https://github.com/libsdl-org/SDL.git' '5d249570393f7a37e037abf22cd6012a4cc56a71'
PinnedSource "$deps/glslang" 'https://github.com/KhronosGroup/glslang.git' 'e1b562a8bed273a02f30b59b66a5d499793cede5'
PinnedSource "$deps/glslang/External/spirv-tools" 'https://github.com/KhronosGroup/SPIRV-Tools.git' 'ef96ed763b43b59b33b31b362f09a02b729fa1c9'
PinnedSource "$deps/glslang/External/spirv-tools/external/spirv-headers" 'https://github.com/KhronosGroup/SPIRV-Headers.git' '496543121ce6419f23d6fa5d7194ba66c36212d2'
PinnedSource "$deps/Vulkan-Headers" 'https://github.com/KhronosGroup/Vulkan-Headers.git' '9a0f3099c8a9607a7c0f3127d8abfdc19a93e8c5'

function BuildDependency([string]$Source, [string]$Build, [string[]]$Options) {
    # The port currently uses Debug /MDd. Keep the dependencies optimized while
    # matching its CRT and C++ iterator ABI; Release /MD cannot be mixed with it.
    cmake -S $Source -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL `
        "-DCMAKE_INSTALL_PREFIX=$prefix" @Options
    if ($LASTEXITCODE -ne 0) { throw 'Dependency configuration failed' }
    cmake --build $Build -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Dependency compilation failed' }
    cmake --install $Build
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }
}
BuildDependency "$deps/SDL2" "$Workspace/work/build/sdl2" @('-DSDL_SHARED=OFF','-DSDL_STATIC=ON','-DSDL_TEST=OFF')
BuildDependency "$deps/glslang" "$Workspace/work/build/glslang" @(
    '-DBUILD_SHARED_LIBS=OFF','-DENABLE_OPT=ON','-DENABLE_GLSLANG_BINARIES=OFF',
    '-DGLSLANG_TESTS=OFF','-DSPIRV_SKIP_TESTS=ON','-DSPIRV_SKIP_EXECUTABLES=ON',
    '-DSPIRV_WERROR=OFF','-DALLOW_EXTERNAL_SPIRV_TOOLS=OFF')
Write-Output 'Pinned Vulkan dependencies ready'
