#ifndef LUMA_LSP_REFACTORING_PROVIDER_HPP
#define LUMA_LSP_REFACTORING_PROVIDER_HPP

#include <memory>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_document_store.hpp"
#include "lsp_server_state_lock.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// Refactoring provider framework
// ═══════════════════════════════════════════════════════════════════════════
//
// This mirrors the quick-fix framework (see lsp_quickfix_handler.hpp) for
// cursor/selection-driven refactorings. Unlike quick fixes — which match
// against diagnostics — refactorings inspect the request selection and the
// analysed program, so the context carries the raw request params (which hold
// the selection range) rather than a diagnostic.
//
// To add a refactoring:
//   1. Create a class deriving from RefactoringProvider.
//   2. Implement generate() — inspect the context and return zero or more
//      CodeAction JSON values.
//   3. Register an instance in get_refactoring_registry().

// Bundles everything a refactoring provider needs to locate code and build
// edits. Passed by const reference to generate().
struct RefactoringContext {
    const std::string& uri;
    const AnalysisResult& analysis;
    const DocumentStore& documents;
    const LockToken& lock_token;
    const JsonValue& params;
};

// Base class for a single refactoring (or a small family of related ones).
class RefactoringProvider {
public:
    virtual ~RefactoringProvider() = default;

    // Produce zero or more code-action JSON values for the request context.
    // Returning an empty vector is valid when the refactoring does not apply.
    [[nodiscard]] virtual std::vector<JsonValue> generate(const RefactoringContext& ctx) const = 0;
};

// Registry of refactoring providers, queried on every codeAction request.
// Providers run in registration order; each contributes its actions.
class RefactoringRegistry {
public:
    // Register a provider. Ownership is transferred to the registry.
    void register_provider(std::unique_ptr<RefactoringProvider> provider) {
        providers_.push_back(std::move(provider));
    }

    // Invoke every provider and collect the resulting code actions.
    [[nodiscard]] std::vector<JsonValue> collect(const RefactoringContext& ctx) const {
        std::vector<JsonValue> actions;
        for (const auto& provider : providers_) {
            auto produced = provider->generate(ctx);
            actions.insert(actions.end(), std::make_move_iterator(produced.begin()),
                           std::make_move_iterator(produced.end()));
        }
        return actions;
    }

    // Number of registered providers (useful for tests).
    [[nodiscard]] std::size_t provider_count() const {
        return providers_.size();
    }

private:
    std::vector<std::unique_ptr<RefactoringProvider>> providers_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_REFACTORING_PROVIDER_HPP
