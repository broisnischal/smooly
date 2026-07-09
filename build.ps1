# build.ps1 — compile smooly.exe (GUI app) with g++ / w64devkit.
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out  = Join-Path $root "smooly.exe"

# Locate g++ (PATH, then common portable install dirs).
$gpp = $null
$c = Get-Command g++ -ErrorAction SilentlyContinue
if ($c) { $gpp = $c.Source }
else {
    foreach ($p in @("C:\w64devkit\bin\g++.exe","C:\msys64\mingw64\bin\g++.exe","C:\MinGW\bin\g++.exe")) {
        if (Test-Path $p) { $gpp = $p; break }
    }
}
if (-not $gpp) { Write-Host "No g++ found. See README.md > Build." -ForegroundColor Red; exit 1 }

# gcc invokes 'as'/'ld'/'windres' by short name — its bin dir must be on PATH.
# Prepend it to PATH to ensure we load the correct DLLs/executables of the toolchain first.
$bin = Split-Path $gpp -Parent
$env:Path = "$bin;$env:Path"
$windres = Join-Path $bin "windres.exe"

Write-Host "Compiling resources (manifest)..." -ForegroundColor Cyan
& $windres (Join-Path $root "app.rc") -o (Join-Path $root "app_res.o")
if ($LASTEXITCODE -ne 0) { Write-Host "windres failed" -ForegroundColor Red; exit 1 }

Write-Host "Compiling smooly.exe..." -ForegroundColor Cyan
& $gpp -O2 -static -mwindows -std=c++17 `
    (Join-Path $root "smooth.cpp") (Join-Path $root "app_res.o") `
    -o $out `
    -luser32 -lgdi32 -lgdiplus -lcomctl32 -lshell32 -ladvapi32 -lwinmm -ldwmapi -luxtheme
if ($LASTEXITCODE -eq 0) {
    Write-Host "OK -> $out" -ForegroundColor Green
} else {
    Write-Host "build failed" -ForegroundColor Red; exit 1
}
