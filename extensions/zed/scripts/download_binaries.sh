#!/usr/bin/env bash
# Download the luma_lsp binary from GitHub releases and verify it against the
# release's SHA256SUMS manifest before extracting it.
# Referenced by extension.toml [language_servers.luma-lsp.binary].fetch_script.
# See extensions/shared/download-spec.md for the download protocol specification.
set -euo pipefail

readonly REPO="d3v0n5h1r3/luma"
readonly CHECKSUMS_FILENAME="SHA256SUMS"

cleanup() {
    rm -f -- "bin/${asset_name:-}" "bin/${CHECKSUMS_FILENAME}"
}
trap cleanup EXIT

# Prints the SHA-256 digest of a file (sha256sum on Linux, shasum on macOS).
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 -- "$1" | cut -d ' ' -f 1
    fi
}

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
    base_url="https://github.com/${REPO}/releases/latest/download"
else
    base_url="https://github.com/${REPO}/releases/download/${version}"
fi

printf 'Downloading %s...\n' "${asset_name}"
curl -fSL --proto-redir =https "${base_url}/${asset_name}" -o "bin/${asset_name}"
curl -fSL --proto-redir =https "${base_url}/${CHECKSUMS_FILENAME}" -o "bin/${CHECKSUMS_FILENAME}"

# A SHA256SUMS line is "<64-hex digest> <space or *><file name>". The digest
# length is checked with length() because mawk before 1.3.4-20200717 (the
# default awk on Debian 12 and Ubuntu 22.04) has no {n} interval expressions.
expected="$(awk -v name="${asset_name}" '
    length($1) == 64 && $1 ~ /^[0-9a-fA-F]+$/ { file = $2; sub(/^\*/, "", file); if (file == name) { print tolower($1); exit } }
' "bin/${CHECKSUMS_FILENAME}")"
if [[ -z "$expected" ]]; then
    printf '%s has no entry for %s; refusing to install.\n' "${CHECKSUMS_FILENAME}" "${asset_name}" >&2
    exit 1
fi

actual="$(sha256_of "bin/${asset_name}")"
if [[ "$actual" != "$expected" ]]; then
    printf 'Checksum mismatch for %s (expected %s, got %s); refusing to install.\n' \
        "${asset_name}" "$expected" "$actual" >&2
    exit 1
fi

tar -xzf "bin/${asset_name}" -C bin
chmod +x -- "bin/luma_lsp"
printf 'Done. Binary at bin/luma_lsp\n'
