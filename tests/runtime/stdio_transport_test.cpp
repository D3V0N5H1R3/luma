// Unit tests for shared/protocol/stdio_transport.cpp — the concrete stdin/stdout
// transport that both the LSP language server and the DAP debug adapter use in
// production (luma::lsp::StdioTransport and the DAP Transport are aliases of it).
//
// The other transports are exercised by mocks (LSP) or real sockets (DAP TCP),
// but this one drives the real process file descriptors, so it is tested here by
// redirecting stdout/stdin to temporary files.  The descriptors are switched to
// binary mode to mirror the _setmode() calls the real server entry points make,
// which is what keeps the \r\n framing bytes intact on Windows.  With that in
// place the exact bytes write_message() emits and the messages read_message()
// reconstructs can both be asserted.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "json/json.hpp"
#include "protocol/message_frame.hpp"
#include "protocol/stdio_transport.hpp"
#include "test_framework.hpp"

using luma::json::JsonValue;
using luma::protocol::StdioTransport;

namespace {

// ─── Cross-platform file-descriptor helpers ───
// Windows spells the POSIX descriptor calls with a leading underscore; wrap them
// so the test bodies read the same on both platforms.

[[nodiscard]] int dup_fd(int fd) {
#ifdef _WIN32
    return _dup(fd);
#else
    return ::dup(fd);
#endif
}

void dup2_fd(int from_fd, int to_fd) {
#ifdef _WIN32
    _dup2(from_fd, to_fd);
#else
    ::dup2(from_fd, to_fd);
#endif
}

[[nodiscard]] int fileno_of(std::FILE* stream) {
#ifdef _WIN32
    return _fileno(stream);
#else
    return ::fileno(stream);
#endif
}

void close_fd(int fd) {
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
}

// Put a descriptor into binary mode so the CRT never rewrites \r\n (a no-op on
// POSIX, where there is no text/binary distinction).
void set_binary(int fd) {
#ifdef _WIN32
    _setmode(fd, _O_BINARY);
#else
    (void)fd;
#endif
}

// Open a file, using the bounds-checked fopen_s on Windows (plain std::fopen
// trips the MSVC C4996 deprecation, which the build treats as an error).
[[nodiscard]] std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::FILE* stream = nullptr;
    if (::fopen_s(&stream, path.string().c_str(), mode) != 0) {
        return nullptr;
    }
    return stream;
#else
    return std::fopen(path.c_str(), mode);
#endif
}

[[nodiscard]] std::filesystem::path unique_temp_path(const char* tag) {
    static std::atomic<unsigned> counter{0};
    const auto seq = counter.fetch_add(1, std::memory_order_relaxed);
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("luma_" + std::string{tag} + "_" + std::to_string(ticks) + "_" + std::to_string(seq));
}

// RAII: redirect the process stdout to `path` (binary) for the guard's lifetime,
// restoring the original descriptor on destruction even if a test throws.
class StdoutToFile {
public:
    explicit StdoutToFile(std::filesystem::path path) : path_{std::move(path)} {
        std::fflush(stdout);
        saved_fd_ = dup_fd(fileno_of(stdout));
        file_ = open_file(path_, "wb");
        dup2_fd(fileno_of(file_), fileno_of(stdout));
        set_binary(fileno_of(stdout));
    }

    ~StdoutToFile() {
        // Deliberately do NOT flush stdout here.  The write tests must observe
        // only the bytes the code under test flushed itself; flushing on the
        // harness's behalf (here or in capture_stdout) would deliver an
        // unflushed message to the file and mask a missing fflush() in
        // write_message() — the single most important property of the transport.
        dup2_fd(saved_fd_, fileno_of(stdout));
        close_fd(saved_fd_);
        std::fclose(file_);
    }

    StdoutToFile(const StdoutToFile&) = delete;
    StdoutToFile& operator=(const StdoutToFile&) = delete;
    StdoutToFile(StdoutToFile&&) = delete;
    StdoutToFile& operator=(StdoutToFile&&) = delete;

private:
    std::filesystem::path path_;
    int saved_fd_{-1};
    std::FILE* file_{nullptr};
};

