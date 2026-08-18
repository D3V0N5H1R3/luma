use zed_extension_api as zed;

// ─── Path normalization ───────────────────────────────────────────

/// Normalizes path separators to the platform convention.
///
/// On Windows, `worktree.which()` can return paths with mixed separators
/// (e.g., `C:\Users\...\luma/build/Release/luma.exe`) because the worktree
/// root uses backslashes while relative segments use forward slashes.
/// PowerShell rejects such paths as command names. This function replaces
/// forward slashes with backslashes on Windows so the path is valid.
///
/// Uses the runtime `zed::Os` (not `cfg!(windows)`) because the extension
/// compiles to `wasm32-wasip1` — compile-time target checks are always
/// non-Windows.
pub(crate) fn normalize_path(path: String, os: zed::Os) -> String {
    if os == zed::Os::Windows {
        path.replace('/', "\\")
    } else {
        path
    }
}

// ─── JSON merge helper ────────────────────────────────────────────

/// Recursively merges `overrides` into `base`. For object values, merges
/// recursively; for all other types (including arrays), `overrides` replaces
/// `base` entirely. Array elements are never individually merged.
pub(crate) fn merge_json(base: &mut zed::serde_json::Value, overrides: &zed::serde_json::Value) {
    if let (Some(base_obj), Some(override_obj)) = (base.as_object_mut(), overrides.as_object()) {
        for (key, value) in override_obj {
            let entry = base_obj
                .entry(key.clone())
                .or_insert(zed::serde_json::Value::Null);
            merge_json(entry, value);
        }
    } else {
        *base = overrides.clone();
    }
}
