# MariusFX Replay Unhang -- L1: hosts-file CDN sinkhole
#
# Adds 127.0.0.1 redirects for the FiveM RP server CDNs embedded in your
# recorded .clip files. With these in place, when the Rockstar Editor opens
# a clip, FiveM's resource fetcher tries to reach the CDN, hits localhost
# (no listener), gets ECONNREFUSED, and -- if FiveM has any sane retry
# limit -- moves on, letting the editor finally load.
#
# Run from an *elevated* PowerShell prompt:
#   powershell -ExecutionPolicy Bypass -File apply_hosts.ps1
#
# Idempotent: re-running adds nothing new. Use remove_hosts.ps1 to undo.

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'

$hostsPath = "$env:windir\System32\drivers\etc\hosts"
$marker    = "# === MariusFX Replay Unhang (BEGIN) ==="
$endMarker = "# === MariusFX Replay Unhang (END) ==="

# Domains to sinkhole. Extracted from inspecting the user's clip set:
#   - cache.bay.life       (production CDN, observed 2026-05-08, 09, 12)
#   - cachetest.bay.life   (test CDN, observed 2026-05-14)
$domains = @(
    'cache.bay.life',
    'cachetest.bay.life'
)

# Read current content
$content = Get-Content -LiteralPath $hostsPath -Raw

if ($content -match [regex]::Escape($marker)) {
    Write-Host "[apply_hosts] Block already present, nothing to do." -ForegroundColor Yellow
    Write-Host "Current MariusFX block:" -ForegroundColor Yellow
    $extract = ($content -split [regex]::Escape($marker), 2)[1]
    $extract = ($extract -split [regex]::Escape($endMarker), 2)[0]
    Write-Host $extract
    exit 0
}

# Build the new block
$lines = @()
$lines += ''
$lines += $marker
$lines += "# Added on $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') by apply_hosts.ps1"
$lines += "# Sinkholes the recording-server CDN(s) so the Rockstar Editor stops"
$lines += "# hanging on 'Preparing Clip'. Remove with remove_hosts.ps1."
foreach ($d in $domains) {
    $lines += ('127.0.0.1 ' + $d)
}
$lines += $endMarker
$lines += ''

$block = $lines -join "`r`n"

# Backup once per day
$backupDir = Join-Path $PSScriptRoot 'hosts_backups'
if (-not (Test-Path -LiteralPath $backupDir)) { New-Item -ItemType Directory -Path $backupDir -Force | Out-Null }
$stamp     = (Get-Date -Format 'yyyyMMdd_HHmmss')
$backup    = Join-Path $backupDir "hosts.$stamp.bak"
Copy-Item -LiteralPath $hostsPath -Destination $backup -Force
Write-Host ("[apply_hosts] Backup written to {0}" -f $backup) -ForegroundColor DarkGray

# Append the block
Add-Content -LiteralPath $hostsPath -Value $block -Encoding ASCII

# Flush DNS so the new resolution is picked up immediately
ipconfig /flushdns | Out-Null

Write-Host ""
Write-Host "[apply_hosts] OK. Sinkholed domains:" -ForegroundColor Green
foreach ($d in $domains) { Write-Host ("  - " + $d) }
Write-Host ""
Write-Host "Verify resolution:" -ForegroundColor Cyan
foreach ($d in $domains) {
    try {
        $r = Resolve-DnsName -Name $d -Type A -DnsOnly -ErrorAction Stop | Select-Object -First 1
        Write-Host ("  {0,-25} -> {1}" -f $d, $r.IPAddress)
    } catch {
        Write-Host ("  {0,-25} -> resolve failed" -f $d) -ForegroundColor Yellow
    }
}
