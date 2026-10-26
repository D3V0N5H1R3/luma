#include "analysis/resolver/resolver.hpp"

#include <format>
#include <stdexcept>
#include <type_traits>

#include "analysis/ast/ast_dispatch.hpp"
#include "analysis/ast/declaration.hpp"
#include "common/resource_limits.hpp"
#include "common/scope_guard.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// ─────────── ResolveScope ───────────

std::uint16_t ResolveScope::define(std::string_view name, bool is_mutable) {
    if (next_slot_ == k_max_slot_index) [[unlikely]] {
        throw std::runtime_error{
            std::format("too many local variables in scope (maximum {})", k_max_slot_index)};
    }

    const ResolvedVar var{.slot_index = next_slot_++, .is_mutable = is_mutable};

    const auto it = variables_.insert_or_assign(std::string{name}, var).first;
    locals_.emplace(it->first);

    return var.slot_index;
}

bool ResolveScope::define_if_absent(std::string_view name, bool is_mutable) {
    if (lookup(name)) {
        return false;
    }

    (void)define(name, is_mutable);
    return true;
}

// Cache the resolved variable for future lookups in this scope.
// variables_ is mutable because lookup() is logically const — it
// doesn't change the scope's meaning, only memoises the resolution.
std::optional<ResolvedVar> ResolveScope::lookup(std::string_view name) const {
    auto it = variables_.find(name);

    if (it != variables_.end()) {
        return it->second;
    }

    if (parent_) {
        // NOTE: emplace() during lookup is safe because we never hold iterators across the call.
        auto found = parent_->lookup(name);

        if (found) {
            // Adjust frame_depth for the depth difference.
            // The caller uses this to decide local vs upvalue.
            auto adjusted = *found;
            adjusted.frame_depth = static_cast<std::uint16_t>(adjusted.frame_depth + 1);
            variables_.emplace(std::string{name}, adjusted);
            return adjusted;
        }
    }

    return std::nullopt;
}

bool ResolveScope::has_local(std::string_view name) const {
    return locals_.contains(name);
}

// ─────────── NameResolver ───────────

std::vector<Diagnostic> NameResolver::resolve(Program& program) {
    clear_diagnostics();

    // Create the global scope.
    current_scope_ = std::make_shared<ResolveScope>();

    // First pass: register all top-level declarations so that forward
    // references resolve correctly.
    for (const auto& decl : program.declarations) {
        dispatch_declaration(*decl, [this](const auto& node) {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, FunctionDeclaration> ||
                          std::is_same_v<T, RecordDeclaration>) {
                register_named_declaration(node.name);
            } else if constexpr (std::is_same_v<T, ChoiceDeclaration>) {
                register_choice(node);
            } else if constexpr (std::is_same_v<T, NamespaceDeclaration>) {
                register_namespace(node);
            }
            // Include, Interface, TypeAlias, Use are handled elsewhere or
            // do not introduce names in this pass.
        });
    }

    // Process `use` declarations: register bare names for namespace members.
    for (const auto& decl : program.declarations) {
        dispatch_declaration(*decl, [this, &program](const auto& node) {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, UseDeclaration>) {
                resolve_use_declaration(node, program);
            }
        });
    }

    // Second pass: resolve all references.
    for (const auto& decl : program.declarations) {
        resolve_declaration(*decl);
    }

    for (const auto& stmt : program.statements) {
        resolve_statement(*stmt);
    }

    return take_diagnostics();
}

void NameResolver::push_scope() {
    current_scope_ = std::make_shared<ResolveScope>(current_scope_);
}

void NameResolver::pop_scope() {
    if (current_scope_) {
        current_scope_ = current_scope_->parent();
    }
}

// ─────────── Shared helpers ───────────

void NameResolver::resolve_body(const std::vector<std::unique_ptr<Statement>>& body) {
    for (const auto& stmt : body) {
        resolve_statement(*stmt);
    }
}

void NameResolver::resolve_scoped_body(const std::vector<std::unique_ptr<Statement>>& body) {
    auto guard = make_scope_guard();
    resolve_body(body);
}

void NameResolver::resolve_parameters(const std::vector<Parameter>& params) {
    for (const auto& param : params) {
        (void)current_scope_->define(param.name, param.is_mutable);

        if (param.default_value) {
            resolve_expression(*param.default_value);
        }
    }
}

