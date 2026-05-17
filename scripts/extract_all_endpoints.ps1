# Extract every unique CDN host and raw IP:port endpoint from every .clip
# in the user's clips folder. Output: a JSON manifest of what we need to
# sinkhole at hosts/firewall level.

$ErrorActionPreference = 'Stop'
$dir = "C:\Users\Marius\AppData\Local\Rockstar Games\GTA V\videos\clips"
$exe = "C:\Users\Marius\Dev\MariusFX\tools\mfx_clip_cleaner\build\mfx_clip_cleaner.exe"

$clips = Get-ChildItem -LiteralPath $dir -Filter "*.clip" |
    Where-Object { $_.Name -notmatch "\.(cleaned|original|bak)" }

$ipSet   = New-Object System.Collections.Generic.HashSet[string]
$hostSet = New-Object System.Collections.Generic.HashSet[string]

$ipv4PortRe = [regex]'^(\d{1,3}(?:\.\d{1,3}){3}):\d+$'

$counter = 0
foreach ($c in $clips) {
    $counter++
    $out = & $exe --inspect $c.FullName 2>&1
    $epStartIdx = ($out | Select-String -Pattern "^Endpoints" | Select-Object -First 1).LineNumber
    if (-not $epStartIdx) { continue }
    for ($i = $epStartIdx; $i -lt $out.Length; $i++) {
        $line = $out[$i]
        if ($line -match "^    - (.+)$") {
            $ep = $matches[1].Trim()
            if ($ipv4PortRe.IsMatch($ep)) {
                # IP:port -> firewall list. Strip port for IP-only blocking.
                $ip = $ep.Split(':')[0]
                [void]$ipSet.Add($ip)
            } else {
                # Hostname -> hosts file
                [void]$hostSet.Add($ep)
            }
        } elseif ($line -match "^[A-Z]") {
            break  # next section
        }
    }
}

$manifest = [ordered]@{
    generated_at  = (Get-Date -Format 'o')
    clips_scanned = $clips.Count
    hostnames     = ($hostSet | Sort-Object)
    ips           = ($ipSet | Sort-Object)
}

$json = $manifest | ConvertTo-Json -Depth 4
$outFile = "C:\Users\Marius\Dev\MariusFX\tools\mfx_replay_unhang\endpoints.json"
[System.IO.File]::WriteAllText($outFile, $json, [System.Text.UTF8Encoding]::new($false))

Write-Host ("Scanned $counter clips") -ForegroundColor Green
Write-Host ("  Hostnames: $($hostSet.Count)") -ForegroundColor Cyan
$hostSet | Sort-Object | ForEach-Object { Write-Host ("    - $_") }
Write-Host ("  IP[:port]: $($ipSet.Count)") -ForegroundColor Cyan
$ipSet | Sort-Object | ForEach-Object { Write-Host ("    - $_") }
Write-Host ""
Write-Host ("Manifest: $outFile") -ForegroundColor DarkGray
