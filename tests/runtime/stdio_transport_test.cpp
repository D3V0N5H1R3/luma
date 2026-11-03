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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
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
using luma::test::ScopedPathCleanup;
using luma::test::unique_temp_path;

namespace {

// ─── Cross-platform file-descriptor helpers ───
// Windows spells the POSIX descriptor calls with a leading underscore; wrap them
// so the test bodies read the same on both platforms.  Each returns the raw
// result (-1 on failure) so callers can check it.

[[nodiscard]] int dup_fd(int fd) {
#ifdef _WIN32
    return _dup(fd);
#else
    return ::dup(fd);
#endif
}

[[nodiscard]] int dup2_fd(int from_fd, int to_fd) {
#ifdef _WIN32
    return _dup2(from_fd, to_fd);
#else
    return ::dup2(from_fd, to_fd);
#endif
}

[[nodiscard]] int fileno_of(std::FILE* stream) {
#ifdef _WIN32
    return _fileno(stream);
#else
    return ::fileno(stream);
#endif
}

[[nodiscard]] int close_fd(int fd) {
#ifdef _WIN32
    return _close(fd);
#else
    return ::close(fd);
#endif
}

// Put a descriptor into binary mode so the CRT never rewrites \r\n (a no-op on
// POSIX, where there is no text/binary distinction).
void set_binary(int fd) {
#ifdef _WIN32
    if (_setmode(fd, _O_BINARY) == -1) {
        throw std::runtime_error{"set_binary: _setmode failed for fd " + std::to_string(fd)};
    }
#else
    (void)fd;
#endif
}

struct FileCloser {
    void operator()(std::FILE* stream) const noexcept {
        (void)std::fclose(stream);
    }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

// Open a file, using the bounds-checked fopen_s on Windows (plain std::fopen
// trips the MSVC C4996 deprecation, which the build treats as an error).
// Throws when the file cannot be opened.
[[nodiscard]] FilePtr open_file(const std::filesystem::path& path, const char* mode) {
    std::FILE* stream = nullptr;
#ifdef _WIN32
    if (::fopen_s(&stream, path.string().c_str(), mode) != 0) {
        stream = nullptr;
    }
#else
    stream = std::fopen(path.c_str(), mode);
#endif
    if (stream == nullptr) {
        throw std::runtime_error{"open_file: cannot open " + path.string()};
    }
    return FilePtr{stream};
}

// Flush a stream's buffered output and return its descriptor, so a redirect of
// that descriptor never strands bytes written before the redirect began.
[[nodiscard]] int flushed_fileno(std::FILE* stream) {
    if (std::fflush(stream) != 0) {
        throw std::runtime_error{"flushed_fileno: fflush failed"};
    }
    return fileno_of(stream);
}

// Write `contents` to `path` and open it for binary reading.
[[nodiscard]] FilePtr write_and_open(const std::filesystem::path& path,
                                     const std::string& contents) {
    luma::test::write_file(path, contents);
    return open_file(path, "rb");
}

// RAII: point descriptor `target_fd` at `source_fd` for the guard's lifetime,
// restoring the original descriptor on destruction even if a test throws.
class DescriptorRedirect {
public:
    DescriptorRedirect(int source_fd, int target_fd)
        : target_fd_{target_fd}, saved_fd_{dup_fd(target_fd)} {
        if (saved_fd_ < 0) {
            throw std::runtime_error{"DescriptorRedirect: dup failed"};
        }

        if (dup2_fd(source_fd, target_fd_) < 0) {
            (void)close_fd(saved_fd_);
            throw std::runtime_error{"DescriptorRedirect: dup2 failed"};
        }
    }

    ~DescriptorRedirect() noexcept {
        // A destructor cannot report failure; restoring the descriptor is best
        // effort, and the saved duplicate is always released.
        (void)dup2_fd(saved_fd_, target_fd_);
        (void)close_fd(saved_fd_);
    }

    DescriptorRedirect(const DescriptorRedirect&) = delete;
    DescriptorRedirect& operator=(const DescriptorRedirect&) = delete;
    DescriptorRedirect(DescriptorRedirect&&) = delete;
    DescriptorRedirect& operator=(DescriptorRedirect&&) = delete;

private:
    int target_fd_;
    int saved_fd_;
};

// RAII: redirect the process stdout to `path` (binary) for the guard's lifetime,
// restoring the original descriptor on destruction even if a test throws.
//
// Deliberately do NOT flush stdout on destruction.  The write tests must observe
// only the bytes the code under test flushed itself; flushing on the harness's
// behalf (here or in capture_stdout) would deliver an unflushed message to the
// file and mask a missing fflush() in write_message() — the single most
// important property of the transport.  Members are destroyed in reverse order:
// the descriptor is restored first, then the file is closed.
class StdoutToFile {
public:
    explicit StdoutToFile(const std::filesystem::path& path)
        : file_{open_file(path, "wb")}, redirect_{fileno_of(file_.get()), flushed_fileno(stdout)} {
        set_binary(fileno_of(stdout));
    }

private:
    FilePtr file_;
    DescriptorRedirect redirect_;
};

// Capture everything written to stdout while `action` runs, returning the bytes.
// No fflush() is performed on the test's behalf: the captured bytes are exactly
// those the code under test flushed itself, so a transport that forgets to flush
// is caught rather than masked.
template <typename Fn> [[nodiscard]] std::string capture_stdout(Fn&& action) {
    const ScopedPathCleanup cleanup{unique_temp_path("luma_stdout_", ".bin")};
    {
        const StdoutToFile guard{cleanup.path()};
        std::forward<Fn>(action)();
    }

    return luma::test::read_file_text(cleanup.path());
}

// RAII: redirect the process stdin to read from a file preloaded with
// `contents` (binary), restoring the original descriptor and removing the file
// on destruction.
class StdinFromString {
public:
    explicit StdinFromString(const std::string& contents)
        : cleanup_{unique_temp_path("luma_stdin_", ".bin")},
          file_{write_and_open(cleanup_.path(), contents)},
          redirect_{fileno_of(file_.get()), fileno_of(stdin)} {
        set_binary(fileno_of(stdin));
    }

private:
    ScopedPathCleanup cleanup_;
    FilePtr file_;
    DescriptorRedirect redirect_;
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
    ASSERT_NE(separator, std::string::npos);

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

// ─── main ───

int main() {
    RUN(test_write_message_emits_exact_framing);
    RUN(test_write_message_two_messages_are_independently_framed);
    RUN(test_write_message_content_length_matches_body);

    RUN(test_read_message_parses_framed_message);
    RUN(test_read_message_reads_sequential_messages);
    RUN(test_read_message_returns_nullopt_on_empty_input);
    RUN(test_read_message_returns_nullopt_after_last_message);

    return SUMMARY();
}
