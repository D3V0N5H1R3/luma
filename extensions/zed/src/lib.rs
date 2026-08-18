mod download;
mod generated;
mod labels;
mod util;
mod zip_extract;

use crate::generated::config_defaults::{DEFAULT_DIAGNOSTICS_ON_SAVE, DEFAULT_INLAY_HINTS_ENABLED};
use zed_extension_api::{self as zed, lsp, CodeLabel};

struct LumaExtension {
    platform: (zed::Os, zed::Architecture),
}

/// Indicates how a binary was resolved.
enum BinarySource {
    /// Found on the system PATH.
    Path(String),
    /// Downloaded from GitHub releases.
    Downloaded(String),
}

impl BinarySource {
    fn path(&self) -> &str {
        match self {
            Self::Path(p) | Self::Downloaded(p) => p,
        }
    }
}

impl LumaExtension {
    /// Builds a user-facing error message when no pre-built binary is available.
    fn no_binary_message(binary_name: &str) -> String {
        format!(
            "No pre-built {binary_name} binary available for this platform. \
             Build it with: cmake --build build --config Release --target {binary_name} --parallel\n\
             Then add the build output directory to your PATH."
        )
    }

    /// Fetches the latest GitHub release for the Luma repository.
    fn fetch_latest_release() -> zed::Result<zed::GithubRelease> {
        zed::latest_github_release(
            crate::generated::config::GITHUB_REPO,
            zed::GithubReleaseOptions {
                require_assets: true,
                pre_release: false,
            },
        )
    }

    /// Resolves a binary by checking PATH first, then downloading from GitHub.
    ///
    /// Returns a [`BinarySource`] indicating how the binary was found, so the
    /// caller can decide on environment variables (PATH-found binaries inherit
    /// the shell environment; downloaded binaries do not).
    fn resolve_binary(
        &self,
        binary_name: &str,
        worktree: &zed::Worktree,
        reporter: &dyn download::StatusReporter,
        recovery_hint: &str,
    ) -> zed::Result<BinarySource> {
        if let Some(path) = worktree.which(binary_name) {
            return Ok(BinarySource::Path(crate::util::normalize_path(
                path,
                self.platform.0,
            )));
        }

        if !generated::config::AUTO_DOWNLOAD_ENABLED {
            return Err(Self::no_binary_message(binary_name));
        }

        let (platform, arch) = self.platform;
        let asset_name = download::platform_asset_name_for(binary_name, platform, arch)
            .ok_or_else(|| Self::no_binary_message(binary_name))?;

        let release = Self::fetch_latest_release()?;

        let path = download::download_binary(
            &download::ZedDownloader,
            reporter,
            binary_name,
            platform,
            &asset_name,
            &release,
            recovery_hint,
        )?;

        Ok(BinarySource::Downloaded(path))
    }
}

// ─── Extension implementation ─────────────────────────────────────

impl zed::Extension for LumaExtension {
    fn new() -> Self {
        Self {
            platform: zed::current_platform(),
        }
    }

