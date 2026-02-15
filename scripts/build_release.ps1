$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$distDir = Join-Path $root "dist"
if (-not (Test-Path $distDir)) {
    New-Item -ItemType Directory -Path $distDir | Out-Null
}

$exePath = Join-Path $root "bin\gui\Mafia Savegame Editor.exe"
if (-not (Test-Path $exePath)) {
    throw "Missing build output: $exePath. Build GUI first."
}

$releaseExe = Join-Path $distDir "Mafia Savegame Editor.exe"
Copy-Item $exePath $releaseExe -Force

Write-Output "Release executable prepared: $releaseExe"
