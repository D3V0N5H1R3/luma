use super::labels::{completion_label, keyword_label, symbol_label};
use super::util::merge_json;
use zed_extension_api::{self as zed, lsp, CodeLabelSpan};

// Helper: build a completion with the given kind and label.
fn make_completion(
    kind: lsp::CompletionKind,
    label: &str,
    detail: Option<&str>,
) -> lsp::Completion {
    lsp::Completion {
        label: label.to_string(),
        kind: Some(kind),
        detail: detail.map(|s| s.to_string()),
        label_details: None,
        insert_text_format: None,
    }
}

#[test]
fn label_function_with_detail() {
    let comp = make_completion(lsp::CompletionKind::Function, "greet", Some("string"));
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "function string greet()");
    assert_eq!(label.filter_range.start, 16);
    assert_eq!(label.filter_range.end, 21);
}

#[test]
fn label_function_without_detail() {
    let comp = make_completion(lsp::CompletionKind::Function, "main", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "function void main()");
}

#[test]
fn label_variable_with_type() {
    let comp = make_completion(lsp::CompletionKind::Variable, "count", Some("integer"));
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "integer count");
}

#[test]
fn label_module() {
    let comp = make_completion(lsp::CompletionKind::Module, "Math", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "namespace Math");
}

#[test]
fn label_enum() {
    let comp = make_completion(lsp::CompletionKind::Enum, "Color", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "choice Color");
}

#[test]
fn label_struct() {
    let comp = make_completion(lsp::CompletionKind::Struct, "Point", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "record Point");
}

#[test]
fn label_interface() {
    let comp = make_completion(lsp::CompletionKind::Interface, "Printable", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "interface Printable");
}

#[test]
fn label_keyword() {
    let comp = make_completion(lsp::CompletionKind::Keyword, "function", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "function");
}

// Symbol label tests

fn make_symbol(kind: lsp::SymbolKind, name: &str) -> lsp::Symbol {
    lsp::Symbol {
        name: name.to_string(),
        kind,
    }
}

#[test]
fn symbol_function() {
    let sym = make_symbol(lsp::SymbolKind::Function, "greet");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "function void greet()");
}

#[test]
fn symbol_record() {
    let sym = make_symbol(lsp::SymbolKind::Struct, "Point");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "record Point");
}

#[test]
fn symbol_choice() {
    let sym = make_symbol(lsp::SymbolKind::Enum, "Shape");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "choice Shape");
}

#[test]
fn symbol_namespace() {
    let sym = make_symbol(lsp::SymbolKind::Namespace, "Utils");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "namespace Utils");
}

// ── Edge case tests ───────────────────────────────────────────

#[test]
fn label_method_with_detail() {
    let comp = make_completion(lsp::CompletionKind::Method, "length", Some("integer"));
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "function integer length()");
}

#[test]
fn label_variable_without_type() {
    let comp = make_completion(lsp::CompletionKind::Variable, "x", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "x");
}

#[test]
fn label_variable_with_empty_detail() {
    let comp = make_completion(lsp::CompletionKind::Variable, "x", Some(""));
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "x");
}

#[test]
fn label_class_as_namespace() {
    let comp = make_completion(lsp::CompletionKind::Class, "String", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "namespace String");
}

#[test]
fn label_constant() {
    let comp = make_completion(lsp::CompletionKind::Constant, "PI", Some("number"));
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "number PI");
}

#[test]
fn label_field() {
    let comp = make_completion(lsp::CompletionKind::Field, "name", Some("string"));
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "string name");
}

#[test]
fn label_enum_member() {
    let comp = make_completion(lsp::CompletionKind::EnumMember, "Red", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "Red");
}

#[test]
fn label_constructor() {
    let comp = make_completion(lsp::CompletionKind::Constructor, "Circle", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "Circle");
}

#[test]
fn label_type_parameter() {
    let comp = make_completion(lsp::CompletionKind::TypeParameter, "T", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "T");
}

#[test]
fn label_no_kind_returns_none() {
    let comp = lsp::Completion {
        label: "test".to_string(),
        kind: None,
        detail: None,
        label_details: None,
        insert_text_format: None,
    };
    assert!(completion_label(&comp).is_none());
}

#[test]
fn symbol_interface() {
    let sym = make_symbol(lsp::SymbolKind::Interface, "Drawable");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "interface Drawable");
}

#[test]
fn symbol_module() {
    let sym = make_symbol(lsp::SymbolKind::Module, "IO");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "namespace IO");
}

#[test]
fn symbol_class() {
    let sym = make_symbol(lsp::SymbolKind::Class, "Buffer");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "record Buffer");
}

#[test]
fn symbol_variable() {
    let sym = make_symbol(lsp::SymbolKind::Variable, "count");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "count");
}

#[test]
fn symbol_constant() {
    let sym = make_symbol(lsp::SymbolKind::Constant, "MAX");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "MAX");
}

#[test]
fn symbol_constructor() {
    let sym = make_symbol(lsp::SymbolKind::Constructor, "new");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "function void new()");
}

#[test]
fn symbol_method() {
    let sym = make_symbol(lsp::SymbolKind::Method, "to_string");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "function void to_string()");
}

#[test]
fn symbol_type_parameter() {
    let sym = make_symbol(lsp::SymbolKind::TypeParameter, "T");
    let label = symbol_label(&sym).unwrap();
    assert_eq!(label.code, "T");
}

// ── Span range validation ─────────────────────────────────────

