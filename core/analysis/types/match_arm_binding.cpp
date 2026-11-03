#include "analysis/types/match_arm_binding.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/match_pattern.hpp"
#include "analysis/types/generic_resolver.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/string_hash.hpp"
#include "common/string_utils.hpp"

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
                               : TypeInfo::make(TypeInfo::Kind::StdlibAny));
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

namespace {

// The choice declaration a choice/variant pattern names.  The pattern's type
// name is the choice's canonical name — qualified for a namespaced choice
// (e.g. "Json.Value") — so the direct lookup resolves it unambiguously, even
// when another choice shares its bare name.  A bare name still occurs for a
// `use`-imported choice matched via a two-part pattern; fall back to a
// declaration whose bare name matches, preferring one that declares the
// pattern's variant (which disambiguates a bare-name clash).
[[nodiscard]] const ChoiceDeclaration* find_pattern_choice(TypeCheckingServices& tc,
                                                           const MatchPattern& pattern) {
    if (const auto it = tc.choices().find(pattern.enum_type()); it != tc.choices().end()) {
        return it->second;
    }

    const ChoiceDeclaration* bare_match{nullptr};

    for (const auto& [key, decl] : tc.choices()) {
        if (decl->name != pattern.enum_type()) {
            continue;
        }

        if (std::ranges::any_of(decl->variants, [&](const ChoiceVariant& v) {
                return v.name == pattern.enum_variant();
            })) {
            return decl;
        }

        bare_match = decl;
    }

    return bare_match;
}

// The choice declaration a choice-typed subject names (its canonical name, or
// — for a `use`-imported choice — a bare name that only one declaration has).
[[nodiscard]] const ChoiceDeclaration* find_subject_choice(TypeCheckingServices& tc,
                                                           const TypeInfo& subject_type) {
    if (const auto it = tc.choices().find(subject_type.name); it != tc.choices().end()) {
        return it->second;
    }

    const ChoiceDeclaration* found{nullptr};

    for (const auto& [key, decl] : tc.choices()) {
        if (decl->name != subject_type.name) {
            continue;
        }

        if (found != nullptr && found != decl) {
            return nullptr; // ambiguous bare name — do not guess
        }

        found = decl;
    }

    return found;
}

// Does a pattern's type name denote the given choice declaration?  A live map
// key is compared by declaration identity (keeping two choices that share a
// bare name apart); any other name — e.g. the bare name of a namespaced choice
// used inside its namespace — by the declaration's bare name.
[[nodiscard]] bool pattern_names_choice(TypeCheckingServices& tc, std::string_view enum_type,
                                        const ChoiceDeclaration& choice) {
    if (const auto it = tc.choices().find(enum_type); it != tc.choices().end()) {
        return it->second == &choice;
    }

    return enum_type == choice.name;
}

[[nodiscard]] bool declares_variant(const ChoiceDeclaration& choice, std::string_view variant) {
    return std::ranges::any_of(choice.variants,
                               [&](const ChoiceVariant& v) { return v.name == variant; });
}

void report_unknown_variant(TypeCheckingServices& tc, const ChoiceDeclaration& choice,
                            std::string_view choice_name, std::string_view variant,
                            const SourceLocation& location) {
    std::vector<std::string_view> names;
    names.reserve(choice.variants.size());

    for (const auto& v : choice.variants) {
        names.emplace_back(v.name);
    }

    tc.error(std::format("choice '{}' has no variant '{}'", choice_name, variant), location,
             suggest_name(names, variant), DiagnosticCode::UndefinedField);
}

} // namespace

