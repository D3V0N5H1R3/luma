#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analysis/ast/ast_dispatch.hpp"
#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/lexer/token_type.hpp"
#include "lsp_analysis_service_impl.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_type_formatter.hpp"
#include "lsp_types.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

using util::annotation_to_string;

// ═══════════════════════════════════════════════════════════
// Symbol collection helpers (anonymous namespace)
// ═══════════════════════════════════════════════════════════

namespace {

// Estimate the last source line of a block of statements (1-based).
// Returns 0 when the vector is empty.
[[nodiscard]] int estimate_block_end(const std::vector<std::unique_ptr<Statement>>& stmts) {
    const auto line_of = [](const std::unique_ptr<Statement>& s) {
        return s ? s->location.line : 0;
    };
    const auto it = std::ranges::max_element(stmts, {}, line_of);
    const int end_line = it != stmts.end() ? line_of(*it) : 0;
    // Heuristic: add 1 line for the closing brace.
    return end_line > 0 ? end_line + 1 : 0;
}

// Strip a `wrapper<...>` generic wrapper from a rendered type string, returning
// the inner type.  `wrapper` must include the trailing '<' (e.g. "result<",
// "optional<", "array<").  Returns nullopt unless `type` is `wrapper` followed
// by a non-empty inner type and a closing '>'.
[[nodiscard]] std::optional<std::string> strip_generic_wrapper(std::string_view type,
                                                               std::string_view wrapper) {
    if (type.size() > wrapper.size() + 1 && type.starts_with(wrapper) && type.back() == '>') {
        return std::string{type.substr(wrapper.size(), type.size() - wrapper.size() - 1)};
    }
    return std::nullopt;
}

// Resolves the binding type for a match arm based on the arm kind and subject type.
[[nodiscard]] std::string resolve_binding_type(MatchArm::Kind kind,
                                               const std::string& subject_type) {
    if (kind == MatchArm::Kind::SuccessResult) {
        if (auto inner = strip_generic_wrapper(subject_type, "result<")) {
            return *inner;
        }
    }
    if (kind == MatchArm::Kind::FailureResult) {
        return "string";
    }
    if (kind == MatchArm::Kind::SomeCase) {
        if (auto inner = strip_generic_wrapper(subject_type, "optional<")) {
            return *inner;
        }
    }
    return std::string{util::k_unknown_type};
}

// ─── Call graph collector ───
//
// Uses dispatch_expression / dispatch_statement from ast_dispatch.hpp so the
// kind→type mapping is maintained in one place. An overloaded functor
// provides per-kind handlers; a catch-all template covers leaf nodes that
// cannot contain calls.
//
// Safety note: All static_cast<const XxxExpression&> downcasts in this file are
// guarded by an immediately preceding check of expr.kind == ExpressionKind::Xxx.
// The AST types do not expose a static_kind member, so a template helper is not
// feasible. Maintainers must not remove kind checks without updating the casts.

struct CallCollector {
    std::unordered_set<std::string>& callees;

    void visit_expr(const Expression& expr);
    void visit_stmts(const std::vector<std::unique_ptr<Statement>>& stmts);

    // ─── Helpers ───

    void visit_optional_expr(const ExpressionPtr& e) {
        if (e) {
            visit_expr(*e);
        }
    }

    void visit_expr_list(const std::vector<ExpressionPtr>& exprs) {
        for (const auto& e : exprs) {
            visit_optional_expr(e);
        }
    }

    void visit_match_arms(const std::vector<MatchArm>& arms) {
        for (const auto& arm : arms) {
            visit_optional_expr(arm.body_expr);
            visit_stmts(arm.body);
        }
    }

    void visit_record_fields(const std::vector<RecordFieldInit>& fields) {
        for (const auto& field : fields) {
            visit_optional_expr(field.value);
        }
    }

    // ─── Expression handlers ───

    // Leaf expressions with no child expressions.
    template <typename T> void operator()(const T& /*unused*/) {}

    void operator()(const CallExpression& call) {
        if (call.callee && call.callee->kind == ExpressionKind::Variable) {
            callees.insert(static_cast<const VariableExpression&>(*call.callee).name);
        } else if (call.callee && call.callee->kind == ExpressionKind::FieldAccess) {
            const auto& fa = static_cast<const FieldAccessExpression&>(*call.callee);
            callees.insert(fa.field_name);
            if (fa.object && fa.object->kind == ExpressionKind::Variable) {
                const auto& obj = static_cast<const VariableExpression&>(*fa.object);
                callees.insert(obj.name + "." + fa.field_name);
            }
        }
        visit_optional_expr(call.callee);
        visit_expr_list(call.arguments);
        for (const auto& na : call.named_arguments) {
            visit_optional_expr(na.value);
        }
    }