#[test]
fn label_spans_are_valid_ranges() {
    let comp = make_completion(lsp::CompletionKind::Function, "greet", Some("string"));
    let label = completion_label(&comp).unwrap();

    for span in &label.spans {
        if let CodeLabelSpan::CodeRange(range) = span {
            assert!(range.start <= range.end, "span start > end");
            assert!(
                (range.end as usize) <= label.code.len(),
                "span exceeds code length"
            );
        }
    }
}

#[test]
fn keyword_label_spans_are_valid() {
    let label = keyword_label("record", "MyRecord");
    assert_eq!(label.code, "record MyRecord");
    assert!(
        matches!(&label.spans[0], CodeLabelSpan::CodeRange(range) if range.start == 7 && range.end == 15)
    );
}

// ── Checksum verification tests ────────────────────────────────

use super::download::{lookup_expected_hash, verify_checksum};

#[test]
fn lookup_hash_finds_matching_entry() {
    let content = format!(
        "{}  luma-linux-x86_64.tar.gz\n{}  luma-macos-aarch64.tar.gz\n",
        "a".repeat(64),
        "b".repeat(64)
    );
    let hash = lookup_expected_hash(&content, "luma-linux-x86_64.tar.gz");
    assert_eq!(hash, Some("a".repeat(64)));
}

#[test]
fn lookup_hash_returns_none_for_missing() {
    let content = format!("{}  luma-linux-x86_64.tar.gz\n", "a".repeat(64));
    assert_eq!(lookup_expected_hash(&content, "nonexistent"), None);
}

#[test]
fn lookup_hash_handles_empty() {
    assert_eq!(lookup_expected_hash("", "anything"), None);
}

#[test]
fn lookup_hash_trims_filename() {
    let content = format!("{}  luma-linux-x86_64.tar.gz  \n", "c".repeat(64));
    let hash = lookup_expected_hash(&content, "luma-linux-x86_64.tar.gz");
    assert_eq!(hash, Some("c".repeat(64)));
}

#[test]
fn lookup_hash_rejects_short_hash() {
    // A non-64-char first token is not a valid SHA-256 digest and must be ignored.
    let content = "abc123  luma-linux-x86_64.tar.gz\n";
    assert_eq!(
        lookup_expected_hash(content, "luma-linux-x86_64.tar.gz"),
        None
    );
}

#[test]
fn lookup_hash_strips_binary_mode_marker() {
    // sha256sum binary mode emits "<hash> *<filename>".
    let content = format!("{} *luma-linux-x86_64.tar.gz\n", "d".repeat(64));
    let hash = lookup_expected_hash(&content, "luma-linux-x86_64.tar.gz");
    assert_eq!(hash, Some("d".repeat(64)));
}

#[test]
fn lookup_hash_matches_shared_conformance_fixture() {
    // extensions/shared/sha256sums-sample.txt is the golden input shared by the
    // VS Code and Zed parser tests. Running the real lookup_expected_hash
    // over it proves the Rust parser agrees with the VS Code implementation.
    let content = include_str!("../../shared/sha256sums-sample.txt");

    assert_eq!(
        lookup_expected_hash(content, "luma_lsp-linux-x86_64.tar.gz"),
        Some("1".repeat(64))
    );
    assert_eq!(
        lookup_expected_hash(content, "luma_lsp-macos-aarch64.tar.gz"),
        Some("2".repeat(64))
    );
    // The "*" binary-mode marker is stripped.
    assert_eq!(
        lookup_expected_hash(content, "luma_dap-windows-x86_64.zip"),
        Some("3".repeat(64))
    );
    // The uppercase digest normalises to lowercase.
    assert_eq!(
        lookup_expected_hash(content, "luma-linux-x86_64.tar.gz"),
        Some("a".repeat(64))
    );
    // Comment lines and the too-short digest are ignored.
    assert_eq!(
        lookup_expected_hash(content, "ignored-short-hash.tar.gz"),
        None
    );
}

#[test]
fn verify_checksum_accepts_correct_hash() {
    use std::io::Write;
    let dir = std::env::temp_dir().join("luma_test_checksum");
    let _ = std::fs::create_dir_all(&dir);
    let file_path = dir.join("test_file.txt");
    let mut f = std::fs::File::create(&file_path).unwrap();
    f.write_all(b"hello world").unwrap();
    drop(f);

    // SHA-256 of "hello world"
    let expected = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";
    assert!(verify_checksum(file_path.to_str().unwrap(), expected).is_ok());

    // Wrong hash
    assert!(verify_checksum(
        file_path.to_str().unwrap(),
        "0000000000000000000000000000000000000000000000000000000000000000"
    )
    .is_err());

    let _ = std::fs::remove_dir_all(&dir);
}

// ── merge_json tests ──────────────────────────────────────────

#[test]
fn merge_json_overrides_scalar() {
    let mut base = zed::serde_json::json!({"a": 1});
    let overrides = zed::serde_json::json!({"a": 2});
    merge_json(&mut base, &overrides);
    assert_eq!(base, zed::serde_json::json!({"a": 2}));
}

#[test]
fn merge_json_adds_new_key() {
    let mut base = zed::serde_json::json!({"a": 1});
    let overrides = zed::serde_json::json!({"b": 2});
    merge_json(&mut base, &overrides);
    assert_eq!(base, zed::serde_json::json!({"a": 1, "b": 2}));
}

