<#
.SYNOPSIS
Show reconnect, recent windows and refresh file list at native sizes in all packs.
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
if(!$OutputPath) { $OutputPath = Join-Path $root 'docs/icon-packs/Window-Actions.png' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))) | Out-Null
$packs = @('Dark','Aurora','Copper','Paper','Neon-Circuit')
$backgrounds = @('#20252d','#171d2c','#292422','#f5f7fa','#101522')
$names = @('Reconnect','Recents','Refresh')
$labels = @('Reconnect','Recent windows','Refresh file list')
$sizes = @(16,24,32,48,64)
$positions = @(0,38,82,132,196)
$sheet = [Drawing.Bitmap]::new(1080,750)
$g = [Drawing.Graphics]::FromImage($sheet)
$title = [Drawing.Font]::new('Segoe UI Semibold',24,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
$font = [Drawing.Font]::new('Segoe UI',13,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
$small = [Drawing.Font]::new('Segoe UI',10,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
try {
    $g.Clear([Drawing.ColorTranslator]::FromHtml('#101522'))
    $g.DrawString('DC++ / window and file-list actions', $title, [Drawing.Brushes]::White, 24, 18)
    for($col=0; $col -lt 3; $col++) { $g.DrawString($labels[$col], $font, [Drawing.Brushes]::LightGray, (150+$col*305), 72) }
    for($row=0; $row -lt $packs.Count; $row++) {
        $top = 110+$row*128
        $bg = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($backgrounds[$row]))
        $fg = if($packs[$row] -eq 'Paper') { [Drawing.Brushes]::DarkSlateGray } else { [Drawing.Brushes]::LightGray }
        $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $PackDirectory ($packs[$row]+'.dcico')))
        try {
            $g.FillRectangle($bg,0,$top,1080,128)
            $g.DrawString($packs[$row].Replace('-',' '),$font,$fg,20,($top+44))
            for($col=0; $col -lt 3; $col++) {
                $stream = $zip.GetEntry('icons/'+$names[$col]+'.ico').Open()
                $memory = [IO.MemoryStream]::new()
                try {
                    $stream.CopyTo($memory)
                    for($i=0; $i -lt $sizes.Count; $i++) {
                        $size = $sizes[$i]
                        $x = 150+$col*305+$positions[$i]
                        $y = $top+22+(64-$size)/2
                        $memory.Position = 0
                        $icon = [Drawing.Icon]::new($memory,$size,$size)
                        try { $g.DrawIcon($icon,[Drawing.Rectangle]::new($x,$y,$size,$size)) } finally { $icon.Dispose() }
                        $g.DrawString([string]$size,$small,$fg,$x,($top+96))
                    }
                } finally { $stream.Dispose(); $memory.Dispose() }
            }
        } finally { $bg.Dispose(); $zip.Dispose() }
    }
    $sheet.Save([IO.Path]::GetFullPath($OutputPath),[Drawing.Imaging.ImageFormat]::Png)
} finally { $g.Dispose(); $sheet.Dispose(); $title.Dispose(); $font.Dispose(); $small.Dispose() }
Write-Host "Saved native-size action preview: $OutputPath"
