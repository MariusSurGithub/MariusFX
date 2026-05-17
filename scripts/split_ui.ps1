# Split ui.cpp into 5 .inl modules + slim ui.cpp.
# Boundaries (1-indexed, inclusive) match function start lines documented in code.

$ErrorActionPreference = 'Stop'
$src = "C:\Users\Marius\Dev\MariusFX\mariusfx\ui\ui.cpp"
$dir = "C:\Users\Marius\Dev\MariusFX\mariusfx\ui"

# Read & strip BOM
$bytes = [System.IO.File]::ReadAllBytes($src)
$u8    = [System.Text.Encoding]::UTF8.GetString($bytes)
while ($u8.Length -gt 0 -and [int]$u8[0] -eq 0xFEFF) { $u8 = $u8.Substring(1) }

# Normalise line endings to LF for slicing, we will reapply CRLF on output
$u8 = $u8.Replace("`r`n", "`n")
$lines = $u8.Split("`n")

Write-Output ("Total lines: " + $lines.Length)

# Build a list of slices. Format: name, start (1-indexed), end (1-indexed, inclusive).
# Slicing model:
#   ui.cpp keeps                       1 ..   89  (preamble + 'namespace {' opens anon)
#   ui_state.inl gets                 90 .. 1377  (state + widgets + draw_uniform)
#   ui_chrome.inl gets              1378 .. 2039  (preset popup + toolbar)
#   ui_pipeline.inl gets            2040 .. 2657  (tech row + pipeline column)
#   ui_params.inl gets              2658 .. 2885  (parameter editor)
#   ui_panels.inl gets              2886 .. 3400  (statusbar + stats + settings)
#   ui.cpp resumes                  3401 ..  end  ('} // namespace' + public API + rest)

$slices = @(
    @{ name = "ui_state.inl";    s = 90;   e = 1377 },
    @{ name = "ui_chrome.inl";   s = 1378; e = 2039 },
    @{ name = "ui_pipeline.inl"; s = 2040; e = 2657 },
    @{ name = "ui_params.inl";   s = 2658; e = 2885 },
    @{ name = "ui_panels.inl";   s = 2886; e = 3400 }
)

$utf8 = New-Object System.Text.UTF8Encoding($false)
$bom  = [byte[]]@(0xEF, 0xBB, 0xBF)

# Emit each .inl
foreach ($sl in $slices) {
    $hdr = @(
        "// " + ("=" * 76),
        "// " + $sl.name + " - included by ui.cpp inside namespace mariusfx::ui::{anonymous}.",
        "// This is not a stand-alone translation unit. It exists only as a logical",
        "// module to keep ui.cpp browsable. Do not compile or include directly.",
        "// " + ("=" * 76),
        ""
    )
    $body = $lines[($sl.s - 1) .. ($sl.e - 1)]
    $content = ($hdr + $body) -join "`r`n"
    $out = $bom + $utf8.GetBytes($content)
    [System.IO.File]::WriteAllBytes((Join-Path $dir $sl.name), $out)
    Write-Output ("Wrote " + $sl.name + "  (" + ($sl.e - $sl.s + 1) + " lines)")
}

# Build new ui.cpp:
#   lines 1..89  +  blank  +  includes (5)  +  blank  +  lines 3401..end
$pre  = $lines[0 .. 88]                            # lines 1..89
$post = $lines[3400 .. ($lines.Length - 1)]        # lines 3401..end

$includes = @(
    "",
    "// Logical modules. See ui_<name>.inl for the actual code. Each file is",
    "// pulled into the anonymous namespace above so internal helpers keep",
    "// file-scope linkage without polluting the public API surface.",
    "#include `"ui_state.inl`"",
    "#include `"ui_chrome.inl`"",
    "#include `"ui_pipeline.inl`"",
    "#include `"ui_params.inl`"",
    "#include `"ui_panels.inl`"",
    ""
)

$new = ($pre + $includes + $post) -join "`r`n"
[System.IO.File]::WriteAllBytes($src, $bom + $utf8.GetBytes($new))
Write-Output ("Wrote ui.cpp  (slim, " + ($pre.Length + $includes.Length + $post.Length) + " lines)")
