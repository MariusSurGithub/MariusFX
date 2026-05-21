# Repair hlsl_transpiler.cpp lines 543-560

$file = "mariusfx\transpiler\hlsl_transpiler.cpp"
$lines = Get-Content $file

# Find and replace corrupted section (lines 543-560)
$newLines = @()
for ($i = 0; $i < $lines.Count; $i++) {
    if ($i -ge 542 -and $i -le 559) {
        if ($i -eq 542) {
            # Rebuild the correct namespace section
            $newLines += '        "namespace ReShade {\n"'
            $newLines += '        "    // GBuffer/depth intrinsics\n"'
            $newLines += '        "    bool HasNativeNormals(float2 texcoord) { return false; }\n"'
            $newLines += '        "    float3 GetNativeNormal(float2 texcoord) { return float3(0, 0, 1); }\n"'
            $newLines += '        "    bool HasNativeDepth(float2 texcoord) { return true; }\n"'
            $newLines += '        "    float GetLinearizedDepth(float2 texcoord) { return 0.5; }\n"'
            $newLines += '        "    \n"'
            $newLines += '        "    // Screen info intrinsics\n"'
            $newLines += '        "    float GetAspectRatio() { return BUFFER_WIDTH / (float)BUFFER_HEIGHT; }\n"'
            $newLines += '        "    float2 GetResolution() { return float2(BUFFER_WIDTH, BUFFER_HEIGHT); }\n"'
            $newLines += '        "    float2 GetPixelSize() { return float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT); }\n"'
            $newLines += '        "    \n"'
            $newLines += '        "    // Timing intrinsics (stubbed)\n"'
            $newLines += '        "    float GetFrameTime() { return 16.67; }\n"'
            $newLines += '        "    uint GetFrameCount() { return 0; }\n"'
            $newLines += '        "}\n"'
            $newLines += '        "#define GetLinearizedDepth(tc) ReShade::GetLinearizedDepth(tc)\n"'
        }
        # Skip corrupted lines
    } else {
        $newLines += $lines[$i]
    }
}

Set-Content $file $newLines
Write-Host "Repaired! Lines 543-560 rebuilt."
