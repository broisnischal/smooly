# build-cursors.ps1 - turn a folder of cursor PNGs into a smooly cursor theme.
#
#   powershell -ExecutionPolicy Bypass -File tools\build-cursors.ps1 [-Art <dir>]
#
# Reads <Art>\manifest.ini, resamples each role PNG to every configured size via
# png2cur.exe, and writes cursors\<name>\<Size>\*.cur plus an install.inf that
# smooly's scanThemes() auto-discovers (also right-click "Install"-able in Windows).
# Roles without a PNG are skipped and fall back to the Windows default.
#
# Self-contained: builds png2cur.exe with g++ (w64devkit) if missing. No Python.
param([string]$Art = "")
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent          # repo root (tools\ is under it)
$tools = $PSScriptRoot
if (-not $Art) { $Art = Join-Path $tools "cursor-art" }
$manifest = Join-Path $Art "manifest.ini"
if (-not (Test-Path $manifest)) { Write-Host "No manifest: $manifest" -ForegroundColor Red; exit 1 }

# --- ensure png2cur.exe exists (build with g++ if needed) --------------------
$png2cur = Join-Path $tools "png2cur.exe"
if (-not (Test-Path $png2cur)) {
    $gpp = (Get-Command g++ -ErrorAction SilentlyContinue).Source
    if (-not $gpp) { foreach ($p in @("C:\w64devkit\bin\g++.exe","C:\msys64\mingw64\bin\g++.exe")) { if (Test-Path $p) { $gpp = $p; break } } }
    if (-not $gpp) { Write-Host "png2cur.exe missing and no g++ found to build it." -ForegroundColor Red; exit 1 }
    $bin = Split-Path $gpp -Parent
    if ($env:Path -notlike "*$bin*") { $env:Path = "$bin;$env:Path" }
    Write-Host "Building png2cur.exe..." -ForegroundColor Cyan
    & $gpp -O2 (Join-Path $tools "png2cur.cpp") -o $png2cur -lgdiplus -lgdi32 -municode
    if ($LASTEXITCODE -ne 0) { Write-Host "png2cur build failed" -ForegroundColor Red; exit 1 }
}

# --- tiny INI parser (section -> ordered list of "key = value") --------------
function Read-Ini($path) {
    $ini = [ordered]@{}; $sec = ""
    foreach ($line in Get-Content -LiteralPath $path) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith(";") -or $t.StartsWith("#")) { continue }
        if ($t -match '^\[(.+)\]$') { $sec = $Matches[1]; if (-not $ini.Contains($sec)) { $ini[$sec] = [ordered]@{} }; continue }
        $eq = $t.IndexOf('='); if ($eq -lt 0) { continue }
        $k = $t.Substring(0,$eq).Trim(); $v = $t.Substring($eq+1).Trim().Trim('"')
        $ini[$sec][$k] = $v
    }
    return $ini
}
$ini = Read-Ini $manifest

$name = $ini['theme']['name']; if (-not $name) { $name = "Custom" }
$attr = $ini['theme']['attribution']

# Canonical role table - must mirror kThemeRoles in smooth.cpp.
$RoleMap = @{
    pointer=@{reg='Arrow';       file='Pointer.cur'}
    help=@{reg='Help';           file='Help.cur'}
    work=@{reg='AppStarting';    file='Work.ani'}
    busy=@{reg='Wait';           file='Busy.ani'}
    cross=@{reg='Crosshair';     file='Cross.cur'}
    text=@{reg='IBeam';          file='Text.cur'}
    unavailable=@{reg='No';      file='Unavailable.cur'}
    vert=@{reg='SizeNS';         file='Vert.cur'}
    horz=@{reg='SizeWE';         file='Horz.cur'}
    dgn1=@{reg='SizeNWSE';       file='Dgn1.cur'}
    dgn2=@{reg='SizeNESW';       file='Dgn2.cur'}
    move=@{reg='SizeAll';        file='Move.cur'}
    alternate=@{reg='UpArrow';   file='Alternate.cur'}
    link=@{reg='Hand';           file='Link.cur'}
}
# Fixed emit order (Windows scheme string order).
$ORDER = @('pointer','help','work','busy','cross','text','unavailable','vert','horz','dgn1','dgn2','move','alternate','link')

$sizes = $ini['sizes']; if (-not $sizes -or $sizes.Count -eq 0) { $sizes = [ordered]@{Regular=32; Large=48; 'Extra-Large'=64} }
$outThemeDir = Join-Path (Join-Path $root "cursors") $name
Write-Host "Building theme '$name' -> $outThemeDir" -ForegroundColor Cyan

