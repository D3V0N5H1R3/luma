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
#include <string_view>

#include "diagnostic_log.hpp"

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
            flush_partial_line();
        } catch (const std::exception& e) {
            write_adapter_diagnostic(std::string{"DAP: error in ~LineBufferedStreamBuf: "} +
                                     e.what());
        } catch (...) {
            write_adapter_diagnostic("DAP: unknown exception in ~LineBufferedStreamBuf");
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

        const char c = traits_type::to_char_type(ch);
        (void)xsputn(&c, 1);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize count) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        const std::string_view text{s, static_cast<std::size_t>(count)};
        std::size_t start = 0;

        // Emit every complete line; keep the trailing partial line buffered.
        for (auto newline = text.find('\n'); newline != std::string_view::npos;
             newline = text.find('\n', start)) {
            buffer_.append(text.substr(start, newline - start + 1));
            flush_fn_(buffer_);
            buffer_.clear();
            start = newline + 1;
        }

        buffer_.append(text.substr(start));
        return count;
    }

    // std::flush / std::endl: emit a pending partial line (e.g. a prompt with
    // no newline) instead of holding it until the next newline.
    int sync() override {
        flush_partial_line();
        return 0;
    }

private:
    void flush_partial_line() {
        const std::lock_guard<std::mutex> lock(mutex_);

        if (!buffer_.empty()) {
            flush_fn_(buffer_);
            buffer_.clear();
        }
    }

    FlushFn flush_fn_;
    std::string buffer_; // GUARDED_BY(mutex_)
    // Held while flush_fn_ runs, so the output chain is mutex_ → send_mutex_ →
    // the transport's write mutex; no lock in that chain is ever taken in the
    // opposite order (see dap_lock_ordering.hpp), so this cannot deadlock.
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
            write_adapter_diagnostic(std::string{"DAP: error restoring stream buffer: "} +
                                     e.what());
        } catch (...) {
            write_adapter_diagnostic("DAP: unknown exception restoring stream buffer");
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