void NameResolver::resolve_match_arm(const MatchArm& arm) {
    auto guard = make_scope_guard();

    if (arm.comparison_value()) {
        resolve_expression(*arm.comparison_value());
    }

    for (const auto& alt : arm.alternatives) {
        if (alt.comparison_value()) {
            resolve_expression(*alt.comparison_value());
        }
    }

    if (arm.has_binding()) {
        (void)current_scope_->define(arm.binding_name(), false);
    }

    for (const auto& binding : arm.choice_bindings()) {
        if (!binding.empty() && binding != "_") {
            (void)current_scope_->define(binding, false);
        }
    }

    if (arm.guard) {
        resolve_expression(*arm.guard);
    }

    resolve_body(arm.body);

    if (arm.body_expr) {
        resolve_expression(*arm.body_expr);
    }
}

// ─────────── First-pass registration helpers ───────────

void NameResolver::register_named_declaration(std::string_view name) {
    (void)current_scope_->define(name, false);
}

void NameResolver::register_choice(const ChoiceDeclaration& choice) {
    for (const auto& variant : choice.variants) {
        (void)current_scope_->define(make_qualified(choice.name, variant.name), false);
    }
}

void NameResolver::define_namespace_member(const Declaration& member,
                                           std::string_view namespace_name, bool absent_only) {
    const auto define_name = [this, absent_only](std::string_view name) {
        if (absent_only) {
            (void)current_scope_->define_if_absent(name, false);
        } else {
            (void)current_scope_->define(name, false);
        }
    };

    dispatch_declaration(member, [&](const auto& node) {
        using M = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<M, FunctionDeclaration> ||
                      std::is_same_v<M, RecordDeclaration>) {
            define_name(make_qualified(namespace_name, node.name));
        } else if constexpr (std::is_same_v<M, ChoiceDeclaration>) {
            for (const auto& variant : node.variants) {
                const auto qualified_variant = make_qualified(node.name, variant.name);
                define_name(make_qualified(namespace_name, qualified_variant));
            }
        }
        // Other declaration kinds inside namespaces are ignored.
    });
}

void NameResolver::register_namespace(const NamespaceDeclaration& ns) {
    for (const auto& inner : ns.declarations) {
        define_namespace_member(*inner, ns.name, /*absent_only=*/false);
    }
}

// ─────────── Use-declaration processing ───────────

const NamespaceDeclaration* NameResolver::find_namespace(const Program& program,
                                                         std::string_view name) {
    for (const auto& decl : program.declarations) {
        if (decl->kind != DeclarationKind::Namespace) {
            continue;
        }

        const auto& ns = static_cast<const NamespaceDeclaration&>(*decl);

        if (ns.name == name) {
            return &ns;
        }
    }

    return nullptr;
}

void NameResolver::resolve_use_declaration(const UseDeclaration& use_decl, const Program& program) {
    const auto& path = use_decl.namespace_path;

    // A wildcard import is just the namespace name; a specific import is
    // "Namespace.member".
    const auto split = split_module(path);

    if (!split) {
        if (const auto* ns = find_namespace(program, path)) {
            resolve_wildcard_import(*ns);
        }

        return;
    }

    const std::string ns_name{split->first};
    const std::string member_name{split->second};

    if (const auto* ns = find_namespace(program, ns_name)) {
        resolve_specific_import(*ns, member_name);
    }
}

// Wildcard import (`use Namespace`): bring every public member into scope.
void NameResolver::resolve_wildcard_import(const NamespaceDeclaration& ns) {
    for (const auto& inner : ns.declarations) {
        if (inner->is_internal_to_namespace) {
            continue;
        }

        define_namespace_member(*inner, /*namespace_name=*/"", /*absent_only=*/true);
    }
}

// Specific import (`use Namespace.member`): bring one member's bare name into
// scope, or — when the member is a choice type — its variants (Choice.Variant).
void NameResolver::resolve_specific_import(const NamespaceDeclaration& ns,
                                           std::string_view member_name) {
    for (const auto& inner : ns.declarations) {
        if (inner->kind != DeclarationKind::Choice) {
            continue;
        }

        const auto& choice = static_cast<const ChoiceDeclaration&>(*inner);

        if (choice.name == member_name) {
            define_namespace_member(*inner, /*namespace_name=*/"", /*absent_only=*/true);
            return;
        }
    }

    (void)current_scope_->define_if_absent(member_name, false);
}

// ─────────── Declaration resolution ───────────

