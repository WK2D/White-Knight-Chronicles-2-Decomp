param([string]$Workspace = (Resolve-Path "$PSScriptRoot/../../.."))
# Player launch only. Research runs use probe.ps1 and separate private helpers.
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name WKC2Native -ErrorAction SilentlyContinue).Count) {
    throw 'White Knight Chronicles II is already running; leaving it alone'
}
. "$PSScriptRoot/media-env.ps1" -Workspace $Workspace
$env:WKC2_PLAYER_MODE = '1' # executable clears inherited research variables
$env:WKC2_DISCARD_LOGS = '1'
$env:PS3_VERBOSE = '0'
$env:PS3_VFS_ROOT = Join-Path $Workspace 'work/wkc2-disc'
$env:PS3_TITLE = 'White Knight Chronicles II'
$env:WKC2_GUEST_PRESENT_ONLY = '1'
$env:PS3RECOMP_VK_GUEST_PROGRAMS = '1'
$env:PS3RECOMP_VK_WINDOW = '1'
$env:PS3RECOMP_RSX_ENGINE = 'dispatch'
$env:WKC2_SKIP_INTRO = '1'
$env:WKC2_FLIP_ON_VBLANK = '1'
$env:GCM_FLIPCB_ONTICK = '1'
$env:GCM_SEMA_ACQUIRE = '1'
$env:RSX_ENGINE_ZTEX_GPU = '1'
$env:RSX_VK_PRESENT_MODE = 'mailbox'
$exe = Join-Path $Workspace 'work/play-native/WKC2Native.exe'
$elf = Join-Path $Workspace 'work/wkc2-inspection/PS3_GAME/USRDIR/EBOOT.elf'
$game = Start-Process -FilePath $exe -ArgumentList ('"' + $elf + '"') `
    -WorkingDirectory $Workspace -WindowStyle Hidden -PassThru
$null = $game.Handle
$game.WaitForExit()
$game.Refresh()
exit $game.ExitCode
