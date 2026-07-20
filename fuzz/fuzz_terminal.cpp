#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/io/terminal_key_decoder.hpp"

// LibFuzzer entry point for the Terminal key decoder (luma::terminal_detail::
// decode_key, declared in core/runtime/stdlib/io/terminal_key_decoder.hpp).
//
// decode_key is the trust-boundary parser behind Terminal.read_key / read_input:
// it turns the raw byte stream a terminal delivers in raw mode — single
// keystrokes, multi-byte UTF-8 code points, and ANSI escape sequences for
// arrows, function keys, modifier combinations and SGR mouse reports — into a
// stable key-name string. The bytes are entirely under the control of whatever
// is connected to stdin (a real terminal, a pipe, a malicious program), so the
// parser must never crash, read out of bounds or loop unboundedly on hostile
// input. The POSIX backend (terminal_input_posix.cpp) drives this exact code
// with live stdin, which no unit test can reach deterministically; this target
// exercises the grammar directly over arbitrary bytes instead.
//
// The decoder is decoupled from blocking I/O via a byte_reader callback: the
// first input byte seeds decode_key, and the remainder feed the callback, which
// yields -1 once exhausted (mirroring an escape-sequence timeout). Each input is
// decoded with mouse reporting both disabled and enabled so that the SGR mouse
// branch is reached. A non-empty-result oracle runs on top of the never-crash
// contract: every byte maps to a branch that returns a non-empty classification,
// so an empty result signals a parser regression.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > luma::fuzz::max_input_size) {
        return 0;
    }

    return luma::fuzz::run([&] {
        const int first = data[0];

        for (const bool mouse_mode : {false, true}) {
            std::size_t pos{1};

            auto read_more = [&]() -> int {
                if (pos >= size) {
                    return -1;
                }

                return data[pos++];
            };

            const std::string key = luma::terminal_detail::decode_key(first, read_more, mouse_mode);

            if (key.empty()) {
                luma::fuzz::trap(); // every byte must classify to a non-empty key name.
            }

            luma::fuzz::do_not_optimize(key.size());
        }
    });
}
