#!/usr/bin/env bash
# Download luma_lsp binary from GitHub releases.
# Referenced by extension.toml [language_servers.luma-lsp.binary].fetch_script.
# See extensions/shared/download-spec.md for the download protocol specification.
set -euo pipefail

readonly REPO="d3v0n5h1r3/luma"

cleanup() {
    rm -f -- "bin/${asset_name:-}"
}
trap cleanup EXIT

version="${LUMA_VERSION:-latest}"
platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"

case "$platform" in
    linux)  os="linux" ;;
    darwin) os="macos" ;;
    *)      printf 'Unsupported platform: %s\n' "$platform" >&2; exit 1 ;;
esac

case "$arch" in
    x86_64)         arch="x86_64" ;;
    aarch64|arm64)  arch="aarch64" ;;
    *)              printf 'Unsupported architecture: %s\n' "$arch" >&2; exit 1 ;;
esac

suffix="${os}-${arch}.tar.gz"
asset_name="luma_lsp-${suffix}"

mkdir -p bin

if [[ "$version" == "latest" ]]; then
    download_url="https://github.com/${REPO}/releases/latest/download/${asset_name}"
else
    download_url="https://github.com/${REPO}/releases/download/${version}/${asset_name}"
fi

printf 'Downloading %s...\n' "${asset_name}"
curl -fSL --proto-redir =https "$download_url" -o "bin/${asset_name}"
tar -xzf "bin/${asset_name}" -C bin
rm -f -- "bin/${asset_name}"
chmod +x -- "bin/luma_lsp"
printf 'Done. Binary at bin/luma_lsp\n'
