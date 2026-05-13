Param(
	[Parameter(Mandatory = $true)][string]
	$path,
	[string]
	$config = "",
	[string]
	$platform = ""
)

$exists = Test-Path $path
$version = 0,0,0,0

# Get version from existing file
if ($exists -and $(Get-Content $path | Out-String) -match "VERSION_FULL (\d+).(\d+).(\d+).(\d+)") {
	$version = [int]::Parse($matches[1]), [int]::Parse($matches[2]), [int]::Parse($matches[3]), [int]::Parse($matches[4])
}
elseif ($(git describe --tags) -match "v(\d+)\.(\d+)\.(\d+)(-\d+-\w+)?") {
	$version = [int]::Parse($matches[1]), [int]::Parse($matches[2]), [int]::Parse($matches[3]), 0
}

$global:ReShadeVersion = $version

# Increment build version for release builds
if (($config -eq "Release") -or
    ($config -eq "Release Signed")) {
	$version[3] += 1
	"Updating version to $([string]::Join('.', $version)) ..."
}
elseif ($exists) {
	return
}

# MariusFX: dropped the "UNOFFICIAL" suffix that upstream appends when no
# sign.pfx is present. That suffix surfaces in the About panel and in any
# log line that prints VERSION_STRING_PRODUCT, which would clearly read as
# "ReShade-derived dev build" to anyone looking. The clean numeric form
# matches what users expect from a shipped product.

# Update version file with the new version information
@"
#pragma once

#define VERSION_FULL $([string]::Join('.', $version))
#define VERSION_MAJOR $($version[0])
#define VERSION_MINOR $($version[1])
#define VERSION_REVISION $($version[2])
#define VERSION_BUILD $($version[3])

#define VERSION_STRING_FILE "$([string]::Join('.', $version))"
#define VERSION_STRING_PRODUCT "$($version[0]).$($version[1]).$($version[2])"
"@ | Out-File -FilePath $path -Encoding ASCII
