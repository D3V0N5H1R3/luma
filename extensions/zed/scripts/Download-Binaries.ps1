# Download Luma LSP binary for Zed (Windows) and verify it against the
# release's SHA256SUMS manifest before extracting it.
# Counterpart to download_binaries.sh for Unix systems.
#
# Usage:
#     pwsh Download-Binaries.ps1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Enforce TLS 1.2+ for secure downloads.
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12 -bor [System.Net.SecurityProtocolType]::Tls13

$Repo = 'd3v0n5h1r3/luma'
$BinaryName = 'luma_lsp'

# Detect architecture.
$Arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq [System.Runtime.InteropServices.Architecture]::Arm64) {
    'aarch64'
}
else {
    'x86_64'
}

$Suffix = "windows-${Arch}.zip"
$AssetName = "${BinaryName}-${Suffix}"

# Fetch the requested release (LUMA_VERSION, e.g. v0.16.0) or the latest one.
$Version = if ($env:LUMA_VERSION) { $env:LUMA_VERSION } else { 'latest' }
$ReleaseUrl = if ($Version -eq 'latest') {
    "https://api.github.com/repos/${Repo}/releases/latest"
}
else {
    "https://api.github.com/repos/${Repo}/releases/tags/${Version}"
}
Write-Host "Fetching release from ${ReleaseUrl}..."
$Release = Invoke-RestMethod -Uri $ReleaseUrl -Headers @{ Accept = 'application/json' }
$Tag = $Release.tag_name
Write-Host "Latest release: ${Tag}"

# Find asset download URL.
$Asset = $Release.assets | Where-Object { $_.name -eq $AssetName }
if (-not $Asset) {
    Write-Error "Asset '${AssetName}' not found in release ${Tag}."
    exit 1
}

$DownloadUrl = $Asset.browser_download_url
$ChecksumsName = 'SHA256SUMS'
$ChecksumsAsset = $Release.assets | Where-Object { $_.name -eq $ChecksumsName }
if (-not $ChecksumsAsset) {
    Write-Error "Release ${Tag} has no ${ChecksumsName}; refusing to install an unverified binary."
    exit 1
}

$BinDir = Join-Path -Path $PSScriptRoot -ChildPath '..' 'bin'
$ArchivePath = Join-Path -Path $BinDir -ChildPath $AssetName
$ChecksumsPath = Join-Path -Path $BinDir -ChildPath $ChecksumsName

# Create bin directory.
New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

try {
    # Download.
    Write-Host "Downloading ${DownloadUrl}..."
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $ArchivePath
    Invoke-WebRequest -Uri $ChecksumsAsset.browser_download_url -OutFile $ChecksumsPath

    # Verify. A SHA256SUMS line is "<64-hex digest> <space or *><file name>".
    $Expected = Get-Content -LiteralPath $ChecksumsPath |
        ForEach-Object {
            if ($_ -match '^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$' -and $Matches[2] -eq $AssetName) {
                $Matches[1].ToLowerInvariant()
            }
        } |
        Select-Object -First 1
    if (-not $Expected) {
        throw "${ChecksumsName} has no entry for ${AssetName}; refusing to install."
    }
    $Actual = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Actual -ne $Expected) {
        throw "Checksum mismatch for ${AssetName} (expected ${Expected}, got ${Actual}); refusing to install."
    }

    # Extract. -LiteralPath: -Path would treat [ ] in the path as wildcards.
    Write-Host "Extracting ${AssetName}..."
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $BinDir -Force
}
catch [System.Net.WebException] {
    Write-Error "Network error downloading binary: $_"
    exit 1
}
catch {
    Write-Error "Failed to download, verify, or extract binary: $_"
    exit 1
}
finally {
    # Clean up the archive and the checksum manifest.
    foreach ($Downloaded in @($ArchivePath, $ChecksumsPath)) {
        if (Test-Path -LiteralPath $Downloaded) {
            Remove-Item -LiteralPath $Downloaded -Force
        }
    }
}

$BinaryPath = Join-Path -Path $BinDir -ChildPath "${BinaryName}.exe"
if (Test-Path -Path $BinaryPath) {
    Write-Host "Installed: ${BinaryPath}"
}
else {
    Write-Error "Binary not found after extraction: ${BinaryPath}"
    exit 1
}
