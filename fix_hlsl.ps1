# Fix hlsl_transpiler.cpp - Add global GetLinearizedDepth macro after namespace ReShade

$file = "mariusfx\transpiler\hlsl_transpiler.cpp"
$content = Get-Content $file -Raw

# For CS preamble: Add #define after namespace closes
$content = $content -replace '(\s+"namespace ReShade \{\\n"[^}]+"\}\\n")', "`$1`n        `"#define GetLinearizedDepth(tc) ReShade::GetLinearizedDepth(tc)\n`""

# For PS preamble: Add after oss << "}\n\n";
$content = $content -replace '(oss << "\}\\n\\n";[\r\n]+\s+// Include full source)', "oss << `"#define GetLinearizedDepth(tc) ReShade::GetLinearizedDepth(tc)\n\n`";`n            `$1"

Set-Content $file $content -NoNewline
Write-Host "Fixed!"