void NameResolver::resolve_declaration(const Declaration& decl) {
    dispatch_declaration(decl, [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, FunctionDeclaration>) {
            auto guard = make_scope_guard();
            resolve_parameters(node.parameters);
            resolve_body(node.body);
        } else if constexpr (std::is_same_v<T, NamespaceDeclaration>) {
            for (const auto& inner : node.declarations) {
                resolve_declaration(*inner);
            }
        }
        // Record, Choice, Interface, TypeAlias, Include, Use need no further resolution.
    });
}

void NameResolver::resolve_statement(const Statement& stmt) {
    dispatch_statement(stmt, [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, VariableDeclStatement>) {
            if (node.initializer) {
                resolve_expression(*node.initializer);
            }

            (void)current_scope_->define(node.name, node.is_mutable);
        } else if constexpr (std::is_same_v<T, AssignmentStatement> ||
                             std::is_same_v<T, CompoundAssignmentStatement>) {
            resolve_expression(*node.target);
            resolve_expression(*node.value);
        } else if constexpr (std::is_same_v<T, IncrementStatement> ||
                             std::is_same_v<T, DecrementStatement>) {
            resolve_expression(*node.target);
        } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
            resolve_expression(*node.expression);
        } else if constexpr (std::is_same_v<T, ReturnStatement>) {
            if (node.value) {
                resolve_expression(*node.value);
            }
        } else if constexpr (std::is_same_v<T, ForStatement>) {
            resolve_for_statement(node);
        } else if constexpr (std::is_same_v<T, IfStatement>) {
            resolve_if_statement(node);
        } else if constexpr (std::is_same_v<T, WhileStatement>) {
            resolve_expression(*node.condition);
            resolve_scoped_body(node.body);
        } else if constexpr (std::is_same_v<T, MatchStatement>) {
            resolve_match_statement(node);
        } else if constexpr (std::is_same_v<T, TryStatement>) {
            resolve_try_statement(node);
        } else if constexpr (std::is_same_v<T, TupleDestructuringStatement>) {
            resolve_expression(*node.initializer);

            for (const auto& [type, name] : node.bindings) {
                (void)current_scope_->define(name, node.is_mutable);
            }
        } else if constexpr (std::is_same_v<T, RecordDestructuringStatement>) {
            resolve_expression(*node.initializer);

            for (const auto& name : node.fields) {
                (void)current_scope_->define(name, node.is_mutable);
            }
        } else if constexpr (std::is_same_v<T, BlockStatement>) {
            resolve_scoped_body(node.statements);
        }
        // Break and Continue introduce no names and reference none.
    });
}

void NameResolver::resolve_expression(const Expression& expr) {
    // Guard against native stack overflow on pathologically deep expression ASTs
    // (e.g. a very long flat `a + b + c + ...` chain the parser builds
    // iteratively).  The parser already caps nesting; resolution annotates the
    // AST rather than reporting, so it simply stops descending here.
    if (++expression_depth_ > ResourceLimits::max_expression_depth) {
        --expression_depth_;
        return;
    }

    const ScopeGuard guard{[this] { --expression_depth_; }};

    dispatch_expression(expr, [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, BinaryExpression> || std::is_same_v<T, PipeExpression> ||
                      std::is_same_v<T, ErrorPipeExpression>) {
            resolve_expression(*node.left);
            resolve_expression(*node.right);
        } else if constexpr (std::is_same_v<T, UnaryExpression> ||
                             std::is_same_v<T, DowncastExpression> ||
                             std::is_same_v<T, IsExpression> ||
                             std::is_same_v<T, AwaitExpression>) {
            resolve_expression(*node.operand);
        } else if constexpr (std::is_same_v<T, CallExpression>) {
            resolve_expression(*node.callee);

            for (const auto& arg : node.arguments) {
                resolve_expression(*arg);
            }

            for (const auto& named : node.named_arguments) {
                resolve_expression(*named.value);
            }
        } else if constexpr (std::is_same_v<T, FieldAccessExpression>) {
            resolve_expression(*node.object);
        } else if constexpr (std::is_same_v<T, IndexAccessExpression>) {
            resolve_expression(*node.object);
            resolve_expression(*node.index);
        } else if constexpr (std::is_same_v<T, LambdaExpression>) {
            resolve_lambda_expression(node);
        } else if constexpr (std::is_same_v<T, IfExpression>) {
            resolve_if_expression(node);
        } else if constexpr (std::is_same_v<T, MatchExpression>) {
            resolve_match_expression(node);
        } else if constexpr (std::is_same_v<T, RecordCreationExpression>) {
            for (const auto& field : node.fields) {
                resolve_expression(*field.value);
            }
        } else if constexpr (std::is_same_v<T, RecordWithExpression>) {
            resolve_expression(*node.base);

            for (const auto& field : node.overrides) {
                resolve_expression(*field.value);
            }
        } else if constexpr (std::is_same_v<T, ArrayLiteralExpression> ||
                             std::is_same_v<T, TupleLiteralExpression>) {
            for (const auto& elem : node.elements) {
                resolve_expression(*elem);
            }
        } else if constexpr (std::is_same_v<T, DictionaryLiteralExpression>) {
            for (const auto& entry : node.entries) {
                resolve_expression(*entry.key);
                resolve_expression(*entry.value);
            }
        } else if constexpr (std::is_same_v<T, StringInterpolationExpression>) {
            for (const auto& expr : node.expressions) {
                resolve_expression(*expr);
            }
        } else if constexpr (std::is_same_v<T, SuccessExpression> ||
                             std::is_same_v<T, SomeExpression>) {
            resolve_expression(*node.value);
        } else if constexpr (std::is_same_v<T, FailureExpression>) {
            resolve_expression(*node.message);
        } else if constexpr (std::is_same_v<T, RangeExpression>) {
            resolve_expression(*node.start);
            resolve_expression(*node.end);
        } else if constexpr (std::is_same_v<T, SpawnExpression>) {
            resolve_expression(*node.call);
        } else if constexpr (std::is_same_v<T, TaskScopeExpression>) {
            resolve_scoped_body(node.body);
        }
        // Variable and Literal carry no nested names to resolve.
    });
}

