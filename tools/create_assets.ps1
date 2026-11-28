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
        $shield = [System.Drawing.Drawing2D.GraphicsPath]::new()
        $points = [System.Drawing.PointF[]]@(
            [System.Drawing.PointF]::new(64,18),
            [System.Drawing.PointF]::new(99,31),
            [System.Drawing.PointF]::new(96,67),
            [System.Drawing.PointF]::new(64,103),
            [System.Drawing.PointF]::new(32,67),
            [System.Drawing.PointF]::new(29,31))
        $shield.AddPolygon($points)
        $g.FillPath($cyan,$shield)
        $shield.Dispose()
        $g.FillRectangle($background,61,38,7,50)
        $g.FillRectangle($background,48,44,18,7)
        $g.FillRectangle($background,64,68,20,6)
        $g.FillEllipse($background,79,46,10,10)
        $g.FillRectangle($background,82,54,4,18)
        $font1 = [System.Drawing.Font]::new('Segoe UI',20,[System.Drawing.FontStyle]::Bold)
        $font2 = [System.Drawing.Font]::new('Segoe UI',11,[System.Drawing.FontStyle]::Regular)
        $g.DrawString('SHIELD',$font1,$white,103,27)
        $g.DrawString('ONLINE ARCHIVE',$font2,$cyan,106,73)
        $font1.Dispose(); $font2.Dispose()
    } else {
        $g.FillEllipse($surface,2,2,44,44)
        $shield = [System.Drawing.Drawing2D.GraphicsPath]::new()
        $points = [System.Drawing.PointF[]]@(
            [System.Drawing.PointF]::new(24,5),
            [System.Drawing.PointF]::new(42,12),
            [System.Drawing.PointF]::new(40,29),
            [System.Drawing.PointF]::new(24,44),
            [System.Drawing.PointF]::new(8,29),
            [System.Drawing.PointF]::new(6,12))
        $shield.AddPolygon($points)
        $g.FillPath($cyan,$shield)
        $shield.Dispose()
        $g.FillRectangle($background,22,14,5,23)
        $g.FillRectangle($background,14,16,11,5)
        $g.FillRectangle($background,25,27,10,4)
        $g.FillEllipse($background,31,17,6,6)
        $g.FillRectangle($background,33,21,3,8)
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
