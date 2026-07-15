<#
.SYNOPSIS
Build and package x64 MinGW DC++ debug/release artifacts.

.DESCRIPTION
Interactive helper for producing a small diagnostic distribution zip. The zip
contains the unstripped executable, its GNU debug companion PDB, and
changelog-bfe.txt.

Package names use:
DCPlusPlus-Experimental-VERSION-COMPILER-[Release|Debug]-MSVCRT-gitrev.zip

By default the script prompts for the build configuration and writes packages
to the root-level dist directory. It can also be used non-interactively:

  powershell -ExecutionPolicy Bypass -File scripts\dist-build.ps1 -Configuration Both
#>

[CmdletBinding()]
param(
	[ValidateSet("Debug", "Release", "Both")]
	[string]$Configuration,

	[string]$OutputDirectory,

	[string]$SConsCommand = "scons",

	[string]$CompilerLabel,

	[switch]$NoBuild,

	[switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
	return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

function Read-Configuration {
	Write-Host ""
	Write-Host "Select x64 MinGW build configuration:"
	Write-Host "  1. Debug"
	Write-Host "  2. Release"
	Write-Host "  3. Both"

	while($true) {
		$choice = Read-Host "Build configuration [3]"
		if([string]::IsNullOrWhiteSpace($choice)) {
			return "Both"
		}

		switch($choice.Trim()) {
			"1" { return "Debug" }
			"2" { return "Release" }
			"3" { return "Both" }
			default { Write-Host "Please enter 1, 2, or 3." }
		}
	}
}

function Read-YesNo {
	param(
		[string]$Prompt,
		[bool]$DefaultYes
	)

	if($DefaultYes) {
		$suffix = "[Y/n]"
	} else {
		$suffix = "[y/N]"
	}

	while($true) {
		$answer = Read-Host "$Prompt $suffix"
		if([string]::IsNullOrWhiteSpace($answer)) {
			return $DefaultYes
		}

		switch($answer.Trim().ToLowerInvariant()) {
			"y" { return $true }
			"yes" { return $true }
			"n" { return $false }
			"no" { return $false }
			default { Write-Host "Please answer yes or no." }
		}
	}
}

function Get-ModeList {
	param(
		[string]$SelectedConfiguration
	)

	switch($SelectedConfiguration.ToLowerInvariant()) {
		"debug" { return @("debug") }
		"release" { return @("release") }
		"both" { return @("debug", "release") }
		default { throw "Unknown configuration: $SelectedConfiguration" }
	}
}

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
		throw "Refusing to remove path outside expected root. Path: $fullPath Root: $fullRoot"
	}
}

