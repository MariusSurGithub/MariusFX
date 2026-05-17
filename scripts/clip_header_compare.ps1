param([string[]]$Clips, [string]$Out)
$ErrorActionPreference = 'Stop'
$sb = New-Object System.Text.StringBuilder

foreach ($c in $Clips) {
    $bytes = [System.IO.File]::ReadAllBytes($c)
    $size  = $bytes.Length
    $name  = Split-Path $c -Leaf

    # Header fields (per the layout reverse-engineered from clip 10-Juin-2024-Seq-0005)
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
    $tsLocal   = if ($unixTs -gt 0) { ([DateTimeOffset]::FromUnixTimeSeconds($unixTs).LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss")) } else { "(zero)" }

    # Find the offset where resource URLs begin. Look for first byte length
    # followed by "ci://" or "https://" magic.
    $resStart = -1
    for ($i = 0x200; $i -lt [Math]::Min($size, 0x4000); $i++) {
        if ($i + 7 -lt $size) {
            $tag = [System.Text.Encoding]::ASCII.GetString($bytes, $i + 1, 5)
            if ($tag -eq "ci://") { $resStart = $i; break }
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
    [void]$sb.AppendLine("")
}

if ($Out) { Set-Content -LiteralPath $Out -Value $sb.ToString() -Encoding utf8 } else { Write-Output $sb.ToString() }
