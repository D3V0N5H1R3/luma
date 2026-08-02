// DapProtocolHandler unit tests — disconnect-callback delivery on transport
// write failure (B01) and message-loop shutdown semantics.

#include <atomic>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dap_protocol_handler.hpp"
#include "json/json.hpp"
#include "test_framework.hpp"

using namespace luma::dap;
using luma::json::JsonValue;

namespace {

// A transport whose write_message() can be told to fail (simulating a
// broken pipe) and whose read_message() replays a queued sequence of
// messages before returning nullopt (EOF).
class FakeTransport : public luma::protocol::Transport {
public:
    void enqueue_request(const std::string& command, int seq) {
        JsonValue::ObjectType msg;
        msg["type"] = JsonValue(std::string("request"));
        msg["command"] = JsonValue(command);
        msg["seq"] = JsonValue(seq);
        msg["arguments"] = JsonValue(JsonValue::ObjectType{});
        inbox_.emplace_back(JsonValue(std::move(msg)));
    }

    [[nodiscard]] std::optional<JsonValue> read_message() override {
        if (read_idx_ >= inbox_.size()) {
            return std::nullopt;
        }
        return inbox_[read_idx_++];
    }

    void set_write_fails(bool fails) {
        write_fails_.store(fails);
    }

    void write_message(const JsonValue& message) override {
        if (write_fails_.load()) {
            throw std::runtime_error("broken pipe");
        }
        written_.push_back(message);
    }

    [[nodiscard]] const std::vector<JsonValue>& written() const {
        return written_;
    }

protected:
    [[nodiscard]] std::optional<std::string> read_line() override {
        return std::nullopt;
    }

    [[nodiscard]] std::string read_exact(std::size_t /*count*/) override {
        return {};
    }

private:
    std::vector<JsonValue> inbox_;
    std::size_t read_idx_{0};
    std::atomic<bool> write_fails_{false};
    std::vector<JsonValue> written_;
};

// ─── B01: send_event broken-pipe path must invoke the disconnect callback ─

// Regression: a broken pipe encountered inside send_event() (called from a
// thread other than the one running run(), e.g. the execution thread
// emitting a `stopped` event) must still cause the session to be torn down.
// Previously send_event() only set the disconnected_ flag; nothing ever
// invoked disconnect_callback_, so run() would eventually exit but the
// callback — which terminates the debug session — was never called,
// orphaning the session indefinitely.
void test_send_event_broken_pipe_invokes_disconnect_callback() {
    FakeTransport transport;
    transport.enqueue_request("initialize", 1);

    DapProtocolHandler handler{transport};

    std::atomic<int> disconnect_calls{0};
    handler.set_disconnect_callback([&] { disconnect_calls.fetch_add(1); });

    handler.register_handler("initialize", [](const JsonValue&) {
        return DapProtocolHandler::HandlerResult::ok();
    });

    // Simulate send_event() being called from the execution thread and
    // hitting a broken pipe *before* run() is ever invoked — mirrors the
    // real timing where the event write races the message loop.
    transport.set_write_fails(true);
    handler.send_event("stopped", JsonValue(JsonValue::ObjectType{}));

    ASSERT_TRUE(handler.is_disconnected());
    ASSERT_EQ(disconnect_calls.load(), 0); // Not yet invoked — deferred to run().

    // run() must observe the flag on loop entry, exit immediately, and
    // invoke the disconnect callback exactly once before returning.
    const auto exit_code = handler.run();

    ASSERT_EQ(exit_code, 0);
    ASSERT_EQ(disconnect_calls.load(), 1);
}

// A write failure during send_response() must still invoke the disconnect
// callback exactly once — the pre-existing, already-correct path — and must
// not double-invoke it once run() subsequently exits.
void test_send_response_broken_pipe_invokes_disconnect_callback_once() {
    FakeTransport transport;
    transport.enqueue_request("initialize", 1);

    DapProtocolHandler handler{transport};

    std::atomic<int> disconnect_calls{0};
    handler.set_disconnect_callback([&] { disconnect_calls.fetch_add(1); });

    handler.register_handler("initialize", [](const JsonValue&) {
        return DapProtocolHandler::HandlerResult::ok();
    });

    transport.set_write_fails(true);

    const auto exit_code = handler.run();

    ASSERT_EQ(exit_code, 0);
    ASSERT_TRUE(handler.is_disconnected());
    // Invoked once by signal_disconnect() inside send_response(); run()'s
    // post-loop invoke_disconnect_callback_once() must not fire it again.
    ASSERT_EQ(disconnect_calls.load(), 1);
}

// A clean EOF (no write failures) must still invoke the disconnect callback
// exactly once so the owning server always tears down the session when the
// message loop exits, regardless of why.
void test_clean_eof_invokes_disconnect_callback_once() {
    FakeTransport transport; // No queued messages — read_message() returns EOF immediately.

    DapProtocolHandler handler{transport};

    std::atomic<int> disconnect_calls{0};
    handler.set_disconnect_callback([&] { disconnect_calls.fetch_add(1); });

    const auto exit_code = handler.run();

    ASSERT_EQ(exit_code, 0);
    ASSERT_EQ(disconnect_calls.load(), 1);
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Protocol Handler Tests");

    RUN(test_send_event_broken_pipe_invokes_disconnect_callback);
    RUN(test_send_response_broken_pipe_invokes_disconnect_callback_once);
    RUN(test_clean_eof_invokes_disconnect_callback_once);

    return SUMMARY();
}
