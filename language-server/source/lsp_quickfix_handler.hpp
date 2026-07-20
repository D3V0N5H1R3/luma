#ifndef LUMA_LSP_QUICKFIX_HANDLER_HPP
#define LUMA_LSP_QUICKFIX_HANDLER_HPP

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_code_action_builder.hpp"
#include "lsp_config.hpp"
#include "lsp_document_store.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// Quick-fix handler framework
// ═══════════════════════════════════════════════════════════════════════════
//
// Each quick-fix follows a uniform 5-step pattern (see LS-24):
//
//   1. Match   — Does this handler apply to the given diagnostic?
//   2. Extract — Pull relevant names/symbols from the diagnostic message.
//   3. Locate  — Find the target site in the token stream.
//   4. Build   — Construct one or more CodeActions with TextEdits.
//   5. Emit    — Return the actions.
//
// This framework encapsulates that pattern behind a virtual interface so
// that new quick-fixes can be added by implementing a handler class rather
// than extending a monolithic if-else chain.
//
// Migration guide
// ───────────────
// The existing quick-fixes in collect_quick_fixes() continue to work
// unchanged.  To migrate one:
//
//   1. Create a class that derives from QuickFixHandler.
//   2. Implement matches() — return true when the diagnostic code/message
//      identifies the specific warning or error.
//   3. Implement generate() — build and return CodeAction JSON values.
//   4. Register an instance in QuickFixRegistry (see below).
//   5. Remove the corresponding if-block from collect_quick_fixes().
//
// Example:
//
//   class SelfAssignmentFix final : public QuickFixHandler {
//   public:
//       [[nodiscard]] bool matches(const Diagnostic& diag) const override {
//           return diag.code == "W0005" ||
//                  diag.message.starts_with("Self-assignment: '");
//       }
//
//       [[nodiscard]] std::vector<JsonValue> generate(
//           const Diagnostic& diag, const QuickFixContext& ctx) const override {
//           std::vector<JsonValue> actions;
//           actions.push_back(CodeActionBuilder()
//               .set_title("Remove self-assignment")
//               .set_kind(code_action_kind::k_quickfix)
//               .set_diagnostics({diag})
//               .add_edit(ctx.uri,
//                   Range{Position{diag.range.start.line, 0},
//                         Position{diag.range.start.line + 1, 0}}, "")
//               .build());
//           return actions;
//       }
//   };
//
// Then in the registry setup:
//
//   registry.register_handler(std::make_unique<SelfAssignmentFix>());
//
// ═══════════════════════════════════════════════════════════════════════════

// Bundles everything a quick-fix handler might need to locate code and
// build edits.  Passed by const reference to generate().
struct QuickFixContext {
    const std::string& uri;
    const AnalysisResult& analysis;
    const DocumentStore& documents;
    const LockToken& lock_token;
    const LspAnalysisCache& analysis_cache;
};

// Base class for individual quick-fix handlers.
//
// Each subclass encapsulates matching logic and code-action generation for
// a single diagnostic kind (or a small family of related diagnostics).
class QuickFixHandler {
public:
    virtual ~QuickFixHandler() = default;

    // Return true if this handler can produce a fix for the given diagnostic.
    [[nodiscard]] virtual bool matches(const Diagnostic& diag) const = 0;

    // Produce zero or more code-action JSON values for the diagnostic.
    // Returning an empty vector is valid (e.g. when preconditions are not met
    // even though the diagnostic code matched).
    [[nodiscard]] virtual std::vector<JsonValue> generate(const Diagnostic& diag,
                                                          const QuickFixContext& ctx) const = 0;
};

// Compile-time registry of quick-fix handlers.
//
// Handlers are registered once at startup (or lazily on first use) and
// queried on every textDocument/codeAction request.  The registry is
// intentionally simple — no dynamic plugin loading, no priority ordering.
// Handlers are tried in registration order; every matching handler
// contributes its actions.
class QuickFixRegistry {
public:
    // Register a handler.  Ownership is transferred to the registry.
    void register_handler(std::unique_ptr<QuickFixHandler> handler) {
        handlers_.push_back(std::move(handler));
    }

    // Iterate over all diagnostics, invoke every matching handler, and
    // collect the resulting code actions.
    [[nodiscard]] std::vector<JsonValue> collect_fixes(const std::vector<Diagnostic>& diagnostics,
                                                       const QuickFixContext& ctx) const {
        std::vector<JsonValue> actions;

        for (const auto& diag : diagnostics) {
            for (const auto& handler : handlers_) {
                if (handler->matches(diag)) {
                    auto fixes = handler->generate(diag, ctx);
                    actions.insert(actions.end(), std::make_move_iterator(fixes.begin()),
                                   std::make_move_iterator(fixes.end()));
                }
            }
        }

        return actions;
    }

    // Number of registered handlers (useful for tests).
    [[nodiscard]] std::size_t handler_count() const {
        return handlers_.size();
    }

private:
    std::vector<std::unique_ptr<QuickFixHandler>> handlers_;
};

// ─── Common quick-fix action builders ───────────────────────────────────
//
// Many quick fixes share the same structural pattern: remove a line,
// prefix an identifier with '_', etc.  These helpers encapsulate the
// most repeated CodeAction construction patterns so that individual
// quick-fix implementations stay concise while remaining independently
// readable.

// Build a quick-fix action that removes the entire diagnostic line.
// Used by self-assignment removal, unreachable code removal, and
// unused-variable removal.
[[nodiscard]] inline JsonValue build_line_removal_action(const std::string& title,
                                                         const std::string& uri,
                                                         const Diagnostic& diag) {
    return CodeActionBuilder()
        .set_title(title)
        .set_kind(code_action_kind::k_quickfix)
        .set_diagnostics({diag})
        .add_edit(uri,
                  Range{Position{diag.range.start.line, 0}, Position{diag.range.start.line + 1, 0}},
                  "")
        .build();
}

} // namespace luma::lsp

#endif // LUMA_LSP_QUICKFIX_HANDLER_HPP
