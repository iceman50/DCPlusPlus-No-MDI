<#
.SYNOPSIS
Build the x64 MinGW release NSIS installer.

.DESCRIPTION
Stages the distributable payload, generates the English NSIS string catalog,
and compiles installer/DCPlusPlus.nsi. The installed DCPlusPlus.exe is copied
from DCPlusPlus-stripped.exe and is accompanied by its matching PDB, the BFE
changelog, legal notices, flat Themes, flat Emoticons, and bundled icon packs.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\installer-build.ps1

.NOTES
Copyright (C) 2026 iceman50
#>

[CmdletBinding()]
param(
	[string]$ReleaseDirectory,

	[string]$OutputDirectory,

	[string]$MakensisPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-RequiredFile {
	param(
		[string]$Path,
		[string]$Description
	)

	if(-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "$Description was not found: $Path"
	}
}

function Assert-PathUnderRoot {
	param(
		[string]$Path,
		[string]$Root
	)

	$fullPath = [System.IO.Path]::GetFullPath($Path)
	$fullRoot = [System.IO.Path]::GetFullPath($Root)
	if(-not $fullRoot.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
		$fullRoot += [System.IO.Path]::DirectorySeparatorChar
	}

	if(-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to remove path outside the repository. Path: $fullPath Root: $fullRoot"
	}
}

function Resolve-Makensis {
	param([string]$RequestedPath)

	if(-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
		Assert-RequiredFile -Path $RequestedPath -Description "NSIS compiler"
		return (Resolve-Path -LiteralPath $RequestedPath).Path
	}

	$command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
	if($null -ne $command) {
		return $command.Source
	}

	$standardPath = Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
	Assert-RequiredFile -Path $standardPath -Description "NSIS compiler"
	return $standardPath
}

function Write-EnglishCatalog {
	param(
		[string]$StringsPath,
		[string]$StageDirectory
	)

	[xml]$strings = Get-Content -LiteralPath $StringsPath -Raw
	$encoding = [System.Text.UnicodeEncoding]::new($false, $true)
	$i18n = @(
		"; Copyright (C) 2026 iceman50",
		'!insertmacro MUI_LANGUAGE "English"',
		'!insertmacro LANGFILE_INCLUDE "_English.nsh"'
	) -join "`r`n"
	[System.IO.File]::WriteAllText((Join-Path $StageDirectory "i18n.nsh"), "$i18n`r`n", $encoding)

	$catalog = [System.Collections.Generic.List[string]]::new()
	$catalog.Add("; Copyright (C) 2026 iceman50")
	$catalog.Add('!insertmacro LANGFILE_EXT "English"')
	foreach($entry in $strings.Strings.ChildNodes) {
		if($entry.NodeType -ne [System.Xml.XmlNodeType]::Element) {
			continue
		}

		$value = $entry.InnerText.Replace('"', '$\"')
		$catalog.Add(('${{LangFileString}} {0} "{1}"' -f $entry.Name, $value))
	}
	[System.IO.File]::WriteAllText((Join-Path $StageDirectory "_English.nsh"), (($catalog -join "`r`n") + "`r`n"), $encoding)
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
	$ReleaseDirectory = Join-Path $repoRoot "build\release-mingw-x64\bin"
} elseif(-not [System.IO.Path]::IsPathRooted($ReleaseDirectory)) {
	$ReleaseDirectory = Join-Path $repoRoot $ReleaseDirectory
}
$ReleaseDirectory = [System.IO.Path]::GetFullPath($ReleaseDirectory)

if([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Join-Path $repoRoot "dist"
} elseif(-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
	$OutputDirectory = Join-Path $repoRoot $OutputDirectory
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

$makensis = Resolve-Makensis -RequestedPath $MakensisPath
$stageDirectory = Join-Path $repoRoot "build\installer-release"
$strippedExecutable = Join-Path $ReleaseDirectory "DCPlusPlus-stripped.exe"
$debugSymbols = Join-Path $ReleaseDirectory "DCPlusPlus.pdb"
$themesDirectory = Join-Path $repoRoot "Themes"
$emoticonsDirectory = Join-Path $repoRoot "Emoticons"
$iconPacksDirectory = Join-Path $repoRoot "IconPacks"

Assert-RequiredFile -Path $strippedExecutable -Description "Stripped release executable"
Assert-RequiredFile -Path $debugSymbols -Description "Release PDB"
Assert-RequiredFile -Path (Join-Path $repoRoot "changelog-bfe.txt") -Description "BFE changelog"
if(-not (Test-Path -LiteralPath $themesDirectory -PathType Container)) {
	throw "Themes directory was not found: $themesDirectory"
}
if(-not (Test-Path -LiteralPath $emoticonsDirectory -PathType Container)) {
	throw "Emoticons directory was not found: $emoticonsDirectory"
}
if(-not (Test-Path -LiteralPath $iconPacksDirectory -PathType Container)) {
	throw "IconPacks directory was not found: $iconPacksDirectory"
}
Assert-RequiredFile -Path (Join-Path $iconPacksDirectory "Dark.dcico") -Description "Bundled dark icon package"
Assert-RequiredFile -Path (Join-Path $iconPacksDirectory "Neon-Circuit.dcico") -Description "Bundled Neon Circuit icon package"
foreach($packName in @("Aurora", "Copper", "Paper")) {
	Assert-RequiredFile -Path (Join-Path $iconPacksDirectory "$packName.dcico") -Description "Bundled $packName icon package"
}
if(Test-Path -LiteralPath (Join-Path $themesDirectory "Bundled")) {
	throw "Themes must be stored directly in Themes; Bundled is not allowed."
}
if(Test-Path -LiteralPath (Join-Path $emoticonsDirectory "Bundled")) {
	throw "Emoticons must be stored directly in Emoticons; Bundled is not allowed."
}

if(Test-Path -LiteralPath $stageDirectory) {
	Assert-PathUnderRoot -Path $stageDirectory -Root $repoRoot
	Remove-Item -LiteralPath $stageDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$installerFiles = @("DCPlusPlus.nsi", "Install.ico", "Uninstall.ico", "Strings.xml")
foreach($file in $installerFiles) {
	Copy-Item -LiteralPath (Join-Path $repoRoot "installer\$file") -Destination $stageDirectory -Force
}

Copy-Item -LiteralPath $strippedExecutable -Destination (Join-Path $stageDirectory "DCPlusPlus.exe") -Force
Copy-Item -LiteralPath $debugSymbols -Destination (Join-Path $stageDirectory "DCPlusPlus.pdb") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "changelog-bfe.txt") -Destination $stageDirectory -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "dcppboot.xml") -Destination $stageDirectory -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "dcppboot.nonlocal.xml") -Destination $stageDirectory -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "License.txt") -Destination $stageDirectory -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "ThirdPartyLicenses.txt") -Destination $stageDirectory -Force
Copy-Item -LiteralPath $themesDirectory -Destination (Join-Path $stageDirectory "Themes") -Recurse -Force
Copy-Item -LiteralPath $emoticonsDirectory -Destination (Join-Path $stageDirectory "Emoticons") -Recurse -Force
Copy-Item -LiteralPath $iconPacksDirectory -Destination (Join-Path $stageDirectory "IconPacks") -Recurse -Force
Write-EnglishCatalog -StringsPath (Join-Path $stageDirectory "Strings.xml") -StageDirectory $stageDirectory