#[test]
fn merge_json_deep_merge() {
    let mut base = zed::serde_json::json!({
        "luma": {
            "inlayHints": { "enabled": true }
        }
    });
    let overrides = zed::serde_json::json!({
        "luma": {
            "inlayHints": { "enabled": false }
        }
    });
    merge_json(&mut base, &overrides);
    assert_eq!(base["luma"]["inlayHints"]["enabled"], false);
}

#[test]
fn merge_json_preserves_unrelated_keys() {
    let mut base = zed::serde_json::json!({
        "luma": {
            "inlayHints": { "enabled": true },
            "other": "value"
        }
    });
    let overrides = zed::serde_json::json!({
        "luma": {
            "inlayHints": { "enabled": false }
        }
    });
    merge_json(&mut base, &overrides);
    assert_eq!(base["luma"]["inlayHints"]["enabled"], false);
    assert_eq!(base["luma"]["other"], "value");
}

#[test]
fn merge_json_no_overrides_keeps_defaults() {
    let mut base = zed::serde_json::json!({
        "luma": {
            "inlayHints": { "enabled": true }
        }
    });
    let overrides = zed::serde_json::json!({});
    merge_json(&mut base, &overrides);
    assert_eq!(base["luma"]["inlayHints"]["enabled"], true);
}

// ── bool_setting helper tests ─────────────────────────────────

use super::bool_setting;

#[test]
fn bool_setting_reads_nested_value() {
    let v = zed::serde_json::json!({"inlayHints": {"enabled": false}});
    assert!(!bool_setting(Some(&v), &["inlayHints", "enabled"], true));
}

#[test]
fn bool_setting_falls_back_on_missing_key() {
    let v = zed::serde_json::json!({"inlayHints": {}});
    assert!(bool_setting(Some(&v), &["inlayHints", "enabled"], true));
}

#[test]
fn bool_setting_falls_back_on_none() {
    assert!(bool_setting(None, &["inlayHints", "enabled"], true));
}

#[test]
fn bool_setting_falls_back_on_non_bool() {
    let v = zed::serde_json::json!({"inlayHints": {"enabled": "yes"}});
    assert!(bool_setting(Some(&v), &["inlayHints", "enabled"], true));
}

// ── build_workspace_config tests ──────────────────────────────

use super::build_workspace_config;

#[test]
fn build_workspace_config_never_sends_code_lens() {
    // Zed does not support code lens (VS Code only, per FEATURE_PARITY.md).
    // The workspace configuration sent to the LSP must not include a
    // `codeLens` key regardless of user settings, since Zed has no way to
    // request or render code lenses.
    let config = build_workspace_config(None);
    assert!(config["luma"].get("codeLens").is_none());

    let user_settings = zed::serde_json::json!({"luma": {"codeLens": {"enabled": true}}});
    let config = build_workspace_config(Some(&user_settings));
    assert!(config["luma"].get("codeLens").is_none());
}

#[test]
fn build_workspace_config_reads_inlay_hints_and_diagnostics() {
    let user_settings = zed::serde_json::json!({"luma": {"inlayHints": {"enabled": false}, "diagnostics": {"onSave": true}}});
    let config = build_workspace_config(Some(&user_settings));
    assert_eq!(config["luma"]["inlayHints"]["enabled"], false);
    assert_eq!(config["luma"]["diagnostics"]["onSave"], true);
}

// ── Additional label coverage (property, snippet, guards) ─────

#[test]
fn label_property_with_type() {
    let comp = make_completion(lsp::CompletionKind::Property, "width", Some("integer"));
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "integer width");
}

#[test]
fn label_property_without_type() {
    let comp = make_completion(lsp::CompletionKind::Property, "width", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "width");
}

#[test]
fn label_snippet_uses_literal_span() {
    let comp = make_completion(lsp::CompletionKind::Snippet, "for", None);
    let label = completion_label(&comp).unwrap();
    assert_eq!(label.code, "for");
    match &label.spans[0] {
        CodeLabelSpan::Literal(literal) => {
            assert_eq!(literal.text, "for");
            assert_eq!(literal.highlight_name.as_deref(), Some("keyword"));
        }
        _ => panic!("snippet label should use a literal span"),
    }
}

#[test]
fn label_unhandled_completion_kind_returns_none() {
    // `Text` has no Luma keyword mapping, so no label is produced.
    let comp = make_completion(lsp::CompletionKind::Text, "plain", None);
    assert!(completion_label(&comp).is_none());
}

#[test]
fn label_non_ascii_completion_returns_none() {
    // Byte-offset ranges are only valid for ASCII, so non-ASCII labels are skipped.
    let comp = make_completion(lsp::CompletionKind::Function, "café", Some("string"));
    assert!(completion_label(&comp).is_none());
}

#[test]
fn symbol_non_ascii_returns_none() {
    let sym = make_symbol(lsp::SymbolKind::Function, "café");
    assert!(symbol_label(&sym).is_none());
}

#[test]
fn symbol_unhandled_kind_returns_none() {
    // `File` has no Luma keyword mapping, so no label is produced.
    let sym = make_symbol(lsp::SymbolKind::File, "main.luma");
    assert!(symbol_label(&sym).is_none());
}

// ── no_binary_message / BinarySource ──────────────────────────

use super::{BinarySource, LumaExtension};

#[test]
fn no_binary_message_mentions_binary_and_build_command() {
    let msg = LumaExtension::no_binary_message("luma_lsp");
    assert!(msg.contains("luma_lsp"));
    assert!(msg.contains("cmake --build"));
    assert!(msg.contains("PATH"));
}

