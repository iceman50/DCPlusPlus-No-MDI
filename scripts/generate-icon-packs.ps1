<#
.SYNOPSIS
Build the Dark, Aurora, Copper, Paper and Neon Circuit DC++ icon packs.
.DESCRIPTION
Uses the original artwork in icon-packs/*.cs. Requires Windows
PowerShell 5.1 and System.Drawing; no downloaded fonts or graphics tools.
Each pack uses matching ring-free three-orb artwork in icon-packs/artwork.
All icons have eleven transparent 32-bit frames from 16 through 256 px.
Interface glyphs are optically sized; application logos use filtered PNG masters.
ZIP member order, timestamps and metadata are fixed for reproducible builds.
.NOTES
Copyright (C) 2026 iceman50
#>
[CmdletBinding()]
param(
    [ValidateSet('Dark', 'Aurora', 'Copper', 'Paper', 'Neon-Circuit')]
    [string[]]$Pack = @('Dark', 'Aurora', 'Copper', 'Paper', 'Neon-Circuit'),
    [string]$SourceDirectory,
    [string]$OutputDirectory,
    # For callers that need a single pack at a custom filename.
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
if($null -eq ('IconPackRenderer' -as [type])) {
    $sources = @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'icon-packs') -Filter '*.cs' | Sort-Object Name | Select-Object -ExpandProperty FullName)
    Add-Type -Path $sources -ReferencedAssemblies System.Drawing.dll
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(!$SourceDirectory) { $SourceDirectory = Join-Path $repoRoot 'res' }
if(!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'IconPacks' }
if($OutputPath -and $Pack.Count -ne 1) { throw 'OutputPath requires exactly one pack.' }
$icons = @(Get-ChildItem -LiteralPath $SourceDirectory -Filter '*.ico' -File | Sort-Object Name)
if($icons.Count -eq 0) { throw "No source icons in $SourceDirectory" }
$utf8 = [Text.UTF8Encoding]::new($false)

function Add-MemberBytes($Archive, [string]$Name, [byte[]]$Bytes) {
    $entry = $Archive.CreateEntry($Name, [IO.Compression.CompressionLevel]::Optimal)
    $entry.LastWriteTime = [DateTimeOffset]::new(2026, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
    $stream = $entry.Open()
    try { $stream.Write($Bytes, 0, $Bytes.Length) } finally { $stream.Dispose() }
}

foreach($theme in $Pack) {
    $target = if($OutputPath) { [IO.Path]::GetFullPath($OutputPath) } else { [IO.Path]::GetFullPath((Join-Path $OutputDirectory "$theme.dcico")) }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    $scheme = if($theme -eq 'Paper') { 'light' } else { 'dark' }
    $version = if($theme -in @('Dark', 'Neon-Circuit')) { '2.0' } else { '1.0' }
    $displayName = $theme.Replace('-', ' ')
    $manifest = [Text.StringBuilder]::new()
    [void]$manifest.Append("<?xml version=`"1.0`" encoding=`"utf-8`"?>`n<!-- Copyright (C) 2026 iceman50 -->`n")
    [void]$manifest.Append("<dcico><Name>$displayName</Name><Version>$version</Version><Scheme>$scheme</Scheme><Icons>`n")
    foreach($icon in $icons) {
        $name = [Security.SecurityElement]::Escape($icon.Name)
        [void]$manifest.Append("  <Icon Name=`"$name`" File=`"icons/$name`"/>`n")
    }
    [void]$manifest.Append('</Icons></dcico>')
    $temporary = $target + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        $file = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try {
            $zip = [IO.Compression.ZipArchive]::new($file, [IO.Compression.ZipArchiveMode]::Create, $true, $utf8)
            try {
                Add-MemberBytes $zip 'info.xml' $utf8.GetBytes($manifest.ToString())
                $orbColor = @{ Dark = 'soft blue'; Aurora = 'teal'; Copper = 'warm copper'; Paper = 'deep ink blue'; 'Neon-Circuit' = 'electric blue' }[$theme]
                $branding = "three-orb motif enlarged, without the ring, in $orbColor."
                $readme = "$theme DC++ icon pack $version`r`nCopyright (C) 2026 iceman50`r`nOriginal interface artwork; $branding`r`nDesigned for $scheme surfaces. Select in Settings > Appearance > Icons.`r`nRegenerate with scripts/generate-icon-packs.ps1 -Pack $theme.`r`nLicensed under the repository's GNU GPL version 3 or later.`r`n"
                Add-MemberBytes $zip 'README.txt' $utf8.GetBytes($readme)
                foreach($icon in $icons) {
                    $bytes = if($icon.Name -eq 'DCPlusPlus.ico') {
                        $artworkName = if($theme -eq 'Neon-Circuit') { 'Neon' } else { $theme }
                        [OrbApplicationIcon]::Create((Join-Path $PSScriptRoot "icon-packs/artwork/$artworkName-DCPlusPlus.png"))
                    }
                        else { [IconPackRenderer]::CreateIcon($icon.Name, $theme) }
                    Add-MemberBytes $zip ('icons/' + $icon.Name) $bytes
                }
            } finally { $zip.Dispose() }
        } finally { $file.Dispose() }
        Move-Item -LiteralPath $temporary -Destination $target -Force
    } finally {
        if(Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
    Write-Host "Generated $theme : $($icons.Count) icons in $target"
}
