# Deploy MariusFX to FiveM. Pushes both the host DLL (dxgi.dll) and the
# hot-reloadable UI DLL (MariusFXUI.dll). Run with FiveM closed for the
# host swap; MariusFXUI.dll can also be redeployed live via deploy_ui.ps1.

$root        = "C:\Users\Marius\Dev\MariusFX"
$host_src    = Join-Path $root "bin\x64\Release\ReShade64.dll"
$ui_src      = Join-Path $root "bin\hot\MariusFXUI.dll"
$plugins_dir = "C:\Users\Marius\AppData\Local\FiveM\FiveM.app\plugins"
$host_dst    = Join-Path $plugins_dir "dxgi.dll"
$ui_dst      = Join-Path $plugins_dir "MariusFXUI.dll"

if (-not (Test-Path $host_src)) {
    Write-Host "ReShade64.dll missing - build first (msbuild ReShade.sln /t:ReShade)." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $ui_src)) {
    Write-Host "MariusFXUI.dll missing - build first (scripts\build_ui_hot.bat)." -ForegroundColor Red
    exit 1
}

function Push-File ($src, $dst, $label) {
    try {
        Copy-Item -Force $src $dst -ErrorAction Stop
        $i = Get-Item $dst
        Write-Host ("  {0,-18} {1,8} bytes  {2:yyyy-MM-dd HH:mm:ss}" -f $label, $i.Length, $i.LastWriteTime) -ForegroundColor Green
    }
    catch {
        Write-Host ("  {0,-18} FAILED - {1}" -f $label, $_.Exception.Message) -ForegroundColor Yellow
        return $false
    }
    return $true
}

$ok = $true
Write-Host "Deploying to $plugins_dir"
$ok = (Push-File $host_src $host_dst "dxgi.dll")        -and $ok
$ok = (Push-File $ui_src   $ui_dst   "MariusFXUI.dll")  -and $ok

if (-not $ok) {
    Write-Host "Deploy incomplete - close FiveM and retry." -ForegroundColor Yellow
    exit 1
}
Write-Host "OK." -ForegroundColor Green
