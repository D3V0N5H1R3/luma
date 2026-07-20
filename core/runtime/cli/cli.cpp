#include "runtime/cli/cli.hpp"

#include <iomanip>
#include <iostream>
#include <string_view>

#include "runtime/cli/cli_args.hpp"

namespace luma {

void print_usage() {
    std::cout << "Luma " << luma_version << "\n"
              << "\n"
              << "Usage:\n"
              << "  luma                          Start interactive REPL\n"
              << "  luma <file.luma> [args...]    Run a Luma program\n"
              << "\n"
              << "Options:\n";

    for (const auto& desc : k_flag_descriptors) {
        if (desc.short_form.empty()) {
            std::cout << "      " << std::left << std::setw(13) << desc.long_form;
        } else {
            std::cout << "  " << desc.short_form << ", " << std::left << std::setw(13)
                      << desc.long_form;
        }

        std::cout << desc.description << "\n";
    }

    std::cout << "\n"
              << "Flags may appear in any order. Arguments after the .luma\n"
              << "file are available via Process.get_arguments().\n"
              << "\n"
              << "Exit codes:\n"
              << "  0  Success\n"
              << "  1  Runtime error\n"
              << "  2  Type error\n"
              << "  3  Syntax error\n"
              << "  4  Compile error\n"
              << "  5  Usage error (invalid arguments)\n";
}

} // namespace luma
