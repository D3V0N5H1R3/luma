#ifndef LUMA_LSP_SYMBOL_LOOKUP_HPP
#define LUMA_LSP_SYMBOL_LOOKUP_HPP

#include <string>
#include <utility>
#include <vector>

#include "common/string_hash.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_optional_ref.hpp"

namespace luma::lsp {

// Convenience wrapper for querying SemanticAnalysis results.
// Provides null-safe lookup with consistent optional_ref<> return type,
// eliminating repetitive find/end-check boilerplate in handler code.
class SymbolLookup {
public:
    explicit SymbolLookup(const SemanticAnalysis& semantic) : semantic_{semantic} {}

    // Construct from an AnalysisResult (delegates to its semantic member).
    explicit SymbolLookup(const AnalysisResult& result) : semantic_{result.semantic} {}

    // Find a user-defined function by name.
    [[nodiscard]] optional_ref<const UserFunctionInfo>
    find_function(const std::string& name) const {
        return find_in_map(semantic_.symbols.user_functions, name);
    }

    // Find a top-level symbol definition by name.
    [[nodiscard]] optional_ref<const SymbolDefinition>
    find_definition(const std::string& name) const {
        return find_in_map(semantic_.symbols.definitions, name);
    }

    // Find a record definition by name.
    [[nodiscard]] optional_ref<const RecordInfo> find_record(const std::string& name) const {
        return find_in_map(semantic_.symbols.record_definitions, name);
    }

    // Find a doc comment for a symbol by name.
    [[nodiscard]] optional_ref<const std::string> find_doc_comment(const std::string& name) const {
        return find_in_map(semantic_.symbols.doc_comments, name);
    }

    // Find choice type variants by type name.
    [[nodiscard]] optional_ref<const std::vector<std::string>>
    find_choice_variants(const std::string& name) const {
        return find_in_map(semantic_.symbols.choice_variants, name);
    }

    // Find function-local variables by function name.
    [[nodiscard]] optional_ref<const StringMap<std::string>>
    find_function_locals(const std::string& function_name) const {
        return find_in_map(semantic_.locals.function_locals, function_name);
    }

    // Find function body line range by function name.
    [[nodiscard]] optional_ref<const std::pair<int, int>>
    find_function_body_range(const std::string& function_name) const {
        return find_in_map(semantic_.functions.function_body_ranges, function_name);
    }

    // Find the origin file path for a symbol (from includes).
    [[nodiscard]] optional_ref<const std::string>
    find_symbol_origin(const std::string& name) const {
        return find_in_map(semantic_.includes.symbol_origins, name);
    }

    // Find interface implementations (interface name → list of record names).
    [[nodiscard]] optional_ref<const std::vector<std::string>>
    find_interface_implementations(const std::string& name) const {
        return find_in_map(semantic_.symbols.interface_implementations, name);
    }

    // Direct access to the underlying semantic analysis (for uncommon lookups).
    [[nodiscard]] const SemanticAnalysis& semantic() const {
        return semantic_;
    }

private:
    template <typename MapType>
    [[nodiscard]] optional_ref<const typename MapType::mapped_type>
    find_in_map(const MapType& m, const std::string& key) const {
        auto it = m.find(key);
        if (it != m.end()) {
            return it->second;
        }
        return {};
    }

    const SemanticAnalysis& semantic_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_SYMBOL_LOOKUP_HPP
