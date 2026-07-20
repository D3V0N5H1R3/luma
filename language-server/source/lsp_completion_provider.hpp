#ifndef LUMA_LSP_COMPLETION_PROVIDER_HPP
#define LUMA_LSP_COMPLETION_PROVIDER_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "lsp_analysis_result.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// CompletionProvider — strategy pattern for completion item building.
//
// Each provider encapsulates the logic for one category of completion
// items (keywords, user functions, parameters, local variables, type
// names, annotations, etc.).  Providers are stateless: all inputs
// arrive through CompletionProviderContext.
//
// This framework is opt-in.  Existing append_*_completions() free
// functions continue to work — providers can be adopted incrementally
// by replacing individual append_* calls with a provider invocation.
//
// Migration path:
//   1. Create a concrete CompletionProvider subclass.
//   2. Move the body of the corresponding append_*() function into
//      the provider's append_completions() override.
//   3. Register the provider in the CompletionProviderRegistry.
//   4. In generic_completions(), call registry.append_all() instead
//      of the individual append_*() calls.
//   5. Remove the old free function once all callers are migrated.
//
// Design notes:
//   - No dynamic plugin loading — providers are compiled in.
//   - No virtual dispatch overhead in the hot path until a provider
//     is actually registered and used.
//   - The context bundles everything a provider might need so that
//     the interface stays stable as new data sources are added.
// ═══════════════════════════════════════════════════════════

// Read-only context passed to every CompletionProvider.
// Bundles the analysis result, cursor position, and configuration
// flags so that providers don't need access to LspServer internals.
struct CompletionProviderContext {
    const AnalysisResult& analysis;
    int luma_line; // 1-based line number
    bool snippet_support;
    std::optional<std::string> enclosing_function; // resolved once, shared
};

// Abstract base for all completion providers.
class CompletionProvider {
public:
    virtual ~CompletionProvider() = default;

    // Append zero or more completion items to `items`.
    // Implementations must not clear existing items.
    virtual void append_completions(const CompletionProviderContext& ctx,
                                    JsonValue::ArrayType& items) const = 0;

    // Human-readable name for logging and debugging.
    [[nodiscard]] virtual std::string_view name() const = 0;
};

// Registry that collects providers and invokes them in order.
// Providers are invoked in registration order; earlier providers
// can therefore influence sort-text priority.
class CompletionProviderRegistry {
public:
    // Register a provider.  The registry takes ownership.
    void add(std::unique_ptr<CompletionProvider> provider);

    // Invoke every registered provider, appending to `items`.
    void append_all(const CompletionProviderContext& ctx, JsonValue::ArrayType& items) const;

    // Number of registered providers (useful for tests).
    [[nodiscard]] std::size_t size() const {
        return providers_.size();
    }

    [[nodiscard]] bool empty() const {
        return providers_.empty();
    }

private:
    std::vector<std::unique_ptr<CompletionProvider>> providers_;
};

// ═══════════════════════════════════════════════════════════
// Concrete providers — one per completion category.
//
// Each class mirrors an existing append_*_completions() function
// from the anonymous namespace in lsp_server_completion.cpp.
// ═══════════════════════════════════════════════════════════

// Provides user-defined top-level function completions.
class UserFunctionCompletionProvider final : public CompletionProvider {
public:
    void append_completions(const CompletionProviderContext& ctx,
                            JsonValue::ArrayType& items) const override;

    [[nodiscard]] std::string_view name() const override {
        return "user_functions";
    }
};

// Provides type name completions (records and choice types).
class TypeNameCompletionProvider final : public CompletionProvider {
public:
    void append_completions(const CompletionProviderContext& ctx,
                            JsonValue::ArrayType& items) const override;

    [[nodiscard]] std::string_view name() const override {
        return "type_names";
    }
};

// Provides annotation completions (@main, @test).
class AnnotationCompletionProvider final : public CompletionProvider {
public:
    void append_completions(const CompletionProviderContext& ctx,
                            JsonValue::ArrayType& items) const override;

    [[nodiscard]] std::string_view name() const override {
        return "annotations";
    }
};

// Factory: creates a registry pre-loaded with all built-in providers.
[[nodiscard]] CompletionProviderRegistry create_default_provider_registry();

} // namespace luma::lsp

#endif // LUMA_LSP_COMPLETION_PROVIDER_HPP
