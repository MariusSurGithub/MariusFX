# MariusFX Replay Unhang -- removes the hosts block added by apply_hosts.ps1
#
# Run from an *elevated* PowerShell prompt:
#   powershell -ExecutionPolicy Bypass -File remove_hosts.ps1

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'

$hostsPath = "$env:windir\System32\drivers\etc\hosts"
$marker    = "# === MariusFX Replay Unhang (BEGIN) ==="
$endMarker = "# === MariusFX Replay Unhang (END) ==="

$content = Get-Content -LiteralPath $hostsPath -Raw

if ($content -notmatch [regex]::Escape($marker)) {
    Write-Host "[remove_hosts] No MariusFX block in hosts, nothing to do." -ForegroundColor Yellow
    exit 0
}

# Backup once before mutating
$backupDir = Join-Path $PSScriptRoot 'hosts_backups'
if (-not (Test-Path -LiteralPath $backupDir)) { New-Item -ItemType Directory -Path $backupDir -Force | Out-Null }
$stamp     = (Get-Date -Format 'yyyyMMdd_HHmmss')
$backup    = Join-Path $backupDir "hosts.$stamp.preremove.bak"
Copy-Item -LiteralPath $hostsPath -Destination $backup -Force
Write-Host ("[remove_hosts] Backup at {0}" -f $backup) -ForegroundColor DarkGray

# Strip the block (greedy DOTALL between markers + surrounding blank lines)
$pattern = '(?s)\r?\n?' + [regex]::Escape($marker) + '.*?' + [regex]::Escape($endMarker) + '\r?\n?'
$new = [regex]::Replace($content, $pattern, "`r`n")

Set-Content -LiteralPath $hostsPath -Value $new -Encoding ASCII -NoNewline
ipconfig /flushdns | Out-Null

Write-Host "[remove_hosts] OK. MariusFX block stripped." -ForegroundColor Green
