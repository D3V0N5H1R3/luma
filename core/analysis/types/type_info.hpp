// ─────────────────────────────────────────────────────────────────────────────
// Type Info — Core Type Representation
// ─────────────────────────────────────────────────────────────────────────────
// Defines the fundamental data types used throughout the type checker:
//   - TypeInfo: a resolved type within the type system
//   - SymbolInfo: a bound symbol with its type and usage metadata
//   - TypeScope: lexical scope for type and variable bindings
//   - TypeRefinement: flow-sensitive narrowed type after is<T> checks
//   - Resolved symbol table types (ResolvedSymbol, ResolvedFunction, etc.)
//   - SymbolTable: the full resolved symbol table exported to the LSP
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"

namespace luma {

struct Statement;

using StatementPtr = std::unique_ptr<Statement>;

// ─────────────────────── Resolved Type ───────────────────────

// TypeInfo: Semantic type information (type-checking level).
// Represents the resolved, validated type after analysis.  Compare with
// TypeAnnotation (ast/expression.hpp) which is the syntactic type expression
// as written by the programmer.
//
// TODO: TypeInfo and ReturnTypeDesc (shared/stdlib/stdlib_return_type.hpp)
// represent overlapping concepts.  ReturnTypeDesc is a lightweight
// descriptor that avoids depending on this analysis library.  A future
// shared TypeDescriptor could unify both; see ReturnTypeDesc for details.
struct TypeInfo {
    enum class Kind {
        Array,
        Boolean,
        Channel,
        Choice,
        Dictionary,
        Func,
        Integer,
        Interface,
        Namespace,
        None,
        Number,
        Optional,
        Range,
        Record,
        Reference,
        Result,
        Socket,
        StdlibAny,
        String,
        Task,
        Tuple,
        Unknown,
        Void
    };

    Kind kind{Kind::Unknown};
    std::string name;                  // Enum, Record, Interface name
    std::vector<TypeInfo> inner_types; // element/param types

    // return_type uses shared_ptr (rather than value semantics like
    // inner_types) to break the recursive type definition: a Func's
    // TypeInfo can itself contain a Func return type, which would
    // create an infinitely-sized value type.  The indirection also
    // allows deferred resolution when the return type is not yet known
    // during incremental type checking.
    std::shared_ptr<TypeInfo> return_type; // Func return type

    [[nodiscard]] bool operator==(const TypeInfo& other) const;

    // Formats this type as a human-readable string (e.g. "array<integer>").
    // Not cached: all call sites are on cold diagnostic paths (error/warn
    // messages), so the formatting cost is negligible.  Caching was considered
    // but rejected because TypeInfo fields are public and mutated extensively
    // throughout the type checker, making cache invalidation unreliable
    // without a major encapsulation refactor.
    [[nodiscard]] std::string to_string() const;

    // Cached variant: looks up or computes the string representation using
    // an external cache map.  The caller owns the cache and is responsible
    // for discarding it when any TypeInfo in the set may have been mutated.
    // Useful in batch scenarios (e.g. LSP hover, diagnostic rendering) where
    // the same type is formatted repeatedly and no mutation occurs between
    // lookups.
    //
    // Limitation: the cache key is a raw pointer, so entries become stale
    // if the TypeInfo object is mutated in place.  Callers must clear the
    // cache between mutation-prone phases.
    using ToStringCache = std::unordered_map<const TypeInfo*, std::string>;
    [[nodiscard]] static const std::string& to_string_cached(const TypeInfo& type,
                                                             ToStringCache& cache);
    [[nodiscard]] bool is_numeric() const;

    // Factory helpers.
    //
    // Organised by shape:
    //   make(Kind)                      — simple type with no inner types.
    //   make_array/dict/optional/task/  — wrapper around a single inner type.
    //     channel/reference(TypeInfo)     Each sets the appropriate Kind and
    //                                    stores the argument in inner_types.
    //                                    They are kept as separate named
    //                                    factories (rather than a single
    //                                    make_wrapper(Kind, TypeInfo)) because
    //                                    the named forms are self-documenting
    //                                    at call sites.
    //   make_result(TypeInfo [, TypeInfo]) — one or two inner types.
    //   make_func(vector, TypeInfo)      — function type (params + return).
    //   make_tuple(vector<TypeInfo>)     — variadic inner types.
    //   make_named(Kind, name)          — named type (Record, Choice, etc.).
    //   make_generic(Kind, name, args)  — named type with type arguments.
    [[nodiscard]] static TypeInfo make(Kind k);
    [[nodiscard]] static TypeInfo make_array(TypeInfo element);
    [[nodiscard]] static TypeInfo make_dict(TypeInfo value);
    [[nodiscard]] static TypeInfo make_result(TypeInfo value);
    [[nodiscard]] static TypeInfo make_result(TypeInfo value, TypeInfo error);
    [[nodiscard]] static TypeInfo make_optional(TypeInfo inner);
    [[nodiscard]] static TypeInfo make_task(TypeInfo inner);
    [[nodiscard]] static TypeInfo make_channel(TypeInfo inner);
    [[nodiscard]] static TypeInfo make_reference(TypeInfo inner);
    [[nodiscard]] static TypeInfo make_func(std::vector<TypeInfo> param_types,
                                            TypeInfo return_type);
    [[nodiscard]] static TypeInfo make_tuple(std::vector<TypeInfo> elements);
    [[nodiscard]] static TypeInfo make_named(Kind k, const std::string& name);
    // Generic named type: Record or Interface with type arguments (e.g., Box<integer>).
    [[nodiscard]] static TypeInfo make_generic(Kind k, std::string name,
                                               std::vector<TypeInfo> type_args);

