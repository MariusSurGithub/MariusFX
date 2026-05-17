# MariusFX Replay Unhang -- L1 deploy
#
# Reads endpoints.json (produced by scripts/extract_all_endpoints.ps1) and:
#   1. Adds 127.0.0.1 entries to %WINDIR%\System32\drivers\etc\hosts for
#      every CDN hostname embedded in the user's recorded .clip files.
#   2. Creates Windows Firewall outbound block rules for every raw IP
#      embedded in those clips.
#
# Effect: when the Rockstar Editor opens any of the 200+ recorded clips,
# FiveM's resource fetcher fires HTTPS requests at those endpoints, hits
# either localhost (no listener) or a firewall block, and -- if the
# fetcher has any sane timeout/retry budget -- gives up and lets the
# editor continue with whatever the local 15GB compcache holds.
#
# Run from an *elevated* PowerShell prompt:
#   powershell -ExecutionPolicy Bypass -File apply_unhang.ps1
#
# Idempotent: re-running adds nothing new. Use remove_unhang.ps1 to undo.
# 100% offline-compatible: no clip is touched, no GTA process is hooked.

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$here       = Split-Path -Parent $MyInvocation.MyCommand.Definition
$manifest   = Join-Path $here 'endpoints.json'
$hostsPath  = "$env:windir\System32\drivers\etc\hosts"
$marker     = '# === MariusFX Replay Unhang (BEGIN) ==='
$endMarker  = '# === MariusFX Replay Unhang (END) ==='
$rulePrefix = 'MFX-Replay-Unhang-'

if (-not (Test-Path -LiteralPath $manifest)) {
    Write-Error ("endpoints.json not found at {0}. Run scripts/extract_all_endpoints.ps1 first." -f $manifest)
    exit 1
}

$data = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$hostnames = @($data.hostnames)
$ips       = @($data.ips)

Write-Host ('[apply] Sinkholing {0} hostnames and {1} IPs from {2} clips.' -f `
    $hostnames.Count, $ips.Count, $data.clips_scanned) -ForegroundColor Cyan

# ----------------------------------------------------------------------
# Step 1: hosts file
# ----------------------------------------------------------------------
$content = Get-Content -LiteralPath $hostsPath -Raw

if ($content -match [regex]::Escape($marker)) {
    Write-Host '[apply] hosts: MariusFX block already present, skipping.' -ForegroundColor Yellow
} else {
    $backupDir = Join-Path $here 'hosts_backups'
    if (-not (Test-Path -LiteralPath $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    }
    $stamp  = (Get-Date -Format 'yyyyMMdd_HHmmss')
    $backup = Join-Path $backupDir ("hosts.$stamp.bak")
    Copy-Item -LiteralPath $hostsPath -Destination $backup -Force
    Write-Host ('[apply] hosts: backup -> {0}' -f $backup) -ForegroundColor DarkGray

    $lines = @()
    $lines += ''
    $lines += $marker
    $lines += ('# Added on {0} by apply_unhang.ps1' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
    $lines += '# Sinkholes the recording-server CDN(s) so the Rockstar Editor stops'
    $lines += '# hanging on "Preparing Clip". Remove with remove_unhang.ps1.'
    foreach ($h in ($hostnames | Sort-Object)) {
        $lines += ('127.0.0.1 ' + $h)
    }
    $lines += $endMarker
    $lines += ''

    Add-Content -LiteralPath $hostsPath -Value ($lines -join "`r`n") -Encoding ASCII
    ipconfig /flushdns | Out-Null
    Write-Host '[apply] hosts: block written.' -ForegroundColor Green
}

# ----------------------------------------------------------------------
# Step 2: Windows Firewall outbound block rules
# ----------------------------------------------------------------------
$existing = Get-NetFirewallRule -DisplayName ($rulePrefix + '*') -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host ('[apply] firewall: {0} existing rule(s) already present, skipping.' -f $existing.Count) -ForegroundColor Yellow
} else {
    foreach ($ip in $ips) {
        $name = $rulePrefix + $ip
        New-NetFirewallRule `
            -DisplayName $name `
            -Direction   Outbound `
            -Action      Block `
            -RemoteAddress $ip `
            -Profile     Any `
            -Description ('Block FiveM Rockstar Editor reconnect to recording server {0} (auto-added by MariusFX Replay Unhang).' -f $ip) `
            | Out-Null
        Write-Host ('  + ' + $name) -ForegroundColor Green
    }
    Write-Host ('[apply] firewall: {0} outbound block rule(s) added.' -f $ips.Count) -ForegroundColor Green
}

# ----------------------------------------------------------------------
# Verification
# ----------------------------------------------------------------------
Write-Host ''
Write-Host '=== DNS verification (expect 127.0.0.1) ===' -ForegroundColor Cyan
foreach ($h in $hostnames) {
    try {
        $r = Resolve-DnsName -Name $h -Type A -DnsOnly -ErrorAction Stop | Select-Object -First 1
        $ok = ($r.IPAddress -eq '127.0.0.1')
        $tag = if ($ok) { 'OK   ' } else { 'WARN ' }
        Write-Host ('  {0} {1,-25} -> {2}' -f $tag, $h, $r.IPAddress)
    } catch {
        Write-Host ('  ERR  {0,-25} -> resolve failed' -f $h) -ForegroundColor Yellow
    }
}

Write-Host ''
Write-Host '=== Firewall rules in place ===' -ForegroundColor Cyan
Get-NetFirewallRule -DisplayName ($rulePrefix + '*') | ForEach-Object {
    $addr = ($_ | Get-NetFirewallAddressFilter).RemoteAddress
    Write-Host ('  - {0,-40} block -> {1}' -f $_.DisplayName, $addr)
}

Write-Host ''
Write-Host 'Done. Now open FiveM and try opening a recent clip in the Rockstar Editor.' -ForegroundColor Green
Write-Host 'Roll back with:  powershell -ExecutionPolicy Bypass -File remove_unhang.ps1' -ForegroundColor DarkGray