Push-Location -LiteralPath $stageDirectory
try {
	& $makensis /V4 "DCPlusPlus.nsi"
	if($LASTEXITCODE -ne 0) {
		throw "NSIS failed with exit code $LASTEXITCODE."
	}
} finally {
	Pop-Location
}

$rawInstaller = Join-Path $stageDirectory "DCPlusPlus-xxx.exe"
Assert-RequiredFile -Path $rawInstaller -Description "Compiled installer"
$versionLine = Select-String -LiteralPath (Join-Path $repoRoot "dcpp\version.h") -Pattern '^\s*#define\s+VERSIONSTRING\s+"([^"]+)"' | Select-Object -First 1
if($null -eq $versionLine) {
	throw "Unable to determine the application version."
}
$version = $versionLine.Matches[0].Groups[1].Value
$revision = (& git -C $repoRoot rev-parse --short HEAD).Trim()
if($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
	throw "Unable to determine the Git revision."
}
$timestamp = [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss'Z'", [System.Globalization.CultureInfo]::InvariantCulture)
$installerName = "DCPlusPlus-Experimental-$version-MinGW-w64-x64-Release-MSVCRT-$revision-$timestamp-Setup.exe"
$installerPath = Join-Path $OutputDirectory $installerName
Copy-Item -LiteralPath $rawInstaller -Destination $installerPath -Force

Write-Host "Created NSIS installer:"
Write-Host "  $installerPath"
Write-Output $installerPath
