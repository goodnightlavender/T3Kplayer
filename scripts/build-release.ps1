# build-release.ps1 - Package TONE3000 Player VST3 for a Windows release.
#
# Run from the nam-fork/ directory (or anywhere - paths are resolved relative
# to this script's location). Assumes the VST3 bundle has already been built
# by the post-commit hook into NeuralAmpModeler/build-win/TONE3000Player.vst3/.
#
# PowerShell 5.1 compatible.

[CmdletBinding()]
param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# 1. Resolve repo paths relative to this script (nam-fork/scripts/...)
# ---------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir "..") | Select-Object -ExpandProperty Path

$VstBundle  = Join-Path $RepoRoot "NeuralAmpModeler\build-win\TONE3000Player.vst3"
$ConfigH    = Join-Path $RepoRoot "NeuralAmpModeler\config.h"
$LicenseSrc = Join-Path $RepoRoot "LICENSE"
$NoticeSrc  = Join-Path $RepoRoot "NOTICE.md"
$ReadmeSrc  = Join-Path $RepoRoot "README.md"

Write-Host "[build-release] Repo root : $RepoRoot"
Write-Host "[build-release] Config    : $Configuration"

# ---------------------------------------------------------------------------
# 2. Verify the VST3 bundle exists (it's a directory on Windows, not a file)
# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $VstBundle -PathType Container)) {
    Write-Error "VST3 bundle not found at: $VstBundle`nBuild the plug-in first - see docs/setup/BUILD-WIN.md."
}
Write-Host "[build-release] Found VST3 bundle: $VstBundle"

# ---------------------------------------------------------------------------
# 3. Parse PLUG_VERSION_STR from NeuralAmpModeler/config.h
# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $ConfigH -PathType Leaf)) {
    Write-Error "config.h not found at: $ConfigH"
}

$configText = Get-Content -LiteralPath $ConfigH -Raw
$versionMatch = [regex]::Match($configText, 'PLUG_VERSION_STR\s+"([^"]+)"')
if (-not $versionMatch.Success) {
    Write-Error "Could not find PLUG_VERSION_STR in $ConfigH"
}
$Version = $versionMatch.Groups[1].Value
Write-Host "[build-release] Parsed version: $Version"

# ---------------------------------------------------------------------------
# 4. Compute / clean the staging directory
# ---------------------------------------------------------------------------
$ReleaseRoot = Join-Path $RepoRoot "build-win\release"
$StageName   = "TONE3000Player-v$Version-win-x64"
$StageDir    = Join-Path $ReleaseRoot $StageName

if (-not (Test-Path -LiteralPath $ReleaseRoot)) {
    New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null
}

if (Test-Path -LiteralPath $StageDir) {
    Write-Host "[build-release] Removing existing staging dir: $StageDir"
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
Write-Host "[build-release] Staging   : $StageDir"

# ---------------------------------------------------------------------------
# 5. Copy artifacts into the staging directory
# ---------------------------------------------------------------------------

# 5a. The whole .vst3 bundle (directory). Copy the folder itself, not just
# its contents, so the resulting tree is StageDir\TONE3000Player.vst3\...
Write-Host "[build-release] Copying VST3 bundle ..."
Copy-Item -LiteralPath $VstBundle -Destination $StageDir -Recurse -Force

# 5b. LICENSE (required), NOTICE.md and README.md (warn-on-missing).
if (Test-Path -LiteralPath $LicenseSrc -PathType Leaf) {
    Copy-Item -LiteralPath $LicenseSrc -Destination $StageDir -Force
    Write-Host "[build-release] Copied LICENSE"
} else {
    Write-Error "LICENSE not found at $LicenseSrc - cannot ship release without it."
}

if (Test-Path -LiteralPath $NoticeSrc -PathType Leaf) {
    Copy-Item -LiteralPath $NoticeSrc -Destination $StageDir -Force
    Write-Host "[build-release] Copied NOTICE.md"
} else {
    Write-Warning "NOTICE.md not found at $NoticeSrc - continuing without it."
}

if (Test-Path -LiteralPath $ReadmeSrc -PathType Leaf) {
    Copy-Item -LiteralPath $ReadmeSrc -Destination $StageDir -Force
    Write-Host "[build-release] Copied README.md"
} else {
    Write-Warning "README.md not found at $ReadmeSrc - continuing without it."
}

# 5c. Inline-generated INSTALL.txt
$installText = @"
TONE3000 Player - Installation
==============================

1. Copy TONE3000Player.vst3 (the .vst3 folder, not just the files inside it)
   to:

       C:\Program Files\Common Files\VST3\

   This requires Administrator privileges.

2. Open your DAW and rescan plug-ins.

3. TONE3000 Player should appear under VST3 effects.
"@
$installPath = Join-Path $StageDir "INSTALL.txt"
Set-Content -LiteralPath $installPath -Value $installText -Encoding ASCII
Write-Host "[build-release] Wrote INSTALL.txt"

# ---------------------------------------------------------------------------
# 6. ZIP the staging dir.
#
# Note: on PowerShell 5.1, Compress-Archive (which uses System.IO.Compression
# .ZipFile under the hood) preserves directory entries when -Path points to
# a directory. Passing $StageDir (no trailing wildcard) produces an archive
# whose top-level entry is "$StageName\..." with the .vst3 folder preserved
# as a real directory inside it. The .vst3 directory must remain a real
# directory (not be flattened) - confirm this on first run.
# ---------------------------------------------------------------------------
$ZipPath = Join-Path $ReleaseRoot "$StageName.zip"
if (Test-Path -LiteralPath $ZipPath) {
    Write-Host "[build-release] Removing existing zip: $ZipPath"
    Remove-Item -LiteralPath $ZipPath -Force
}

Write-Host "[build-release] Compressing -> $ZipPath"
Compress-Archive -Path $StageDir -DestinationPath $ZipPath -CompressionLevel Optimal -Force

if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) {
    Write-Error "Compress-Archive did not produce $ZipPath"
}

# ---------------------------------------------------------------------------
# 7. SHA-256 next to the zip, in "<hex>  <filename>" (sha256sum-compatible)
# ---------------------------------------------------------------------------
$hash    = Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256
$hashHex = $hash.Hash.ToLowerInvariant()
$zipFileName = Split-Path $ZipPath -Leaf
$shaPath = "$ZipPath.sha256"
Set-Content -LiteralPath $shaPath -Value "$hashHex  $zipFileName" -Encoding ASCII
Write-Host "[build-release] Wrote SHA-256 file: $shaPath"

# ---------------------------------------------------------------------------
# 8. Summary
# ---------------------------------------------------------------------------
$zipInfo = Get-Item -LiteralPath $ZipPath
$sizeMB  = [math]::Round($zipInfo.Length / 1MB, 2)
$tag     = "v$Version"

Write-Host ""
Write-Host "============================================================"
Write-Host " TONE3000 Player release packaged"
Write-Host "============================================================"
Write-Host " Version : $Version"
Write-Host " Zip     : $ZipPath"
Write-Host " Size    : $sizeMB MB"
Write-Host " SHA-256 : $hashHex"
Write-Host " SHA file: $shaPath"
Write-Host ""
Write-Host " Suggested release command:"
Write-Host "   gh release create $tag ``"
Write-Host "     `"$ZipPath`" ``"
Write-Host "     `"$shaPath`" ``"
Write-Host "     --title `"TONE3000 Player $tag`" ``"
Write-Host "     --notes `"Initial Windows VST3 build. See INSTALL.txt inside the zip.`""
Write-Host "============================================================"
