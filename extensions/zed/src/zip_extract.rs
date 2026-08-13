// Minimal ZIP archive extractor (pure Rust, WASM-safe).
//
// Replaces the `zip` crate to avoid the `typed-path` transitive dependency
// whose `#![feature(wasip2)]` / `std::os::wasi` usage breaks stable-channel
// Rust when Zed compiles extensions for `wasm32-wasip2`.
//
// Supports only Stored (method 0) and Deflated (method 8) entries — the two
// compression methods used by GitHub release assets. Uses the `flate2` crate
// (already a dependency) for inflation.

use std::io::{Cursor, Read, Write};

use flate2::read::DeflateDecoder;

/// Extract all files from a ZIP archive in `bytes` into `dest_dir`.
///
/// Directories are created automatically. Symbolic links and advanced
/// features (ZIP64 extended headers, encryption, etc.) are not supported —
/// the archive simply contains flat file entries, which is sufficient for
/// our GitHub-release binary archives.
pub(crate) fn extract_zip(bytes: &[u8], dest_dir: &str) -> Result<(), String> {
    let eocd = find_eocd(bytes)?;
    let cd_offset = read_u32(bytes, eocd + 16) as usize;
    let entry_count = read_u16(bytes, eocd + 10) as usize;

    let mut pos = cd_offset;
    for _ in 0..entry_count {
        if pos + 46 > bytes.len() {
            return Err("Truncated central directory entry".into());
        }
        let sig = read_u32(bytes, pos);
        if sig != 0x0201_4b50 {
            return Err(format!("Bad central directory signature: {sig:#010x}"));
        }

        let name_len = read_u16(bytes, pos + 28) as usize;
        let extra_len = read_u16(bytes, pos + 30) as usize;
        let comment_len = read_u16(bytes, pos + 32) as usize;
        let local_offset = read_u32(bytes, pos + 42) as usize;

        let name_bytes = &bytes[pos + 46..pos + 46 + name_len];
        let name = String::from_utf8_lossy(name_bytes);

        // Skip directory entries (trailing /).
        if !name.ends_with('/') {
            extract_local_entry(bytes, local_offset, dest_dir, &name)?;
        }

        pos += 46 + name_len + extra_len + comment_len;
    }

    Ok(())
}

/// Locate the End of Central Directory record.
fn find_eocd(bytes: &[u8]) -> Result<usize, String> {
    // EOCD is at least 22 bytes. Search backwards for the signature.
    if bytes.len() < 22 {
        return Err("Archive too small to contain EOCD".into());
    }
    let search_start = bytes.len().saturating_sub(22 + 65535); // max comment = 65535
    for i in (search_start..=bytes.len() - 22).rev() {
        if read_u32(bytes, i) == 0x0605_4b50 {
            return Ok(i);
        }
    }
    Err("End of Central Directory record not found".into())
}

/// Extract a single file from its local file header.
fn extract_local_entry(
    bytes: &[u8],
    offset: usize,
    dest_dir: &str,
    name: &str,
) -> Result<(), String> {
    if offset + 30 > bytes.len() {
        return Err("Truncated local file header".into());
    }
    let sig = read_u32(bytes, offset);
    if sig != 0x0403_4b50 {
        return Err(format!("Bad local file header signature: {sig:#010x}"));
    }

    let method = read_u16(bytes, offset + 8);
    let compressed_size = read_u32(bytes, offset + 18) as usize;
    let name_len = read_u16(bytes, offset + 26) as usize;
    let extra_len = read_u16(bytes, offset + 28) as usize;

    let data_start = offset + 30 + name_len + extra_len;
    if data_start + compressed_size > bytes.len() {
        return Err("Truncated file data".into());
    }
    let raw = &bytes[data_start..data_start + compressed_size];

    // Guard against path traversal.
    let sanitised = sanitise_path(name);
    let out_path = format!("{dest_dir}/{sanitised}");

    // Ensure parent directory exists.
    if let Some(parent_end) = out_path.rfind('/') {
        let parent = &out_path[..parent_end];
        std::fs::create_dir_all(parent)
            .map_err(|e| format!("Failed to create directory {parent}: {e}"))?;
    }

    let mut out_file = std::fs::File::create(&out_path)
        .map_err(|e| format!("Failed to create file {out_path}: {e}"))?;

    match method {
        0 => {
            // Stored — raw bytes.
            out_file
                .write_all(raw)
                .map_err(|e| format!("Failed to write {out_path}: {e}"))?;
        }
        8 => {
            // Deflated — inflate via flate2.
            let mut decoder = DeflateDecoder::new(Cursor::new(raw));
            let mut buf = Vec::new();
            decoder
                .read_to_end(&mut buf)
                .map_err(|e| format!("Failed to inflate {out_path}: {e}"))?;
            out_file
                .write_all(&buf)
                .map_err(|e| format!("Failed to write {out_path}: {e}"))?;
        }
        _ => {
            return Err(format!(
                "Unsupported compression method {method} for {name}"
            ));
        }
    }

    Ok(())
}

/// Strip path-traversal components (`..`, leading `/`).
fn sanitise_path(name: &str) -> String {
    name.split('/')
        .filter(|c| !c.is_empty() && *c != ".." && *c != ".")
        .collect::<Vec<_>>()
        .join("/")
}

// ─── Little-endian readers ───────────────────────────────────────

fn read_u16(buf: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([buf[offset], buf[offset + 1]])
}

fn read_u32(buf: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        buf[offset],
        buf[offset + 1],
        buf[offset + 2],
        buf[offset + 3],
    ])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sanitise_strips_traversal() {
        assert_eq!(sanitise_path("../../../etc/passwd"), "etc/passwd");
        assert_eq!(sanitise_path("foo/bar/baz.exe"), "foo/bar/baz.exe");
        assert_eq!(sanitise_path("/absolute/path"), "absolute/path");
        assert_eq!(sanitise_path("./relative"), "relative");
    }
}