// ─────────── resolve_statement helpers ───────────

void NameResolver::resolve_for_statement(const ForStatement& for_stmt) {
    resolve_expression(*for_stmt.iterable);

    auto guard = make_scope_guard();

    if (!for_stmt.destructure_variables.empty()) {
        for (const auto& var : for_stmt.destructure_variables) {
            (void)current_scope_->define(var, false);
        }
    } else {
        (void)current_scope_->define(for_stmt.loop_variable, false);
    }

    if (!for_stmt.index_variable.empty()) {
        (void)current_scope_->define(for_stmt.index_variable, false);
    }

    resolve_body(for_stmt.body);
}

void NameResolver::resolve_if_statement(const IfStatement& if_stmt) {
    resolve_expression(*if_stmt.condition);

    resolve_scoped_body(if_stmt.then_body);

    if (!if_stmt.else_body.empty()) {
        resolve_scoped_body(if_stmt.else_body);
    }
}

void NameResolver::resolve_match_statement(const MatchStatement& match_stmt) {
    resolve_expression(*match_stmt.subject);

    for (const auto& arm : match_stmt.arms) {
        resolve_match_arm(arm);
    }
}

void NameResolver::resolve_try_statement(const TryStatement& try_stmt) {
    resolve_scoped_body(try_stmt.try_body);

    {
        auto guard = make_scope_guard();

        if (!try_stmt.catch_var.empty()) {
            (void)current_scope_->define(try_stmt.catch_var, false);
        }

        resolve_body(try_stmt.catch_body);
    }

    if (!try_stmt.finally_body.empty()) {
        resolve_scoped_body(try_stmt.finally_body);
    }
}

// ─────────── resolve_expression helpers ───────────

void NameResolver::resolve_lambda_expression(const LambdaExpression& lambda) {
    auto guard = make_scope_guard();

    resolve_parameters(lambda.parameters);

    if (lambda.is_expression_body() && (lambda.expression_body() != nullptr)) {
        resolve_expression(*lambda.expression_body());
    } else {
        resolve_body(lambda.statements());
    }
}

void NameResolver::resolve_if_expression(const IfExpression& if_expr) {
    resolve_expression(*if_expr.condition);

    {
        auto guard = make_scope_guard();

        if (if_expr.then_expr() != nullptr) {
            resolve_expression(*if_expr.then_expr());
        }

        resolve_body(if_expr.then_body());
    }

    if (!if_expr.else_body().empty() || (if_expr.else_expr() != nullptr)) {
        auto guard = make_scope_guard();

        if (if_expr.else_expr() != nullptr) {
            resolve_expression(*if_expr.else_expr());
        }

        resolve_body(if_expr.else_body());
    }
}

void NameResolver::resolve_match_expression(const MatchExpression& match_expr) {
    resolve_expression(*match_expr.subject);

    for (const auto& arm : match_expr.arms) {
        resolve_match_arm(arm);
    }
}

} // namespace luma