#[test]
fn binary_source_path_accessor_returns_inner_path() {
    assert_eq!(
        BinarySource::Path("/usr/bin/luma_lsp".to_string()).path(),
        "/usr/bin/luma_lsp"
    );
    assert_eq!(
        BinarySource::Downloaded("cache/luma_dap".to_string()).path(),
        "cache/luma_dap"
    );
}

// ── Platform asset naming ─────────────────────────────────────

use super::download::{binary_filename, platform_asset_name_for, platform_suffix};

#[test]
fn platform_asset_name_for_supported_targets() {
    assert_eq!(
        platform_asset_name_for("luma_lsp", zed::Os::Linux, zed::Architecture::X8664).as_deref(),
        Some("luma_lsp-linux-x86_64.tar.gz")
    );
    assert_eq!(
        platform_asset_name_for("luma_lsp", zed::Os::Linux, zed::Architecture::Aarch64).as_deref(),
        Some("luma_lsp-linux-aarch64.tar.gz")
    );
    assert_eq!(
        platform_asset_name_for("luma_lsp", zed::Os::Mac, zed::Architecture::X8664).as_deref(),
        Some("luma_lsp-macos-x86_64.tar.gz")
    );
    assert_eq!(
        platform_asset_name_for("luma_dap", zed::Os::Mac, zed::Architecture::Aarch64).as_deref(),
        Some("luma_dap-macos-aarch64.tar.gz")
    );
    assert_eq!(
        platform_asset_name_for("luma_dap", zed::Os::Windows, zed::Architecture::X8664).as_deref(),
        Some("luma_dap-windows-x86_64.zip")
    );
    assert_eq!(
        platform_asset_name_for("luma_dap", zed::Os::Windows, zed::Architecture::Aarch64)
            .as_deref(),
        Some("luma_dap-windows-aarch64.zip")
    );
}

#[test]
fn platform_asset_name_for_unsupported_arch_is_none() {
    // 32-bit x86 is not a published target.
    assert!(platform_asset_name_for("luma_lsp", zed::Os::Linux, zed::Architecture::X86).is_none());
    assert!(platform_suffix(zed::Os::Mac, zed::Architecture::X86).is_none());
}

#[test]
fn platform_suffix_extension_matches_os() {
    assert!(platform_suffix(zed::Os::Windows, zed::Architecture::X8664)
        .unwrap()
        .ends_with(".zip"));
    assert!(platform_suffix(zed::Os::Linux, zed::Architecture::X8664)
        .unwrap()
        .ends_with(".tar.gz"));
    assert!(platform_suffix(zed::Os::Mac, zed::Architecture::Aarch64)
        .unwrap()
        .ends_with(".tar.gz"));
}

#[test]
fn binary_filename_adds_exe_on_windows_only() {
    assert_eq!(
        binary_filename("luma_lsp", zed::Os::Windows),
        "luma_lsp.exe"
    );
    assert_eq!(
        binary_filename("luma_dap", zed::Os::Windows),
        "luma_dap.exe"
    );
    assert_eq!(binary_filename("luma_lsp", zed::Os::Linux), "luma_lsp");
    assert_eq!(binary_filename("luma_dap", zed::Os::Mac), "luma_dap");
}

// ── download_binary orchestration (mocked downloader) ─────────
// The BinaryDownloader/StatusReporter traits exist so the full download
// pipeline can be exercised without network access or archive extraction.
// These tests cover caching, error reporting, per-platform archive handling,
// and the checksum verify/skip/abort branches.

use super::download::{download_binary, BinaryDownloader, StatusReporter, ZedDownloader};
use crate::generated::download_constants::CHECKSUMS_FILENAME;
use std::cell::RefCell;

fn file_type_name(file_type: &zed::DownloadedFileType) -> &'static str {
    match file_type {
        zed::DownloadedFileType::Gzip => "gzip",
        zed::DownloadedFileType::GzipTar => "gzip-tar",
        zed::DownloadedFileType::Zip => "zip",
        zed::DownloadedFileType::Uncompressed => "uncompressed",
    }
}

struct DownloadCall {
    url: String,
    output_path: String,
    file_type: String,
}

struct ExtractCall {
    archive_path: String,
    dest_dir: String,
    file_type: String,
}

struct MockDownloader {
    calls: RefCell<Vec<DownloadCall>>,
    extract_calls: RefCell<Vec<ExtractCall>>,
    make_exec_calls: RefCell<Vec<String>>,
    /// If a download URL contains this substring, the download fails.
    fail_url_substring: Option<String>,
    /// `Some` makes `read_text_file` return this content; `None` makes it fail.
    sums_content: Option<String>,
    /// Bytes written to disk when the archive is downloaded uncompressed for
    /// verification. This is what `verify_checksum` hashes, so it must match the
    /// SHA256SUMS entry for a successful verify.
    archive_content: Vec<u8>,
    /// When set, a successful `extract_archive` call writes `binary_content`
    /// here, simulating the archive being unpacked.
    extract_path: Option<String>,
    /// Bytes written to `extract_path` on extraction. Deliberately DISTINCT from
    /// `archive_content` in the checksum tests to prove the code verifies the
    /// archive, not the extracted binary.
    binary_content: Vec<u8>,
    /// Result returned by `extract_archive` (default `Ok`).
    extract_result: Result<(), String>,
    make_exec_result: Result<(), String>,
}

