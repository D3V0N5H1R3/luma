#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
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
// error, or signals EOF.  This target drives that logic with arbitrary bytes.

namespace {

// In-memory transport that streams the fuzzer's bytes through the shared
// framing logic.  read_raw is the only I/O primitive BufferedTransport needs;
// writes are discarded.
class MemoryTransport final : public luma::protocol::BufferedTransport {
public:
    explicit MemoryTransport(std::string data) : data_{std::move(data)} {}

    void write_message(const luma::json::JsonValue& /*message*/) override {}

protected:
    std::size_t read_raw(std::span<char> buf) override {
        const std::size_t remaining = data_.size() - pos_;
        const std::size_t n = std::min(buf.size(), remaining);
        if (n > 0) {
            std::memcpy(buf.data(), data_.data() + pos_, n);
            pos_ += n;
        }
        return n; // 0 signals EOF.
    }

private:
    std::string data_;
    std::size_t pos_{0};
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(data, size, luma::fuzz::max_input_size, [&](const std::string& s) {
        MemoryTransport transport{s};

        // Drain framed messages until EOF.  A successful read_message() always
        // consumes at least one byte — the blank line that ends the header
        // block is read straight from the stream — so an N-byte input yields at
        // most N messages.  Bounding the loop by `size` is therefore a tight,
        // provably-correct guard against any zero-consuming iteration.
        for (std::size_t i = 0; i <= size; ++i) {
            const auto message = transport.read_message();
            if (!message) {
                break;
            }
            luma::fuzz::do_not_optimize(message->to_string().size());
        }
    });
}
