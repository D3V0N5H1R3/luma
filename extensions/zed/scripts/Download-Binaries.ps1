# Download Luma LSP binary for Zed (Windows).
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
} else {
    'x86_64'
}

$Suffix = "windows-${Arch}.zip"
$AssetName = "${BinaryName}-${Suffix}"

# Fetch latest release tag.
$ReleaseUrl = "https://api.github.com/repos/${Repo}/releases/latest"
Write-Host "Fetching latest release from ${ReleaseUrl}..."
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
$BinDir = Join-Path -Path $PSScriptRoot -ChildPath '..' 'bin'
$ArchivePath = Join-Path -Path $BinDir -ChildPath $AssetName

# Create bin directory.
New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

try {
    # Download.
    Write-Host "Downloading ${DownloadUrl}..."
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $ArchivePath

    # Extract.
    Write-Host "Extracting ${AssetName}..."
    Expand-Archive -Path $ArchivePath -DestinationPath $BinDir -Force
}
catch [System.Net.WebException] {
    Write-Error "Network error downloading binary: $_"
    exit 1
}
catch {
    Write-Error "Failed to download or extract binary: $_"
    exit 1
}
finally {
    # Clean up archive.
    if (Test-Path -Path $ArchivePath) {
        Remove-Item -Path $ArchivePath -Force
    }
}

$BinaryPath = Join-Path -Path $BinDir -ChildPath "${BinaryName}.exe"
if (Test-Path -Path $BinaryPath) {
    Write-Host "Installed: ${BinaryPath}"
} else {
    Write-Error "Binary not found after extraction: ${BinaryPath}"
    exit 1
}