    // Inner-type accessors.
    //
    // Symmetric with the make_* factories: they name the conventional slot in
    // inner_types so call sites read as intent instead of decoding the layout
    // with a raw subscript.  Each returns a reference into inner_types and
    // carries the same precondition as the make_* factory that produced the
    // type — the wrapper must actually hold its inner type(s).  Calling on a
    // mismatched Kind or an empty inner_types is undefined, exactly like the
    // inner_types[...] access these replace, so guard with the Kind/size
    // checks the surrounding code already performs.
    //
    //   element_type()      — wrapped element of array/optional/task/channel/
    //                         reference          (inner_types[0]).
    //   value_type()        — value of a dictionary                (inner_types[0]).
    //   result_value_type() — success type of a result             (inner_types[0]).
    //   result_error_type() — error type of a result               (inner_types[1]).
    [[nodiscard]] const TypeInfo& element_type() const {
        return inner_types[0];
    }

    [[nodiscard]] const TypeInfo& value_type() const {
        return inner_types[0];
    }

    [[nodiscard]] const TypeInfo& result_value_type() const {
        return inner_types[0];
    }

    [[nodiscard]] const TypeInfo& result_error_type() const {
        return inner_types[1];
    }
};

// ─────────────────────── Symbol Info ───────────────────────

struct SymbolInfo {
    TypeInfo type;
    bool is_mutable{false};
    bool is_unique{false};    // marked with 'unique' keyword
    bool is_consumed{false};  // consumed (moved) — only for unique values
    bool is_borrow{false};    // borrowed reference — cannot be consumed
    bool is_read{false};      // variable was read at least once
    bool is_written{false};   // variable was assigned after declaration
    bool is_parameter{false}; // function/lambda parameter (for distinct warnings)
    SourceLocation location;  // declaration location for diagnostic messages
};

// ─────────────────────── Variable Modifiers ───────────────────────

// Mutability and ownership flags supplied when defining a variable in a
// TypeScope.  Grouping them in a struct (designated-initialised at call sites)
// removes the silent transposition hazard of three adjacent bool parameters.
struct VariableModifiers {
    bool is_mutable{};
    bool is_unique{};
    bool is_borrow{};
};

// ─────────────────────── Type Scope ───────────────────────

// Note: TypeScope uses a shared_ptr-linked parent chain and tracks ownership
// state (unique/borrow/consumed) with flow-sensitive snapshot/restore.  It
// shares a superficial push/pop/lookup shape with ResolveScope and
// LinterTracker::ScopeData but has different semantics, so the three are not
// unified into a single Scope<Symbol> template.  See common/scope_stack.hpp
// §Component Usage for the full rationale.

class TypeScope {
public:
    explicit TypeScope(std::shared_ptr<TypeScope> parent = nullptr);

    void define(std::string_view name, TypeInfo type, VariableModifiers modifiers,
                SourceLocation loc = {});
    [[nodiscard]] const SymbolInfo* lookup(std::string_view name) const;
    [[nodiscard]] SymbolInfo* lookup_mut(std::string_view name);
    [[nodiscard]] bool has_local(std::string_view name) const;
    [[nodiscard]] std::shared_ptr<TypeScope> parent() const;

    // Explicit mutation methods — prefer these over lookup_mut() for clarity.
    void mark_consumed(std::string_view name, bool consumed = true);
    void mark_read(std::string_view name);
    void mark_written(std::string_view name);
    void mark_as_parameter(std::string_view name);

    // Returns all locally-defined unique variables (for scope-exit checks).
    [[nodiscard]] std::vector<std::pair<std::string, SymbolInfo>> unconsumed_unique_locals() const;

    // Returns all locally-defined symbols (for unused variable / mutable checks).
    [[nodiscard]] const StringMap<SymbolInfo>& locals() const;

