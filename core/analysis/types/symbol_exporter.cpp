// ─────────────────────────────────────────────────────────────────────────────
// Symbol Exporter                        (SymbolExporter implementation)
// ─────────────────────────────────────────────────────────────────────────────

#include "analysis/types/symbol_exporter.hpp"

#include <utility>

#include "analysis/ast/declaration.hpp"
#include "analysis/types/stdlib_type_handler.hpp"
#include "analysis/types/type_checking_context.hpp"

namespace luma {

SymbolTable SymbolExporter::build(const TypeCheckingServices& services) const {
    SymbolTable table;

    // Export stdlib return types.
    table.stdlib_signatures = services.stdlib_handler().build_signature_map();

    // Export user-defined functions.
    for (const auto& [name, func_decl] : services.functions()) {
        ResolvedFunction exported_function;
        exported_function.name = name;
        exported_function.location = func_decl->location;
        exported_function.is_test = func_decl->is_test;

        // When the function declares a return type, prefer the type inferred
        // during check() — recorded on the Func-typed symbol in the active
        // scope — over the bare annotation.  It stays Unknown otherwise.
        if (!func_decl->return_type.name().empty()) {
            if (const auto* symbol = services.lookup_variable(name)) {
                if (symbol->type.kind == TypeInfo::Kind::Func && symbol->type.return_type) {
                    exported_function.return_type = *symbol->type.return_type;
                }
            }
        }

        // Parameter names are exported in declaration order; their types are
        // left Unknown because full resolution needs the lexical scope active
        // during check(), which the exporter does not retain.
        for (const auto& param : func_decl->parameters) {
            const TypeInfo param_type = TypeInfo::make(TypeInfo::Kind::Unknown);
            exported_function.parameters.emplace_back(param.name, param_type);
        }

        table.functions[name] = std::move(exported_function);
    }

    // Export record definitions.
    for (const auto& [name, record_decl] : services.records()) {
        ResolvedRecord exported_record;
        exported_record.name = name;
        exported_record.location = record_decl->location;

        for (const auto& field : record_decl->fields) {
            const TypeInfo field_type = TypeInfo::make(TypeInfo::Kind::Unknown);
            exported_record.fields.emplace_back(field.name, field_type);
        }

        table.records[name] = std::move(exported_record);
    }

    // Export choice definitions.
    for (const auto& [name, choice_decl] : services.choices()) {
        ResolvedChoice exported_choice;
        exported_choice.name = name;
        exported_choice.location = choice_decl->location;

        for (const auto& variant : choice_decl->variants) {
            exported_choice.variants.push_back(variant.name);
        }

        table.choices[name] = std::move(exported_choice);
    }

    // Export top-level scope variables.
    if (const auto& scope = services.context().current_scope) {
        for (const auto& [name, symbol] : scope->locals()) {
            // Skip functions — they're already in the functions map.
            if (symbol.type.kind == TypeInfo::Kind::Func) {
                continue;
            }

            ResolvedSymbol exported_symbol;
            exported_symbol.name = name;
            exported_symbol.type = symbol.type;
            exported_symbol.location = symbol.location;
            exported_symbol.is_mutable = symbol.is_mutable;
            exported_symbol.is_parameter = symbol.is_parameter;
            table.variables[name] = std::move(exported_symbol);
        }
    }

    return table;
}

} // namespace luma
