# Auto-test script for MariusFX Transpiler
param(
    [int]$WaitSeconds = 30,
    [switch]$SkipBuild,
    [switch]$KeepFiveMOpen
)

$ErrorActionPreference = "Stop"

Write-Host "=== MariusFX Auto-Test Script ===" -ForegroundColor Cyan
Write-Host ""

# Paths
$SolutionPath = "C:\Users\Marius\AppData\Local\FiveM\FiveM.app\MariusFX_src\ReShade.sln"
$DllSource = "C:\Users\Marius\AppData\Local\FiveM\FiveM.app\MariusFX_src\bin\x64\Release\ReShade64.dll"
$DllTarget = "C:\Users\Marius\AppData\Local\FiveM\FiveM.app\plugins\dxgi.dll"
$LogPath = "C:\Users\Marius\AppData\Local\FiveM\FiveM.app\plugins\MariusFX.log"
$FiveMExe = "C:\Users\Marius\AppData\Local\FiveM\FiveM.exe"

# Step 1: Build
if (-not $SkipBuild) {
    Write-Host "[1/6] Building solution..." -ForegroundColor Yellow
    & "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
        $SolutionPath `
        /p:Configuration=Release `
        /p:Platform="64-bit" `
        /m `
        /v:minimal `
        /t:ReShade | Out-Null
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }
    Write-Host "Build successful" -ForegroundColor Green
} else {
    Write-Host "[1/6] Skipping build" -ForegroundColor Gray
}

# Step 2: Kill existing processes
Write-Host "[2/6] Stopping FiveM/GTA..." -ForegroundColor Yellow
Get-Process | Where-Object {$_.ProcessName -like "*FiveM*" -or $_.ProcessName -like "*GTA*"} | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Write-Host "Processes stopped" -ForegroundColor Green

# Step 3: Deploy
Write-Host "[3/6] Deploying DLL..." -ForegroundColor Yellow
Copy-Item $DllSource $DllTarget -Force
Write-Host "DLL deployed" -ForegroundColor Green

# Step 4: Backup log
Write-Host "[4/6] Preparing log..." -ForegroundColor Yellow
if (Test-Path $LogPath) {
    Clear-Content $LogPath
}
Write-Host "Log ready" -ForegroundColor Green

# Step 5: Launch FiveM
Write-Host "[5/6] Launching FiveM..." -ForegroundColor Yellow
Start-Process $FiveMExe
Write-Host "FiveM launched, waiting $WaitSeconds seconds..." -ForegroundColor Green
Start-Sleep -Seconds $WaitSeconds

# Step 6: Analyze
Write-Host "[6/6] Analyzing logs..." -ForegroundColor Yellow
Write-Host ""

if (-not (Test-Path $LogPath)) {
    Write-Host "Log file not found!" -ForegroundColor Red
    exit 1
}

$logContent = Get-Content $LogPath -Raw

# Count events
$attempted = ([regex]::Matches($logContent, "register_shader:")).Count
$success = ([regex]::Matches($logContent, "Shader registered successfully")).Count
$failed = ([regex]::Matches($logContent, "Failed to register")).Count
$skipped = ([regex]::Matches($logContent, "Skipping.*low confidence")).Count
$errors = ([regex]::Matches($logContent, "Compilation failed")).Count

# Results
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "         TRANSPILER RESULTS             " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Shaders Attempted:  $attempted" -ForegroundColor White
Write-Host "Shaders Success:    $success" -ForegroundColor Green
Write-Host "Shaders Failed:     $failed" -ForegroundColor $(if ($failed -gt 0) { "Red" } else { "Green" })
Write-Host "Shaders Skipped:    $skipped" -ForegroundColor Gray
Write-Host "Compilation Errors: $errors" -ForegroundColor $(if ($errors -gt 0) { "Yellow" } else { "Green" })
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Success rate
if ($attempted -gt 0) {
    $rate = [math]::Round(($success / $attempted) * 100, 2)
    Write-Host "Success Rate: $rate%" -ForegroundColor $(if ($rate -gt 50) { "Green" } elseif ($rate -gt 20) { "Yellow" } else { "Red" })
} else {
    Write-Host "No shaders attempted!" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Log: $LogPath" -ForegroundColor Gray

# Close FiveM
if (-not $KeepFiveMOpen) {
    Write-Host ""
    Write-Host "Closing FiveM in 5 seconds..." -ForegroundColor Gray
    Start-Sleep -Seconds 5
    Get-Process | Where-Object {$_.ProcessName -like "*FiveM*" -or $_.ProcessName -like "*GTA*"} | Stop-Process -Force -ErrorAction SilentlyContinue
    Write-Host "FiveM closed" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Test Complete ===" -ForegroundColor Cyan
