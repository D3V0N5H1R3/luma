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
              << "  luma <file.luma> -- [args...] Run, passing all args to the program\n"
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
              << "Luma flags may appear before or after the .luma file. Other\n"
              << "arguments after the file, and every argument after '--', are\n"
              << "passed to the program via Process.get_arguments(). After the\n"
              << "file, an -O argument that is not a valid level (e.g. -O3) is\n"
              << "passed to the program too.\n"
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