impl Default for MockDownloader {
    fn default() -> Self {
        Self {
            calls: RefCell::new(Vec::new()),
            extract_calls: RefCell::new(Vec::new()),
            make_exec_calls: RefCell::new(Vec::new()),
            fail_url_substring: None,
            sums_content: None,
            archive_content: Vec::new(),
            extract_path: None,
            binary_content: Vec::new(),
            extract_result: Ok(()),
            make_exec_result: Ok(()),
        }
    }
}

impl BinaryDownloader for MockDownloader {
    fn download_file(
        &self,
        url: &str,
        output_path: &str,
        file_type: zed::DownloadedFileType,
    ) -> Result<(), String> {
        self.calls.borrow_mut().push(DownloadCall {
            url: url.to_string(),
            output_path: output_path.to_string(),
            file_type: file_type_name(&file_type).to_string(),
        });

        if let Some(substring) = &self.fail_url_substring {
            if url.contains(substring.as_str()) {
                return Err("simulated download failure".to_string());
            }
        }

        // Only `Uncompressed` fetches write to disk here (the archive and the
        // SHA256SUMS manifest); both must land on disk so `verify_checksum` can
        // hash the archive and tests can observe the manifest being cleaned up.
        // Archives are no longer downloaded pre-extracted — extraction is a
        // separate in-process step (see `extract_archive` below).
        if file_type == zed::DownloadedFileType::Uncompressed {
            if output_path.ends_with(CHECKSUMS_FILENAME) {
                let bytes = self.sums_content.as_deref().unwrap_or("<unreadable>");
                write_mock_file(output_path, bytes.as_bytes())?;
            } else {
                write_mock_file(output_path, &self.archive_content)?;
            }
        }

        Ok(())
    }

    fn make_executable(&self, path: &str) -> Result<(), String> {
        self.make_exec_calls.borrow_mut().push(path.to_string());
        self.make_exec_result.clone()
    }

    fn read_text_file(&self, _path: &str) -> Result<String, String> {
        match &self.sums_content {
            Some(content) => Ok(content.clone()),
            None => Err("simulated read failure".to_string()),
        }
    }

    fn extract_archive(
        &self,
        archive_path: &str,
        dest_dir: &str,
        file_type: zed::DownloadedFileType,
    ) -> Result<(), String> {
        self.extract_calls.borrow_mut().push(ExtractCall {
            archive_path: archive_path.to_string(),
            dest_dir: dest_dir.to_string(),
            file_type: file_type_name(&file_type).to_string(),
        });
        // Propagate a simulated extraction failure before writing anything.
        self.extract_result.clone()?;
        // Simulate the unpacked binary appearing on disk at `extract_path`.
        if let Some(path) = &self.extract_path {
            write_mock_file(path, &self.binary_content)?;
        }
        Ok(())
    }
}

/// Writes `content` to `path`, creating parent directories as needed.
fn write_mock_file(path: &str, content: &[u8]) -> Result<(), String> {
    if let Some(parent) = std::path::Path::new(path).parent() {
        std::fs::create_dir_all(parent).map_err(|e| format!("mock create_dir_all failed: {e}"))?;
    }
    std::fs::write(path, content).map_err(|e| format!("mock write failed: {e}"))
}

#[derive(Default)]
struct MockReporter {
    downloading: RefCell<u32>,
    success: RefCell<u32>,
    failed: RefCell<Vec<String>>,
}

impl StatusReporter for MockReporter {
    fn report_downloading(&self) {
        *self.downloading.borrow_mut() += 1;
    }

    fn report_failed(&self, msg: &str) {
        self.failed.borrow_mut().push(msg.to_string());
    }

    fn report_success(&self) {
        *self.success.borrow_mut() += 1;
    }
}

/// Removes a directory tree when dropped, so filesystem-touching tests clean
/// up even if an assertion panics.
struct DirGuard(String);

impl Drop for DirGuard {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn make_release(version: &str, assets: &[(&str, &str)]) -> zed::GithubRelease {
    zed::GithubRelease {
        version: version.to_string(),
        assets: assets
            .iter()
            .map(|&(name, url)| zed::GithubReleaseAsset {
                name: name.to_string(),
                download_url: url.to_string(),
            })
            .collect(),
    }
}

// SHA-256 of "hello world", reused by the checksum tests.
const HELLO_WORLD_SHA256: &str = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";

#[test]
fn download_missing_asset_returns_error_without_reporting() {
    let mock = MockDownloader::default();
    let reporter = MockReporter::default();
    let release = make_release(
        "v1.0.0",
        &[("other-asset.tar.gz", "https://example.com/other")],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    let err = result.unwrap_err();
    assert!(err.contains("No asset named 'luma_lsp-linux-x86_64.tar.gz'"));
    assert!(mock.calls.borrow().is_empty());
    assert_eq!(*reporter.downloading.borrow(), 0);
    assert!(reporter.failed.borrow().is_empty());
}

#[test]
fn download_failure_reports_and_includes_recovery_hint() {
    let version = "dl-fail-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    let mock = MockDownloader {
        fail_url_substring: Some("https://".to_string()),
        ..Default::default()
    };
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[(
            "luma_lsp-linux-x86_64.tar.gz",
            "https://example.com/lsp.tar.gz",
        )],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        " Syntax highlighting will still work.",
    );

