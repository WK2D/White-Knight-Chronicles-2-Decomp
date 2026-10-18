# Package the approved PNG as a multi-resolution Windows icon.
# System.Drawing is part of Windows; no external image packages are needed.
param([string]$ImagePath = "$PSScriptRoot/../assets/wkcii.png",
      [string]$OutputPath = "$PSScriptRoot/../assets/wkcii.ico")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
function Remove-PngMetadata([byte[]]$Bytes) {
    $clean = [IO.MemoryStream]::new()
    try {
        $clean.Write($Bytes,0,8)
        $offset = 8
        while ($offset -lt $Bytes.Length) {
            $length = [uint32]$Bytes[$offset]*16777216 + [uint32]$Bytes[$offset+1]*65536 +
                      [uint32]$Bytes[$offset+2]*256 + [uint32]$Bytes[$offset+3]
            $kind = [Text.Encoding]::ASCII.GetString($Bytes,$offset+4,4)
            if ($kind -in @('IHDR','IDAT','IEND')) { $clean.Write($Bytes,$offset,$length+12) }
            $offset += $length+12
        }
        return ,$clean.ToArray()
    } finally { $clean.Dispose() }
}
$bitmap = [Drawing.Image]::FromFile((Resolve-Path -LiteralPath $ImagePath).Path)
try {
    if ($bitmap.Width -ne $bitmap.Height) { throw 'The icon source must be square' }
    $sizes = @(16,24,32,48,64,128,256)
    $entries = @()
    foreach ($size in $sizes) {
        $small = [Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [Drawing.Graphics]::FromImage($small)
        $stream = [IO.MemoryStream]::new()
        try {
            $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $g.DrawImage($bitmap,0,0,$size,$size)
            $small.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
            $entries += ,(Remove-PngMetadata $stream.ToArray())
        } finally { $stream.Dispose(); $g.Dispose(); $small.Dispose() }
    }
    $file = [IO.File]::Create([IO.Path]::GetFullPath($OutputPath))
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i=0; $i -lt $sizes.Count; $i++) {
            $edge = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
            $writer.Write([byte]$edge); $writer.Write([byte]$edge)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$entries[$i].Length); $writer.Write([uint32]$offset)
            $offset += $entries[$i].Length
        }
        foreach ($entry in $entries) { $writer.Write([byte[]]$entry) }
    } finally { $writer.Dispose(); $file.Dispose() }
} finally { $bitmap.Dispose() }
Write-Output "Generated $OutputPath (16, 24, 32, 48, 64, 128, 256 pixels)"