    void operator()(const BinaryExpression& bin) {
        visit_optional_expr(bin.left);
        visit_optional_expr(bin.right);
    }

    void operator()(const PipeExpression& pipe) {
        visit_optional_expr(pipe.left);
        visit_optional_expr(pipe.right);
    }

    void operator()(const ErrorPipeExpression& ep) {
        visit_optional_expr(ep.left);
        visit_optional_expr(ep.right);
    }

    void operator()(const UnaryExpression& un) {
        visit_optional_expr(un.operand);
    }

    void operator()(const IfExpression& if_expr) {
        visit_optional_expr(if_expr.condition);

        if (const auto* e = if_expr.then_expr()) {
            visit_expr(*e);
        }

        if (const auto* e = if_expr.else_expr()) {
            visit_expr(*e);
        }
    }

    void operator()(const ArrayLiteralExpression& arr) {
        visit_expr_list(arr.elements);
    }

    void operator()(const TupleLiteralExpression& tup) {
        visit_expr_list(tup.elements);
    }

    void operator()(const DictionaryLiteralExpression& dict) {
        for (const auto& entry : dict.entries) {
            visit_optional_expr(entry.key);
            visit_optional_expr(entry.value);
        }
    }

    void operator()(const MatchExpression& match) {
        visit_optional_expr(match.subject);
        visit_match_arms(match.arms);
    }

    void operator()(const LambdaExpression& lam) {
        if (const auto* e = lam.expression_body()) {
            visit_expr(*e);
        } else {
            visit_stmts(lam.statements());
        }
    }

    void operator()(const RecordCreationExpression& rec) {
        visit_record_fields(rec.fields);
    }

    void operator()(const RecordWithExpression& rw) {
        visit_optional_expr(rw.base);
        visit_record_fields(rw.overrides);
    }

    void operator()(const IndexAccessExpression& idx) {
        visit_optional_expr(idx.object);
        visit_optional_expr(idx.index);
    }

    void operator()(const FieldAccessExpression& fa) {
        visit_optional_expr(fa.object);
    }

    void operator()(const StringInterpolationExpression& si) {
        visit_expr_list(si.expressions);
    }

    void operator()(const DowncastExpression& dc) {
        visit_optional_expr(dc.operand);
    }

    void operator()(const IsExpression& is) {
        visit_optional_expr(is.operand);
    }

    void operator()(const SpawnExpression& sp) {
        visit_optional_expr(sp.call);
    }

    void operator()(const TaskScopeExpression& ts) {
        visit_stmts(ts.body);
    }

    void operator()(const AwaitExpression& aw) {
        visit_optional_expr(aw.operand);
    }

    void operator()(const SuccessExpression& s) {
        visit_optional_expr(s.value);
    }

    void operator()(const SomeExpression& s) {
        visit_optional_expr(s.value);
    }

    void operator()(const FailureExpression& f) {
        visit_optional_expr(f.message);
    }

    void operator()(const RangeExpression& r) {
        visit_optional_expr(r.start);
        visit_optional_expr(r.end);
    }

    // ─── Statement handlers ───

    // Break / Continue — base Statement, no child expressions.
    void operator()(const Statement& /*unused*/) {}

    void operator()(const ExpressionStatement& es) {
        visit_optional_expr(es.expression);
    }

    void operator()(const VariableDeclStatement& vd) {
        visit_optional_expr(vd.initializer);
    }

    void operator()(const AssignmentStatement& as) {
        visit_optional_expr(as.value);
    }

    void operator()(const CompoundAssignmentStatement& ca) {
        visit_optional_expr(ca.target);
        visit_optional_expr(ca.value);
    }

    void operator()(const IncrementStatement& inc) {
        visit_optional_expr(inc.target);
    }

    void operator()(const DecrementStatement& dec) {
        visit_optional_expr(dec.target);
    }

    void operator()(const ReturnStatement& ret) {
        visit_optional_expr(ret.value);
    }

    void operator()(const BlockStatement& bs) {
        visit_stmts(bs.statements);
    }

    void operator()(const IfStatement& ifs) {
        visit_optional_expr(ifs.condition);
        visit_stmts(ifs.then_body);
        visit_stmts(ifs.else_body);
    }

    void operator()(const ForStatement& fs) {
        visit_optional_expr(fs.iterable);
        visit_stmts(fs.body);
    }

    void operator()(const WhileStatement& ws) {
        visit_optional_expr(ws.condition);
        visit_stmts(ws.body);
    }