    let err = result.unwrap_err();
    assert!(err.contains("Failed to download luma_lsp"));
    assert!(err.contains("Syntax highlighting will still work."));
    assert_eq!(*reporter.downloading.borrow(), 1);
    assert_eq!(reporter.failed.borrow().len(), 1);

    let calls = mock.calls.borrow();
    // The archive is fetched uncompressed first (to verify before extracting);
    // that download fails, so nothing else runs.
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].url, "https://example.com/lsp.tar.gz");
    assert_eq!(
        calls[0].output_path,
        format!("{version_dir}/luma_lsp-linux-x86_64.tar.gz")
    );
    assert_eq!(calls[0].file_type, "uncompressed");
}

#[test]
fn download_success_linux_without_checksums() {
    let version = "linux-ok-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    let mock = MockDownloader::default();
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[(
            "luma_lsp-linux-x86_64.tar.gz",
            "https://example.com/lsp.tar.gz",
        )],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    assert_eq!(result.unwrap(), format!("{version_dir}/luma_lsp"));
    assert_eq!(*reporter.downloading.borrow(), 1);
    assert_eq!(*reporter.success.borrow(), 1);
    assert!(reporter.failed.borrow().is_empty());
    assert_eq!(
        *mock.make_exec_calls.borrow(),
        vec![format!("{version_dir}/luma_lsp")]
    );

    let calls = mock.calls.borrow();
    // No SHA256SUMS asset: fetch the archive uncompressed once (verify skipped),
    // then extract it in-process — no second download.
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].file_type, "uncompressed");
    let extracts = mock.extract_calls.borrow();
    assert_eq!(extracts.len(), 1);
    assert_eq!(extracts[0].file_type, "gzip-tar");
    // Extraction targets the verified archive and unpacks into the install dir.
    assert_eq!(
        extracts[0].archive_path,
        format!("{version_dir}/luma_lsp-linux-x86_64.tar.gz")
    );
    assert_eq!(extracts[0].dest_dir, version_dir);
}

#[test]
fn download_success_windows_uses_exe_and_zip() {
    let version = "win-ok-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    let mock = MockDownloader::default();
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[("luma_lsp-windows-x86_64.zip", "https://example.com/lsp.zip")],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Windows,
        "luma_lsp-windows-x86_64.zip",
        &release,
        "",
    );

    assert_eq!(result.unwrap(), format!("{version_dir}/luma_lsp.exe"));
    assert_eq!(
        *mock.make_exec_calls.borrow(),
        vec![format!("{version_dir}/luma_lsp.exe")]
    );

    let calls = mock.calls.borrow();
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].file_type, "uncompressed");
    let extracts = mock.extract_calls.borrow();
    assert_eq!(extracts.len(), 1);
    assert_eq!(extracts[0].file_type, "zip");
}

#[test]
fn download_make_executable_failure_returns_error() {
    let version = "mkexec-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    let mock = MockDownloader {
        make_exec_result: Err("chmod denied".to_string()),
        ..Default::default()
    };
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[("luma_lsp-linux-x86_64.tar.gz", "https://example.com/a")],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    let err = result.unwrap_err();
    assert!(err.contains("Failed to make luma_lsp executable"));
    assert_eq!(reporter.failed.borrow().len(), 1);
    assert_eq!(*reporter.success.borrow(), 0);
}

#[test]
fn download_cache_hit_skips_download() {
    let version = "cache-9.9.9";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());
    std::fs::create_dir_all(&version_dir).unwrap();
    let binary_path = format!("{version_dir}/luma_lsp");
    std::fs::write(&binary_path, b"cached").unwrap();

    let mock = MockDownloader::default();
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[("luma_lsp-linux-x86_64.tar.gz", "https://example.com/a")],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    assert_eq!(result.unwrap(), binary_path);
    assert!(
        mock.calls.borrow().is_empty(),
        "cache hit must not download"
    );
    assert_eq!(*reporter.downloading.borrow(), 0);
    assert_eq!(*reporter.success.borrow(), 0);
}

#[test]
fn download_cache_path_that_is_a_directory_is_not_a_cache_hit() {
    // check_cached_binary must treat a non-regular-file at the cache path as a
    // miss (not a hit), so the download proceeds instead of returning the dir.
    let version = "cache-dir-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());
    let binary_path = format!("{version_dir}/luma_lsp");
    std::fs::create_dir_all(&binary_path).unwrap();

    let mock = MockDownloader::default();
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[("luma_lsp-linux-x86_64.tar.gz", "https://example.com/a")],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    assert_eq!(result.unwrap(), binary_path);
    assert!(
        !mock.calls.borrow().is_empty(),
        "a directory at the cache path must not count as a cached binary"
    );
    assert_eq!(*reporter.downloading.borrow(), 1);
}

