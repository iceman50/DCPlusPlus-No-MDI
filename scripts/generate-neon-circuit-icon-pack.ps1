<#
.SYNOPSIS
Generate Neon Circuit 2.0 with purpose-built neon artwork and optical DPI sizes.
.DESCRIPTION
Uses the shared pack builder and NeonCircuitArtwork.cs. The application logo
uses enlarged electric-blue orbs without the white ring; all icons have 11 sizes.
.NOTES
Copyright (C) 2026 iceman50
#>
[CmdletBinding()]
param([string]$SourceDirectory, [string]$OutputPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'generate-icon-packs.ps1') -Pack Neon-Circuit -SourceDirectory $SourceDirectory -OutputPath $OutputPath