function Resolve-OutputDirectory {
	param(
		[string]$Path,
		[string]$RepoRoot
	)

	if([System.IO.Path]::IsPathRooted($Path)) {
		return [System.IO.Path]::GetFullPath($Path)
	}

	return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Get-VersionNumber {
	param(
		[string]$RepoRoot
	)

	$versionHeader = Join-Path $RepoRoot "dcpp\version.h"
	$versionLine = Select-String -LiteralPath $versionHeader -Pattern '^\s*#define\s+VERSIONSTRING\s+"([^"]+)"' | Select-Object -First 1
	if($null -eq $versionLine) {
		throw "Unable to find VERSIONSTRING in $versionHeader"
	}

	return $versionLine.Matches[0].Groups[1].Value
}

function Get-GitRevision {
	param(
		[string]$RepoRoot
	)

	Push-Location -LiteralPath $RepoRoot
	try {
		$revision = (& git rev-parse --short HEAD 2>$null)
		if($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
			throw "git rev-parse failed"
		}
		return $revision.Trim()
	} catch {
		$revisionFile = Join-Path $RepoRoot "dcpp\version-rev-id.inc"
		$revisionLine = Select-String -LiteralPath $revisionFile -Pattern '\[([0-9a-fA-F]+)\]' | Select-Object -First 1
		if($null -eq $revisionLine) {
			throw "Unable to determine Git revision from Git or $revisionFile"
		}
		return $revisionLine.Matches[0].Groups[1].Value
	} finally {
		Pop-Location
	}
}

function ConvertTo-PackageToken {
	param(
		[string]$Value
	)

	$token = $Value.Trim() -replace '[^\w.-]+', '-'
	$token = $token.Trim("-")
	if([string]::IsNullOrWhiteSpace($token)) {
		throw "Package token cannot be empty."
	}
	return $token
}

function Get-CompilerLabel {
	$compilerCandidates = @(
		"x86_64-w64-mingw32-g++",
		"g++"
	)

	foreach($compiler in $compilerCandidates) {
		$command = Get-Command $compiler -ErrorAction SilentlyContinue
		if($null -eq $command) {
			continue
		}

		$version = (& $command.Source -dumpfullversion -dumpversion 2>$null | Select-Object -First 1)
		if($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($version)) {
			return ConvertTo-PackageToken -Value "MinGW-w64-$($version.Trim())"
		}
	}

	throw "Unable to determine MinGW compiler version. Pass -CompilerLabel to set it explicitly."
}

function Get-PackageConfigurationName {
	param(
		[string]$Mode
	)

	switch($Mode.ToLowerInvariant()) {
		"debug" { return "Debug" }
		"release" { return "Release" }
		default { throw "Unknown mode: $Mode" }
	}
}

function Invoke-DistBuild {
	param(
		[string]$RepoRoot,
		[string]$Mode,
		[string]$SCons
	)

	$target = "build/$Mode-mingw-x64/bin/DCPlusPlus.exe"
	$sconsArgs = @(
		"tools=mingw",
		"arch=x64",
		"mode=$Mode",
		$target
	)

	Write-Host ""
	Write-Host "Building $Mode x64 MinGW target..."
	Write-Host "$SCons $($sconsArgs -join ' ')"
	Push-Location -LiteralPath $RepoRoot
	try {
		& $SCons @sconsArgs
		if($LASTEXITCODE -ne 0) {
			throw "SCons failed for $Mode with exit code $LASTEXITCODE."
		}
	} finally {
		Pop-Location
	}
}

function New-DistPackage {
	param(
		[string]$RepoRoot,
		[string]$Mode,
		[string]$OutputRoot,
		[string]$VersionNumber,
		[string]$CompilerName,
		[string]$GitRevision,
		[bool]$OverwriteExisting
	)

	$buildName = "$Mode-mingw-x64"
	$binDir = Join-Path $RepoRoot "build\$buildName\bin"
	$exePath = Join-Path $binDir "DCPlusPlus.exe"
	$pdbPath = Join-Path $binDir "DCPlusPlus.pdb"
	$changelogPath = Join-Path $RepoRoot "changelog-bfe.txt"

	Assert-RequiredFile -Path $exePath -Description "Unstripped executable"
	Assert-RequiredFile -Path $pdbPath -Description "Debug companion PDB"
	Assert-RequiredFile -Path $changelogPath -Description "BFE changelog"

	$configurationName = Get-PackageConfigurationName -Mode $Mode
	$zipName = "DCPlusPlus-Experimental-$VersionNumber-$CompilerName-$configurationName-MSVCRT-$GitRevision.zip"
	$zipPath = Join-Path $OutputRoot $zipName
	$stageDir = Join-Path $OutputRoot ".stage-$buildName-$GitRevision"

	if((Test-Path -LiteralPath $zipPath) -and -not $OverwriteExisting) {
		throw "Package already exists: $zipPath. Re-run with -Force to replace it."
	}

	New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
	New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

	try {
		Copy-Item -LiteralPath $exePath -Destination (Join-Path $stageDir "DCPlusPlus.exe") -Force
		Copy-Item -LiteralPath $pdbPath -Destination (Join-Path $stageDir "DCPlusPlus.pdb") -Force
		Copy-Item -LiteralPath $changelogPath -Destination (Join-Path $stageDir "changelog-bfe.txt") -Force

		if(Test-Path -LiteralPath $zipPath) {
			Remove-Item -LiteralPath $zipPath -Force
		}

		Get-ChildItem -LiteralPath $stageDir -File | Compress-Archive -DestinationPath $zipPath -CompressionLevel Optimal -Force
		Write-Host "Created $zipPath"
		return $zipPath
	} finally {
		if(Test-Path -LiteralPath $stageDir) {
			Assert-PathUnderRoot -Path $stageDir -Root $OutputRoot
			Remove-Item -LiteralPath $stageDir -Recurse -Force
		}
	}
}

$repoRoot = Get-RepositoryRoot
$promptForBuild = -not $NoBuild -and -not $PSBoundParameters.ContainsKey("Configuration")

if([string]::IsNullOrWhiteSpace($Configuration)) {
	$Configuration = Read-Configuration
}

$defaultOutputDirectory = Join-Path $repoRoot "dist"
if([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = $defaultOutputDirectory
}
$OutputDirectory = Resolve-OutputDirectory -Path $OutputDirectory -RepoRoot $repoRoot
$versionNumber = Get-VersionNumber -RepoRoot $repoRoot
$compilerName = if([string]::IsNullOrWhiteSpace($CompilerLabel)) {
	Get-CompilerLabel
} else {
	ConvertTo-PackageToken -Value $CompilerLabel
}
$gitRevision = Get-GitRevision -RepoRoot $repoRoot

$runBuild = -not $NoBuild
if($runBuild -and $promptForBuild) {
	$runBuild = Read-YesNo -Prompt "Run SCons before packaging?" -DefaultYes $true
}

$modes = Get-ModeList -SelectedConfiguration $Configuration
$packages = @()

foreach($mode in $modes) {
	if($runBuild) {
		Invoke-DistBuild -RepoRoot $repoRoot -Mode $mode -SCons $SConsCommand
	}
	$packages += New-DistPackage -RepoRoot $repoRoot -Mode $mode -OutputRoot $OutputDirectory -VersionNumber $versionNumber -CompilerName $compilerName -GitRevision $gitRevision -OverwriteExisting ([bool]$Force)
}

Write-Host ""
Write-Host "Distribution package(s):"
foreach($package in $packages) {
	Write-Host "  $package"
}
