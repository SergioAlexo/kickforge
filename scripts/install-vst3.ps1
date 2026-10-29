<#
.SYNOPSIS
  Copies the built Kickforge.vst3 bundle to the system VST3 folder.
.DESCRIPTION
  Source: build\Kickforge_artefacts\<Config>\VST3\Kickforge.vst3 (relative to the repo root).
  Target: C:\Program Files\Common Files\VST3  (needs an elevated PowerShell).
  Usage:  powershell -ExecutionPolicy Bypass -File scripts\install-vst3.ps1 [-Config Release] [-Source <path>]
#>
param(
    [string]$Config = 'Release',
    [string]$Source = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $repo "build\Kickforge_artefacts\$Config\VST3\Kickforge.vst3" }
$destDir = Join-Path $env:CommonProgramFiles 'VST3'   # C:\Program Files\Common Files\VST3

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'ERROR: administrator rights are required to write to' $destDir -ForegroundColor Red
    Write-Host 'Open PowerShell with "Run as administrator" and run this script again.'
    exit 1
}

if (-not (Test-Path $Source)) {
    Write-Host "ERROR: built plugin not found: $Source" -ForegroundColor Red
    Write-Host 'Build it first:  cmake --build build --config Release --target Kickforge_VST3'
    exit 2
}

if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }
$target = Join-Path $destDir 'Kickforge.vst3'
if (Test-Path $target) { Remove-Item -Recurse -Force $target }   # replace the old bundle (folder or single file)
Copy-Item -Recurse -Force $Source $target
Write-Host "Installed: $target" -ForegroundColor Green
Write-Host 'Rescan plugins in your host (REAPER: Preferences > Plug-ins > VST > Re-scan).'
