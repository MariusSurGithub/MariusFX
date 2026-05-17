# Parse 3 sample .clip headers to validate the format is consistent across
# clips of different sizes and recording dates. Writes a comparison report
# next to MariusFX root.

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = "C:\Users\Marius\AppData\Local\Rockstar Games\GTA V\videos\clips"
# Sample 6 clips: 2 smallest, 2 medium, 2 largest, plus the most recent.
$all = Get-ChildItem -LiteralPath $root -Filter "*.clip"
$clips = @()
$clips += $all | Sort-Object Length | Select-Object -First 2
$clips += $all | Sort-Object Length -Descending | Select-Object -First 2
$clips += $all | Sort-Object LastWriteTime -Descending | Select-Object -First 2
$clips = $clips | Select-Object -Unique

$sb = New-Object System.Text.StringBuilder

foreach ($f in $clips) {
    $c = $f.FullName
    $bytes = [System.IO.File]::ReadAllBytes($c)
    $size  = $bytes.Length
    $name  = $f.Name

    $crc1  = "{0:X2} {1:X2} {2:X2} {3:X2}" -f $bytes[0], $bytes[1], $bytes[2], $bytes[3]
    $crc2  = "{0:X2} {1:X2} {2:X2} {3:X2}" -f $bytes[4], $bytes[5], $bytes[6], $bytes[7]
    $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 8, 4)
    $ver   = [BitConverter]::ToUInt32($bytes, 12)
    $hsz   = [BitConverter]::ToUInt32($bytes, 16)
    $count = [BitConverter]::ToUInt32($bytes, 20)
    $buildDate = [System.Text.Encoding]::ASCII.GetString($bytes, 24, 12).TrimEnd([char]0)
    $buildTime = [System.Text.Encoding]::ASCII.GetString($bytes, 36, 9).TrimEnd([char]0)
    $clipName  = [System.Text.Encoding]::ASCII.GetString($bytes, 0x48, 256).TrimEnd([char]0)
    $unixTs    = [BitConverter]::ToUInt32($bytes, 0x168)
    $tsLocal   = if ($unixTs -gt 0) {
        ([DateTimeOffset]::FromUnixTimeSeconds($unixTs).LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss"))
    } else { "(zero)" }

    # Find the offset where the resource URL list begins.
    $resStart = -1
    $maxScan = [Math]::Min($size - 8, 0x4000)
    for ($i = 0x200; $i -lt $maxScan; $i++) {
        $tag = [System.Text.Encoding]::ASCII.GetString($bytes, $i + 1, 5)
        if ($tag -eq "ci://") { $resStart = $i; break }
    }

    # Scan the file for every length-prefixed string that starts with
    # ci:// or https:// or http://. There is record metadata (sha1 hashes,
    # sizes, etc.) interleaved between URLs so we cannot iterate strictly,
    # we look for the (length, url-prefix) pattern.
    $ciCount    = 0
    $httpsCount = 0
    $serverEPs  = New-Object System.Collections.Generic.HashSet[string]
    $resourceNames = New-Object System.Collections.Generic.HashSet[string]
    $maxScanEnd = [Math]::Min($size - 8, 8 * 1024 * 1024)  # urls live in first 8 MB at most
    for ($p = 0x100; $p -lt $maxScanEnd; $p++) {
        $len = $bytes[$p]
        if ($len -lt 8 -or $len -gt 250) { continue }
        if ($p + 1 + $len -gt $size) { continue }
        # Cheap prefix sniff: 'c'/'h' as first char.
        $c0 = $bytes[$p + 1]
        if ($c0 -ne 0x63 -and $c0 -ne 0x68) { continue }
        $s = [System.Text.Encoding]::ASCII.GetString($bytes, $p + 1, $len)
        if ($s.StartsWith("ci://")) {
            $ciCount++
            if ($s -match "^ci://([^/]+)") { [void]$resourceNames.Add($matches[1]) }
            $p += $len  # skip the consumed string
        } elseif ($s.StartsWith("https://") -or $s.StartsWith("http://")) {
            $httpsCount++
            if ($s -match "^https?://([^/]+)") { [void]$serverEPs.Add($matches[1]) }
            $p += $len
        }
    }

    [void]$sb.AppendLine("==========================================================")
    [void]$sb.AppendLine("File:        " + $name)
    [void]$sb.AppendLine("Size:        " + $size + " bytes")
    [void]$sb.AppendLine("CRC[0..3]:   " + $crc1)
    [void]$sb.AppendLine("CRC[4..7]:   " + $crc2)
    [void]$sb.AppendLine("Magic:       '" + $magic + "'")
    [void]$sb.AppendLine("Version:     " + $ver)
    [void]$sb.AppendLine("HeaderSize:  0x" + ("{0:X}" -f $hsz) + " (" + $hsz + ")")
    [void]$sb.AppendLine("Count?:      " + $count)
    [void]$sb.AppendLine("BuildDate:   '" + $buildDate + "'")
    [void]$sb.AppendLine("BuildTime:   '" + $buildTime + "'")
    [void]$sb.AppendLine("ClipName:    '" + $clipName + "'")
    [void]$sb.AppendLine("UnixTS:      " + $unixTs + "  (" + $tsLocal + ")")
    [void]$sb.AppendLine("ResourceOff: 0x" + ("{0:X}" -f $resStart))
    [void]$sb.AppendLine("ci:// URLs:  " + $ciCount)
    [void]$sb.AppendLine("http(s)://:  " + $httpsCount)
    [void]$sb.AppendLine("Resources:   " + ($resourceNames.Count) + "  [" + (($resourceNames | Sort-Object) -join ", ") + "]")
    [void]$sb.AppendLine("ServerEPs:   " + (($serverEPs | Sort-Object) -join "; "))
    [void]$sb.AppendLine("")
}

$outFile = "C:\Users\Marius\Dev\MariusFX\clip_compare.txt"
[System.IO.File]::WriteAllText($outFile, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Output ("Wrote " + (Get-Item $outFile).Length + " bytes to " + $outFile)
