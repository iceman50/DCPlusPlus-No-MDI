<#
.SYNOPSIS
Generate the redesigned Dark DC++ icon pack.
.NOTES
Copyright (C) 2026 iceman50
#>
[CmdletBinding()]
param([string]$SourceDirectory, [string]$OutputPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'generate-icon-packs.ps1') -Pack Dark -SourceDirectory $SourceDirectory -OutputPath $OutputPath
