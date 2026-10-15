param(
    [string]$Workspace = (Resolve-Path "$PSScriptRoot/../../.."),
    [ValidateRange(1,600)][int]$Seconds = 15,
    [switch]$TraceCalls,
    [switch]$CaptureFrames,
    [ValidateRange(1,32)][int]$CaptureFrameCount = 4,
    [ValidateRange(1,3600)][int]$CaptureEvery = 1,
    [ValidateRange(0,3600)][int]$CaptureSkip = 0
)
$ErrorActionPreference = 'Stop'
$logRoot = Join-Path $Workspace 'work/probes'
$missRoot = Join-Path $Workspace 'work/wkc2-analysis/spu-misses'
New-Item -ItemType Directory -Force $logRoot,$missRoot | Out-Null
$tag = Get-Date -Format 'yyyyMMdd-HHmmss'
if ($CaptureFrames) {
    $frameRoot = Join-Path $logRoot "$tag.frames"
    New-Item -ItemType Directory -Force $frameRoot | Out-Null
    $env:WKC2_GUEST_PRESENT_ONLY = '1'
    $env:CELLMARK_DUMP = [string]$CaptureFrameCount
    $env:CELLMARK_DUMP_DIR = $frameRoot
    $env:CELLMARK_DUMP_EVERY = [string]$CaptureEvery
    $env:CELLMARK_DUMP_SKIP = [string]$CaptureSkip
}
$env:PS3_VFS_ROOT = Join-Path $Workspace 'work/wkc2-disc'
$env:PS3_TITLE = 'White Knight Chronicles II - native research'
$env:SPU_DUMP_MISS = $missRoot
$env:PS3_NULL_READ_STACK = '1'
$env:WKC2_PROGRESS = '1'
if ($TraceCalls) { $env:WKC2_TRACE_CALLS = '1' } else { Remove-Item Env:WKC2_TRACE_CALLS -ErrorAction SilentlyContinue }
$elf = Join-Path $Workspace 'work/wkc2-inspection/PS3_GAME/USRDIR/EBOOT.elf'
$exe = Join-Path $Workspace 'work/build/wkc2/WKC2Native.exe'
$p = Start-Process -FilePath $exe -ArgumentList ('"'+$elf+'"') -WorkingDirectory $Workspace -WindowStyle Hidden -RedirectStandardOutput "$logRoot/$tag.stdout.log" -RedirectStandardError "$logRoot/$tag.stderr.log" -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
$timedOut = $false
while (-not $p.HasExited) {
    $remainingMs = [Math]::Ceiling(($deadline - [DateTime]::UtcNow).TotalMilliseconds)
    if ($remainingMs -le 0) { $timedOut = $true; break }
    if ($p.WaitForExit([int][Math]::Min(5000, $remainingMs))) { break }
}
if ($timedOut) { Stop-Process -Id $p.Id -Force; $p.WaitForExit() }
$flips = $null
$presented = $null
$lastVerification = Select-String -LiteralPath "$logRoot/$tag.stderr.log" -Pattern '\[verification\] guest_flip_requests=(\d+) guest_frames_presented=(\d+)' | Select-Object -Last 1
if ($lastVerification) {
    $flips = [uint32]$lastVerification.Matches[0].Groups[1].Value
    $presented = [uint32]$lastVerification.Matches[0].Groups[2].Value
}
[ordered]@{pid=$p.Id; timed_out=$timedOut; exit_code=$p.ExitCode; seconds_limit=$Seconds; guest_present_only=($env:WKC2_GUEST_PRESENT_ONLY -eq '1'); capture_directory=$env:CELLMARK_DUMP_DIR; capture_count=$CaptureFrameCount; capture_every=$CaptureEvery; capture_skip=$CaptureSkip; controller_script=$env:PAD_SCRIPT; last_observed_guest_flips=$flips; last_observed_presented_frames=$presented; title_screen_verified=$false; gameplay_verified=$false} | ConvertTo-Json | Set-Content "$logRoot/$tag.result.json"
Write-Output "Probe logs: $logRoot/$tag.*"