    fn language_server_command(
        &mut self,
        language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> zed::Result<zed::Command> {
        let reporter = download::LspStatusReporter(language_server_id);
        let binary = self.resolve_binary(
            generated::config::BINARY_LSP,
            worktree,
            &reporter,
            " Syntax highlighting will still work.",
        )?;

        Ok(zed::Command {
            command: binary.path().to_string(),
            args: vec![],
            env: env_for(&binary, worktree),
        })
    }

    fn language_server_initialization_options(
        &mut self,
        _language_server_id: &zed::LanguageServerId,
        _worktree: &zed::Worktree,
    ) -> zed::Result<Option<zed::serde_json::Value>> {
        Ok(None)
    }

    // LSP config defaults (e.g. inlayHints.enabled) are defined per-editor:
    //   VS Code: extensions/vscode/package.json ("luma.inlayHints.enabled")
    //   Zed:     here (language_server_workspace_configuration)
    // Shared defaults: extensions/shared/defaults.json.
    // Error severity contract: extensions/shared/error-handling.md.
    // Users can override in Zed settings under lsp.luma-lsp.settings.
    fn language_server_workspace_configuration(
        &mut self,
        _language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> zed::Result<Option<zed::serde_json::Value>> {
        let user_settings = zed::settings::LspSettings::for_worktree("luma-lsp", worktree)
            .ok()
            .and_then(|lsp_settings| lsp_settings.settings);

        let mut config = build_workspace_config(user_settings.as_ref());

        // Merge any remaining user settings (additional LSP options, etc.).
        if let Some(ref user) = user_settings {
            util::merge_json(&mut config, user);
        }

        Ok(Some(config))
    }

    fn label_for_completion(
        &self,
        _language_server_id: &zed::LanguageServerId,
        completion: lsp::Completion,
    ) -> Option<CodeLabel> {
        labels::completion_label(&completion)
    }

    fn label_for_symbol(
        &self,
        _language_server_id: &zed::LanguageServerId,
        symbol: lsp::Symbol,
    ) -> Option<CodeLabel> {
        labels::symbol_label(&symbol)
    }

    fn get_dap_binary(
        &mut self,
        _adapter_name: String,
        config: zed::DebugTaskDefinition,
        user_provided_debug_adapter_path: Option<String>,
        worktree: &zed::Worktree,
    ) -> Result<zed::DebugAdapterBinary, String> {
        let request_args = zed::StartDebuggingRequestArguments {
            configuration: config.config,
            request: zed::StartDebuggingRequestArgumentsRequest::Launch,
        };

        // Use user-provided path if supplied.
        if let Some(path) = user_provided_debug_adapter_path {
            return Ok(zed::DebugAdapterBinary {
                command: Some(path),
                arguments: vec![],
                envs: vec![],
                cwd: None,
                connection: None,
                request_args,
            });
        }

        let reporter = download::StderrStatusReporter;
        let binary = self.resolve_binary(generated::config::BINARY_DAP, worktree, &reporter, "")?;

        let envs = env_for(&binary, worktree);

        Ok(zed::DebugAdapterBinary {
            command: Some(binary.path().to_string()),
            arguments: vec![],
            envs,
            cwd: None,
            connection: None,
            request_args,
        })
    }
}

zed::register_extension!(LumaExtension);

// ─── Helpers ──────────────────────────────────────────────────────

/// Returns the environment variables a resolved binary should run with.
///
/// PATH-found binaries inherit the worktree shell environment so they behave
/// like a manual invocation; downloaded binaries run with an empty environment
/// to avoid inheriting stale or conflicting variables. Applied to both the LSP
/// and DAP binaries so their environment policy never silently diverges.
fn env_for(binary: &BinarySource, worktree: &zed::Worktree) -> Vec<(String, String)> {
    match binary {
        BinarySource::Path(_) => worktree.shell_env(),
        BinarySource::Downloaded(_) => vec![],
    }
}

/// Read a boolean from a nested path within a JSON value, falling back to `default`.
///
/// `path` is a slice of object keys traversed in order.
/// Returns `default` if any key is missing or the final value is not a boolean.
fn bool_setting(value: Option<&zed::serde_json::Value>, path: &[&str], default: bool) -> bool {
    value
        .and_then(|mut v| {
            for key in path {
                v = v.get(key)?;
            }
            v.as_bool()
        })
        .unwrap_or(default)
}

/// Build the `luma` workspace-configuration payload sent to the language
/// server, before merging any additional raw user settings on top.
///
/// Deliberately omits `codeLens` — per [`extensions/FEATURE_PARITY.md`], code
/// lens is VS Code only, so Zed must never advertise it as enabled to the LSP.
fn build_workspace_config(
    user_settings: Option<&zed::serde_json::Value>,
) -> zed::serde_json::Value {
    let luma = user_settings.and_then(|s| s.get("luma"));

    let inlay_hints_enabled = bool_setting(
        luma,
        &["inlayHints", "enabled"],
        DEFAULT_INLAY_HINTS_ENABLED,
    );
    let diagnostics_on_save = bool_setting(
        luma,
        &["diagnostics", "onSave"],
        DEFAULT_DIAGNOSTICS_ON_SAVE,
    );

    zed::serde_json::json!({
        "luma": {
            "inlayHints": { "enabled": inlay_hints_enabled },
            "diagnostics": { "onSave": diagnostics_on_save }
        }
    })
}

#[cfg(test)]
mod tests;
