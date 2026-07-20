// parser_cursor.hpp — shared scanner cursor for the JSON and XML parsers.
//
// Both parsers are hand-written recursive-descent scanners over a
// std::string_view.  They share the same cursor state (input/position/depth)
// and the same peek/advance/expect/depth-guard primitives, differing only in
// how they report errors (JSON throws std::runtime_error, XML throws
// RuntimeError) and in how they treat whitespace.  This template factors out
// the common primitives; each parser supplies an error-reporting policy and
// keeps its own skip_whitespace().

#pragma once

#include <cstddef>
#include <string_view>

namespace luma::parser_detail {

// Cursor over the input text, parameterised by an error-reporting policy.
// ErrorPolicy must provide three [[noreturn]] static members:
//   void unexpected_end();                       -- end of input reached
//   void expected(char c, std::size_t position); -- expected `c` at `position`
//   void too_deep();                             -- nesting depth limit exceeded
// Derived parsers bring the protected members into scope with using-declarations
// and access input_/pos_/depth_ directly for format-specific scanning.
template <typename ErrorPolicy> class ParserCursor {
public:
    explicit ParserCursor(std::string_view input) : input_{input} {}

protected:
    std::string_view input_;
    std::size_t pos_{0};
    int depth_{0};

    // True when the cursor has consumed all input.
    [[nodiscard]] bool at_end() const {
        return pos_ >= input_.size();
    }

    // Return the current character without consuming it.
    [[nodiscard]] char peek() const {
        if (at_end()) {
            ErrorPolicy::unexpected_end();
        }

        return input_[pos_];
    }

    // Consume and return the current character.
    char advance() {
        if (at_end()) {
            ErrorPolicy::unexpected_end();
        }

        return input_[pos_++];
    }

    // Consume the current character, requiring it to equal `c`.
    void expect(char c) {
        if (advance() != c) {
            ErrorPolicy::expected(c, pos_ - 1);
        }
    }

    // Enter a nested value, rejecting documents deeper than `limit`.
    void enter_depth(int limit) {
        if (++depth_ > limit) {
            ErrorPolicy::too_deep();
        }
    }
};

} // namespace luma::parser_detail
