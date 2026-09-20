<#
.SYNOPSIS
Render contact sheets from the actual packaged ICO files at native pixel sizes.
.NOTES
Copyright (C) 2026 iceman50
#>
[CmdletBinding()]
param([string]$PackDirectory, [string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(!$PackDirectory) { $PackDirectory = Join-Path $repoRoot 'IconPacks' }
if(!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'docs/icon-packs' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$themes = @('Dark', 'Aurora', 'Copper', 'Paper', 'Neon-Circuit')
$backgrounds = @('#20252d', '#171d2c', '#292422', '#f5f7fa', '#101522')
$subtitles = @('Cool slate / balanced duotone', 'Teal & violet / richer color', 'Warm metal / matte surfaces', 'Crisp ink / light surfaces', 'Electric cyan / luminous accents')
$font = [Drawing.Font]::new('Segoe UI', 9, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
$label = [Drawing.Font]::new('Segoe UI', 12, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
$title = [Drawing.Font]::new('Segoe UI Semibold', 30, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
$smallTitle = [Drawing.Font]::new('Segoe UI Semibold', 14, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)

function Draw-PackIcon($Graphics, $Archive, [string]$Name, [int]$Size, [int]$X, [int]$Y) {
    $entry = $Archive.GetEntry("icons/$Name.ico")
    if(!$entry) { throw "Missing $Name" }
    $inputStream = $entry.Open()
    $memory = [IO.MemoryStream]::new()
    try {
        $inputStream.CopyTo($memory)
        $memory.Position = 0
        $icon = [Drawing.Icon]::new($memory, $Size, $Size)
        try { $Graphics.DrawIcon($icon, [Drawing.Rectangle]::new($X, $Y, $Size, $Size)) } finally { $icon.Dispose() }
    } finally { $inputStream.Dispose(); $memory.Dispose() }
}

$overview = [Drawing.Bitmap]::new(($themes.Count * 360), 840)
$og = [Drawing.Graphics]::FromImage($overview)
try {
    for($t = 0; $t -lt $themes.Count; $t++) {
        $theme = $themes[$t]
        $displayName = $theme.Replace('-', ' ')
        $bg = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($backgrounds[$t]))
        $fg = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($(if($theme -eq 'Paper') { '#344255' } else { '#e0e7ef' })))
        $muted = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($(if($theme -eq 'Paper') { '#687586' } else { '#a3aebc' })))
        $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $PackDirectory "$theme.dcico"))
        try {
            $names = @($zip.Entries | Where-Object FullName -Like 'icons/*.ico' | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_.FullName) } | Sort-Object)
            $sheet = [Drawing.Bitmap]::new(1260, 1140)
            $g = [Drawing.Graphics]::FromImage($sheet)
            try {
                $g.Clear($bg.Color)
                $g.DrawString("$displayName / DC++", $title, $fg, 28, 22)
                $g.DrawString('86 icons  /  each cell: 16, 24 and 32 px  /  actual packaged frames', $label, $muted, 30, 66)
                for($i = 0; $i -lt $names.Count; $i++) {
                    $x = 30 + ($i % 7) * 176
                    $y = 112 + [Math]::Floor($i / 7) * 78
                    Draw-PackIcon $g $zip $names[$i] 16 $x ($y + 8)
                    Draw-PackIcon $g $zip $names[$i] 24 ($x + 32) ($y + 4)
                    Draw-PackIcon $g $zip $names[$i] 32 ($x + 76) $y
                    $g.DrawString($names[$i], $label, $muted, $x, ($y + 39))
                }
                $sheet.Save((Join-Path $OutputDirectory "$theme.png"), [Drawing.Imaging.ImageFormat]::Png)
            } finally { $g.Dispose(); $sheet.Dispose() }

            $left = $t * 360
            $og.FillRectangle($bg, $left, 0, 360, 840)
            $og.DrawString('DC++  /  ICON COLLECTION', $font, $muted, ($left + 28), 28)
            $og.DrawString($displayName, $title, $fg, ($left + 26), 54)
            $og.DrawString($subtitles[$t], $label, $muted, ($left + 28), 100)
            $featured = @('PublicHubs','Search','FavoriteHubs','Directory','Download','Upload','Settings','Trusted','Chat','UserOn','Queue','Plugins','Notepad','NetStats','Secure','Notifications')
            for($i = 0; $i -lt $featured.Count; $i++) {
                $x = $left + 34 + ($i % 4) * 78
                $y = 158 + [Math]::Floor($i / 4) * 92
                Draw-PackIcon $og $zip $featured[$i] 40 $x $y
                $og.DrawString($featured[$i], $font, $muted, ($x - 7), ($y + 50))
            }
            $og.DrawString('TOOLBAR / 24 PX', $smallTitle, $fg, ($left + 28), 554)
            $toolbar = @('PublicHubs','Search','FavoriteHubs','Queue','Download','Upload','Settings','Refresh')
            for($i = 0; $i -lt $toolbar.Count; $i++) { Draw-PackIcon $og $zip $toolbar[$i] 24 ($left + 29 + $i * 38) 590 }
            $og.DrawString('PRESENCE / 16 PX', $smallTitle, $fg, ($left + 28), 650)
            $presence = @('UserOn','UserAway','UserOff','UserNoCon','UserNoSlot','UserOp','UserReg','UserBot')
            for($i = 0; $i -lt $presence.Count; $i++) {
                if($presence[$i] -in @('UserNoCon','UserNoSlot','UserOp','UserReg','UserBot')) {
                    Draw-PackIcon $og $zip 'User' 16 ($left + 33 + $i * 38) 688
                }
                Draw-PackIcon $og $zip $presence[$i] 16 ($left + 33 + $i * 38) 688
            }
            $og.DrawString('86 icons  /  11 interface sizes  /  16-256 px', $label, $muted, ($left + 28), 765)
            $og.DrawString($(if($theme -eq 'Paper') { 'Designed for light appearance' } else { 'Designed for dark appearance' }), $label, $muted, ($left + 28), 786)
        } finally { $zip.Dispose(); $bg.Dispose(); $fg.Dispose(); $muted.Dispose() }
    }
    $overview.Save((Join-Path $OutputDirectory 'Collection.png'), [Drawing.Imaging.ImageFormat]::Png)
} finally { $og.Dispose(); $overview.Dispose(); $font.Dispose(); $label.Dispose(); $title.Dispose(); $smallTitle.Dispose() }
Write-Host "Saved packaged-icon previews in $OutputDirectory"
