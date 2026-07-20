// Windows implementations of the platform_process primitives.
// Compiled only on Windows (see core/runtime/CMakeLists.txt).

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/platform_utils.hpp"
#include "common/resource_limits.hpp"
#include "runtime/stdlib/system/platform_process.hpp"
#include "runtime/stdlib/system/process_module.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace luma::platform_process {

// Build a safe Windows command-line string by individually quoting each
// argument according to the MSVC CRT parsing rules.  This prevents
// metacharacter injection when using CreateProcessA.  Declared in
// platform_process.hpp so the quoting grammar can be unit tested directly.
std::string build_windows_cmdline(const std::vector<std::string>& argv) {
    std::string cmdline{};

    bool first_arg{true};

    for (const auto& arg : argv) {
        if (!first_arg) {
            cmdline += ' ';
        }

        first_arg = false;

        if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) {
            cmdline += arg;

            continue;
        }

        cmdline += '"';

        for (auto it = arg.begin();; ++it) {
            std::size_t num_backslashes{0};

            while (it != arg.end() && *it == '\\') {
                ++num_backslashes;
                ++it;
            }

            if (it == arg.end()) {
                // Double trailing backslashes before the closing quote.
                cmdline.append(num_backslashes * 2, '\\');

                break;
            }

            if (*it == '"') {
                cmdline.append((num_backslashes * 2) + 1, '\\');

                cmdline += '"';
            } else {
                cmdline.append(num_backslashes, '\\');

                cmdline += *it;
            }
        }

        cmdline += '"';
    }

    return cmdline;
}

// Windows implementation: spawn via CreateProcessA with a pipe for stdout/stderr.
std::pair<int, std::string> execute_command(std::string_view cmd) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;

    if (CreatePipe(&read_pipe, &write_pipe, &sa, 0) == FALSE) {
        return {-1, ""};
    }

    // Prevent the read handle from being inherited by the child.
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;

    PROCESS_INFORMATION pi{};

    // Tokenize and re-quote to prevent metacharacter injection,
    // matching the POSIX path which uses tokenize + execvp.
    auto argv = tokenize_command(cmd);

    if (argv.empty()) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);

        return {-1, ""};
    }

    std::string safe_cmd{build_windows_cmdline(argv)};

    const BOOL ok = CreateProcessA(nullptr, safe_cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    // Close the write end — the child owns its copy.
    CloseHandle(write_pipe);

    if (ok == FALSE) {
        CloseHandle(read_pipe);

        return {-1, ""};
    }

    // Read child stdout.
    std::string output{};
    std::array<char, k_pipe_buffer_size> buffer{};
    DWORD bytes_read{0};

    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                    nullptr) != FALSE &&
           bytes_read > 0) {
        if (output.size() + bytes_read > ResourceLimits::max_process_output_size) {
            TerminateProcess(pi.hProcess, 1);

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(read_pipe);

            return {-1, ""};
        }
        output.append(buffer.data(), bytes_read);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code{0};

    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(read_pipe);

    // Windows DWORD exit codes > INT_MAX are clamped to -1 to match POSIX WEXITSTATUS behaviour.
    const int safe_code = (exit_code > static_cast<DWORD>(std::numeric_limits<int>::max()))
                              ? -1
                              : static_cast<int>(exit_code);

    return {safe_code, std::move(output)};
}

std::int64_t current_process_id() {
    return static_cast<std::int64_t>(GetCurrentProcessId());
}

int set_environment_variable(const std::string& name, const std::string& value) {
    return _putenv_s(name.c_str(), value.c_str());
}

std::optional<std::vector<std::pair<std::string, std::string>>> all_environment_variables() {
    std::vector<std::pair<std::string, std::string>> result;

    auto* const env_block = GetEnvironmentStringsW();

    if (!env_block) {
        return std::nullopt;
    }

    // Each entry is "KEY=VALUE\0", double-null terminates.
    for (const wchar_t* p = env_block; *p != L'\0';) {
        const std::wstring entry{p};
        p += entry.size() + 1;

        const auto eq = entry.find(L'=');

        if (eq == 0 || eq == std::wstring::npos) {
            continue; // Skip entries like "=C:=C:\\"
        }

        result.emplace_back(wstring_to_utf8(entry.substr(0, eq)),
                            wstring_to_utf8(entry.substr(eq + 1)));
    }

    FreeEnvironmentStringsW(env_block);

    return result;
}

} // namespace luma::platform_process
