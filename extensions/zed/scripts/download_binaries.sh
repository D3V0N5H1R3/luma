#!/usr/bin/env bash
# Download luma_lsp binary from GitHub releases.
# Referenced by extension.toml [language_servers.luma-lsp.binary].fetch_script.
# See extensions/shared/download-spec.md for the download protocol specification.
set -euo pipefail

cleanup() {
    rm -f -- "bin/${ASSET_NAME:-}"
}
trap cleanup EXIT

VERSION="${LUMA_VERSION:-latest}"
PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

case "$PLATFORM" in
  linux)  OS="linux" ;;
  darwin) OS="macos" ;;
  *)      printf 'Unsupported platform: %s\n' "$PLATFORM" && exit 1 ;;
esac

case "$ARCH" in
  x86_64)         ARCH="x86_64" ;;
  aarch64|arm64)  ARCH="aarch64" ;;
  *)              printf 'Unsupported architecture: %s\n' "$ARCH" && exit 1 ;;
esac

SUFFIX="${OS}-${ARCH}.tar.gz"
ASSET_NAME="luma_lsp-${SUFFIX}"
REPO="d3v0n5h1r3/luma"

mkdir -p bin

if [ "$VERSION" = "latest" ]; then
  DOWNLOAD_URL="https://github.com/${REPO}/releases/latest/download/${ASSET_NAME}"
else
  DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/${ASSET_NAME}"
fi

printf 'Downloading %s...\n' "${ASSET_NAME}"
curl -fSL --proto-redir =https "$DOWNLOAD_URL" -o "bin/${ASSET_NAME}"
tar -xzf "bin/${ASSET_NAME}" -C bin
rm -f -- "bin/${ASSET_NAME}"
chmod +x -- "bin/luma_lsp"
printf 'Done. Binary at bin/luma_lsp\n'
