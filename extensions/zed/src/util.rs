use zed_extension_api as zed;

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
