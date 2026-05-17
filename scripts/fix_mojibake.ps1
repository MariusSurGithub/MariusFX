# Repair UTF-8 mojibake produced when a previous bulk-edit round-tripped
# UTF-8 bytes through CP-1252. The trick: every "mojibake run" is a
# sequence of high-range Latin-1 chars (typically starting with U+00E2 = a-circumflex)
# that is exactly the CP-1252 misreading of valid UTF-8 bytes.
#
# We detect those runs (regex: 1+ chars in U+0080..U+00FF) and re-encode
# each run via CP-1252 -> bytes -> UTF-8. ASCII and already-correct UTF-8
# chars (above U+00FF) stay untouched.

param(
    [Parameter(Mandatory=$true)] [string] $Path
)

$bytes  = [System.IO.File]::ReadAllBytes($Path)
$utf8   = [System.Text.Encoding]::UTF8.GetString($bytes)
# .NET's UTF8.GetString does NOT strip BOMs — they survive as U+FEFF
# code points in the decoded string. Strip every leading U+FEFF so we
# don't accumulate multiple BOMs across repeat invocations.
while ($utf8.Length -gt 0 -and [int]$utf8[0] -eq 0xFEFF) { $utf8 = $utf8.Substring(1) }
# Also strip any replacement chars (\uFFFD) the previous broken pass
# may have left behind so we get a clean re-encode.
$utf8   = $utf8.Replace([char]0xFFFD, '')
$cp1252 = [System.Text.Encoding]::GetEncoding(1252)
$utf8E  = New-Object System.Text.UTF8Encoding($false)

# A char is "CP1252-encodable but non-ASCII" if it's part of the Latin-1
# supplement OR one of the punctuation chars CP-1252 maps onto bytes
# 0x80..0x9F (€, ', ", -, ., dagger, etc.). Mojibake runs are stretches of
# 2+ such chars in a row. ASCII and "real" Unicode chars (e.g. U+2500
# box-drawing, U+2022 bullet that is NOT CP-1252-encodable) get left alone.
$cp1252Chars = New-Object 'System.Collections.Generic.HashSet[char]'
foreach ($b in 0x80..0xFF) {
    try {
        $s = $cp1252.GetString([byte[]]@($b))
        if ($s.Length -eq 1) { [void]$cp1252Chars.Add($s[0]) }
    } catch {}
}

# Walk the string, collecting runs of CP-1252 chars and round-tripping
# them. Single isolated CP-1252 chars (e.g. an intentional 'é' in a
# French comment) get a length-1 run that's still treated as mojibake;
# this is intentional because the bulk-edit corruption hits everywhere.
$sb = New-Object System.Text.StringBuilder
$run = New-Object System.Text.StringBuilder
foreach ($ch in $utf8.ToCharArray()) {
    if ($cp1252Chars.Contains($ch)) {
        [void]$run.Append($ch)
    } else {
        if ($run.Length -gt 0) {
            try {
                $b = $cp1252.GetBytes($run.ToString())
                [void]$sb.Append($utf8E.GetString($b))
            } catch {
                [void]$sb.Append($run.ToString())
            }
            [void]$run.Clear()
        }
        [void]$sb.Append($ch)
    }
}
if ($run.Length -gt 0) {
    try {
        $b = $cp1252.GetBytes($run.ToString())
        [void]$sb.Append($utf8E.GetString($b))
    } catch {
        [void]$sb.Append($run.ToString())
    }
}
$repaired = $sb.ToString()

$bom    = [byte[]]@(0xEF,0xBB,0xBF)
$out    = $utf8E.GetBytes($repaired)
[System.IO.File]::WriteAllBytes($Path, $bom + $out)

Write-Host ("Wrote {0} bytes." -f ($bom.Length + $out.Length))
