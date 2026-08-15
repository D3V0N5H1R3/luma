#include "runtime/cli/cli_commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "runtime/cli/cli_internal.hpp"

namespace {

constexpr std::string_view k_default_manifest = R"({
    "inlayHints": { "enabled": true },
    "codeLens": { "enabled": true },
    "diagnostics": { "onSave": false }
}
)";

} // namespace

namespace luma {

int run_pkg_command(const ParsedArgs& args) {
    // Accept optional "init" subcommand for backward compatibility
    // (luma init, luma pkg, luma pkg init all do the same thing).
    if (!args.program_args.empty() && args.program_args[0] == "help") {
        std::cout << "Usage: luma init\n\n"
                  << "Create a luma.json project configuration file in the current directory.\n"
                  << "The language server reads this file for per-project settings.\n";
        return exit_code::success;
    }

    const auto manifest_path = std::filesystem::current_path() / "luma.json";

    if (std::filesystem::exists(manifest_path)) {
        return report_cli_error("luma.json already exists", exit_code::usage_error);
    }

    std::ofstream manifest(manifest_path);
    manifest << k_default_manifest;
    manifest.close();

    // Check after close() so a write error only surfaced when the final
    // buffered bytes are committed is still reported rather than printing
    // success over a partially written manifest.
    if (!manifest) {
        return report_cli_error("cannot write luma.json", exit_code::runtime_error);
    }

    std::cout << "Created luma.json\n";
    return exit_code::success;
}

} // namespace luma
