# MariusFX Replay Unhang -- L1 rollback
#
# Strips the hosts block and removes every Windows Firewall rule that
# apply_unhang.ps1 added.
#
# Run from an *elevated* PowerShell prompt:
#   powershell -ExecutionPolicy Bypass -File remove_unhang.ps1

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$here       = Split-Path -Parent $MyInvocation.MyCommand.Definition
$hostsPath  = "$env:windir\System32\drivers\etc\hosts"
$marker     = '# === MariusFX Replay Unhang (BEGIN) ==='
$endMarker  = '# === MariusFX Replay Unhang (END) ==='
$rulePrefix = 'MFX-Replay-Unhang-'

# ----------------------------------------------------------------------
# Step 1: hosts cleanup
# ----------------------------------------------------------------------
$content = Get-Content -LiteralPath $hostsPath -Raw
if ($content -match [regex]::Escape($marker)) {
    $backupDir = Join-Path $here 'hosts_backups'
    if (-not (Test-Path -LiteralPath $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    }
    $stamp  = (Get-Date -Format 'yyyyMMdd_HHmmss')
    $backup = Join-Path $backupDir ("hosts.$stamp.preremove.bak")
    Copy-Item -LiteralPath $hostsPath -Destination $backup -Force
    Write-Host ('[remove] hosts: backup -> {0}' -f $backup) -ForegroundColor DarkGray

    $pattern = '(?s)\r?\n?' + [regex]::Escape($marker) + '.*?' + [regex]::Escape($endMarker) + '\r?\n?'
    $new = [regex]::Replace($content, $pattern, "`r`n")
    Set-Content -LiteralPath $hostsPath -Value $new -Encoding ASCII -NoNewline
    ipconfig /flushdns | Out-Null
    Write-Host '[remove] hosts: MariusFX block stripped.' -ForegroundColor Green
} else {
    Write-Host '[remove] hosts: no MariusFX block found, nothing to do.' -ForegroundColor Yellow
}

# ----------------------------------------------------------------------
# Step 2: firewall cleanup
# ----------------------------------------------------------------------
$rules = Get-NetFirewallRule -DisplayName ($rulePrefix + '*') -ErrorAction SilentlyContinue
if ($rules) {
    foreach ($r in $rules) {
        Remove-NetFirewallRule -InputObject $r
        Write-Host ('  - removed ' + $r.DisplayName) -ForegroundColor DarkGray
    }
    Write-Host ('[remove] firewall: {0} rule(s) removed.' -f $rules.Count) -ForegroundColor Green
} else {
    Write-Host '[remove] firewall: no MFX rules found.' -ForegroundColor Yellow
}
