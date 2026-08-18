// AUTO-GENERATED from extensions/shared/defaults.json
// DO NOT EDIT. Run: python generate-config-code.py --zed

#![allow(dead_code)]

/// Absolute path to the luma_lsp binary. If empty, the extension downloads it automatically or searches PATH.
pub const DEFAULT_LSP_PATH: &str = "";

/// Automatically check for language server updates on extension activation.
pub const DEFAULT_LSP_AUTO_UPDATE: bool = true;

/// Absolute path to the luma interpreter binary. If empty, the extension searches PATH.
pub const DEFAULT_INTERPRETER_PATH: &str = "";

/// Absolute path to the luma_dap debug adapter binary. If empty, the extension searches PATH.
pub const DEFAULT_DAP_PATH: &str = "";

/// Only report linter warnings on save (not while typing). Syntax and type errors are always reported immediately.
pub const DEFAULT_DIAGNOSTICS_ON_SAVE: bool = false;

/// Show inferred type annotations and parameter names as inlay hints. Disabled by default to keep the source uncluttered.
pub const DEFAULT_INLAY_HINTS_ENABLED: bool = false;

/// Show reference counts above functions and types as code lenses.
pub const DEFAULT_CODE_LENS_ENABLED: bool = true;
