#ifndef LUMA_DAP_DEBUG_STREAM_UTILS_HPP
#define LUMA_DAP_DEBUG_STREAM_UTILS_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Stream redirection utilities for the DAP debug session.
//
// LineBufferedStreamBuf  — a std::streambuf that collects output and flushes
//                          complete lines to a user-supplied callback.
// StreamRedirectGuard    — RAII guard that redirects an std::ostream to a
//                          different streambuf and restores the original on
//                          destruction.
//
// Used by DebugExecutionEngine to capture stdout/stderr and re-emit them as
// DAP OutputEvents.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>

namespace luma::dap {

// A streambuf that flushes line-by-line to the output callback.
class LineBufferedStreamBuf : public std::streambuf {
public:
    using FlushFn = std::function<void(const std::string&)>;

    explicit LineBufferedStreamBuf(FlushFn flush_fn) : flush_fn_(std::move(flush_fn)) {}

    // Flushes any remaining partial line on destruction.  If the buffer
    // contains text without a trailing newline, it is flushed as-is to
    // ensure no output is silently lost at shutdown.
    ~LineBufferedStreamBuf() override {
        try {
            const std::lock_guard<std::mutex> lock(mutex_);

            if (!buffer_.empty()) {
                flush_fn_(buffer_);
                buffer_.clear();
            }
        } catch (const std::exception& e) {
            std::cerr << "DAP: error in ~LineBufferedStreamBuf: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "DAP: unknown exception in ~LineBufferedStreamBuf\n";
        }
    }

    LineBufferedStreamBuf(const LineBufferedStreamBuf&) = delete;
    LineBufferedStreamBuf& operator=(const LineBufferedStreamBuf&) = delete;
    LineBufferedStreamBuf(LineBufferedStreamBuf&&) = delete;
    LineBufferedStreamBuf& operator=(LineBufferedStreamBuf&&) = delete;

protected:
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) {
            return traits_type::eof();
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        buffer_ += static_cast<char>(ch);

        if (ch == '\n') {
            flush_fn_(buffer_);
            buffer_.clear();
        }

        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize count) override {
        const std::lock_guard<std::mutex> lock(mutex_);

        for (std::streamsize i = 0; i < count; ++i) {
            buffer_ += static_cast<char>(s[i]);

            if (s[i] == '\n') {
                flush_fn_(buffer_);
                buffer_.clear();
            }
        }

        return count;
    }

private:
    FlushFn flush_fn_;
    std::string buffer_; // GUARDED_BY(mutex_)
    // Leaf-level lock — never held while acquiring any other mutex.
    std::mutex mutex_;
};

// RAII guard for std::ostream rdbuf() redirection.
class StreamRedirectGuard {
public:
    explicit StreamRedirectGuard(std::ostream& stream, std::streambuf* new_buf)
        : stream_(stream), old_buf_(stream.rdbuf(new_buf)) {}

    ~StreamRedirectGuard() noexcept {
        try {
            stream_.rdbuf(old_buf_);
        } catch (const std::exception& e) {
            std::cerr << "DAP: error restoring stream buffer: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "DAP: unknown exception restoring stream buffer\n";
        }
    }

    StreamRedirectGuard(const StreamRedirectGuard&) = delete;
    StreamRedirectGuard& operator=(const StreamRedirectGuard&) = delete;
    StreamRedirectGuard(StreamRedirectGuard&&) = delete;
    StreamRedirectGuard& operator=(StreamRedirectGuard&&) = delete;

private:
    std::ostream& stream_;
    std::streambuf* old_buf_;
};

} // namespace luma::dap

#endif // LUMA_DAP_DEBUG_STREAM_UTILS_HPP