#[test]
fn download_checksum_match_succeeds() {
    let version = "sum-ok-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());
    let binary_path = format!("{version_dir}/luma_lsp");
    let asset = "luma_lsp-linux-x86_64.tar.gz";

    // The archive hashes to HELLO_WORLD_SHA256; the extracted binary has
    // DIFFERENT bytes. Verification must therefore hash the ARCHIVE, not the
    // extracted binary, for this to succeed.
    let mock = MockDownloader {
        archive_content: b"hello world".to_vec(),
        extract_path: Some(binary_path.clone()),
        binary_content: b"the extracted binary contents".to_vec(),
        sums_content: Some(format!("{HELLO_WORLD_SHA256}  {asset}\n")),
        ..Default::default()
    };
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[
            (asset, "https://example.com/lsp.tar.gz"),
            ("SHA256SUMS", "https://example.com/SHA256SUMS"),
        ],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        asset,
        &release,
        "",
    );

    assert_eq!(result.unwrap(), binary_path);
    assert!(
        std::path::Path::new(&binary_path).exists(),
        "verified archive must be extracted"
    );
    assert_eq!(*reporter.success.borrow(), 1);
    assert!(reporter.failed.borrow().is_empty());
    // Archive (uncompressed) + SHA256SUMS (uncompressed); extraction is a
    // separate in-process step, not a download.
    let calls = mock.calls.borrow();
    assert_eq!(calls.len(), 2);
    assert_eq!(calls[0].file_type, "uncompressed");
    assert_eq!(calls[1].file_type, "uncompressed");
    let extracts = mock.extract_calls.borrow();
    assert_eq!(extracts.len(), 1);
    assert_eq!(extracts[0].file_type, "gzip-tar");
}

#[test]
fn download_checksum_mismatch_aborts_before_extract() {
    let version = "sum-bad-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());
    let binary_path = format!("{version_dir}/luma_lsp");
    let asset = "luma_lsp-linux-x86_64.tar.gz";

    // The archive bytes do NOT hash to the SHA256SUMS entry, so verification
    // fails and extraction must never run.
    let mock = MockDownloader {
        archive_content: b"tampered archive".to_vec(),
        extract_path: Some(binary_path.clone()),
        binary_content: b"should never be written".to_vec(),
        sums_content: Some(format!("{HELLO_WORLD_SHA256}  {asset}\n")),
        ..Default::default()
    };
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[
            (asset, "https://example.com/lsp.tar.gz"),
            ("SHA256SUMS", "https://example.com/SHA256SUMS"),
        ],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        asset,
        &release,
        "",
    );

    let err = result.unwrap_err();
    assert!(err.contains("Checksum mismatch"));
    assert!(err.contains("aborting install"));
    assert!(
        !std::path::Path::new(&binary_path).exists(),
        "extraction must not run when the archive fails verification"
    );
    assert!(
        mock.make_exec_calls.borrow().is_empty(),
        "make_executable must not run on a failed verify"
    );
    assert_eq!(reporter.failed.borrow().len(), 1);
    assert_eq!(*reporter.success.borrow(), 0);
    // Archive + SHA256SUMS were fetched; extraction never ran.
    assert_eq!(mock.calls.borrow().len(), 2);
    assert!(
        mock.extract_calls.borrow().is_empty(),
        "extraction must not run when the archive fails verification"
    );
}

#[test]
fn download_aborts_when_sha256sums_download_fails() {
    let version = "sums-dlfail-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    // Only the SHA256SUMS download fails; the archive download succeeds.
    let mock = MockDownloader {
        fail_url_substring: Some("SHA256SUMS".to_string()),
        ..Default::default()
    };
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[
            (
                "luma_lsp-linux-x86_64.tar.gz",
                "https://example.com/lsp.tar.gz",
            ),
            ("SHA256SUMS", "https://example.com/SHA256SUMS"),
        ],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    // SHA256SUMS is present but cannot be fetched, so the install aborts rather
    // than proceeding unverified (matches download-spec.md and VS Code).
    let err = result.unwrap_err();
    assert!(err.contains("Failed to download SHA256SUMS"));
    assert_eq!(*reporter.success.borrow(), 0);
    assert_eq!(reporter.failed.borrow().len(), 1);
    assert!(mock.make_exec_calls.borrow().is_empty());
    // Archive + failed SHA256SUMS; extraction never ran.
    assert_eq!(mock.calls.borrow().len(), 2);
    assert!(mock.extract_calls.borrow().is_empty());
}

#[test]
fn download_aborts_when_entry_missing() {
    let version = "entry-miss-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    let mock = MockDownloader {
        sums_content: Some(format!("{}  some-other-file.tar.gz\n", "a".repeat(64))),
        ..Default::default()
    };
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[
            (
                "luma_lsp-linux-x86_64.tar.gz",
                "https://example.com/lsp.tar.gz",
            ),
            ("SHA256SUMS", "https://example.com/SHA256SUMS"),
        ],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    // The manifest exists but has no entry for this asset, so the archive is
    // unverifiable and the install aborts.
    let err = result.unwrap_err();
    assert!(err.contains("no entry in SHA256SUMS"));
    assert_eq!(*reporter.success.borrow(), 0);
    assert!(mock.make_exec_calls.borrow().is_empty());
    assert_eq!(mock.calls.borrow().len(), 2);
    assert!(mock.extract_calls.borrow().is_empty());
}

#[test]
fn download_aborts_when_sha256sums_unreadable() {
    let version = "sums-unread-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    // The SHA256SUMS asset is present and downloads, but reading it back fails
    // (`sums_content: None`), so the archive cannot be verified.
    let mock = MockDownloader::default();
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[
            (
                "luma_lsp-linux-x86_64.tar.gz",
                "https://example.com/lsp.tar.gz",
            ),
            ("SHA256SUMS", "https://example.com/SHA256SUMS"),
        ],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    let err = result.unwrap_err();
    assert!(err.contains("Failed to read SHA256SUMS"));
    assert_eq!(*reporter.success.borrow(), 0);
    assert!(mock.make_exec_calls.borrow().is_empty());
    assert_eq!(mock.calls.borrow().len(), 2);
    assert!(mock.extract_calls.borrow().is_empty());
    // The downloaded (but unreadable) manifest must not be left behind.
    assert!(
        !std::path::Path::new(&format!("{version_dir}/{CHECKSUMS_FILENAME}")).exists(),
        "unreadable SHA256SUMS must be cleaned up on the abort path"
    );
}

