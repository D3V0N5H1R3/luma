use zed_extension_api::{lsp, CodeLabel, CodeLabelSpan, CodeLabelSpanLiteral};

// ─── Shared label construction helpers ────────────────────────────

/// Build a `CodeLabel` with the given `code`, highlighting and filtering on the
/// `start..end` byte range. The three public helpers below differ only in how
/// they derive `code` and that range, so they all delegate here.
fn range_label(code: String, start: usize, end: usize) -> CodeLabel {
    CodeLabel {
        code,
        spans: vec![CodeLabelSpan::CodeRange((start..end).into())],
        filter_range: (start..end).into(),
    }
}

/// Build a CodeLabel of the form `"{keyword} {name}"`, highlighting the name portion.
pub(crate) fn keyword_label(keyword: &str, name: &str) -> CodeLabel {
    let code = format!("{keyword} {name}");
    let name_start = keyword.len() + 1;
    let name_end = name_start + name.len();
    range_label(code, name_start, name_end)
}

/// Build a CodeLabel that is just the raw name with no keyword prefix.
pub(crate) fn plain_label(name: &str) -> CodeLabel {
    range_label(name.to_string(), 0, name.len())
}

/// Build a CodeLabel of the form `"function {return_type} {name}()"`, highlighting the name.
///
/// Callers with no return-type detail pass `"void"`. All range math uses byte
/// offsets, which match char offsets because the callers restrict labels to ASCII.
pub(crate) fn function_label(return_type: &str, name: &str) -> CodeLabel {
    let code = format!("function {return_type} {name}()");
    let name_start = "function ".len() + return_type.len() + 1;
    let name_end = name_start + name.len();
    range_label(code, name_start, name_end)
}

/// Maps an LSP completion kind to a Luma keyword for simple keyword-labeled kinds.
/// Returns `None` for kinds that need custom handling (Function, Method, etc.).
fn completion_kind_keyword(kind: &lsp::CompletionKind) -> Option<&'static str> {
    match kind {
        lsp::CompletionKind::Module | lsp::CompletionKind::Class => Some("namespace"),
        lsp::CompletionKind::Enum => Some("choice"),
        lsp::CompletionKind::Struct => Some("record"),
        lsp::CompletionKind::Interface => Some("interface"),
        _ => None,
    }
}

/// Maps an LSP symbol kind to a Luma keyword for simple keyword-labeled kinds.
/// Returns `None` for kinds that need custom handling (Function, Method, etc.).
fn symbol_kind_keyword(kind: &lsp::SymbolKind) -> Option<&'static str> {
    match kind {
        lsp::SymbolKind::Class | lsp::SymbolKind::Struct => Some("record"),
        lsp::SymbolKind::Enum => Some("choice"),
        lsp::SymbolKind::Interface => Some("interface"),
        lsp::SymbolKind::Namespace | lsp::SymbolKind::Module => Some("namespace"),
        _ => None,
    }
}

// ─── Standalone label helpers (testable without LanguageServerId) ─

/// Builds a `CodeLabel` for an LSP completion item.
///
/// Returns `None` for non-ASCII labels. All range calculations use byte offsets,
/// which are correct only for ASCII. Luma identifiers, keywords, and type names
/// are restricted to ASCII, so this guard is safe.
pub(crate) fn completion_label(completion: &lsp::Completion) -> Option<CodeLabel> {
    if !completion.label.is_ascii() {
        return None;
    }
    match completion.kind.as_ref()? {
        lsp::CompletionKind::Function | lsp::CompletionKind::Method => {
            let detail = completion.detail.as_deref().unwrap_or("");
            let return_type = if detail.is_empty() { "void" } else { detail };
            Some(function_label(return_type, &completion.label))
        }
        lsp::CompletionKind::Variable
        | lsp::CompletionKind::Constant
        | lsp::CompletionKind::Field
        | lsp::CompletionKind::Property => {
            let label = &completion.label;
            match completion.detail.as_deref() {
                Some(detail) if !detail.is_empty() => Some(keyword_label(detail, label)),
                _ => Some(plain_label(label)),
            }
        }
        lsp::CompletionKind::Keyword => Some(plain_label(&completion.label)),
        lsp::CompletionKind::Snippet => {
            let label_len = completion.label.len();
            Some(CodeLabel {
                code: completion.label.clone(),
                spans: vec![CodeLabelSpan::Literal(CodeLabelSpanLiteral {
                    text: completion.label.clone(),
                    highlight_name: Some("keyword".to_string()),
                })],
                filter_range: (0..label_len).into(),
            })
        }
        lsp::CompletionKind::EnumMember
        | lsp::CompletionKind::Constructor
        | lsp::CompletionKind::TypeParameter => Some(plain_label(&completion.label)),
        kind => {
            completion_kind_keyword(kind).map(|keyword| keyword_label(keyword, &completion.label))
        }
    }
}

// The match arms below map LSP symbol/completion kinds to Luma keyword labels.
// A data-driven map (kind → keyword string) was considered, but match is
// preferred: the Function/Method arms need custom formatting, the compiler
// warns on missing variants, and the total arm count is small.

/// Builds a `CodeLabel` for an LSP document/workspace symbol.
///
/// Returns `None` for non-ASCII names. See [`completion_label`] for the rationale.
pub(crate) fn symbol_label(symbol: &lsp::Symbol) -> Option<CodeLabel> {
    if !symbol.name.is_ascii() {
        return None;
    }
    let name = &symbol.name;

    match symbol.kind {
        lsp::SymbolKind::Function | lsp::SymbolKind::Method | lsp::SymbolKind::Constructor => {
            Some(function_label("void", name))
        }
        lsp::SymbolKind::Variable
        | lsp::SymbolKind::Constant
        | lsp::SymbolKind::EnumMember
        | lsp::SymbolKind::TypeParameter => Some(plain_label(name)),
        kind => symbol_kind_keyword(&kind).map(|keyword| keyword_label(keyword, name)),
    }
}