$builtAny = $false
foreach ($sizeName in $sizes.Keys) {
    $px = [int]$sizes[$sizeName]
    $dir = Join-Path $outThemeDir $sizeName
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $emitted = @{}   # role -> output filename actually produced

    # .cur roles from [roles]
    foreach ($role in $ini['roles'].Keys) {
        if (-not $RoleMap.ContainsKey($role)) { Write-Host "  ! unknown role '$role' skipped" -ForegroundColor Yellow; continue }
        $spec = $ini['roles'][$role] -split ',' | ForEach-Object { $_.Trim() }
        $srcPng = Join-Path $Art $spec[0]
        if (-not (Test-Path $srcPng)) { continue }   # no art for this role -> fall back
        $refHotX = [int]$spec[1]; $refHotY = [int]$spec[2]
        $hx = [int][math]::Round($refHotX * $px / 32.0)
        $hy = [int][math]::Round($refHotY * $px / 32.0)
        $outFile = $RoleMap[$role].file
        $outCur = Join-Path $dir $outFile
        & $png2cur $srcPng $outCur $hx $hy $px
        if ($LASTEXITCODE -ne 0) { Write-Host "  png2cur failed for $role" -ForegroundColor Red; exit 1 }
        $emitted[$role] = $outFile
    }
    # verbatim-copied roles from [copy] (e.g. .ani)
    if ($ini['copy']) {
        foreach ($role in $ini['copy'].Keys) {
            if (-not $RoleMap.ContainsKey($role)) { continue }
            $srcAni = Join-Path $Art $ini['copy'][$role]
            if (-not (Test-Path $srcAni)) { continue }
            $outFile = $RoleMap[$role].file
            Copy-Item -LiteralPath $srcAni -Destination (Join-Path $dir $outFile) -Force
            $emitted[$role] = $outFile
        }
    }

    if ($emitted.Count -eq 0) { Write-Host "  ($sizeName) no source art found - skipped" -ForegroundColor Yellow; continue }
    if (-not $emitted.ContainsKey('pointer')) { Write-Host "  ($sizeName) WARNING: no pointer.png - theme needs a Pointer.cur to appear" -ForegroundColor Yellow }

    # --- write install.inf (app-readable [Strings] + Windows-installable) ----
    $inf = New-Object System.Text.StringBuilder
    $curDir = "Cursors\$name-$sizeName"
    [void]$inf.AppendLine("; Auto-generated by smooly build-cursors.ps1")
    if ($attr) { [void]$inf.AppendLine("; $attr") }
    [void]$inf.AppendLine("")
    [void]$inf.AppendLine('[Version]'); [void]$inf.AppendLine('signature="$CHICAGO$"'); [void]$inf.AppendLine("")
    [void]$inf.AppendLine('[DefaultInstall]'); [void]$inf.AppendLine('CopyFiles = Scheme.Cur'); [void]$inf.AppendLine('AddReg = Scheme.Reg,Wreg'); [void]$inf.AppendLine("")
    [void]$inf.AppendLine('[DestinationDirs]'); [void]$inf.AppendLine('Scheme.Cur = 10,"%CUR_DIR%"'); [void]$inf.AppendLine("")

    $ordered = $ORDER | Where-Object { $emitted.ContainsKey($_) }
    $schemeVals = ($ordered | ForEach-Object { "%10%\%CUR_DIR%\%$_%" }) -join ','
    [void]$inf.AppendLine('[Scheme.Reg]')
    [void]$inf.AppendLine("HKCU,`"Control Panel\Cursors\Schemes`",`"%SCHEME_NAME%`",,`"$schemeVals`"")
    [void]$inf.AppendLine("")
    [void]$inf.AppendLine('[Wreg]')
    [void]$inf.AppendLine('HKCU,"Control Panel\Cursors",,0x00020000,"%SCHEME_NAME%"')
    foreach ($r in $ordered) { [void]$inf.AppendLine("HKCU,`"Control Panel\Cursors`",$($RoleMap[$r].reg),0x00020000,`"%10%\%CUR_DIR%\%$r%`"") }
    [void]$inf.AppendLine("")
    [void]$inf.AppendLine('[Scheme.Cur]')
    foreach ($r in $ordered) { [void]$inf.AppendLine($emitted[$r]) }
    [void]$inf.AppendLine("")
    [void]$inf.AppendLine('[Strings]')
    [void]$inf.AppendLine("CUR_DIR = `"$curDir`"")
    [void]$inf.AppendLine("SCHEME_NAME = `"$name-$sizeName`"")
    foreach ($r in $ordered) { [void]$inf.AppendLine("$r = `"$($emitted[$r])`"") }

    $infPath = Join-Path $dir "install.inf"
    [System.IO.File]::WriteAllText($infPath, $inf.ToString(), (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ("  {0,-12} {1} roles -> {2}" -f $sizeName, $emitted.Count, $dir) -ForegroundColor Green
    $builtAny = $true
}

if ($builtAny) {
    Write-Host "`nDone. '$name' will appear in smooly's Cursor theme list." -ForegroundColor Green
    if ($attr) { Write-Host "Remember to add attribution to cursors\ATTRIBUTION.txt: $attr" -ForegroundColor Cyan }
} else {
    Write-Host "`nNothing built - drop your PNGs in $Art (see manifest.ini)." -ForegroundColor Yellow
}
