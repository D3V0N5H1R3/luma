#include "analysis/types/match_arm_binding.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/match_pattern.hpp"
#include "analysis/types/generic_resolver.hpp"
#include "analysis/types/type_checking_context.hpp"

namespace luma::match_arm_binding {

void bind_arm_names(TypeCheckingServices& tc, const MatchArm& arm, const TypeInfo& subject_type) {
    if (!arm.has_binding()) {
        return;
    }

    // has_binding() guarantees one of the three capture kinds below.  Each binds
    // the capture name to the value/error/element type carried by the subject,
    // falling back to a permissive type when the subject is not concrete.
    const auto define_binding = [&](const TypeInfo& binding_type) {
        tc.context().current_scope->define(arm.binding_name(), binding_type, {});
    };

    switch (arm.kind()) {
        case MatchArm::Kind::SuccessResult:
            define_binding(
                (subject_type.kind == TypeInfo::Kind::Result && !subject_type.inner_types.empty())
                    ? subject_type.result_value_type()
                    : TypeInfo::make(TypeInfo::Kind::StdlibAny));
            break;
        case MatchArm::Kind::FailureResult:
            define_binding((subject_type.kind == TypeInfo::Kind::Result &&
                            subject_type.inner_types.size() >= 2)
                               ? subject_type.result_error_type()
                               : TypeInfo::make(TypeInfo::Kind::String));
            break;
        case MatchArm::Kind::SomeCase:
            define_binding(
                (subject_type.kind == TypeInfo::Kind::Optional && !subject_type.inner_types.empty())
                    ? subject_type.element_type()
                    : TypeInfo::make(TypeInfo::Kind::StdlibAny));
            break;
        default:
            break;
    }
}

void bind_choice_fields(TypeCheckingServices& tc, const MatchArm& arm, const TypeInfo& subject_type,
                        const SourceLocation& location, bool report_unknown_variant) {
    if (arm.kind() != MatchArm::Kind::ChoiceCase || !arm.has_choice_bindings()) {
        return;
    }

    const auto ch_it = tc.choices().find(arm.enum_type());

    // The arm's enum_type is the choice's canonical name — qualified for a
    // namespaced choice (e.g. "Json.Value") — so the direct find above resolves
    // it unambiguously, even when another choice shares its bare name.  A bare
    // name still occurs for a `use`-imported choice matched via a two-part
    // pattern; fall back to a declaration whose bare name matches and that
    // actually declares the arm's variant (the variant check disambiguates any
    // bare-name clash).
    const ChoiceDeclaration* choice_decl_ptr =
        ch_it != tc.choices().end() ? ch_it->second : nullptr;

    if (choice_decl_ptr == nullptr) {
        for (const auto& [key, decl] : tc.choices()) {
            const bool bare_name_matches = decl->name == arm.enum_type();
            const bool declares_variant =
                std::ranges::any_of(decl->variants, [&](const ChoiceVariant& v) {
                    return v.name == arm.enum_variant();
                });

            if (bare_name_matches && declares_variant) {
                choice_decl_ptr = decl;
                break;
            }
        }
    }

    if (choice_decl_ptr == nullptr) {
        return;
    }

    const auto& choice_decl = *choice_decl_ptr;

    // For generic choices, bind the type params for the duration of variant
    // field resolution so recursive field types resolve correctly, without
    // clobbering an outer generic function's type params:
    //   - when the subject carries concrete type args, bind to those (the
    //     ParamGuard save/restore);
    //   - otherwise bind them as unknown placeholders (the BindingScope
    //     push/pop lifecycle).
    // Both guards restore the prior bindings on scope exit.
    const bool is_generic = !choice_decl.type_params.empty();

    std::optional<GenericResolver::ParamGuard> concrete_guard;
    std::optional<GenericResolver::BindingScope> unknown_scope;

    if (is_generic && !subject_type.inner_types.empty()) {
        std::vector<std::string> names;
        names.reserve(choice_decl.type_params.size());
        for (const auto& tp : choice_decl.type_params) {
            names.push_back(tp.name);
        }
        concrete_guard.emplace(tc.generics().bindings(), names, subject_type.inner_types);
    } else if (is_generic) {
        unknown_scope.emplace(tc.generics(), choice_decl.type_params);
    }

    const auto variant_it =
        std::ranges::find(choice_decl.variants, arm.enum_variant(), &ChoiceVariant::name);

    if (variant_it == choice_decl.variants.end()) {
        if (report_unknown_variant) {
            tc.error(
                std::format("choice '{}' has no variant '{}'", arm.enum_type(), arm.enum_variant()),
                location, "check the choice type definition for available variants");
        }

        return;
    }

    const auto& variant = *variant_it;
    const auto& choice_bindings = arm.choice_bindings();

    if (choice_bindings.size() != variant.fields.size()) {
        tc.error(std::format("choice variant '{}.{}' has {} field(s) "
                             "but {} binding(s) provided",
                             arm.enum_type(), arm.enum_variant(), variant.fields.size(),
                             choice_bindings.size()),
                 location, "match the number of bindings to the variant's field count");
    }

    for (std::size_t i{0}; i < choice_bindings.size() && i < variant.fields.size(); ++i) {
        const auto field_type = tc.resolve_type(variant.fields[i].type);

        tc.context().current_scope->define(choice_bindings[i], field_type, {});
    }
}

void merge_arm_ownership(TypeCheckingServices& tc, const TypeScope::OwnershipSnapshot& before,
                         const std::vector<TypeScope::OwnershipSnapshot>& arm_snapshots) {
    // Conservatively prevent use-after-move across arms: if any arm consumed a
    // variable that was still live before the match, mark it consumed here too.
    for (const auto& [name, was_consumed_before] : before) {
        if (was_consumed_before) {
            continue;
        }

        const bool consumed_in_any = std::ranges::any_of(arm_snapshots, [&](const auto& snap) {
            return std::ranges::any_of(
                snap, [&](const auto& entry) { return entry.first == name && entry.second; });
        });

        if (consumed_in_any) {
            tc.context().current_scope->mark_consumed(name, true);
        }
    }
}

} // namespace luma::match_arm_binding
