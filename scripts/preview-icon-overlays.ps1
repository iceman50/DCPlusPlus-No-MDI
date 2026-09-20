<#
.SYNOPSIS
Preview corner overlays alone and composited at 16, 32 and 48 pixels.
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
if(!$OutputPath) { $OutputPath = Join-Path $root 'docs/icon-packs/User-Overlays.png' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))) | Out-Null
$packs = @('Dark','Aurora','Copper','Paper','Neon-Circuit')
$backgrounds = @('#20252d','#171d2c','#292422','#f5f7fa','#101522')
$examples = @(
    @{ Label='Bot overlay'; Icons=@('UserBot') },
    @{ Label='User + bot'; Icons=@('User','UserBot') },
    @{ Label='Bot + operator'; Icons=@('User','UserBot','UserOp') },
    @{ Label='Away + blocked'; Icons=@('UserAway','UserNoCon') },
    @{ Label='Hub + operator'; Icons=@('HubOn','UserOp') },
    @{ Label='Hub + registered'; Icons=@('HubOn','UserReg') }
)
$sheet = [Drawing.Bitmap]::new(1230,750)
$g = [Drawing.Graphics]::FromImage($sheet)
$title = [Drawing.Font]::new('Segoe UI Semibold',24,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
$font = [Drawing.Font]::new('Segoe UI',12,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
try {
    $g.Clear([Drawing.ColorTranslator]::FromHtml('#101522'))
    $g.DrawString('DC++ / overlays at 16, 32 and 48 px', $title, [Drawing.Brushes]::White, 24, 18)
    for($col=0; $col -lt $examples.Count; $col++) { $g.DrawString($examples[$col].Label,$font,[Drawing.Brushes]::LightGray,(145+$col*180),76) }
    for($row=0; $row -lt $packs.Count; $row++) {
        $top=110+$row*128
        $bg=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($backgrounds[$row]))
        $fg=if($packs[$row] -eq 'Paper') { [Drawing.Brushes]::DarkSlateGray } else { [Drawing.Brushes]::LightGray }
        $zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $PackDirectory ($packs[$row]+'.dcico')))
        try {
            $g.FillRectangle($bg,0,$top,1230,128)
            $g.DrawString($packs[$row].Replace('-',' '),$font,$fg,20,($top+44))
            for($col=0; $col -lt $examples.Count; $col++) {
                $x=145+$col*180
                foreach($size in @(16,32,48)) {
                    foreach($name in $examples[$col].Icons) {
                        $stream=$zip.GetEntry('icons/'+$name+'.ico').Open()
                        $memory=[IO.MemoryStream]::new()
                        try {
                            $stream.CopyTo($memory); $memory.Position=0
                            $icon=[Drawing.Icon]::new($memory,$size,$size)
                            try { $g.DrawIcon($icon,[Drawing.Rectangle]::new($x,($top+24+(48-$size)/2),$size,$size)) } finally { $icon.Dispose() }
                        } finally { $stream.Dispose(); $memory.Dispose() }
                    }
                    $g.DrawString([string]$size,$font,$fg,$x,($top+89))
                    $x+=$size+16
                }
            }
        } finally { $zip.Dispose(); $bg.Dispose() }
    }
    $sheet.Save([IO.Path]::GetFullPath($OutputPath),[Drawing.Imaging.ImageFormat]::Png)
} finally { $g.Dispose(); $sheet.Dispose(); $title.Dispose(); $font.Dispose() }
Write-Host "Saved composed overlays: $OutputPath"
