#include "lsp_completion_provider.hpp"

#include <format>
#include <string>

#include "lsp_constants.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// CompletionProviderRegistry
// ═══════════════════════════════════════════════════════════

void CompletionProviderRegistry::add(std::unique_ptr<CompletionProvider> provider) {
    providers_.push_back(std::move(provider));
}

void CompletionProviderRegistry::append_all(const CompletionProviderContext& ctx,
                                            JsonValue::ArrayType& items) const {
    for (const auto& provider : providers_) {
        provider->append_completions(ctx, items);
    }
}

// ═══════════════════════════════════════════════════════════
// UserFunctionCompletionProvider
// ═══════════════════════════════════════════════════════════

void UserFunctionCompletionProvider::append_completions(const CompletionProviderContext& ctx,
                                                        JsonValue::ArrayType& items) const {
    for (const auto& [name, info] : ctx.analysis.semantic.symbols.user_functions) {
        if (util::is_qualified_name(name)) {
            continue; // skip namespace-qualified members
        }

        const std::string insert_text = call_snippet_insert_text(name, ctx.snippet_support);
        const std::string detail =
            info.return_type.empty() ? "(user function)" : "-> " + info.return_type;

        items.push_back(CompletionItemBuilder()
                            .label(name)
                            .kind(constants::completion_kind::function)
                            .detail(detail)
                            .documentation(info.signature)
                            .insert_text(insert_text)
                            .insert_text_format(ctx.snippet_support
                                                    ? constants::insert_text_format::snippet
                                                    : constants::insert_text_format::plaintext)
                            .sort_text("1" + name)
                            .data("user:" + name)
                            .build());
    }
}

// ═══════════════════════════════════════════════════════════
// TypeNameCompletionProvider
// ═══════════════════════════════════════════════════════════

void TypeNameCompletionProvider::append_completions(const CompletionProviderContext& ctx,
                                                    JsonValue::ArrayType& items) const {
    // Record types.
    for (const auto& [name, rec_info] : ctx.analysis.semantic.symbols.record_definitions) {
        if (name.starts_with(k_interface_record_prefix)) {
            continue;
        }
        std::string insert_text;
        if (ctx.snippet_support && !rec_info.fields.empty()) {
            insert_text = name + " { $0 }";
        }
        items.push_back(CompletionItemBuilder()
                            .label(name)
                            .kind(constants::completion_kind::struct_)
                            .detail("(record)")
                            .insert_text(insert_text)
                            .insert_text_format(ctx.snippet_support
                                                    ? constants::insert_text_format::snippet
                                                    : constants::insert_text_format::plaintext)
                            .sort_text("2" + name)
                            .build());
    }

    // Choice types.
    for (const auto& [name, variants] : ctx.analysis.semantic.symbols.choice_variants) {
        const std::string detail = std::format("(choice — {} variant{})", variants.size(),
                                               variants.size() == 1 ? "" : "s");
        items.push_back(CompletionItemBuilder()
                            .label(name)
                            .kind(constants::completion_kind::enum_)
                            .detail(detail)
                            .sort_text("2" + name)
                            .build());
    }
}

// ═══════════════════════════════════════════════════════════
// AnnotationCompletionProvider
// ═══════════════════════════════════════════════════════════

void AnnotationCompletionProvider::append_completions(const CompletionProviderContext& /* ctx */,
                                                      JsonValue::ArrayType& items) const {
    items.push_back(CompletionItemBuilder()
                        .label("@main")
                        .kind(constants::completion_kind::keyword)
                        .detail("(annotation)")
                        .build());
    items.push_back(CompletionItemBuilder()
                        .label("@test")
                        .kind(constants::completion_kind::keyword)
                        .detail("(annotation)")
                        .build());
}

// ═══════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════

CompletionProviderRegistry create_default_provider_registry() {
    CompletionProviderRegistry registry;
    registry.add(std::make_unique<UserFunctionCompletionProvider>());
    registry.add(std::make_unique<TypeNameCompletionProvider>());
    registry.add(std::make_unique<AnnotationCompletionProvider>());
    return registry;
}

} // namespace luma::lsp
