<#
.SYNOPSIS
Preview the five packaged orb logos at native Windows icon sizes.
.NOTES
Copyright (C) 2026 iceman50
#>
[CmdletBinding()]
param([string]$PackDirectory, [string]$OutputPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(!$PackDirectory) { $PackDirectory = Join-Path $root 'IconPacks' }
if(!$OutputPath) { $OutputPath = Join-Path $root 'docs/icon-packs/Orb-Logos.png' }
$packs = @('Dark','Aurora','Copper','Paper','Neon-Circuit')
$colors = @('#20252d','#171d2c','#292422','#f5f7fa','#101522')
$labels = @('Soft blue','Teal','Warm copper','Deep ink blue','Electric blue')
$sheet = [Drawing.Bitmap]::new(1280,428)
$g = [Drawing.Graphics]::FromImage($sheet)
$font = [Drawing.Font]::new('Segoe UI',13,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
$title = [Drawing.Font]::new('Segoe UI Semibold',22,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
try {
    for($i=0; $i -lt $packs.Count; $i++) {
        $left = $i*256
        $foreground = if($packs[$i] -eq 'Paper') { [Drawing.Brushes]::DarkSlateGray } else { [Drawing.Brushes]::LightGray }
        $brush = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($colors[$i]))
        try { $g.FillRectangle($brush,$left,0,256,428) } finally { $brush.Dispose() }
        $g.DrawString($packs[$i].Replace('-',' '),$title,$foreground,($left+22),18)
        $g.DrawString($labels[$i],$font,$foreground,($left+22),51)
        $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $PackDirectory ($packs[$i]+'.dcico')))
        $memory = [IO.MemoryStream]::new()
        try {
            $inputStream = $zip.GetEntry('icons/DCPlusPlus.ico').Open()
            try { $inputStream.CopyTo($memory) } finally { $inputStream.Dispose() }
            $memory.Position=0
            $large=[Drawing.Icon]::new($memory,128,128)
            try { $g.DrawIcon($large,[Drawing.Rectangle]::new(($left+64),90,128,128)) } finally { $large.Dispose() }
            $g.DrawString('128 px',$font,$foreground,($left+108),228)
            $x=$left+20
            foreach($size in @(16,24,32,48,64)) {
                $memory.Position=0
                $icon=[Drawing.Icon]::new($memory,$size,$size)
                try { $g.DrawIcon($icon,[Drawing.Rectangle]::new($x,(285+(64-$size)/2),$size,$size)) } finally { $icon.Dispose() }
                $g.DrawString("$size",$font,$foreground,$x,365)
                $x+=$size+8
            }
            $g.DrawString('Native toolbar / taskbar sizes',$font,$foreground,($left+22),401)
        } finally { $memory.Dispose(); $zip.Dispose() }
    }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))) | Out-Null
    $sheet.Save([IO.Path]::GetFullPath($OutputPath),[Drawing.Imaging.ImageFormat]::Png)
} finally { $g.Dispose(); $sheet.Dispose(); $font.Dispose(); $title.Dispose() }
Write-Host "Saved packaged orb previews: $OutputPath"
