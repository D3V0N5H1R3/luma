// ─────────────────────────────────────────────────────────────────────────────
// Interface Satisfaction                   (TypeChecker partial implementation)
// ─────────────────────────────────────────────────────────────────────────────
// Structural type checking with generic parameter binding.  These methods
// verify that a concrete record (or source interface) provides every field
// required by a target interface, substituting generic type arguments during
// comparison.
//
//   check_structural_satisfaction()  — Shared field-by-field structural check.
//   satisfies_interface()            — Record-vs-interface structural check.
//   satisfies_interface_interface()  — Interface-vs-interface structural
//                                      subtyping.
//
// Split from type_checker_resolve.cpp for organisational clarity.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/types/type_checker.hpp"
#include "common/scope_guard.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Interface satisfaction
// ═══════════════════════════════════════════════════════════

// Shared structural check: verifies that every target field has a matching
// source field with an assignable type, substituting generic type arguments
// via prefixed bindings to avoid name collisions.
bool TypeChecker::check_structural_satisfaction(
    const std::vector<TypeParam>& source_params, const std::vector<TypeInfo>& source_type_args,
    const std::vector<RecordField>& source_fields, const std::vector<TypeParam>& target_params,
    const std::vector<TypeInfo>& target_type_args, const std::vector<RecordField>& target_fields,
    std::string_view source_prefix, std::string_view target_prefix) {
    // Temporarily bind type params with prefixed names to avoid collisions
    // when source and target share the same type parameter name.
    std::vector<std::pair<std::string, TypeInfo>> prefixed_entries;

    const auto add_prefixed_bindings = [&](std::string_view prefix,
                                           const std::vector<TypeParam>& params,
                                           const std::vector<TypeInfo>& type_args) {
        for (std::size_t i{0}; i < params.size() && i < type_args.size(); ++i) {
            prefixed_entries.emplace_back(std::string{prefix} + params[i].name, type_args[i]);
        }
    };
    add_prefixed_bindings(source_prefix, source_params, source_type_args);
    add_prefixed_bindings(target_prefix, target_params, target_type_args);

    const GenericResolver::ParamGuard prefixed_guard{generics_.bindings(), prefixed_entries};

    // Save bare type-param names BEFORE the field resolution loop so we can
    // restore them afterwards (the loop temporarily aliases prefixed names
    // to bare names, which would corrupt outer-scope bindings).
    //
    // NOTE: We cannot use ParamGuard here because it inserts keys
    // on construction.  Absent keys must remain absent until the field loop
    // aliases them — premature insertion would make resolve_type() see a
    // stale Unknown binding instead of falling through to named-type lookup.
    std::vector<std::pair<std::string, std::optional<TypeInfo>>> saved_bare;

    const auto collect_bare = [&](const std::vector<TypeParam>& params) {
        for (const auto& tp : params) {
            const bool already_saved =
                std::ranges::any_of(saved_bare, [&](const auto& e) { return e.first == tp.name; });
            if (already_saved) {
                continue;
            }
            const auto it = generics_.bindings().find(tp.name);
            saved_bare.emplace_back(tp.name, it != generics_.bindings().end()
                                                 ? std::optional{it->second}
                                                 : std::nullopt);
        }
    };
    collect_bare(target_params);
    collect_bare(source_params);

    const ScopeGuard bare_guard{[&] {
        for (auto& entry : std::views::reverse(saved_bare)) {
            if (entry.second) {
                generics_.bindings()[entry.first] = *entry.second;
            } else {
                generics_.bindings().erase(entry.first);
            }
        }
    }};

    // Alias the prefixed bindings back to their bare type-param names so that
    // resolve_type() sees the correct substitution for this side's fields.
    const auto alias_prefixed_to_bare = [&](std::string_view prefix,
                                            const std::vector<TypeParam>& params) {
        for (const auto& param : params) {
            const auto it = generics_.bindings().find(std::string{prefix} + param.name);
            if (it != generics_.bindings().end()) {
                generics_.bindings()[param.name] = it->second;
            }
        }
    };

    bool result{true};

    for (const auto& target_field : target_fields) {
        const auto match = std::ranges::find_if(
            source_fields, [&](const auto& f) { return f.name == target_field.name; });

        if (match == source_fields.end()) {
            result = false;
            break;
        }

        alias_prefixed_to_bare(target_prefix, target_params);
        const auto target_field_type = resolve_type(target_field.type);

        alias_prefixed_to_bare(source_prefix, source_params);
        const auto source_field_type = resolve_type(match->type);

        if (!is_assignable(target_field_type, source_field_type)) {
            result = false;
            break;
        }
    }

    return result;
}

bool TypeChecker::satisfies_interface(std::string_view record_name, std::string_view iface_name,
                                      const std::vector<TypeInfo>& source_type_args,
                                      const std::vector<TypeInfo>& target_type_args) {
    const auto rec_it = records_.find(record_name);
    const auto iface_it = interfaces_.find(iface_name);

    if (rec_it == records_.end() || iface_it == interfaces_.end()) {
        return false;
    }

    // Break structural-satisfaction cycles on recursive interfaces: a
    // (source, target) pair already under evaluation is treated as satisfied
    // (the coinductive rule) so the recursion terminates instead of overflowing
    // the stack.  The "rec:" prefix keeps record sources distinct from the
    // interface sources used by satisfies_interface_interface().
    const std::pair<std::string, std::string> key{"rec:" + std::string{record_name},
                                                  std::string{iface_name}};
    if (!interface_satisfaction_in_progress_.insert(key).second) {
        return true;
    }
    const ScopeGuard cycle_guard{[&] { interface_satisfaction_in_progress_.erase(key); }};

    const auto& record = *rec_it->second;
    const auto& iface = *iface_it->second;

    return check_structural_satisfaction(record.type_params, source_type_args, record.fields,
                                         iface.type_params, target_type_args, iface.fields,
                                         "rec:", "iface:");
}

bool TypeChecker::satisfies_interface_interface(std::string_view source_iface_name,
                                                std::string_view target_iface_name,
                                                const std::vector<TypeInfo>& source_type_args,
                                                const std::vector<TypeInfo>& target_type_args) {
    const auto src_it = interfaces_.find(source_iface_name);
    const auto tgt_it = interfaces_.find(target_iface_name);

    if (src_it == interfaces_.end() || tgt_it == interfaces_.end()) {
        return false;
    }

    // Same coinductive cycle break as satisfies_interface(), for mutually- or
    // self-recursive interfaces.  The "iface:" prefix keeps interface sources
    // distinct from record sources in the shared in-progress set.
    const std::pair<std::string, std::string> key{"iface:" + std::string{source_iface_name},
                                                  std::string{target_iface_name}};
    if (!interface_satisfaction_in_progress_.insert(key).second) {
        return true;
    }
    const ScopeGuard cycle_guard{[&] { interface_satisfaction_in_progress_.erase(key); }};

    const auto& source_iface = *src_it->second;
    const auto& target_iface = *tgt_it->second;

    return check_structural_satisfaction(source_iface.type_params, source_type_args,
                                         source_iface.fields, target_iface.type_params,
                                         target_type_args, target_iface.fields, "src:", "tgt:");
}

} // namespace luma
