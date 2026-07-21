# build-data.ps1 — Package all GameData assets into .oz* containers
# Run from repo root after building OzPack.
# Requires: System\OzPack.exe (built via `mingw32-make ozpack`)

$OZPACK   = "$PSScriptRoot\System\OzPack.exe"
$GAMEDATA = "$PSScriptRoot\GameData"
$OUT      = "$PSScriptRoot\System\Data"

function Write-Step { Write-Host "==> $args" -ForegroundColor Cyan }
function Run-OzPack { param($magic, $dir, $out)
    $dir = $dir.Replace("/", "\")
    $out = $out.Replace("/", "\")
    Write-Step "[$magic] $dir -> $out"
    $output = & $OZPACK pack $magic $dir $out 2>&1 | Out-String
    $global:LastExitCode = $LASTEXITCODE
    if ($global:LastExitCode -ne 0) { Write-Host "ERROR: OzPack failed`n$output" -ForegroundColor Red; exit 1 }
    Write-Host "  $output"
}

# Ensure output directory
if (-not (Test-Path $OUT)) { New-Item -ItemType Directory -Force -Path $OUT | Out-Null }

Write-Step "=== Packaging Global Assets ==="

# --- Guns / Weapons ---
$gunDir = "$GAMEDATA\Global\gun"
if (Test-Path $gunDir) {
    Get-ChildItem $gunDir -Directory | ForEach-Object {
        $name = $_.Name
        # Skip empty directories
        if ((Get-ChildItem $_.FullName -File -Recurse | Measure-Object).Count -gt 0) {
            Run-OzPack "OZPK" "$gunDir\$name" "$OUT\gun_$name.ozpak"
        } else {
            Write-Host "  (skipping empty: $name)" -ForegroundColor DarkGray
        }
    }
}

# --- Objects / Props ---
$objDir = "$GAMEDATA\Global\Objects"
if (Test-Path $objDir) {
    Get-ChildItem $objDir -Directory | ForEach-Object {
        $name = $_.Name
        if ((Get-ChildItem $_.FullName -File -Recurse | Measure-Object).Count -gt 0) {
            Run-OzPack "OZPK" "$objDir\$name" "$OUT\object_$name.ozpak"
        } else {
            Write-Host "  (skipping empty: $name)" -ForegroundColor DarkGray
        }
    }
}

# --- Items (textures) ---
$itemsDir = "$GAMEDATA\Global\Items"
if (Test-Path $itemsDir) {
    Run-OzPack "OZTX" $itemsDir "$OUT\items.oztex"
}

# --- Sounds ---
$sndDir = "$GAMEDATA\Global\Sounds"
if (Test-Path $sndDir) {
    Run-OzPack "OZSD" "$sndDir" "$OUT\global.ozsnd"
}

# --- Music (ambience) ---
$ambDir = "$GAMEDATA\Global\Sounds\Ambience"
if (Test-Path $ambDir) {
    Run-OzPack "OZMX" $ambDir "$OUT\ambience.ozmux"
}

# --- Shaders ---
$shaderDir = "$GAMEDATA\Shaders"
if (Test-Path $shaderDir) {
    Run-OzPack "OZPK" $shaderDir "$OUT\shaders.ozpak"
}

# --- Engine icons ---
$engineDir = "$GAMEDATA\Global\Engine"
if (Test-Path $engineDir) {
    Run-OzPack "OZTX" $engineDir "$OUT\engine_icons.oztex"
}

Write-Step "=== Packaging Worlds ==="

# --- Worlds (oztex and ozone) ---
Get-ChildItem "$GAMEDATA\Worlds" -Directory | ForEach-Object {
    $worldName = $_.Name

    # Package tileset textures as .oztex
    $tilesetDir = "$GAMEDATA\Worlds\$worldName\oztex"
    if (Test-Path $tilesetDir) {
        Run-OzPack "OZTX" $tilesetDir "$OUT\world_${worldName}_tex.oztex"
    }

    # Package world file(s) as .ozone (or .ozwn)
    $worldFile = "$GAMEDATA\Worlds\$worldName\World.ozone"
    $wdlFile   = "$GAMEDATA\Worlds\$worldName\World.wdl"
    if (Test-Path $worldFile) {
        Run-OzPack "OZWN" "$GAMEDATA\Worlds\$worldName" "$OUT\world_${worldName}.ozone"
    }
}

Write-Step "=== Verifying Output ==="
Get-ChildItem $OUT -Filter "*.oz*" | Select-Object Name, Length | Format-Table -AutoSize

Write-Step "Done. Packages in $OUT" -ForegroundColor Green