void bind_choice_fields(TypeCheckingServices& tc, const MatchArm& arm, const TypeInfo& subject_type,
                        const SourceLocation& location) {
    if (arm.kind() != MatchArm::Kind::ChoiceCase || !arm.has_choice_bindings()) {
        return;
    }

    const ChoiceDeclaration* choice_decl_ptr = find_pattern_choice(tc, arm);

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
        // check_arm_patterns() reports the unknown variant.
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

void bind_record_fields(TypeCheckingServices& tc, std::string_view record_type,
                        const std::vector<std::string>& fields, const TypeInfo& subject_type,
                        const SourceLocation& location, bool is_mutable) {
    const RecordDeclaration* record = tc.find_record(record_type);

    if (record == nullptr) {
        tc.error(std::format("unknown record type '{}'", record_type), location,
                 "record destructuring requires a declared record type");

        // Still bind the names so downstream references do not cascade errors.
        for (const auto& field : fields) {
            tc.context().current_scope->define(field, TypeInfo::make(TypeInfo::Kind::StdlibAny),
                                               {.is_mutable = is_mutable}, location);
        }

        return;
    }

    // Validate that the value/subject being destructured is an instance of this
    // record type.  This guards both the binding form (`Point { x } = expr`) and
    // the match form (`case Point { x }`): the match exhaustiveness checker
    // treats an unguarded record arm as irrefutable and the compiler emits a
    // refutable `IsType`, so a mismatched pattern type would otherwise
    // type-check yet never match at runtime.  A permissive/unknown subject is
    // left alone.
    if (subject_type.kind != TypeInfo::Kind::Unknown &&
        subject_type.kind != TypeInfo::Kind::StdlibAny) {
        const bool matches =
            subject_type.kind == TypeInfo::Kind::Record &&
            (subject_type.name == record_type || subject_type.name == record->name ||
             tc.find_record(subject_type.name) == record);

        if (!matches) {
            tc.error(std::format("record destructuring expects '{}' but got '{}'", record_type,
                                 subject_type.to_string()),
                     location, "the value must be an instance of the record type");
        }
    }

    // Resolve the record's generic type params from the subject's concrete type
    // arguments (when the subject is that record type); a non-generic record or
    // an unknown subject makes this a harmless no-op.  The guard restores prior
    // bindings on every exit path.
    const auto binding_guard =
        type_check_helpers::bind_type_params(tc, record->type_params, subject_type.inner_types);

    StringSet seen;

    for (const auto& field_name : fields) {
        if (!seen.insert(field_name).second) {
            tc.error(std::format("duplicate field '{}' in record destructuring", field_name),
                     location, "each field may be bound at most once");

            continue;
        }

        const auto field_it = std::ranges::find(record->fields, field_name, &RecordField::name);

        if (field_it == record->fields.end()) {
            tc.error(std::format("record '{}' has no field '{}'", record_type, field_name),
                     location, "check the record definition for available fields");

            tc.context().current_scope->define(field_name,
                                               TypeInfo::make(TypeInfo::Kind::StdlibAny),
                                               {.is_mutable = is_mutable}, location);

            continue;
        }

        const auto field_type = tc.resolve_type(field_it->type);

        // `P { x } = p` at a REPL input's top level redefines the global `x`
        // like `integer x = …` does (a no-op anywhere else, e.g. in a match arm).
        type_check_helpers::check_repl_redefinition(tc, field_name, field_type, location);
        tc.context().current_scope->define(field_name, field_type, {.is_mutable = is_mutable},
                                           location);
    }
}

void merge_branch_ownership(TypeCheckingServices& tc, const TypeScope::OwnershipSnapshot& before,
                            const std::vector<TypeScope::OwnershipSnapshot>& branch_snapshots) {
    // No branch falls through: the code after the if/match is unreachable, so
    // its ownership state is irrelevant — keep the pre-branch state.
    if (branch_snapshots.empty()) {
        tc.context().current_scope->restore_ownership(before);
        return;
    }

    // Conservatively prevent use-after-move across branches: a unique variable
    // consumed on any path is consumed afterwards.  A branch that left a
    // variable untouched keeps its pre-branch state.
    for (const auto& entry : before) {
        // Plain locals rather than a structured binding captured by the lambda
        // below: clang-analyzer mis-models a captured structured-binding
        // reference as an undefined pointer (a core.NullDereference false
        // positive).
        const auto& name = entry.first;
        const bool was_consumed_before = entry.second;

        const auto consumed_after = [&](const TypeScope::OwnershipSnapshot& snapshot) {
            const auto found = std::ranges::find(snapshot, name, &TypeScope::OwnershipEntry::first);
            return found != snapshot.end() ? found->second : was_consumed_before;
        };

        tc.context().current_scope->mark_consumed(
            name, std::ranges::any_of(branch_snapshots, consumed_after));
    }
}

void check_arm_patterns(TypeCheckingServices& tc, const MatchArm& arm, const TypeInfo& subject_type,
                        const SourceLocation& match_location) {
    const bool is_dynamic_subject = subject_type.kind == TypeInfo::Kind::StdlibAny ||
                                    subject_type.kind == TypeInfo::Kind::Unknown;
    const auto& arm_location = arm.location.value_or(match_location);

    // Reports a pattern that can never match a subject of this type.  An
    // interface-typed subject is left alone: its dynamic value is not known.
    const auto require_subject = [&](bool fits, std::string_view pattern_text,
                                     std::string_view expected) {
        if (fits || subject_type.kind == TypeInfo::Kind::Interface) {
            return;
        }

        tc.error(std::format("match arm has {} pattern but subject is '{}'", pattern_text,
                             subject_type.to_string()),
                 arm_location, std::format("{} patterns can only match {}", pattern_text, expected),
                 DiagnosticCode::TypeMismatch);
    };

    // A choice/variant pattern must name a variant of the subject's own
    // choice: the runtime test compares only the variant name, so
    // `case Badge.Circle(label)` on a `Shape` would match `Shape.Circle` and
    // bind its fields under Badge's field types.
    const auto check_choice_pattern = [&](const MatchPattern& pattern) {
        const auto pattern_text =
            std::format("'{}.{}'", pattern.enum_type(), pattern.enum_variant());

        if (is_dynamic_subject || subject_type.kind == TypeInfo::Kind::Interface) {
            if (const auto* named = find_pattern_choice(tc, pattern);
                named != nullptr && !declares_variant(*named, pattern.enum_variant())) {
                report_unknown_variant(tc, *named, pattern.enum_type(), pattern.enum_variant(),
                                       arm_location);
            }

            return;
        }

        if (subject_type.kind != TypeInfo::Kind::Choice) {
            require_subject(false, std::format("choice {}", pattern_text), "values of that choice");
            return;
        }

        const auto* subject_choice = find_subject_choice(tc, subject_type);

        if (subject_choice == nullptr) {
            return;
        }

        if (!pattern_names_choice(tc, pattern.enum_type(), *subject_choice)) {
            tc.error(std::format("match arm pattern {} is not a variant of the subject's choice "
                                 "type '{}'",
                                 pattern_text, subject_type.to_string()),
                     arm_location,
                     std::format("match a '{}' value against its own variants, e.g. 'case "
                                 "{}.{}'",
                                 subject_type.to_string(), subject_type.name,
                                 subject_choice->variants.empty()
                                     ? std::string{"Variant"}
                                     : subject_choice->variants.front().name),
                     DiagnosticCode::TypeMismatch);
            return;
        }

        if (!declares_variant(*subject_choice, pattern.enum_variant())) {
            report_unknown_variant(tc, *subject_choice, pattern.enum_type(), pattern.enum_variant(),
                                   arm_location);
        }
    };

    // Checks one pattern of the arm — the arm itself or a `case A | B`
    // alternative.  Integer, string, and range patterns carry their value
    // in the pattern itself; only comparison patterns hold an expression.
    const auto check_pattern = [&](const MatchPattern& pattern) {
        std::optional<TypeInfo> value_type;

        if (pattern.comparison_value()) {
            value_type = tc.infer_expression_type(*pattern.comparison_value());
        }

        if (pattern.kind() == MatchArm::Kind::ChoiceCase ||
            pattern.kind() == MatchArm::Kind::VariantCase) {
            check_choice_pattern(pattern);
            return;
        }

        if (is_dynamic_subject) {
            return;
        }

        switch (pattern.kind()) {
            case MatchArm::Kind::IntegerRangeCase:
                if (subject_type.kind != TypeInfo::Kind::Integer) {
                    tc.error(std::format("match arm has range case but subject is '{}'",
                                         subject_type.to_string()),
                             pattern.range_location(),
                             "range case arms can only match integer values");
                }
                break;

            case MatchArm::Kind::IntegerCase:
                if (subject_type.kind != TypeInfo::Kind::Integer) {
                    tc.error(std::format("match arm has integer case but subject is '{}'",
                                         subject_type.to_string()),
                             arm_location, "integer case arms can only match integer values");
                }
                break;

            case MatchArm::Kind::StringCase:
                if (subject_type.kind != TypeInfo::Kind::String) {
                    tc.error(std::format("match arm has string case but subject is '{}'",
                                         subject_type.to_string()),
                             arm_location, "string case arms can only match string values");
                }
                break;

            case MatchArm::Kind::BooleanCase:
                require_subject(subject_type.kind == TypeInfo::Kind::Boolean,
                                pattern.boolean_value() ? "'true'" : "'false'", "boolean values");
                break;

            case MatchArm::Kind::SomeCase:
                require_subject(subject_type.kind == TypeInfo::Kind::Optional, "'some'",
                                "optional values");
                break;

            case MatchArm::Kind::NoneCase:
                require_subject(subject_type.kind == TypeInfo::Kind::Optional ||
                                    subject_type.kind == TypeInfo::Kind::None,
                                "'none'", "optional values");
                break;

            case MatchArm::Kind::SuccessResult:
                require_subject(subject_type.kind == TypeInfo::Kind::Result, "'success'",
                                "result values");
                break;

            case MatchArm::Kind::FailureResult:
                require_subject(subject_type.kind == TypeInfo::Kind::Result, "'failure'",
                                "result values");
                break;

            case MatchArm::Kind::Comparison:
                if (value_type && !tc.is_assignable(subject_type, *value_type) &&
                    !tc.is_assignable(*value_type, subject_type)) {
                    tc.error(std::format("match comparison type '{}' is not compatible with "
                                         "subject type '{}'",
                                         value_type->to_string(), subject_type.to_string()),
                             pattern.comparison_value()->location,
                             "ensure the case value type matches the match subject type");
                }
                break;

            default:
                break;
        }
    };

    check_pattern(arm);

    for (const auto& alternative : arm.alternatives) {
        check_pattern(alternative);
    }
}

} // namespace luma::match_arm_binding