    void operator()(const TryStatement& ts) {
        visit_stmts(ts.try_body);
        visit_stmts(ts.catch_body);
        visit_stmts(ts.finally_body);
    }

    void operator()(const MatchStatement& ms) {
        visit_optional_expr(ms.subject);
        visit_match_arms(ms.arms);
    }

    void operator()(const TupleDestructuringStatement& td) {
        visit_optional_expr(td.initializer);
    }
};

void CallCollector::visit_expr(const Expression& expr) {
    dispatch_expression(expr, *this);
}

void CallCollector::visit_stmts(const std::vector<std::unique_ptr<Statement>>& stmts) {
    for (const auto& stmt : stmts) {
        if (stmt) {
            dispatch_statement(*stmt, *this);
        }
    }
}

// ─── Shared variable type lookup ───
// Searches function-local scope first, then file-level local variables.
// Returns an empty string when the variable is not found.
[[nodiscard]] std::string lookup_variable_type(const std::string& var_name,
                                               const AnalysisResult& result,
                                               const std::string& enclosing_function) {
    if (!enclosing_function.empty()) {
        auto fl_it = result.semantic.locals.function_locals.find(enclosing_function);
        if (fl_it != result.semantic.locals.function_locals.end()) {
            auto vl_it = fl_it->second.find(var_name);
            if (vl_it != fl_it->second.end()) {
                return vl_it->second;
            }
        }
    }
    auto lv_it = result.semantic.locals.local_variable_types.find(var_name);
    if (lv_it != result.semantic.locals.local_variable_types.end()) {
        return lv_it->second;
    }
    return {};
}

// ─── Scope range helper ───
// Returns the estimated end line of a block, falling back to `fallback`
// when the block is empty.
[[nodiscard]] int calculate_scope_range(const std::vector<std::unique_ptr<Statement>>& body,
                                        int fallback) {
    const int end = estimate_block_end(body);
    return end > 0 ? end : fallback;
}

// Records a local variable's type and, when inside a function with known scope
// bounds, its scoped lifetime. Scope entries are only stored when both bounds
// are positive; an entry whose end line is 0 can never satisfy the
// `line >= start && line <= end` scope query and would be dead.
void register_local_variable(AnalysisResult& result, const std::string& name,
                             const std::string& type, const std::string& enclosing_function,
                             int scope_start, int scope_end) {
    auto& locals = result.semantic.locals;
    locals.local_variable_types[name] = type;
    if (enclosing_function.empty()) {
        return;
    }
    locals.function_locals[enclosing_function][name] = type;
    if (scope_start > 0 && scope_end > 0) {
        locals.scoped_locals[enclosing_function][name].push_back(
            {.type_string = type, .scope_start_line = scope_start, .scope_end_line = scope_end});
    }
}

// ─── Qualified-name registration ───
// Registers a definition under the fully-qualified name and, when inside
// a namespace (prefix is non-empty), also registers the short name via
// try_emplace so the first definition wins.
void register_qualified_definition(AnalysisResult& result, std::string_view prefix,
                                   const std::string& name, const SourceLocation& location,
                                   const std::string& type_str) {
    const std::string qname = make_qualified(prefix, name);
    result.semantic.symbols.definitions[qname] =
        SymbolDefinition{.location = location, .type_string = type_str, .is_mutable = false};
    if (!prefix.empty()) {
        result.semantic.symbols.definitions.try_emplace(
            name,
            SymbolDefinition{.location = location, .type_string = type_str, .is_mutable = false});
    }
}

// ═══════════════════════════════════════════════════════════
// collect_ast_symbols — per-declaration-kind helpers
// ═══════════════════════════════════════════════════════════

void handle_choice_declaration(const ChoiceDeclaration& ch, AnalysisResult& result,
                               std::string_view prefix) {
    register_qualified_definition(result, prefix, ch.name, ch.location, "choice");

    std::vector<std::string> variants;
    variants.reserve(ch.variants.size());
    for (const auto& v : ch.variants) {
        variants.push_back(v.name);
    }
    result.semantic.symbols.choice_variants[ch.name] = std::move(variants);
}

// Build the full function signature string and return type from a
// FunctionDeclaration.  Populates the UserFunctionInfo entry in the
// analysis result.
[[nodiscard]] std::string build_function_signature(const FunctionDeclaration& func,
                                                   std::string_view prefix,
                                                   std::string& out_ret_type,
                                                   std::string& out_params_sig,
                                                   std::vector<ParamInfo>& out_param_list) {
    std::string sig = "function ";
    if (!prefix.empty()) {
        sig.append(prefix);
        sig += '.';
    }
    sig += func.name;

    std::string params_sig = "(";
    for (std::size_t i{0}; i < func.parameters.size(); ++i) {
        if (i > 0) {
            params_sig += ", ";
        }
        if (func.parameters[i].is_mutable) {
            params_sig += "mutable ";
        }
        params_sig += func.parameters[i].name;
        params_sig += ": ";
        params_sig += annotation_to_string(func.parameters[i].type);
    }
    params_sig += ")";
    sig += params_sig;

    std::string ret_type = annotation_to_string(func.return_type);
    sig += " -> " + ret_type;

    std::vector<ParamInfo> param_list;
    param_list.reserve(func.parameters.size());
    for (const auto& p : func.parameters) {
        param_list.push_back(
            ParamInfo{.name = p.name, .type_string = annotation_to_string(p.type)});
    }

    out_ret_type = std::move(ret_type);
    out_params_sig = std::move(params_sig);
    out_param_list = std::move(param_list);
    return sig;
}

template <typename CollectLocalVarsFn>
void handle_function_declaration(const FunctionDeclaration& func, AnalysisResult& result,
                                 std::string_view prefix,
                                 CollectLocalVarsFn&& collect_local_vars_fn) {
    const std::string qname = make_qualified(prefix, func.name);

    std::string ret_type;
    std::string params_sig;
    std::vector<ParamInfo> param_list;
    std::string sig = build_function_signature(func, prefix, ret_type, params_sig, param_list);

    result.semantic.symbols.definitions[qname] =
        SymbolDefinition{.location = func.location, .type_string = ret_type, .is_mutable = false};

    result.semantic.symbols.user_functions[qname] =
        UserFunctionInfo{.signature = std::move(sig),
                         .return_type = std::move(ret_type),
                         .params_signature = std::move(params_sig),
                         .parameters = std::move(param_list),
                         .location = func.location};

    if (!func.body.empty()) {
        const int start_line = func.location.line;
        int end_line = start_line;
        for (const auto& stmt : func.body) {
            if (stmt && stmt->location.line > end_line) {
                end_line = stmt->location.line;
            }
        }
        result.semantic.functions.function_body_ranges[qname] = {start_line, end_line + 1};
    }

    if (!func.body.empty()) {
        const auto& [fn_start, fn_end] = result.semantic.functions.function_body_ranges[qname];
        collect_local_vars_fn(func.body, result, qname, fn_start, fn_end);
    } else {
        collect_local_vars_fn(func.body, result, qname, 0, 0);
    }

    for (const auto& p : func.parameters) {
        const auto type_str = annotation_to_string(p.type);
        result.semantic.locals.local_variable_types[p.name] = type_str;
        result.semantic.locals.function_locals[qname][p.name] = type_str;
    }
}

void handle_interface_declaration(const InterfaceDeclaration& iface, AnalysisResult& result,
                                  std::string_view prefix) {
    register_qualified_definition(result, prefix, iface.name, iface.location, "interface");

    RecordInfo iface_info{.location = iface.location};
    for (const auto& field : iface.fields) {
        iface_info.fields.emplace_back(field.name, annotation_to_string(field.type));
    }
    result.semantic.symbols
        .record_definitions[std::string{k_interface_record_prefix} + iface.name] =
        std::move(iface_info);
}

[[nodiscard]] std::string handle_namespace_declaration(const NamespaceDeclaration& ns,
                                                       AnalysisResult& result,
                                                       std::string_view prefix) {
    result.semantic.symbols.definitions[ns.name] =
        SymbolDefinition{.location = ns.location, .type_string = "namespace", .is_mutable = false};
    return make_qualified(prefix, ns.name);
}

void handle_record_declaration(const RecordDeclaration& rec, AnalysisResult& result,
                               std::string_view prefix) {
    register_qualified_definition(result, prefix, rec.name, rec.location, "record");

    RecordInfo info{.location = rec.location};
    for (const auto& field : rec.fields) {
        info.fields.emplace_back(field.name, annotation_to_string(field.type));
    }
    result.semantic.symbols.record_definitions[rec.name] = std::move(info);
}

void handle_type_alias_declaration(const TypeAliasDeclaration& alias, AnalysisResult& result,
                                   std::string_view prefix) {
    const auto target = annotation_to_string(alias.target_type);
    register_qualified_definition(result, prefix, alias.name, alias.location, target);
}

// ═══════════════════════════════════════════════════════════
// collect_local_vars — extracted helpers
// ═══════════════════════════════════════════════════════════

// Determine the element type for a for-loop variable by inspecting the
// iterable expression.  Falls back to "unknown".
[[nodiscard]] std::string resolve_loop_variable_type(const ForStatement& fs,
                                                     const AnalysisResult& result,
                                                     const std::string& enclosing_function) {
    if (!fs.iterable) {
        return std::string{util::k_unknown_type};
    }

    if (fs.iterable->kind == ExpressionKind::Variable) {
        const auto& var_expr = static_cast<const VariableExpression&>(*fs.iterable);
        const std::string iter_type =
            lookup_variable_type(var_expr.name, result, enclosing_function);
        if (iter_type == "string") {
            return "string";
        }
        if (auto inner = strip_generic_wrapper(iter_type, "array<")) {
            return *inner;
        }
    } else if (fs.iterable->kind == ExpressionKind::Call) {
        const auto& call = static_cast<const CallExpression&>(*fs.iterable);
        if (call.callee && call.callee->kind == ExpressionKind::Variable) {
            const auto& callee_name = static_cast<const VariableExpression&>(*call.callee).name;
            if (callee_name == "range") {
                return "integer";
            }
        }
    } else if (fs.iterable->kind == ExpressionKind::Range) {
        return "integer";
    }

    return std::string{util::k_unknown_type};
}

// Process all arms of a match statement, registering bindings and
// recursing into arm bodies.
template <typename CollectLocalVarsFn>
void process_match_arms(const MatchStatement& ms, AnalysisResult& result,
                        const std::string& enclosing_function, int scope_end,
                        CollectLocalVarsFn&& collect_local_vars_fn) {
    std::string subject_type;
    if (ms.subject && ms.subject->kind == ExpressionKind::Variable) {
        const auto& var_expr = static_cast<const VariableExpression&>(*ms.subject);
        subject_type = lookup_variable_type(var_expr.name, result, enclosing_function);
    }

    for (const auto& arm : ms.arms) {
        const int arm_end = estimate_block_end(arm.body);
        const int arm_start = !arm.body.empty() && arm.body.front()
                                  ? arm.body.front()->location.line
                                  : ms.location.line;
        const int arm_scope_end = arm_end > 0 ? arm_end : scope_end;

        if (arm.has_binding()) {
            const std::string binding_type = resolve_binding_type(arm.kind(), subject_type);
            result.semantic.locals.local_variable_types[arm.binding_name()] = binding_type;
            if (!enclosing_function.empty()) {
                result.semantic.locals.function_locals[enclosing_function][arm.binding_name()] =
                    binding_type;
                result.semantic.locals.scoped_locals[enclosing_function][arm.binding_name()]
                    .push_back({.type_string = binding_type,
                                .scope_start_line = arm_start,
                                .scope_end_line = arm_scope_end});
            }
        }
        for (const auto& cb : arm.choice_bindings()) {
            if (!cb.empty()) {
                result.semantic.locals.local_variable_types[cb] = std::string{util::k_unknown_type};
                if (!enclosing_function.empty()) {
                    result.semantic.locals.function_locals[enclosing_function][cb] =
                        std::string{util::k_unknown_type};
                    result.semantic.locals.scoped_locals[enclosing_function][cb].push_back(
                        {.type_string = std::string{util::k_unknown_type},
                         .scope_start_line = arm_start,
                         .scope_end_line = arm_scope_end});
                }
            }
        }
        collect_local_vars_fn(arm.body, result, enclosing_function, arm_start, arm_scope_end);
    }
}

// Recurse into a nested block statement using the block's own scope range.
template <typename CollectLocalVarsFn>
void process_block_locals(const BlockStatement& bs, AnalysisResult& result,
                          const std::string& enclosing_function, int scope_end,
                          CollectLocalVarsFn&& collect_local_vars_fn) {
    const int blk_start = bs.location.line;
    collect_local_vars_fn(bs.statements, result, enclosing_function, blk_start,
                          calculate_scope_range(bs.statements, scope_end));
}

// Register the loop and index variables of a for statement and recurse into its body.
template <typename CollectLocalVarsFn>
void process_for_locals(const ForStatement& fs, AnalysisResult& result,
                        const std::string& enclosing_function, int scope_end,
                        CollectLocalVarsFn&& collect_local_vars_fn) {
    const std::string loop_type = resolve_loop_variable_type(fs, result, enclosing_function);

    const int for_start = fs.location.line;
    const int for_scope_end = calculate_scope_range(fs.body, scope_end);

    if (!fs.loop_variable.empty()) {
        register_local_variable(result, fs.loop_variable, loop_type, enclosing_function, for_start,
                                for_scope_end);
    }
    if (!fs.index_variable.empty()) {
        register_local_variable(result, fs.index_variable, "integer", enclosing_function, for_start,
                                for_scope_end);
    }

    collect_local_vars_fn(fs.body, result, enclosing_function, for_start, for_scope_end);
}

// Recurse into the then and else branches of an if statement.
template <typename CollectLocalVarsFn>
void process_if_locals(const IfStatement& ifs, AnalysisResult& result,
                       const std::string& enclosing_function, int scope_end,
                       CollectLocalVarsFn&& collect_local_vars_fn) {
    const int then_start = ifs.location.line;
    const int then_end = estimate_block_end(ifs.then_body);
    collect_local_vars_fn(ifs.then_body, result, enclosing_function, then_start,
                          then_end > 0 ? then_end : scope_end);

    const int else_start = then_end > 0 ? then_end : ifs.location.line;
    collect_local_vars_fn(ifs.else_body, result, enclosing_function, else_start,
                          calculate_scope_range(ifs.else_body, scope_end));
}

// Register the catch variable of a try statement and recurse into its three bodies.
template <typename CollectLocalVarsFn>
void process_try_locals(const TryStatement& ts, AnalysisResult& result,
                        const std::string& enclosing_function, int scope_end,
                        CollectLocalVarsFn&& collect_local_vars_fn) {
    const int try_start = ts.location.line;
    const int try_end = estimate_block_end(ts.try_body);
    const int catch_start = try_end > 0 ? try_end : ts.location.line;
    const int catch_end = estimate_block_end(ts.catch_body);
    const int finally_start = catch_end > 0 ? catch_end : catch_start;

    if (!ts.catch_var.empty()) {
        const int catch_scope_end = catch_end > 0 ? catch_end : scope_end;
        register_local_variable(result, ts.catch_var, "string", enclosing_function, catch_start,
                                catch_scope_end);
    }

    collect_local_vars_fn(ts.try_body, result, enclosing_function, try_start,
                          try_end > 0 ? try_end : scope_end);
    collect_local_vars_fn(ts.catch_body, result, enclosing_function, catch_start,
                          catch_end > 0 ? catch_end : scope_end);
    collect_local_vars_fn(ts.finally_body, result, enclosing_function, finally_start,
                          calculate_scope_range(ts.finally_body, scope_end));
}

// Register a variable declaration's binding in the enclosing scope.
void process_variable_decl_locals(const VariableDeclStatement& var, AnalysisResult& result,
                                  const std::string& enclosing_function, int scope_start,
                                  int scope_end) {
    const auto type_str = annotation_to_string(var.type);
    register_local_variable(result, var.name, type_str, enclosing_function, scope_start, scope_end);
    if (var.is_mutable) {
        result.semantic.locals.mutable_locals.insert(var.name);
    }
}

// Recurse into the body of a while statement.
template <typename CollectLocalVarsFn>
void process_while_locals(const WhileStatement& ws, AnalysisResult& result,
                          const std::string& enclosing_function, int scope_end,
                          CollectLocalVarsFn&& collect_local_vars_fn) {
    const int wh_start = ws.location.line;
    collect_local_vars_fn(ws.body, result, enclosing_function, wh_start,
                          calculate_scope_range(ws.body, scope_end));
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Symbol collection
// ═══════════════════════════════════════════════════════════

void LspAnalysisService::collect_ast_symbols(const std::vector<std::unique_ptr<Declaration>>& decls,
                                             AnalysisResult& result, std::string_view prefix) {
    auto local_vars_fn = [this](const std::vector<std::unique_ptr<Statement>>& stmts,
                                AnalysisResult& res, const std::string& fn, int start, int end) {
        collect_local_vars(stmts, res, fn, start, end);
    };

    for (const auto& decl_ptr : decls) {
        if (!decl_ptr) {
            continue;
        }

        switch (decl_ptr->kind) {
            case DeclarationKind::Choice: {
                const auto& ch = static_cast<const ChoiceDeclaration&>(*decl_ptr);
                handle_choice_declaration(ch, result, prefix);
                break;
            }
            case DeclarationKind::Function: {
                const auto& func = static_cast<const FunctionDeclaration&>(*decl_ptr);
                handle_function_declaration(func, result, prefix, local_vars_fn);
                break;
            }
            case DeclarationKind::Interface: {
                const auto& iface = static_cast<const InterfaceDeclaration&>(*decl_ptr);
                handle_interface_declaration(iface, result, prefix);
                break;
            }
            case DeclarationKind::Namespace: {
                const auto& ns = static_cast<const NamespaceDeclaration&>(*decl_ptr);
                const std::string new_prefix = handle_namespace_declaration(ns, result, prefix);
                collect_ast_symbols(ns.declarations, result, new_prefix);
                break;
            }
            case DeclarationKind::Record: {
                const auto& rec = static_cast<const RecordDeclaration&>(*decl_ptr);
                handle_record_declaration(rec, result, prefix);
                break;
            }
            case DeclarationKind::TypeAlias: {
                const auto& alias = static_cast<const TypeAliasDeclaration&>(*decl_ptr);
                handle_type_alias_declaration(alias, result, prefix);
                break;
            }
            case DeclarationKind::Include: {
                const auto& inc = static_cast<const IncludeDeclaration&>(*decl_ptr);
                result.semantic.includes.include_literals.emplace_back(inc.path, inc.location);
                break;
            }
            default:
                break;
        }
    }
}

void LspAnalysisService::collect_local_vars(const std::vector<std::unique_ptr<Statement>>& stmts,
                                            AnalysisResult& result,
                                            const std::string& enclosing_function, int scope_start,
                                            int scope_end) {
    auto local_vars_fn = [this](const std::vector<std::unique_ptr<Statement>>& s,
                                AnalysisResult& res, const std::string& fn, int start, int end) {
        collect_local_vars(s, res, fn, start, end);
    };

    for (const auto& stmt_ptr : stmts) {
        if (!stmt_ptr) {
            continue;
        }

        switch (stmt_ptr->kind) {
            case StatementKind::Block:
                process_block_locals(static_cast<const BlockStatement&>(*stmt_ptr), result,
                                     enclosing_function, scope_end, local_vars_fn);
                break;
            case StatementKind::For:
                process_for_locals(static_cast<const ForStatement&>(*stmt_ptr), result,
                                   enclosing_function, scope_end, local_vars_fn);
                break;
            case StatementKind::If:
                process_if_locals(static_cast<const IfStatement&>(*stmt_ptr), result,
                                  enclosing_function, scope_end, local_vars_fn);
                break;
            case StatementKind::Try:
                process_try_locals(static_cast<const TryStatement&>(*stmt_ptr), result,
                                   enclosing_function, scope_end, local_vars_fn);
                break;
            case StatementKind::VariableDeclaration:
                process_variable_decl_locals(static_cast<const VariableDeclStatement&>(*stmt_ptr),
                                             result, enclosing_function, scope_start, scope_end);
                break;
            case StatementKind::While:
                process_while_locals(static_cast<const WhileStatement&>(*stmt_ptr), result,
                                     enclosing_function, scope_end, local_vars_fn);
                break;
            case StatementKind::Match:
                process_match_arms(static_cast<const MatchStatement&>(*stmt_ptr), result,
                                   enclosing_function, scope_end, local_vars_fn);
                break;
            default:
                break;
        }
    }
}

void LspAnalysisService::collect_call_graph(const std::vector<std::unique_ptr<Declaration>>& decls,
                                            AnalysisResult& result, std::string_view prefix) {
    for (const auto& decl : decls) {
        if (!decl) {
            continue;
        }

        if (decl->kind == DeclarationKind::Namespace) {
            const auto& ns = static_cast<const NamespaceDeclaration&>(*decl);
            const std::string ns_prefix = make_qualified(prefix, ns.name) + ".";
            collect_call_graph(ns.declarations, result, ns_prefix);
            continue;
        }

        if (decl->kind != DeclarationKind::Function) {
            continue;
        }

        const auto& func = static_cast<const FunctionDeclaration&>(*decl);
        std::unordered_set<std::string> callees;
        CallCollector collector{callees};
        collector.visit_stmts(func.body);

        if (!callees.empty()) {
            const std::string qualified_name = std::string(prefix) + func.name;
            result.semantic.functions.call_graph[qualified_name] = std::move(callees);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Symbol phase
// ═══════════════════════════════════════════════════════════

void LspAnalysisService::symbol_phase(const Program& program, AnalysisResult& result) {
    collect_ast_symbols(program.declarations, result);
    collect_local_vars(program.statements, result);

    // Build sorted function ranges for O(log n) enclosing-function lookup.
    result.semantic.functions.sorted_function_ranges.reserve(
        result.semantic.functions.function_body_ranges.size());
    for (const auto& [fn_name, range] : result.semantic.functions.function_body_ranges) {
        result.semantic.functions.sorted_function_ranges.push_back(
            {.start_line = range.first, .end_line = range.second, .name = fn_name});
    }
    std::ranges::sort(result.semantic.functions.sorted_function_ranges,
                      [](const auto& a, const auto& b) { return a.start_line < b.start_line; });

    // Build reverse map: short name → qualified names for O(1) lookup.
    for (const auto& [qualified_name, fn_info] : result.semantic.symbols.user_functions) {
        if (is_qualified_name(qualified_name)) {
            const std::string short_name{qualified_member(qualified_name)};
            result.semantic.symbols.function_short_names[short_name].push_back(qualified_name);
        }
    }

    // Build interface implementation index.
    build_interface_implementations(result);
}

// Matches records against interfaces and populates interface_implementations.
//
// A record structurally implements an interface when it declares every field
// (name + type) the interface requires.  Rather than testing each interface
// against every record (interfaces x records x fields), build an inverted
// index from field signature -> records that declare it.  Each interface then
// only visits records that share at least one of its fields, and a record
// implements the interface once it matches all of them.
void LspAnalysisService::build_interface_implementations(AnalysisResult& result) {
    const auto& defs = result.semantic.symbols.record_definitions;

    const auto field_signature = [](const std::string& name, const std::string& type) {
        std::string sig = name;
        sig += ':';
        sig += type;
        return sig;
    };

    // Per-record field-signature sets (deduplicated), then an inverted index
    // mapping each signature to the records that declare it.
    std::unordered_map<std::string, std::unordered_set<std::string>> record_field_sets;
    for (const auto& [rec_name, rec_info] : defs) {
        if (rec_name.starts_with(k_interface_record_prefix)) {
            continue;
        }
        auto& field_set = record_field_sets[rec_name];
        field_set.reserve(rec_info.fields.size());
        for (const auto& [fname, ftype] : rec_info.fields) {
            field_set.insert(field_signature(fname, ftype));
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> records_by_field;
    for (const auto& [rec_name, field_set] : record_field_sets) {
        for (const auto& sig : field_set) {
            records_by_field[sig].push_back(rec_name);
        }
    }

    for (const auto& [key, iface_info] : defs) {
        if (!key.starts_with(k_interface_record_prefix) || iface_info.fields.empty()) {
            continue;
        }
        const auto iface_name = key.substr(k_interface_record_prefix.size());

        // Tally how many of the interface's fields each candidate record
        // matches.  A record implements the interface once it matches all of
        // them.  If any interface field is declared by no record at all, the
        // interface cannot be implemented.
        std::unordered_map<std::string, std::size_t> match_counts;
        bool implementable = true;
        for (const auto& [fname, ftype] : iface_info.fields) {
            const auto it = records_by_field.find(field_signature(fname, ftype));
            if (it == records_by_field.end()) {
                implementable = false;
                break;
            }
            for (const auto& rec_name : it->second) {
                ++match_counts[rec_name];
            }
        }
        if (!implementable) {
            continue;
        }

        const std::size_t needed = iface_info.fields.size();
        for (const auto& [rec_name, count] : match_counts) {
            if (count == needed) {
                result.semantic.symbols.interface_implementations[iface_name].push_back(rec_name);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Token index builders
// ═══════════════════════════════════════════════════════════

void LspAnalysisService::build_token_index(AnalysisResult& result) {
    result.metadata.token_index.clear();
    result.metadata.token_index.reserve(result.semantic.tokens.size());

    for (std::size_t i{0}; i < result.semantic.tokens.size(); ++i) {
        const auto& tok = result.semantic.tokens[i];
        const int line = tok.location.line;
        const int col_end = tok.location.column;
        const int col_start = token_start_column_1based(tok);

        result.metadata.token_index.push_back(TokenIndexEntry{
            .line = line, .col_start = col_start, .col_end = col_end, .token_idx = i});
    }

    std::ranges::sort(result.metadata.token_index,
                      [](const TokenIndexEntry& a, const TokenIndexEntry& b) {
                          if (a.line != b.line) {
                              return a.line < b.line;
                          }
                          return a.col_start < b.col_start;
                      });

    // Build the per-line index for O(1) line lookups.
    result.metadata.line_index.build(result.semantic.tokens);

    // Precompute each user function's NAME range now, while the freshly built
    // line_index is valid for THIS result (same scope, before it is cached or
    // moved — the index stores a raw pointer into semantic.tokens that would
    // dangle after a move). The semantic-token classifier then compares against
    // this by-value range O(1) per token instead of rescanning the stream, which
    // would be quadratic over the document. A no-op when no functions were
    // collected (e.g. timeout/error paths), so it costs nothing there.
    for (auto& [qname, info] : result.semantic.symbols.user_functions) {
        info.name_range =
            find_declaration_name_range(result.metadata.line_index, info.location, qname);
    }
}

void LspAnalysisService::build_identifier_index(AnalysisResult& result) {
    result.metadata.identifier_index.clear();

    for (std::size_t i{0}; i < result.semantic.tokens.size(); ++i) {
        if (result.semantic.tokens[i].type == TokenType::Identifier) {
            result.metadata.identifier_index[result.semantic.tokens[i].lexeme].push_back(i);
        }
    }
}

} // namespace luma::lsp
