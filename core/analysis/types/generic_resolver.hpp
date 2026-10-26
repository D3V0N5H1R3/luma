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

#ifndef LUMA_TYPES_GENERIC_RESOLVER_HPP
#define LUMA_TYPES_GENERIC_RESOLVER_HPP

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
struct CallExpression;
struct FunctionDeclaration;

namespace type_check_helpers {
struct CallArgumentMapping;
} // namespace type_check_helpers

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

    // A named argument's name paired with its inferred type.
    using NamedArgumentType = std::pair<std::string, TypeInfo>;

    // Infer and return the concrete return type for a call to the generic
    // function `func` (named `fn_name` at the call site).  `arg_types` and
    // `named_arg_types` are the types of `expr`'s positional and named
    // arguments, already inferred by the caller; a value piped into the call
    // (context().pipe_left_type) also takes part in the inference.  Reports
    // arity, argument type, ownership, and type-parameter bound errors.
    [[nodiscard]] TypeInfo infer_call(const FunctionDeclaration& func, std::string_view fn_name,
                                      const CallExpression& expr,
                                      const std::vector<TypeInfo>& arg_types,
                                      const std::vector<NamedArgumentType>& named_arg_types);

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
    // Bind explicit turbofish type arguments, then infer the remaining type
    // parameters from the piped value and from every positional and named
    // argument mapped to a parameter.
    void infer_from_arguments(const FunctionDeclaration& func, const CallExpression& expr,
                              const type_check_helpers::CallArgumentMapping& mapping,
                              const std::vector<TypeInfo>& arg_types,
                              const std::vector<NamedArgumentType>& named_arg_types);

    // Check one argument against the resolved type of parameter `param_index`
    // (reporting a mismatch together with the inferred type-parameter
    // bindings) and check its unique/borrow ownership.  `arg_label` names the
    // argument in the diagnostic, e.g. "argument 2" or "named argument 'x'".
    void check_argument(const FunctionDeclaration& func, std::string_view fn_name,
                        std::size_t param_index, const Expression& arg, const TypeInfo& arg_type,
                        std::string_view arg_label);

    TypeCheckingServices* services_;
    StringMap<TypeInfo> bindings_;
    std::vector<std::pair<std::string, std::optional<TypeInfo>>> saved_bindings_;
    StringMap<std::vector<TypeParam>> alias_params_;
};

} // namespace luma

#endif // LUMA_TYPES_GENERIC_RESOLVER_HPP