#[test]
fn download_extract_failure_returns_error() {
    let version = "extract-fail-1";
    let version_dir = format!("luma_lsp-{version}");
    let _guard = DirGuard(version_dir.clone());

    // No SHA256SUMS asset, so verification is skipped and extraction runs — but
    // the (in-process) extraction step fails.
    let mock = MockDownloader {
        extract_result: Err("corrupt archive".to_string()),
        ..Default::default()
    };
    let reporter = MockReporter::default();
    let release = make_release(
        version,
        &[("luma_lsp-linux-x86_64.tar.gz", "https://example.com/a")],
    );

    let result = download_binary(
        &mock,
        &reporter,
        "luma_lsp",
        zed::Os::Linux,
        "luma_lsp-linux-x86_64.tar.gz",
        &release,
        "",
    );

    let err = result.unwrap_err();
    assert!(err.contains("Failed to extract luma_lsp"));
    assert!(err.contains("corrupt archive"));
    assert_eq!(*reporter.success.borrow(), 0);
    assert_eq!(reporter.failed.borrow().len(), 1);
    // make_executable must not run after a failed extraction.
    assert!(mock.make_exec_calls.borrow().is_empty());
    // The archive was downloaded once (uncompressed) and extraction attempted.
    assert_eq!(mock.calls.borrow().len(), 1);
    assert_eq!(mock.extract_calls.borrow().len(), 1);
}

// ── Real in-process extraction (ZedDownloader) ────────────────
// The mock tests above cover the orchestration; these prove the actual
// pure-Rust flate2/tar and zip readers unpack a real archive. They run
// natively under `cargo test` (the same reader code is compiled for WASM).

#[test]
fn zed_downloader_extracts_gzip_tar() {
    let dir = format!("rt-tgz-{}", std::process::id());
    let _guard = DirGuard(dir.clone());
    std::fs::create_dir_all(&dir).unwrap();

    // Build a .tar.gz containing a single file named "luma_lsp".
    let mut tar_bytes = Vec::new();
    {
        let mut builder = tar::Builder::new(&mut tar_bytes);
        let content = b"gzip-tar binary bytes";
        let mut header = tar::Header::new_gnu();
        header.set_size(content.len() as u64);
        header.set_mode(0o644);
        header.set_cksum();
        builder
            .append_data(&mut header, "luma_lsp", &content[..])
            .unwrap();
        builder.finish().unwrap();
    }
    let mut gz_bytes = Vec::new();
    {
        use std::io::Write;
        let mut encoder =
            flate2::write::GzEncoder::new(&mut gz_bytes, flate2::Compression::default());
        encoder.write_all(&tar_bytes).unwrap();
        encoder.finish().unwrap();
    }
    let archive_path = format!("{dir}/archive.tar.gz");
    std::fs::write(&archive_path, &gz_bytes).unwrap();

    ZedDownloader
        .extract_archive(&archive_path, &dir, zed::DownloadedFileType::GzipTar)
        .unwrap();

    let extracted = std::fs::read(format!("{dir}/luma_lsp")).unwrap();
    assert_eq!(extracted, b"gzip-tar binary bytes");
}

#[test]
fn zed_downloader_extracts_zip() {
    let dir = format!("rt-zip-{}", std::process::id());
    let _guard = DirGuard(dir.clone());
    std::fs::create_dir_all(&dir).unwrap();

    // Build a .zip containing a single file named "luma_lsp.exe".
    let mut zip_bytes = Vec::new();
    {
        use std::io::Write;
        let mut writer = zip::ZipWriter::new(std::io::Cursor::new(&mut zip_bytes));
        let options = zip::write::SimpleFileOptions::default()
            .compression_method(zip::CompressionMethod::Deflated);
        writer.start_file("luma_lsp.exe", options).unwrap();
        writer.write_all(b"zip binary bytes").unwrap();
        writer.finish().unwrap();
    }
    let archive_path = format!("{dir}/archive.zip");
    std::fs::write(&archive_path, &zip_bytes).unwrap();

    ZedDownloader
        .extract_archive(&archive_path, &dir, zed::DownloadedFileType::Zip)
        .unwrap();

    let extracted = std::fs::read(format!("{dir}/luma_lsp.exe")).unwrap();
    assert_eq!(extracted, b"zip binary bytes");
}

// ── Grammar declaration: single source of truth (B06) ─────────
// The Zed grammar is declared solely by the [grammars.luma] table in
// extension.toml. The legacy standalone grammars/luma.toml manifest must not
// be reintroduced — two declarations drift out of sync (divergent path/pin).

#[test]
fn grammar_declared_only_in_extension_toml() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");

    let stale = std::path::Path::new(manifest_dir).join("grammars/luma.toml");
    assert!(
        !stale.exists(),
        "stale grammars/luma.toml must not exist; declare the grammar only in \
         extension.toml [grammars.luma]"
    );

    let extension_toml =
        std::fs::read_to_string(std::path::Path::new(manifest_dir).join("extension.toml"))
            .expect("extension.toml should be readable");
    assert!(
        extension_toml.contains("[grammars.luma]"),
        "extension.toml must declare the grammar via [grammars.luma]"
    );
}
