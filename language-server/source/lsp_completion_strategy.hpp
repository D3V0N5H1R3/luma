#ifndef LUMA_LSP_COMPLETION_STRATEGY_HPP
#define LUMA_LSP_COMPLETION_STRATEGY_HPP

// ═══════════════════════════════════════════════════════════
// CompletionStrategy — chain-of-responsibility for top-level
// completion context dispatch.
//
// While CompletionProvider (lsp_completion_provider.hpp) handles
// additive sub-categories within generic completions, each
// CompletionStrategy represents a mutually exclusive completion
// context (include paths, type annotations, match arms, pipes,
// etc.).  The strategy chain is evaluated in order; the first
// strategy that matches the context produces the result.
//
// Design:
//   - Each strategy inspects the CompletionContext and returns
//     std::nullopt if it does not apply.
//   - The chain stops at the first non-nullopt result.
//   - A fallback strategy at the end handles generic/dot
//     completions (always returns a value).
// ═══════════════════════════════════════════════════════════

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "json/json.hpp"
#include "lsp_completion_handler.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

// Abstract base for context-aware completion strategies.
class CompletionStrategy {
public:
    virtual ~CompletionStrategy() = default;

    // Attempt to provide completions for the given context.
    // Returns std::nullopt if this strategy does not apply.
    [[nodiscard]] virtual std::optional<JsonValue>
    try_provide(const LspCompletionHandler::CompletionContext& ctx,
                LspHandlerContext& handler_ctx) const = 0;

    // Human-readable name for logging and debugging.
    [[nodiscard]] virtual std::string_view name() const = 0;
};

// Ordered chain of strategies.  Iterates until one produces a result.
class CompletionStrategyChain {
public:
    void add(std::unique_ptr<CompletionStrategy> strategy);

    // Run the chain.  Returns the first non-nullopt result, or nullopt
    // if no strategy matched.
    [[nodiscard]] std::optional<JsonValue>
    try_provide(const LspCompletionHandler::CompletionContext& ctx,
                LspHandlerContext& handler_ctx) const;

    [[nodiscard]] std::size_t size() const {
        return strategies_.size();
    }

private:
    std::vector<std::unique_ptr<CompletionStrategy>> strategies_;
};

// ═══════════════════════════════════════════════════════════
// Concrete strategies
// ═══════════════════════════════════════════════════════════

// Completes file paths after `include "`.
class IncludePathCompletionStrategy final : public CompletionStrategy {
public:
    [[nodiscard]] std::optional<JsonValue>
    try_provide(const LspCompletionHandler::CompletionContext& ctx,
                LspHandlerContext& handler_ctx) const override;

    [[nodiscard]] std::string_view name() const override {
        return "include_path";
    }
};

// Completes type names after `: ` in declarations.
class TypeAnnotationCompletionStrategy final : public CompletionStrategy {
public:
    [[nodiscard]] std::optional<JsonValue>
    try_provide(const LspCompletionHandler::CompletionContext& ctx,
                LspHandlerContext& handler_ctx) const override;

    [[nodiscard]] std::string_view name() const override {
        return "type_annotation";
    }
};

// Completes match arm patterns after `case `.
class MatchArmCompletionStrategy final : public CompletionStrategy {
public:
    [[nodiscard]] std::optional<JsonValue>
    try_provide(const LspCompletionHandler::CompletionContext& ctx,
                LspHandlerContext& handler_ctx) const override;

    [[nodiscard]] std::string_view name() const override {
        return "match_arm";
    }
};

// Completes pipe targets after `|>`.
class PipeCompletionStrategy final : public CompletionStrategy {
public:
    [[nodiscard]] std::optional<JsonValue>
    try_provide(const LspCompletionHandler::CompletionContext& ctx,
                LspHandlerContext& handler_ctx) const override;

    [[nodiscard]] std::string_view name() const override {
        return "pipe";
    }
};

// Completes record field names inside `RecordName { ... }`.
class RecordFieldCompletionStrategy final : public CompletionStrategy {
public:
    [[nodiscard]] std::optional<JsonValue>
    try_provide(const LspCompletionHandler::CompletionContext& ctx,
                LspHandlerContext& handler_ctx) const override;

    [[nodiscard]] std::string_view name() const override {
        return "record_field";
    }
};

// Factory: creates a chain pre-loaded with all built-in strategies.
[[nodiscard]] CompletionStrategyChain create_default_strategy_chain();

} // namespace luma::lsp

#endif // LUMA_LSP_COMPLETION_STRATEGY_HPP
