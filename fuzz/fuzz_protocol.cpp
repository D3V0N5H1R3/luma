#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "fuzz_harness.hpp"
#include "json/json.hpp"
#include "protocol/buffered_transport.hpp"

// LibFuzzer entry point for the LSP/DAP message-framing transport.
//
// Transport::read_message() parses Content-Length headers and the framed JSON
// body from an untrusted byte stream (the editor's stdio).  Malformed headers,
// oversized declared lengths, and partial frames must be handled without
// crashing: the transport either resyncs to the next message, reports a parse
// error, or signals EOF.  This target drives that logic with arbitrary bytes
// and, like the LSP/DAP message loops, keeps reading after a recoverable error
// so the resync and buffered-header paths that follow a bad frame are reached.

namespace {

// In-memory transport that streams the fuzzer's bytes through the shared
// framing logic.  read_raw is the only I/O primitive BufferedTransport needs;
// writes are discarded.
class MemoryTransport final : public luma::protocol::BufferedTransport {
public:
    explicit MemoryTransport(std::string data) : data_{std::move(data)} {
        // Recoverable errors are expected on every malformed frame; reporting
        // them to stderr would only slow the fuzzer down.
        set_error_callback([](std::string_view /*message*/) {});
    }

    void write_message(const luma::json::JsonValue& /*message*/) override {}

protected:
    std::size_t read_raw(std::span<char> buf) override {
        const std::size_t remaining = data_.size() - pos_;
        const std::size_t n = std::min(buf.size(), remaining);
        std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(pos_), n, buf.begin());
        pos_ += n;
        return n; // 0 signals EOF.
    }

private:
    std::string data_;
    std::size_t pos_{0};
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::k_max_input_size, [&](const std::string& s) {
            MemoryTransport transport{s};

            // Drain framed messages until EOF, continuing past recoverable errors
            // as the server loops do.  Every read that yields a message or a
            // recoverable error consumes at least one byte — a header block always
            // ends with a blank line read straight from the stream, and a frame
            // reused from a resync still needs that terminator — so an N-byte input
            // allows at most N such reads.  Bounding the loop by `size` is a tight
            // guard against any zero-consuming iteration.
            for (std::size_t i = 0; i <= size; ++i) {
                const auto message = transport.read_message();
                if (message) {
                    luma::fuzz::do_not_optimize(message->to_string().size());
                } else if (!transport.had_recoverable_read_error()) {
                    break; // EOF.
                }
            }
        });
}
