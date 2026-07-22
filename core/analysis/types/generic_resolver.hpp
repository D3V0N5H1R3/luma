// ─────────────────────────────────────────────────────────────────────────────
// Generic Resolver
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Manage generic type parameter bindings and inference.
//
// Owns the binding state (bindings_, saved_bindings_,
// alias_params_) and provides the core methods for pushing, popping,
// inferring, and validating type parameter bindings during type checking.
//
// Holds a back-reference to TypeCheckingServices for calling resolve_type(),
// infer_expression_type(), error(), is_assignable(), and accessing shared
// state.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis/ast/type_annotation.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/type_info.hpp"
#include "common/string_hash.hpp"

namespace luma {

// Forward declarations
class TypeCheckingServices;
struct TypeParam;
struct Expression;
struct FunctionDeclaration;

// ─────────────────────── Generic Resolver ───────────────────────

class GenericResolver {
public:
    explicit GenericResolver(TypeCheckingServices& services);

    // Push type parameter bindings as unresolved (Unknown).
    void push_params_as_unknown(const std::vector<TypeParam>& params);

    // Push type parameter bindings resolved from provided type arguments.
    void push_params(const std::vector<TypeParam>& params, const std::vector<TypeAnnotation>& args);

    // Pop (restore) type parameter bindings.
    void pop_params(const std::vector<TypeParam>& params);

    // RAII guard that pushes generic bindings on construction and pops on
    // destruction.  Supports both push_params_as_unknown and push_params.
    class BindingScope {
    public:
        // Push type params as unknown (for inference).
        explicit BindingScope(GenericResolver& resolver, const std::vector<TypeParam>& params)
            : resolver_{resolver}, params_{params} {
            resolver_.push_params_as_unknown(params_);
        }

        // Push type params with explicit type arguments.
        explicit BindingScope(GenericResolver& resolver, const std::vector<TypeParam>& params,
                              const std::vector<TypeAnnotation>& args)
            : resolver_{resolver}, params_{params} {
            resolver_.push_params(params_, args);
        }

        // pop_params only performs container lookups/erasures; the sole escape
        // path the analyzer can trace is MSVC STL bad_alloc, which is fatal and
        // cannot be meaningfully handled inside this cleanup destructor.
        ~BindingScope() noexcept { // NOLINT(bugprone-exception-escape)
            resolver_.pop_params(params_);
        }

        BindingScope(const BindingScope&) = delete;
        BindingScope& operator=(const BindingScope&) = delete;

    private:
        GenericResolver& resolver_;
        const std::vector<TypeParam>& params_;
    };

    // Infer type parameter bindings from argument types.
    void infer_param_from_arg(const TypeAnnotation& param_ann, const TypeInfo& arg_type);

    // Infer and return the concrete return type for a generic function call.
    [[nodiscard]] TypeInfo infer_call(const FunctionDeclaration& func,
                                      const std::vector<std::unique_ptr<Expression>>& args,
                                      const std::vector<TypeAnnotation>& explicit_type_args = {},
                                      const SourceLocation& call_location = {});

    // Validate that inferred type parameter bindings satisfy their bounds.
    void validate_bounds(const std::vector<TypeParam>& params, const SourceLocation& location);

    // Reset all state for a fresh check pass.
    void reset();

    // Accessors for the type parameter binding map.
    [[nodiscard]] StringMap<TypeInfo>& bindings();
    [[nodiscard]] const StringMap<TypeInfo>& bindings() const;

    // Accessors for alias type parameter lists.
    [[nodiscard]] StringMap<std::vector<TypeParam>>& alias_params();
    [[nodiscard]] const StringMap<std::vector<TypeParam>>& alias_params() const;

    // RAII guard that saves and restores type-parameter bindings in a
    // StringMap<TypeInfo>.  On construction it records the prior value (or
    // absence) of each named key and overwrites it with the supplied concrete
    // type; on destruction it restores the original state in reverse order so
    // repeated keys unwind to their true original value.
    //
    // Two input shapes are accepted:
    //   * parallel `names` / `args` vectors — used when binding a declaration's
    //     type parameters to a concrete instantiation's type arguments; and
    //   * a combined vector of (name, value) pairs — used when the caller has
    //     already paired names with resolved types (e.g. alias and prefixed
    //     interface-satisfaction bindings).
    //
    // Relationship to BindingScope: BindingScope brackets
    // push_params_as_unknown()/pop_params() on the GenericResolver's own
    // saved-binding stack (the resolver-owned push/pop lifecycle), whereas
    // ParamGuard saves and restores directly in a caller-supplied bindings map
    // for a localised region.
    class ParamGuard {
    public:
        explicit ParamGuard(StringMap<TypeInfo>& bindings, const std::vector<std::string>& names,
                            const std::vector<TypeInfo>& args)
            : bindings_{bindings} {
            const std::size_t count = std::min(names.size(), args.size());
            saved_.reserve(count);
            for (std::size_t i{0}; i < count; ++i) {
                save_and_set(names[i], args[i]);
            }
        }

        explicit ParamGuard(StringMap<TypeInfo>& bindings,
                            const std::vector<std::pair<std::string, TypeInfo>>& entries)
            : bindings_{bindings} {
            saved_.reserve(entries.size());
            for (const auto& [name, value] : entries) {
                save_and_set(name, value);
            }
        }

        ~ParamGuard() noexcept {
            // Restore in reverse order so repeated keys unwind to the value
            // that was live before the guard first overwrote them.
            for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) {
                const auto& saved_value = it->second;
                if (saved_value) {
                    bindings_.insert_or_assign(it->first, *saved_value);
                } else {
                    bindings_.erase(it->first);
                }
            }
        }

        ParamGuard(const ParamGuard&) = delete;
        ParamGuard& operator=(const ParamGuard&) = delete;

    private:
        void save_and_set(const std::string& name, const TypeInfo& value) {
            if (auto it = bindings_.find(name); it != bindings_.end()) {
                saved_.emplace_back(name, it->second);
            } else {
                saved_.emplace_back(name, std::nullopt);
            }
            bindings_.insert_or_assign(name, value);
        }

        StringMap<TypeInfo>& bindings_;
        std::vector<std::pair<std::string, std::optional<TypeInfo>>> saved_;
    };

private:
    // ── infer_call phases ───────────────────────────────────────────────
    // Report an arity mismatch when the effective argument count (including the
    // implicit piped value) falls outside the function's required/maximum range.
    void check_call_arity(const FunctionDeclaration& func, std::size_t arg_count,
                          const SourceLocation& call_location);

    // Bind explicit turbofish type arguments, infer the remaining type
    // parameters from each argument, and apply call-site borrow/unique
    // ownership checks.  Returns the inferred argument types, cached so they are
    // not re-inferred (which could duplicate side effects).
    [[nodiscard]] std::vector<TypeInfo>
    bind_or_infer_params(const FunctionDeclaration& func,
                         const std::vector<std::unique_ptr<Expression>>& args,
                         const std::vector<TypeAnnotation>& explicit_type_args);

    // Emit a detailed diagnostic for each argument whose type is not assignable
    // to its resolved parameter type, listing the inferred type-parameter
    // bindings for context.
    void report_argument_mismatches(const FunctionDeclaration& func,
                                    const std::vector<std::unique_ptr<Expression>>& args,
                                    const std::vector<TypeInfo>& cached_arg_types);

    TypeCheckingServices* services_;
    StringMap<TypeInfo> bindings_;
    std::vector<std::pair<std::string, std::optional<TypeInfo>>> saved_bindings_;
    StringMap<std::vector<TypeParam>> alias_params_;
};

} // namespace luma
