#include "runtime/cli/cli_commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "runtime/cli/cli_internal.hpp"

namespace {

constexpr std::string_view k_default_manifest = R"({
    "name": "my-project",
    "version": "0.1.0",
    "entry": "src/main.luma",
    "dependencies": {}
}
)";

} // namespace

namespace luma {

int run_pkg_command(const ParsedArgs& args) {
    const auto subcommand = args.program_args.empty() ? "help" : args.program_args[0];

    if (subcommand == "init") {
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

    if (subcommand == "help" || subcommand == "--help") {
        std::cout << "Usage: luma pkg <command>\n\n"
                  << "Commands:\n"
                  << "  init    Create a new luma.json manifest\n"
                  << "  help    Show this message\n";
        return exit_code::success;
    }

    return report_cli_error("unknown pkg command '" + std::string{subcommand} + "'",
                            exit_code::usage_error);
}

} // namespace luma
