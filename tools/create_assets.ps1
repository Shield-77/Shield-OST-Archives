Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'
$assetDir = Join-Path $PSScriptRoot '..\assets'
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null

function New-Canvas([int]$width, [int]$height, [string]$output, [bool]$banner) {
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $background = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(3,18,34))
    $surface = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(9,39,64))
    $cyan = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(24,206,231))
    $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(240,248,255))
    $g.FillRectangle($background,0,0,$width,$height)
    if ($banner) {
        $g.FillEllipse($surface,-45,-100,265,265)
        $g.FillEllipse($cyan,28,28,72,72)
        $g.FillEllipse($background,39,39,50,50)
        $g.FillRectangle($cyan,64,46,15,5)
        $g.FillRectangle($cyan,72,47,6,33)
        $g.FillEllipse($cyan,62,76,16,10)
        $font1 = [System.Drawing.Font]::new('Segoe UI',19,[System.Drawing.FontStyle]::Bold)
        $font2 = [System.Drawing.Font]::new('Segoe UI',11,[System.Drawing.FontStyle]::Regular)
        $g.DrawString('SHIELD OST',$font1,$white,107,34)
        $g.DrawString('ARCHIVES',$font2,$cyan,110,77)
        $font1.Dispose(); $font2.Dispose()
    } else {
        $g.FillEllipse($surface,3,3,42,42)
        $g.FillEllipse($cyan,7,7,34,34)
        $g.FillEllipse($background,12,12,24,24)
        $g.FillRectangle($white,24,14,9,3)
        $g.FillRectangle($white,30,15,3,18)
        $g.FillEllipse($white,22,29,11,6)
    }
    $bitmap.Save($output,[System.Drawing.Imaging.ImageFormat]::Png)
    $background.Dispose(); $surface.Dispose(); $cyan.Dispose(); $white.Dispose()
    $g.Dispose(); $bitmap.Dispose()
}
New-Canvas 48 48 (Join-Path $assetDir 'icon.png') $false
New-Canvas 256 128 (Join-Path $assetDir 'banner.png') $true

# Quiet, one second banner tone; no external recording or copyrighted audio.
$rate=8000; $frames=8000
$wav=Join-Path $assetDir 'banner.wav'
$stream=[System.IO.File]::Create($wav)
$writer=[System.IO.BinaryWriter]::new($stream)
$writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
$writer.Write([int](36+$frames*2))
$writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
$writer.Write([int]16); $writer.Write([int16]1); $writer.Write([int16]1)
$writer.Write([int]$rate); $writer.Write([int]($rate*2))
$writer.Write([int16]2); $writer.Write([int16]16)
$writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
$writer.Write([int]($frames*2))
for ($i=0; $i -lt $frames; $i++) {
    $envelope=[Math]::Exp(-$i/1400.0)
    $sample=[int16]([Math]::Round(1700.0*$envelope*[Math]::Sin(2.0*[Math]::PI*440.0*$i/$rate)))
    $writer.Write($sample)
}
$writer.Dispose(); $stream.Dispose()
