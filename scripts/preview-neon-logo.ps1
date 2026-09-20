<#
.SYNOPSIS
Preview the packaged Neon Circuit application logo on dark and light surfaces.
.NOTES
Copyright (C) 2026 iceman50
#>
[CmdletBinding()]
param([string]$PackPath, [string]$OutputPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(!$PackPath) { $PackPath = Join-Path $root 'IconPacks/Neon-Circuit.dcico' }
if(!$OutputPath) { $OutputPath = Join-Path $root 'docs/icon-packs/Neon-Logo.png' }
$zip = [IO.Compression.ZipFile]::OpenRead($PackPath)
$memory = [IO.MemoryStream]::new()
$inputStream = $zip.GetEntry('icons/DCPlusPlus.ico').Open()
try { $inputStream.CopyTo($memory) } finally { $inputStream.Dispose(); $zip.Dispose() }
$sheet = [Drawing.Bitmap]::new(960,420)
$g = [Drawing.Graphics]::FromImage($sheet)
$font = [Drawing.Font]::new('Segoe UI',13,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
$title = [Drawing.Font]::new('Segoe UI Semibold',24,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
try {
    foreach($side in @(0,1)) {
        $background = if($side -eq 0) { [Drawing.ColorTranslator]::FromHtml('#101522') } else { [Drawing.ColorTranslator]::FromHtml('#f5f7fa') }
        $foreground = if($side -eq 0) { [Drawing.Brushes]::LightGray } else { [Drawing.Brushes]::DarkSlateGray }
        $brush = [Drawing.SolidBrush]::new($background)
        try { $g.FillRectangle($brush,($side*480),0,480,420) } finally { $brush.Dispose() }
        $g.DrawString('Neon Circuit / electric blue',$title,$foreground,($side*480+24),18)
        $x = $side*480+36
        foreach($size in @(16,24,32,48,64)) {
            $memory.Position=0
            $icon=[Drawing.Icon]::new($memory,$size,$size)
            try { $g.DrawIcon($icon,[Drawing.Rectangle]::new($x,(305+(64-$size)/2),$size,$size)) } finally { $icon.Dispose() }
            $g.DrawString("$size px",$font,$foreground,$x,383)
            $x+=$size+33
        }
        $memory.Position=0
        $large=[Drawing.Icon]::new($memory,128,128)
        try { $g.DrawIcon($large,[Drawing.Rectangle]::new(($side*480+144),80,192,192)) } finally { $large.Dispose() }
    }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))) | Out-Null
    $sheet.Save([IO.Path]::GetFullPath($OutputPath),[Drawing.Imaging.ImageFormat]::Png)
} finally { $g.Dispose(); $sheet.Dispose(); $font.Dispose(); $title.Dispose(); $memory.Dispose() }
Write-Host "Saved packaged-logo preview: $OutputPath"
