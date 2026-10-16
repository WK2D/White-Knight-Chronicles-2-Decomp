param([Parameter(Mandatory=$true)][string]$Workspace)
# Optional private PyAV installation supplies matching user-owned FFmpeg DLLs.
# Explicit library paths take precedence. No DLL is copied to the public tree.
$mediaLibraries = Join-Path $Workspace 'work/venv/Lib/site-packages/av.libs'
if (Test-Path -LiteralPath $mediaLibraries) {
    if (-not $env:PS3RECOMP_FFMPEG_AVUTIL) {
        $mediaUtil = Get-ChildItem -LiteralPath $mediaLibraries -Filter 'avutil-60-*.dll' | Select-Object -First 1
        if ($mediaUtil) { $env:PS3RECOMP_FFMPEG_AVUTIL = $mediaUtil.FullName }
    }
    if (-not $env:PS3RECOMP_FFMPEG_AVCODEC) {
        $mediaCodec = Get-ChildItem -LiteralPath $mediaLibraries -Filter 'avcodec-62-*.dll' | Select-Object -First 1
        if ($mediaCodec) { $env:PS3RECOMP_FFMPEG_AVCODEC = $mediaCodec.FullName }
    }
    $env:PATH = "$mediaLibraries;$env:PATH"
}
