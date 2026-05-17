# deploy_ui.ps1 - hot redeploy the MariusFX UI to a running FiveM.
#
#   1. Builds MariusFXUI.dll  (scripts\build_ui_hot.bat)
#   2. Copies it into FiveM's plugins folder
#   3. The host DLL (dxgi.dll, already running) detects the mtime change
#      on its next frame and reloads - no FiveM restart, no game pause.
#
# UI state (dock side, panel width, current selection, search filter,
# active sheet) is preserved across the swap by mfxui_state_get/set.

$ErrorActionPreference = 'Stop'
$root        = "C:\Users\Marius\Dev\MariusFX"
$build_bat   = Join-Path $root "scripts\build_ui_hot.bat"
$ui_src      = Join-Path $root "bin\hot\MariusFXUI.dll"
$plugins_dir = "C:\Users\Marius\AppData\Local\FiveM\FiveM.app\plugins"
$ui_dst      = Join-Path $plugins_dir "MariusFXUI.dll"

Write-Host "[1/2] Building MariusFXUI.dll..." -ForegroundColor Cyan
$build_started = Get-Date
& cmd /c "`"$build_bat`""
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build FAILED." -ForegroundColor Red
    exit $LASTEXITCODE
}
$build_dt = (Get-Date) - $build_started

if (-not (Test-Path $ui_src)) {
    Write-Host "Build OK but MariusFXUI.dll missing at $ui_src" -ForegroundColor Red
    exit 1
}

Write-Host "[2/2] Pushing to $plugins_dir..." -ForegroundColor Cyan
try {
    Copy-Item -Force $ui_src $ui_dst -ErrorAction Stop
    $i = Get-Item $ui_dst
    Write-Host ("OK - {0:N0} bytes - built in {1:N1}s" -f $i.Length, $build_dt.TotalSeconds) -ForegroundColor Green
    Write-Host "FiveM should pick it up within ~250ms (mtime debounce)." -ForegroundColor DarkGray
}
catch {
    Write-Host "Copy failed: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "(If FiveM has the file locked, restart it once - the loader uses a per-load copy after.)" -ForegroundColor DarkYellow
    exit 1
}