// Capture everything written to stdout while `action` runs, returning the bytes.
// No fflush() is performed on the test's behalf: the captured bytes are exactly
// those the code under test flushed itself, so a transport that forgets to flush
// is caught rather than masked.
template <typename Fn> [[nodiscard]] std::string capture_stdout(Fn&& action) {
    const auto path = unique_temp_path("stdout");
    {
        StdoutToFile guard{path};
        action();
    }

    std::ifstream in{path, std::ios::binary};
    std::string data{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    in.close();

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return data;
}

// RAII: redirect the process stdin to read from a file preloaded with
// `contents` (binary), restoring the original descriptor on destruction.
class StdinFromString {
public:
    explicit StdinFromString(const std::string& contents) : path_{unique_temp_path("stdin")} {
        {
            std::ofstream out{path_, std::ios::binary};
            out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }

        saved_fd_ = dup_fd(fileno_of(stdin));
        file_ = open_file(path_, "rb");
        dup2_fd(fileno_of(file_), fileno_of(stdin));
        set_binary(fileno_of(stdin));
    }

    ~StdinFromString() {
        dup2_fd(saved_fd_, fileno_of(stdin));
        close_fd(saved_fd_);
        std::fclose(file_);

        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    StdinFromString(const StdinFromString&) = delete;
    StdinFromString& operator=(const StdinFromString&) = delete;
    StdinFromString(StdinFromString&&) = delete;
    StdinFromString& operator=(StdinFromString&&) = delete;

private:
    std::filesystem::path path_;
    int saved_fd_{-1};
    std::FILE* file_{nullptr};
};

[[nodiscard]] JsonValue make_message(const std::string& method, std::int64_t id) {
    JsonValue::ObjectType obj;
    obj["jsonrpc"] = JsonValue{std::string{"2.0"}};
    obj["method"] = JsonValue{method};
    obj["id"] = JsonValue{id};
    return JsonValue{std::move(obj)};
}

[[nodiscard]] std::string framed(const JsonValue& message) {
    const std::string body = message.to_string();
    return luma::protocol::content_length_header(body.size()) + body;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// write_message — Content-Length framing on stdout
// ═══════════════════════════════════════════════════════════

static void test_write_message_emits_exact_framing() {
    const auto message = make_message("ping", 1);

    const std::string captured = capture_stdout([&] {
        StdioTransport transport;
        transport.write_message(message);
    });

    // The concrete transport must produce byte-for-byte the same framing as the
    // shared helper: header, blank-line separator, then the JSON body.
    ASSERT_EQ(captured, framed(message));
}

static void test_write_message_two_messages_are_independently_framed() {
    const auto first = make_message("first", 1);
    const auto second = make_message("second", 2);

    const std::string captured = capture_stdout([&] {
        StdioTransport transport;
        transport.write_message(first);
        transport.write_message(second);
    });

    // Each write must be self-framed and flushed, so the stream is exactly the
    // two frames back to back.
    ASSERT_EQ(captured, framed(first) + framed(second));
}

static void test_write_message_content_length_matches_body() {
    // A body with a multi-byte UTF-8 character: the Content-Length must count
    // bytes, not characters, or the reader would desynchronise.
    JsonValue::ObjectType obj;
    obj["text"] = JsonValue{std::string{"caf\xC3\xA9"}}; // "café"
    const JsonValue message{std::move(obj)};

    const std::string captured = capture_stdout([&] {
        StdioTransport transport;
        transport.write_message(message);
    });

    const auto separator = captured.find("\r\n\r\n");
    ASSERT_TRUE(separator != std::string::npos);

    const auto body = captured.substr(separator + 4);
    const auto declared =
        luma::protocol::try_parse_content_length(captured.substr(0, captured.find("\r\n")));
    ASSERT_TRUE(declared.has_value());
    ASSERT_EQ(body.size(), *declared);
    ASSERT_EQ(body, message.to_string());
}

// ═══════════════════════════════════════════════════════════
// read_message — reconstructing framed messages from stdin
// ═══════════════════════════════════════════════════════════

static void test_read_message_parses_framed_message() {
    const auto message = make_message("hello", 42);
    const StdinFromString redirect{framed(message)};

    StdioTransport transport;
    const auto read = transport.read_message();

    ASSERT_TRUE(read.has_value());
    ASSERT_EQ((*read)["method"].as_string(), std::string{"hello"});
    ASSERT_EQ((*read)["id"].as_integer(), 42);
}

static void test_read_message_reads_sequential_messages() {
    const auto first = make_message("first", 1);
    const auto second = make_message("second", 2);
    const StdinFromString redirect{framed(first) + framed(second)};

    StdioTransport transport;

    const auto read_first = transport.read_message();
    ASSERT_TRUE(read_first.has_value());
    ASSERT_EQ((*read_first)["method"].as_string(), std::string{"first"});

    const auto read_second = transport.read_message();
    ASSERT_TRUE(read_second.has_value());
    ASSERT_EQ((*read_second)["method"].as_string(), std::string{"second"});
}

static void test_read_message_returns_nullopt_on_empty_input() {
    const StdinFromString redirect{""};

    StdioTransport transport;
    ASSERT_FALSE(transport.read_message().has_value());
}

static void test_read_message_returns_nullopt_after_last_message() {
    const auto message = make_message("only", 1);
    const StdinFromString redirect{framed(message)};

    StdioTransport transport;
    ASSERT_TRUE(transport.read_message().has_value());
    // The stream is exhausted after the single frame — the next read is EOF.
    ASSERT_FALSE(transport.read_message().has_value());
}

// ═══════════════════════════════════════════════════════════
// read_raw — opt-in read timeout (POSIX only)
// ═══════════════════════════════════════════════════════════
//
// The timeout path polls the real STDIN file descriptor.  On POSIX a redirected
// pipe descriptor honours poll(), so the "no data before the deadline" branch is
// deterministic.  On Windows the wait targets the Win32 STD_INPUT_HANDLE rather
// than the CRT descriptor a test can redirect, so this branch is not unit-tested
// there; the DAP shutdown path exercises it end to end instead.

#ifndef _WIN32
namespace {

// Exposes the protected read primitive so the timeout branch can be driven
// directly, independent of the buffering in read_message().
class ReadRawProbe : public StdioTransport {
public:
    using StdioTransport::StdioTransport;

    [[nodiscard]] std::size_t call_read_raw(std::span<char> buf) {
        return read_raw(buf);
    }
};

} // namespace

static void test_read_raw_times_out_when_no_data_available() {
    int pipe_fds[2];
    ASSERT_EQ(::pipe(pipe_fds), 0);

    // RAII: point STDIN at the pipe read-end for the scope, restoring the real
    // stdin and closing every fd on destruction — including the unwind path, so
    // a throw from call_read_raw() (wait_for_stdin throws on poll() failure)
    // cannot leak fds or leave stdin redirected for later tests.
    class StdinPipeRedirect {
    public:
        StdinPipeRedirect(int read_fd, int write_fd)
            : read_fd_{read_fd}, write_fd_{write_fd}, saved_fd_{::dup(STDIN_FILENO)} {
            ::dup2(read_fd_, STDIN_FILENO);
        }

        ~StdinPipeRedirect() {
            ::dup2(saved_fd_, STDIN_FILENO);
            ::close(saved_fd_);
            ::close(read_fd_);
            ::close(write_fd_);
        }

        StdinPipeRedirect(const StdinPipeRedirect&) = delete;
        StdinPipeRedirect& operator=(const StdinPipeRedirect&) = delete;
        StdinPipeRedirect(StdinPipeRedirect&&) = delete;
        StdinPipeRedirect& operator=(StdinPipeRedirect&&) = delete;

    private:
        int read_fd_;
        int write_fd_;
        int saved_fd_;
    };

    const StdinPipeRedirect redirect{pipe_fds[0], pipe_fds[1]};

    ReadRawProbe probe;
    probe.set_read_timeout(50); // milliseconds

    char buf[16];
    // The write end stays open, so the read end is not at EOF — poll() must time
    // out and read_raw() must report "no data" as 0 rather than blocking.
    const auto count = probe.call_read_raw(std::span<char>{buf, sizeof(buf)});

    ASSERT_EQ(count, static_cast<std::size_t>(0));
}
#endif

// ─── main ───

int main() {
    RUN(test_write_message_emits_exact_framing);
    RUN(test_write_message_two_messages_are_independently_framed);
    RUN(test_write_message_content_length_matches_body);

    RUN(test_read_message_parses_framed_message);
    RUN(test_read_message_reads_sequential_messages);
    RUN(test_read_message_returns_nullopt_on_empty_input);
    RUN(test_read_message_returns_nullopt_after_last_message);

#ifndef _WIN32
    RUN(test_read_raw_times_out_when_no_data_available);
#endif

    return SUMMARY();
}
