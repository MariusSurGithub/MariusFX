$ErrorActionPreference = 'Stop'
$root = "C:\Users\Marius\AppData\Local\Rockstar Games\GTA V\videos\clips"
$clips = @(
    (Join-Path $root "10-Juin-2024-Séquence-0005.clip"),
    (Join-Path $root "1-Juil-2024-Séquence-0001.clip"),
    (Join-Path $root "6-Nov-2024-Séquence-0001.clip")
)
& "C:\Users\Marius\Dev\MariusFX\scripts\clip_header_compare.ps1" -Clips $clips -Out "C:\Users\Marius\Dev\MariusFX\clip_compare.txt"
Write-Output ("Wrote " + (Get-Item 'C:\Users\Marius\Dev\MariusFX\clip_compare.txt').Length + " bytes")
