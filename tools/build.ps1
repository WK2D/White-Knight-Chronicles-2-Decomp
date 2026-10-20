param(
    [string]$Workspace = (Resolve-Path "$PSScriptRoot/../../.."),
    [switch]$Regenerate,
    [int]$Jobs = 4,
    [ValidateSet('D3D12','Vulkan')][string]$Renderer = 'Vulkan'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/windows-env.ps1" -Workspace $Workspace
$project = (Resolve-Path "$PSScriptRoot/..").Path
$sdk = Join-Path $Workspace 'work/deps/ps3recomp'
$python = Join-Path $Workspace 'work/venv/Scripts/python.exe'
$elf = Join-Path $Workspace 'work/wkc2-inspection/PS3_GAME/USRDIR/EBOOT.elf'
$analysis = Join-Path $Workspace 'work/wkc2-analysis'
$generated = Join-Path $Workspace 'work/wkc2-generated'
$build = Join-Path $Workspace 'work/build/wkc2'
if ($Renderer -eq 'Vulkan') { $build = Join-Path $Workspace 'work/build/wkc2-vulkan' }
$expected = 'a2c20108af45174dbb7d3e2f87f6acaaef09c6b4'
if ((git -C $sdk rev-parse HEAD) -ne $expected) { throw 'SDK revision mismatch' }
function Invoke-CheckedPython {
    & $python @args
    if ($LASTEXITCODE -ne 0) { throw "Python step failed: $args" }
}
Invoke-CheckedPython "$project/tools/apply_sdk_patches.py" --sdk $sdk --patches "$project/patches" --expected-commit $expected
if ($Regenerate) {
    Invoke-CheckedPython "$project/tools/analyze.py" --sdk $sdk --elf $elf --output $analysis
    $meta = Get-Content "$analysis/summary.json" -Raw | ConvertFrom-Json
    Invoke-CheckedPython "$sdk/tools/ppu_lifter.py" $elf --functions "$analysis/functions.bounded.json" --hle-stubs "$analysis/EBOOT.imports.json" --code-end $meta.code_end --chunk-lines 50000 --jobs $Jobs --output "$generated/ppu"
    Invoke-CheckedPython "$sdk/tools/extract_spu_images.py" $elf --output "$analysis/spu-images"
    Invoke-CheckedPython "$project/tools/extract_raw_jobs.py" --sdk $sdk --elf $elf --images "$analysis/spu-images"
    Invoke-CheckedPython "$project/tools/build_spu.py" --sdk $sdk --images "$analysis/spu-images" --lifted "$generated/spu"
}
Invoke-CheckedPython "$project/tools/apply_native_geometry.py" "$generated/spu"
Invoke-CheckedPython "$project/tools/apply_native_normals.py" "$generated/spu"
Invoke-CheckedPython "$project/tools/apply_port_hooks.py" "$generated/ppu"
Invoke-CheckedPython "$project/tools/apply_diagnostics.py" "$generated/ppu"
Invoke-CheckedPython "$project/tools/prepare_ffmpeg.py" "$Workspace/work/deps"
. "$project/tools/media-env.ps1" -Workspace $Workspace
$rendererOptions = @('-DPS3RECOMP_RSX_VULKAN=OFF')
if ($Renderer -eq 'Vulkan') {
    & "$project/tools/prepare-vulkan.ps1" -Workspace $Workspace -Jobs $Jobs
    $rendererOptions = @('-DPS3RECOMP_RSX_VULKAN=ON',
        "-DCMAKE_PREFIX_PATH=$Workspace/work/deps/vulkan-prefix",
        "-DPS3RECOMP_VULKAN_INCLUDE_DIR=$Workspace/work/deps/Vulkan-Headers/include")
}
cmake -S $project -B $build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl "-DPS3RECOMP_DIR=$sdk" "-DRECOMP_DIR=$generated/ppu" "-DSPU_DIR=$generated/spu" "-DDIRECTX_HEADERS_DIR=$Workspace/work/deps/DirectX-Headers" "-DPython3_EXECUTABLE=$python" '-DCMAKE_C_FLAGS=/w' '-DCMAKE_CXX_FLAGS=/w' -DPORT_ENABLE_SPU=ON -DWKC2_BUILD_TESTS=ON -DPS3RECOMP_FFMPEG_RUNTIME=ON "-DPS3RECOMP_FFMPEG_INCLUDE_DIR=$Workspace/work/deps/ffmpeg-8.1" @rendererOptions
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build $build -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
ctest --test-dir $build -R wkc2_ --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Regression checks failed' }
