param(
    [string]$Workspace = (Resolve-Path "$PSScriptRoot/../../.."),
    [switch]$Regenerate,
    [int]$Jobs = 4
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
$expected = 'a2c20108af45174dbb7d3e2f87f6acaaef09c6b4'
if ((git -C $sdk rev-parse HEAD) -ne $expected) { throw 'SDK revision mismatch' }
foreach ($patch in (Get-ChildItem "$project/patches/*.patch" | Sort-Object Name)) {
    git -C $sdk apply --reverse --check $patch.FullName 2>$null
    if ($LASTEXITCODE -eq 0) { continue }
    git -C $sdk apply --check $patch.FullName
    if ($LASTEXITCODE -ne 0) { throw "Patch conflicts: $($patch.Name)" }
    git -C $sdk apply $patch.FullName
    if ($LASTEXITCODE -ne 0) { throw "Patch failed: $($patch.Name)" }
}
function Invoke-CheckedPython {
    & $python @args
    if ($LASTEXITCODE -ne 0) { throw "Python step failed: $args" }
}
if ($Regenerate) {
    Invoke-CheckedPython "$project/tools/analyze.py" --sdk $sdk --elf $elf --output $analysis
    $meta = Get-Content "$analysis/summary.json" -Raw | ConvertFrom-Json
    Invoke-CheckedPython "$sdk/tools/ppu_lifter.py" $elf --functions "$analysis/functions.bounded.json" --hle-stubs "$analysis/EBOOT.imports.json" --code-end $meta.code_end --chunk-lines 50000 --jobs $Jobs --output "$generated/ppu"
    Invoke-CheckedPython "$sdk/tools/extract_spu_images.py" $elf --output "$analysis/spu-images"
    Invoke-CheckedPython "$project/tools/extract_raw_jobs.py" --sdk $sdk --elf $elf --images "$analysis/spu-images"
    Invoke-CheckedPython "$sdk/tools/build_spu_workloads.py" --images "$analysis/spu-images" --lifted "$generated/spu" --out "$generated/spu/spu_workloads.c" --register-fn wkc2_spu_register_all --constructor --title wkc2 --relift
}
Invoke-CheckedPython "$project/tools/apply_port_hooks.py" "$generated/ppu"
Invoke-CheckedPython "$project/tools/apply_diagnostics.py" "$generated/ppu"
cmake -S $project -B $build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl "-DPS3RECOMP_DIR=$sdk" "-DRECOMP_DIR=$generated/ppu" "-DSPU_DIR=$generated/spu" "-DDIRECTX_HEADERS_DIR=$Workspace/work/deps/DirectX-Headers" "-DPython3_EXECUTABLE=$python" '-DCMAKE_C_FLAGS=/w' '-DCMAKE_CXX_FLAGS=/w' -DPORT_ENABLE_SPU=ON -DWKC2_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build $build -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
ctest --test-dir $build -R wkc2_ --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Regression checks failed' }
