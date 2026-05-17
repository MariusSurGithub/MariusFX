param([string]$Clip)
$ErrorActionPreference = 'Stop'
$bytes = [System.IO.File]::ReadAllBytes($Clip)
$size  = $bytes.Length
$out   = New-Object System.Collections.Generic.List[string]
$out.Add("Total: $size bytes")
$out.Add("")

# Extract ASCII strings (min 6 printable chars in a row)
$cur = ""
$start = 0
$found = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $size; $i++) {
    $b = $bytes[$i]
    if ($b -ge 32 -and $b -le 126) {
        if ($cur.Length -eq 0) { $start = $i }
        $cur += [char]$b
    } else {
        if ($cur.Length -ge 6) {
            $found.Add(("{0:X8}  {1}" -f $start, $cur))
        }
        $cur = ""
    }
}
if ($cur.Length -ge 6) {
    $found.Add(("{0:X8}  {1}" -f $start, $cur))
}

$out.Add("Found " + $found.Count + " ASCII strings (>=6 chars)")
$out.Add("")
$out.AddRange($found)
Set-Content -Path "C:\Users\Marius\Dev\MariusFX\clip_strings_dump.txt" -Value ($out -join "`r`n") -Encoding utf8
Write-Output ("Wrote " + $out.Count + " lines to clip_strings_dump.txt")