    // Ownership state snapshot/restore for flow-sensitive analysis.
    // Captures the is_consumed state of all unique variables visible
    // from this scope up through parent scopes.
    //
    // Performance: Only unique variables are captured (filtered by
    // is_unique), so the snapshot is typically very small — most
    // programs have few unique variables in scope at any point.  The
    // snapshot copies variable names (std::string) to decouple lifetime
    // from the scope chain.  Using string_view or indices would avoid
    // copies but create fragile lifetime dependencies on scope maps
    // that may be modified between snapshot and restore.  Given the
    // small number of unique variables, the current approach is both
    // simple and fast enough.
    using OwnershipSnapshot = std::vector<std::pair<std::string, bool>>;
    [[nodiscard]] OwnershipSnapshot snapshot_ownership() const;
    void restore_ownership(const OwnershipSnapshot& snap);

private:
    StringMap<SymbolInfo> symbols_;
    std::shared_ptr<TypeScope> parent_;
};

// ─────────────────────── Type Refinement ───────────────────────

// Flow-sensitive type refinement: tracks narrowed types after is<T> checks.
// When an if-condition is `is<T>(variable)`, the variable's type is narrowed
// to T within the then-branch.
struct TypeRefinement {
    std::string variable_name;
    TypeInfo narrowed_type;
};

// ─────────────────────── Resolved Symbol Table ───────────────────────

// Resolved symbol table — populated after check(), consumable by the LSP.
// Contains all user-defined and resolved symbols with structured types.
struct ResolvedSymbol {
    std::string name;
    TypeInfo type;
    SourceLocation location;
    bool is_mutable{false};
    bool is_parameter{false};
};

struct ResolvedFunction {
    std::string name;
    TypeInfo return_type;
    std::vector<std::pair<std::string, TypeInfo>> parameters; // (name, type) pairs
    SourceLocation location;
    bool is_test{false};
};

struct ResolvedRecord {
    std::string name;
    std::vector<std::pair<std::string, TypeInfo>> fields; // (field_name, type) pairs
    SourceLocation location;
};

struct ResolvedChoice {
    std::string name;
    std::vector<std::string> variants;
    SourceLocation location;
};

struct SymbolTable {
    StringMap<ResolvedSymbol> variables;   // All resolved variable/binding symbols.
    StringMap<ResolvedFunction> functions; // User-defined functions with resolved types.
    StringMap<ResolvedRecord> records;     // Record type definitions.
    StringMap<ResolvedChoice> choices;     // Choice type definitions.
    StringMap<TypeInfo> stdlib_signatures; // Stdlib return types.
};

// ─────────────────────── Compile-Time Type Metadata ───────────────────────

/// Compile-time metadata for built-in types.
struct TypeMetadata {
    std::string_view name;
    std::string_view category; // "Numeric", "Collection", "Primitive", etc.
    bool is_nullable;
    bool is_copyable;
};

/// Returns compile-time metadata for a TypeInfo::Kind.
[[nodiscard]] constexpr TypeMetadata type_metadata(TypeInfo::Kind kind) noexcept {
    using K = TypeInfo::Kind;
    switch (kind) {
        case K::Boolean:
            return {"boolean", "Primitive", false, true};
        case K::Integer:
            return {"integer", "Numeric", false, true};
        case K::Number:
            return {"number", "Numeric", false, true};
        case K::String:
            return {"string", "Primitive", false, true};
        case K::None:
            return {"none", "Primitive", true, true};
        case K::Void:
            return {"void", "Primitive", false, true};
        case K::Array:
            return {"array", "Collection", false, true};
        case K::Dictionary:
            return {"dictionary", "Collection", false, true};
        case K::Tuple:
            return {"tuple", "Composite", false, true};
        case K::Record:
            return {"record", "Composite", false, true};
        case K::Choice:
            return {"choice", "Composite", false, true};
        case K::Optional:
            return {"optional", "Wrapper", true, true};
        case K::Result:
            return {"result", "Wrapper", false, true};
        case K::Range:
            return {"range", "Iterable", false, true};
        case K::Func:
            return {"function", "Callable", false, true};
        case K::Channel:
            return {"channel", "Concurrency", false, false};
        case K::Task:
            return {"task", "Concurrency", false, false};
        case K::Socket:
            return {"socket", "IO", false, false};
        case K::Reference:
            return {"reference", "Wrapper", false, false};
        case K::Interface:
            return {"interface", "Type", false, false};
        case K::Namespace:
            return {"namespace", "Type", false, false};
        case K::StdlibAny:
            return {"any", "Type", true, true};
        case K::Unknown:
            [[unlikely]] return {"unknown", "Type", true, true};
    }
    return {"unknown", "Type", true, true};
}

} // namespace luma
