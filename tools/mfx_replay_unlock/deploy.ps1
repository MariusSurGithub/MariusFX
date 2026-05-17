# deploy.ps1 -------------------------------------------------------------
#
# Build + copy mfx_replay_unlock.asi into FiveM's plugins folder.
#
# This script is independent of MariusFX's deploy.ps1. It does NOT
# touch dxgi.dll, MariusFXUI.dll, or any other MariusFX artifact --
# they live in the same plugins\ folder but for a completely
# different loader chain.
# -------------------------------------------------------------------------

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $PSCommandPath

# -- Build ----------------------------------------------------------------
Write-Host "[1/2] Building mfx_replay_unlock.asi..."
$build_log = Join-Path $here "build.log"
& cmd /c "`"$here\build.bat`"" 2>&1 | Tee-Object -FilePath $build_log
if ($LASTEXITCODE -ne 0) {
    Write-Host "[deploy] build failed (exit $LASTEXITCODE). See $build_log" -ForegroundColor Red
    exit $LASTEXITCODE
}

$asi = Join-Path $here "bin\mfx_replay_unlock.asi"
if (-not (Test-Path $asi)) {
    Write-Host "[deploy] $asi missing after build." -ForegroundColor Red
    exit 1
}

# -- Locate plugins folder ------------------------------------------------
$plugins = "$env:LOCALAPPDATA\FiveM\FiveM.app\plugins"
if (-not (Test-Path $plugins)) {
    New-Item -ItemType Directory -Force -Path $plugins | Out-Null
    Write-Host "[deploy] Created $plugins"
}

# -- Deploy ---------------------------------------------------------------
Write-Host "[2/2] Pushing to $plugins..."
$dst = Join-Path $plugins "mfx_replay_unlock.asi"

# If FiveM is currently running, the .asi is locked by the loader and
# Copy-Item will fail with "in use". Detect early so the user gets a
# clean message instead of a stack trace.
$fivem_running = Get-Process -Name "FiveM*","GTAProcess*","CitizenFX*" -ErrorAction SilentlyContinue
if ($fivem_running -and (Test-Path $dst)) {
    Write-Host "[deploy] FiveM is running and an older mfx_replay_unlock.asi is loaded." -ForegroundColor Yellow
    Write-Host "         Close FiveM completely, then re-run this script." -ForegroundColor Yellow
    Write-Host "         (Unlike MariusFX, .asi files cannot be hot-reloaded -- they are" -ForegroundColor Yellow
    Write-Host "          loaded once at game-process start and unloaded only on exit.)" -ForegroundColor Yellow
    exit 2
}

Copy-Item -Force -LiteralPath $asi -Destination $dst
$size = (Get-Item $dst).Length
"{0} - {1:N0} bytes" -f $dst, $size | Write-Host

# Friendly reminder: also flag the existing log file so the user can
# tail it on next launch.
$log = Join-Path $plugins "mfx_replay_unlock.log"
if (Test-Path $log) {
    Write-Host ""
    Write-Host "Existing log: $log"
    Write-Host "  Tail it during next launch with:"
    Write-Host "    Get-Content `"$log`" -Wait -Tail 30"
}
