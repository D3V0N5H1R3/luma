#ifndef LUMA_INTERPRETER_VALUE_COLLECTIONS_HPP
#define LUMA_INTERPRETER_VALUE_COLLECTIONS_HPP

// Compound value type definitions (ArrayValue, DictionaryValue, TupleValue,
// ResultValue, RecordValue, RangeValue, ChoiceValue, FunctionValue,
// NativeFunctionValue, TaskValue, ChannelValue, SocketValue, ReferenceValue)
// and the CollectionObject hierarchy for rarely-used collection types.
//
// These are the heap-allocated types whose shared_ptr is stored in the
// Value variant.  Separated from value_type.hpp (the Value class itself)
// so that the ~480-line file is split into two focused headers.
//
// ── Memory ownership ──
// Compound values use shared_ptr with copy-on-write semantics.  Values
// are shared until mutated, then cloned via clone_array/clone_dict.
// Primitives use inline value semantics stored directly in the Value
// variant; together these two models are the whole memory strategy.
//
// Depends on value_type.hpp for the complete Value class (compound types
// store Value members, e.g. ArrayValue stores vector<Value>).
//
// ─── Adding a new CollectionObject subtype ──────────────────────────────
//
// CollectionObject is the polymorphic base for rarely-used collection
// types (Queue, Stack, Set, etc.).  All subtypes share a single variant
// slot in Value, keeping the variant small.  Adding a new collection
// type requires coordinated changes across several files:
//
//   1. value_fwd.hpp            — Add a variant to the CollectionKind enum.
//   2. value_collections.hpp    — Define the struct inheriting CollectionObject.
//                                 Override the four pure-virtual methods:
//                                   to_display_string()
//                                   display_type_name()
//                                   deep_copy_value()
//                                   size()
//                                 by_value types must also override equals_to()
//                                 to provide element-wise comparison.  If
//                                 equality is not meaningful for the type,
//                                 override equals_kind() to return
//                                 EqualsKind::by_reference (see XmlValue,
//                                 XmlValue) and leave equals_to() to the base
//                                 default (which returns false).
//   3. value_type.hpp           — Add is_*() and as_*() convenience methods
//                                 to Value for the new collection kind.
//   4. value_equality.cpp       — The generic CollectionObject branch in
//                                 Value::operator== delegates to equals_to();
//                                 no per-type change needed unless the type
//                                 has special equality semantics.
//   5. value_copying.cpp        — The generic CollectionObject branch
//                                 delegates to deep_copy_value(); no per-type
//                                 change needed.
//   6. value_formatting.cpp     — The generic CollectionObject branch
//                                 delegates to to_display_string() and
//                                 display_type_name(); no per-type change
//                                 needed.
//   7. stdlib module            — Add the stdlib module (e.g. stdlib_foo.cpp)
//                                 that registers native functions for the
//                                 new type, and register it in stdlib.cpp.
//
// Steps 4–6 are handled by the virtual dispatch on CollectionObject,
// so only steps 1–3 and 7 require new code.  The virtual methods
// (to_display_string, display_type_name, equals_to, deep_copy_value,
// equals_kind, size) exist specifically to keep this checklist short —
// they centralise formatting, equality, copying, and typeof dispatch so
// that each new subtype only needs to provide its own implementations.
//
// ─── Storage field naming convention ────────────────────────────────────
//
// Collection subtypes use semantically descriptive names for their
// primary storage fields.  When adding a new subtype, follow this
// convention:
//
//   `elements`  — Ordered sequence of homogeneous values.
//                 Used by: ArrayValue, TupleValue, QueueValue,
//                          StackValue, SetValue.
//
//   `entries`   — Key-value pairs (string key → value).
//                 Used by: DictionaryValue, KeyValueStoreValue.
//
//   `fields`    — Named or tagged members of a structured value.
//                 Used by: RecordValue, ChoiceValue.
//
//   Structure-specific names — When the data structure dictates a
//                 specific layout that none of the above describe:
//                   `root`      — (reserved for future use)
//
// Prefer the generic names when they fit.  Use a structure-specific
// name only when the storage layout has no natural analogue above.
//
// All subtypes also expose a uniform `size()` accessor that returns
// the logical element count, hiding storage-specific details from
// callers that only need the collection's cardinality.

#include <atomic>
#include <cstddef>
#include <deque>
#include <future>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/decimal.hpp"
#include "common/string_hash.hpp"
#include "runtime/interpreter/lazy_hash_index.hpp"
#include "runtime/interpreter/value_type.hpp"

namespace luma {

// ──────────── CollectionObject hierarchy ────────────

// CollectionObject — polymorphic base for rarely-used collection types
// (Queue, Stack, Set, Xml, KeyValueStore).
// These types share a single variant slot in Value, reducing the variant from
// 27 to 19 alternatives and simplifying all std::visit dispatch.
//
// ── Why virtual dispatch ──
//
// Virtual dispatch is used because CollectionObject values are stored as
// shared_ptr<CollectionObject> in the Value variant.  Callers that format,
// copy, or compare a Value do not know the concrete subtype at the call
// site — they call to_display_string(), display_type_name(), equals_to(),
// deep_copy_value(), and size() through the base pointer, relying on the
// vtable to reach the correct subtype implementation.  This keeps the
// centralised branches in value_formatting.cpp, value_copying.cpp, and
// value_equality.cpp free of per-type switch statements and makes each
// new collection subtype self-contained (see the "Adding a new
// CollectionObject subtype" guide in this file's header comment).
//
// ── CRTP alternative ──
//
// CRTP (Curiously Recurring Template Pattern) would replace vtable
// lookups with static dispatch by templating the base class on the
// derived type.  This eliminates the ~5 virtual calls per collection
// operation but at a significant cost: every function that accepts a
// CollectionObject would need to become a template (or use type erasure
// separately), increasing code size, compile times, and cognitive
// overhead for contributors.
//
// CRTP is worth reconsidering if profiling of a collection-heavy
// workload shows that virtual dispatch on one of these types is a
// measurable hot-path bottleneck.  The VM's main loop does not call
// CollectionObject methods in its inner dispatch — those calls happen
// in stdlib functions and display formatting — so the current approach
// is sufficient until profiling indicates otherwise.
//
// TODO(perf): Profile collection-heavy workloads to determine if CRTP
// would provide measurable benefit before attempting the refactor.
struct CollectionObject {
    /// Documents the equality semantics of a collection type.
    enum class EqualsKind {
        by_value,     ///< Element-wise deep comparison (arrays, queues, stacks, etc.)
        by_reference, ///< Equality is not meaningful; always returns false.
    };

    explicit CollectionObject(CollectionKind k) : kind_{k} {}

    virtual ~CollectionObject() = default;

    [[nodiscard]] CollectionKind collection_kind() const noexcept {
        return kind_;
    }

    /// Returns the equality semantics for this collection type.
    /// Types returning `by_reference` always compare as not-equal.
    [[nodiscard]] virtual EqualsKind equals_kind() const noexcept {
        return EqualsKind::by_value;
    }

    [[nodiscard]] virtual std::string to_display_string() const = 0;
    [[nodiscard]] virtual std::string display_type_name() const = 0;

    /// Element-wise structural comparison.  Only by_value subtypes need to
    /// override this; by_reference subtypes rely on this default because
    /// equals_collection() short-circuits to false before ever calling it.
    [[nodiscard]] virtual bool equals_to([[maybe_unused]] const CollectionObject& other) const {
        return false;
    }

    [[nodiscard]] virtual Value deep_copy_value() const = 0;

    /// Returns the logical number of elements in this collection.
    [[nodiscard]] virtual std::size_t size() const = 0;

    CollectionObject(const CollectionObject&) = default;
    CollectionObject& operator=(const CollectionObject&) = default;
    CollectionObject(CollectionObject&&) = default;
    CollectionObject& operator=(CollectionObject&&) = default;

private:
    CollectionKind kind_;
};

// ──────────── Primary compound types ────────────

// Copy-on-write array of Values.
//
// The shared_ptr<vector<Value>> allows multiple ArrayValue instances to share
// the same backing storage at zero extra cost (structural sharing).  Sharing
// is transparent until one of the sharers mutates the array; at that point
// the mutating side must call ensure_unique() to break the alias and obtain
// exclusive ownership before modifying elements.
//
// ── When to clone ──
// Any code path that appends, removes, or overwrites array elements must call
// ensure_unique() first.  Read-only paths (iteration, indexing, formatting)
// may use the shared pointer directly without cloning.
//
// ── Thread safety ──
// ArrayValue is not thread-safe.  The Luma runtime gives each task its own
// independent copy of all values: when an array is sent across a Channel the
// channel performs a deep copy, so the sender and receiver always hold
// distinct shared_ptrs that will never alias each other across OS threads.
struct ArrayValue {
    // COW: multiple ArrayValue instances can share the same storage.
    // Mutation must call ensure_unique() first.
    std::shared_ptr<std::vector<Value>> elements;

    ArrayValue() : elements(std::make_shared<std::vector<Value>>()) {}

    // Ensure this instance has exclusive ownership before mutating.
    //
    // ── Threading model ──
    // The Luma runtime uses a single-threaded ownership model for values.
    // Each task (green thread) owns its own copy of every value; values
    // are never shared mutably across OS threads.  When an array is sent
    // to another task via a Channel, the channel performs a deep copy, so
    // the sender and receiver always hold independent shared_ptrs.
    //
    // Because of this single-threaded ownership guarantee, the
    // std::shared_ptr::use_count() check here is safe: only one thread
    // can observe or modify the reference count at any given time.
    // use_count() would be unreliable under concurrent access (it is
    // specified as "approximate" in C++20), but that situation cannot
    // arise under the current threading model.
    //
    // ── Limitations ──
    // If multi-threaded mutable sharing of ArrayValue were ever
    // introduced (e.g. shared-memory concurrency), this COW strategy
    // would need to be replaced with either atomic reference counting
    // plus a mutex, or a different copy/snapshot mechanism.
    // Note: COW is single-threaded only. If arrays are shared across tasks
    // via ReferenceValue, the caller must hold an external lock.
    void ensure_unique() {
        if (elements.use_count() > 1) {
            elements = std::make_shared<std::vector<Value>>(*elements);
        }
    }
};

// ── Shared lazily-built key index ─────────────────────────────────────────
//
// DictionaryValue and RecordValue are both insertion-ordered
// vector<pair<string, Value>> stores fronted by a lazily-built hash index.
// EntryIndex centralises that index so the find / build / rebuild (and, for
// dictionaries, set / erase) logic lives here once instead of being duplicated
// per type.  The owning struct keeps its own storage vector under its natural
// public name (`entries` for dictionaries, `fields` for records) and passes it
// to each call, so existing direct-member call sites such as dict->entries and
// record->fields are unaffected.
struct EntryIndex {
    using Entries = std::vector<std::pair<std::string, Value>>;

    // Small containers are cheaper to scan linearly than to hash: a handful of
    // std::string comparisons beat allocating and populating the unordered_map,
    // so the lazy index is built only once a container grows past this
    // threshold.  Records and dictionaries with no more than this many entries
    // never pay for the map on a lookup.
    static constexpr std::size_t linear_scan_threshold = 8;

    // Look up `key`, lazily (re)building the index over `entries` if stale.
    [[nodiscard]] const Value* find(const Entries& entries, std::string_view key) const {
        // For a small, not-yet-indexed container a linear scan avoids building
        // (and heap-allocating) the hash index entirely.  An already-built
        // index is still used, and larger containers fall through to the O(1)
        // hashed lookup.
        if (index_.is_stale() && entries.size() <= linear_scan_threshold) {
            for (const auto& [k, v] : entries) {
                if (k == key) {
                    return &v;
                }
            }
            return nullptr;
        }
        const auto* idx = index_.find(key, [&entries](auto& map) { build(entries, map); });
        return idx ? &entries[*idx].second : nullptr;
    }

    // Rebuild the index from `entries`.  Call after any direct mutation of the
    // storage vector that bypasses set()/erase().
    void rebuild(const Entries& entries) {
        index_.rebuild([&entries](auto& map) { build(entries, map); });
    }

    // Insert or overwrite `key`, keeping the index consistent.
    void set(Entries& entries, const std::string& key, Value val) {
        if (index_.is_built()) {
            auto& map = index_.raw();
            if (auto it = map.find(key); it != map.end()) {
                entries[it->second].second = std::move(val);
                return;
            }
            index_.insert(key, entries.size());
            entries.emplace_back(key, std::move(val));
        } else {
            for (auto& [k, v] : entries) {
                if (k == key) {
                    v = std::move(val);
                    return;
                }
            }
            entries.emplace_back(key, std::move(val));
        }
    }

    // Remove `key` (if present) and invalidate the index.
    void erase(Entries& entries, std::string_view key) {
        std::erase_if(entries, [&](const auto& pair) { return pair.first == key; });
        if (index_.is_built()) {
            index_.invalidate();
        }
    }

private:
    static void build(const Entries& entries, auto& map) {
        map.reserve(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i) {
            map.emplace(entries[i].first, i);
        }
    }

    mutable LazyHashIndex<std::string, StringHash, std::equal_to<>> index_;
};

// Copy-on-write string-keyed dictionary.
//
// In the Value variant, DictionaryValue is stored inside a shared_ptr, so
// multiple Value instances can reference the same DictionaryValue without
// copying it.  Structural sharing is maintained until a mutation is needed,
// at which point the VM calls clone_dict() (in value_copying.cpp) to produce
// an independent copy before modifying it.
//
// ── When to clone ──
// Any operation that sets, erases, or reorders entries must ensure the
// shared_ptr has unique ownership before modifying.  Read-only operations
// (find, iteration, formatting) may work on the shared pointer directly.
//
// ── Thread safety ──
// Not thread-safe.  Each task receives its own deep copy of captured values
// when spawned or when values are sent across a Channel, ensuring no
// DictionaryValue is ever mutated concurrently by two OS threads.
struct DictionaryValue {
    // Insertion-ordered key-value pairs. Iteration always visits keys in
    // insertion order. Direct mutation of this vector must be followed by
    // rebuild_index() to keep the hash index consistent.
    std::vector<std::pair<std::string, Value>> entries;

    [[nodiscard]] const Value* find(std::string_view key) const {
        return index_.find(entries, key);
    }

    [[nodiscard]] Value* find(std::string_view key) {
        return const_cast<Value*>(std::as_const(*this).find(key));
    }

    // Rebuild the hash index from the current entries vector.
    // Must be called after any direct assignment to `entries`.
    void rebuild_index() {
        index_.rebuild(entries);
    }

    void set(const std::string& key, Value val) {
        index_.set(entries, key, std::move(val));
    }

    void erase(std::string_view key) {
        index_.erase(entries, key);
    }

private:
    mutable EntryIndex index_;
};

// Immutable, fixed-length sequence of Values with potentially heterogeneous types.
//
// TupleValue is stored as a shared_ptr in the Value variant, enabling structural
// sharing between Value copies.  Tuples are semantically immutable in Luma:
// there are no in-place mutation operations, so no clone_tuple() / ensure_unique()
// mechanism is needed.  If a new tuple must differ from an existing one, the
// compiler emits code to construct a fresh TupleValue.
//
// ── Thread safety ──
// Not thread-safe, but immutability means concurrent read access is safe as
// long as no thread constructs or destroys the TupleValue simultaneously.
// Tasks that receive a tuple via a Channel get a deep-copied Value, so they
// always hold an independent shared_ptr.
struct TupleValue {
    std::vector<Value> elements;
};

// Tagged wrapper for fallible computations — Luma's result<T> type.
//
// ── is_success ──
// When true, the result represents the success path: owned_inner holds the
// computed value of type T.  When false, the result is a failure: owned_inner
// holds the error value (typically a string message, but any Value is legal).
//
// ── owned_inner ──
// A shared_ptr<Value> to avoid embedding a Value directly inside ResultValue
// (which would complicate the variant layout).  The pointer is always
// non-null: success() sets it to the payload, failure() sets it to the error
// value.  Callers should dereference with *owned_inner rather than storing
// their own pointer.
//
// ── failure_location ──
// Optionally records the source location at which a failure was created.
// Set by failure() when an optional<SourceLocation> is supplied (e.g. from
// a stdlib function that knows the call site).  Used to produce richer
// error messages if the Luma program does not handle the failure.
struct ResultValue {
    bool is_success{true};
    std::shared_ptr<Value> owned_inner;
    bool has_failure_location{false};
    SourceLocation failure_location;

    // ── Structured error metadata ──
    // When is_success is false, these fields carry machine-readable
    // error context so Luma code can match on error categories rather
    // than parsing the human-readable message string.
    std::string error_code;      // e.g. "index_out_of_bounds", "division_by_zero"
    std::string source_function; // e.g. "Array.get", "Math.floor"

    [[nodiscard]] static std::shared_ptr<ResultValue> success(Value val);

    [[nodiscard]] static std::shared_ptr<ResultValue>
    failure(Value val, std::string error_code = {}, std::string source_function = {},
            std::optional<SourceLocation> location = std::nullopt);
};

// Runtime instance of a Luma record type.
//
// Stored as a shared_ptr<RecordValue> in the Value variant, enabling structural
// sharing between Value copies.  Before any field is mutated the VM calls
// clone_record() (in value_copying.cpp) to ensure exclusive ownership, following
// the same copy-on-write discipline used by ArrayValue and DictionaryValue.
//
// ── When to clone ──
// Any operation that assigns or replaces a field value must hold the only
// reference (use_count() == 1) before modifying.  Read-only field access
// (find_field, iteration, formatting) may use the shared pointer directly.
//
// ── Thread safety ──
// Not thread-safe.  Tasks that receive a record value via a Channel receive a
// deep copy, ensuring the fields vector is never mutated by two threads
// concurrently.
struct RecordValue {
    std::string type_name;
    std::vector<std::pair<std::string, Value>> fields;

    [[nodiscard]] const Value* find_field(std::string_view name) const {
        return index_.find(fields, name);
    }

    [[nodiscard]] Value* find_field(std::string_view name) {
        return const_cast<Value*>(std::as_const(*this).find_field(name));
    }

private:
    mutable EntryIndex index_;
};

struct RangeValue {
    std::int64_t start{0};
    std::int64_t end{0};
    bool inclusive{false};
};

// Runtime value of a Luma choice type (algebraic data type / discriminated union).
//
// ── type_name vs variant ──
// type_name names the choice type (e.g. "Shape"), while variant names the
// active constructor within that type (e.g. "Circle").  Both are stored
// because equality and pattern-matching must check that the types agree
// before comparing variants: two choice values are only comparable when they
// belong to the same choice type.  Storing the type name also allows
// display_type_name() and error messages to distinguish, for example,
// a Shape.Circle from a Color.Red even if both happen to use the same
// variant string.
//
// ── fields ──
// The positional payload values associated with the active constructor.
// For unit variants (no payload) the vector is empty.
struct ChoiceValue {
    [[nodiscard]] bool operator==(const ChoiceValue& other) const;

    std::string type_name;
    std::string variant;
    std::vector<Value> fields;
};

// Runtime closure — wraps a CompiledFunction (bytecode) with captured upvalues.
//
// This is the type stored in Value when a Luma function is referenced at
// runtime.  See CompiledFunction in chunk.hpp for the compile-time bytecode
// representation.
//
// ── upvalues vs upvalue_cells ──
// Captured variables come in two flavours depending on whether the source
// variable was declared mutable:
//
//   Immutable captures — stored directly in the `upvalues` vector.  The value
//   is copied into the slot at closure-creation time and never changes.  The
//   corresponding entry in `upvalue_cells` is null.
//
//   Mutable captures — stored in a heap-allocated shared cell (shared_ptr<Value>)
//   in `upvalue_cells`.  All closures that captured the same mutable variable
//   share the same cell, so any one of them can mutate it and all others see
//   the updated value immediately.  The corresponding entry in `upvalues` is
//   a default-constructed Value and should not be read.
//
// The two vectors are always the same length (== upvalue count of `compiled`).
// To read upvalue i: if upvalue_cells[i] != null, dereference the cell;
// otherwise read upvalues[i].
struct FunctionValue {
    std::string name;
    const CompiledFunction* compiled{nullptr};

    // Captured upvalues.  Immutable captures are stored directly in
    // `upvalues`.  Mutable captures use a shared cell (heap-allocated
    // Value) so that multiple closures closing over the same mutable
    // variable observe each other's mutations.  When `upvalue_cells[i]`
    // is non-null, the upvalue is mutable and the cell is authoritative;
    // `upvalues[i]` is unused in that case.
    std::vector<Value> upvalues;
    std::vector<std::shared_ptr<Value>> upvalue_cells;
};

// C++ function exposed to Luma code — used for all stdlib bindings.
// Wraps a NativeFunction callable with parameter/return type metadata
// so the VM can produce meaningful error messages on type mismatches.
struct NativeFunctionValue {
    std::string name;
    NativeFunction function;
    std::vector<std::string> parameter_types;
    std::string return_type;
};

struct TaskValue {
    TaskValue(std::shared_future<Value> f, std::shared_ptr<CancellationToken> ct)
        : future{std::move(f)}, token{std::move(ct)} {}

    // A shared_future (not a plain future) so that a task handle can be safely
    // copied across threads: each recipient holds its OWN shared_future object
    // over the same shared state.  Concurrently operating on a SINGLE
    // shared_future object from multiple threads is undefined, so deep_copy()
    // clones the wrapper to give every thread a distinct object (see
    // value_copying.cpp).
    std::shared_future<Value> future;
    std::shared_ptr<CancellationToken> token;
};

struct ChannelValue {
    explicit ChannelValue(std::shared_ptr<Channel> c) : channel{std::move(c)} {}

    std::shared_ptr<Channel> channel;
};

// Platform-independent socket handle.
#ifdef _WIN32
using SocketHandle = std::uint64_t; // SOCKET
constexpr SocketHandle invalid_socket_handle{~static_cast<SocketHandle>(0)};
#else
using SocketHandle = int;
constexpr SocketHandle invalid_socket_handle{-1};
#endif

// A socket's role in a connection: an active client peer or a passive
// listening server.  Passed to the SocketValue constructor so call sites
// read self-documentingly instead of a bare true/false.
enum class SocketRole {
    Client,
    Server
};

struct SocketValue {
    std::atomic<SocketHandle> handle{invalid_socket_handle};
    SocketRole role{SocketRole::Client};

    [[nodiscard]] bool is_valid() const noexcept {
        return handle.load(std::memory_order_acquire) != invalid_socket_handle;
    }

    void close_handle() noexcept;

    void close() noexcept {
        close_handle();
    }

    static void increment_count();
    static void decrement_count() noexcept;
    [[nodiscard]] static int open_count() noexcept;

    ~SocketValue() noexcept;
    SocketValue() = default;

    SocketValue(SocketHandle h, SocketRole socket_role) : handle{h}, role{socket_role} {
        if (is_valid()) {
            increment_count();
        }
    }

    SocketValue(const SocketValue&) = delete;
    SocketValue& operator=(const SocketValue&) = delete;

    // The handle is atomic, so ownership is transferred with an exchange rather
    // than std::exchange.  SocketValue is normally held via shared_ptr and never
    // moved, but the move operations are kept correct for completeness.
    SocketValue(SocketValue&& o) noexcept
        : handle{o.handle.exchange(invalid_socket_handle, std::memory_order_acq_rel)},
          role{o.role} {}

    SocketValue& operator=(SocketValue&& o) noexcept {
        if (this != &o) {
            close_handle();
            handle.store(o.handle.exchange(invalid_socket_handle, std::memory_order_acq_rel),
                         std::memory_order_release);
            role = o.role;
        }
        return *this;
    }
};

// An exact base-10 decimal value.  Wraps the immutable luma::Decimal so it can
// live in the Value variant behind a shared_ptr, mirroring the other opaque
// scalar-like handle types.  Immutable: every Decimal operation yields a new
// value, so the wrapper is freely shareable.
struct DecimalValue {
    Decimal value;

    DecimalValue() = default;

    explicit DecimalValue(Decimal decimal) : value{std::move(decimal)} {}
};

// ──────────── Collection subtypes ────────────

struct QueueValue : CollectionObject {
    QueueValue() : CollectionObject(CollectionKind::Queue) {}

    std::vector<Value> elements;

    [[nodiscard]] std::string to_display_string() const override;

    [[nodiscard]] std::string display_type_name() const override {
        return "queue";
    }

    [[nodiscard]] bool equals_to(const CollectionObject& other) const override;
    [[nodiscard]] Value deep_copy_value() const override;

    [[nodiscard]] std::size_t size() const override {
        return elements.size();
    }
};

struct StackValue : CollectionObject {
    StackValue() : CollectionObject(CollectionKind::Stack) {}

    std::vector<Value> elements;

    [[nodiscard]] std::string to_display_string() const override;

    [[nodiscard]] std::string display_type_name() const override {
        return "stack";
    }

    [[nodiscard]] bool equals_to(const CollectionObject& other) const override;
    [[nodiscard]] Value deep_copy_value() const override;

    [[nodiscard]] std::size_t size() const override {
        return elements.size();
    }
};

struct SetValue : CollectionObject {
    SetValue() : CollectionObject(CollectionKind::Set) {}

    std::vector<Value> elements;

    // Lazily-built hash index for O(1) element lookups (equality, contains).
    mutable LazyHashIndex<Value, ValueHash, ValueEqual> hash_index_;

    /// Invalidates the cached hash index.  Call after mutating elements.
    void invalidate_index() noexcept {
        hash_index_.invalidate();
    }

    /// Returns true if the set contains the given value (O(1) amortised).
    [[nodiscard]] bool contains(const Value& value) const {
        return hash_index_.find(value, [this](auto& idx) {
            // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): loop counter, not a real condition.
            for (std::size_t i{0}; i < elements.size(); ++i) {
                idx.emplace(elements[i], i);
            }
        }) != nullptr;
    }

    [[nodiscard]] std::string to_display_string() const override;

    [[nodiscard]] std::string display_type_name() const override {
        return "set";
    }

    [[nodiscard]] bool equals_to(const CollectionObject& other) const override;
    [[nodiscard]] Value deep_copy_value() const override;

    [[nodiscard]] std::size_t size() const override {
        return elements.size();
    }
};

struct XmlValue : CollectionObject {
    XmlValue() : CollectionObject(CollectionKind::Xml) {}

    enum class NodeType {
        Element,
        Text,
        Comment,
        CData
    };
    NodeType node_type{NodeType::Element};
    std::string tag_or_content;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<std::shared_ptr<XmlValue>> children;

    [[nodiscard]] std::shared_ptr<XmlValue> deep_clone() const;

    [[nodiscard]] std::string to_display_string() const override;

    [[nodiscard]] std::string display_type_name() const override {
        return "xml";
    }

    [[nodiscard]] EqualsKind equals_kind() const noexcept override {
        return EqualsKind::by_reference;
    }

    [[nodiscard]] Value deep_copy_value() const override;

    [[nodiscard]] std::size_t size() const override {
        return children.size();
    }
};

struct KeyValueStoreValue : CollectionObject {
    KeyValueStoreValue() : CollectionObject(CollectionKind::KeyValueStore) {}

    StringMap<std::string> entries;
    std::string file_path;
    bool read_only{false};
    mutable std::mutex mutex;

    [[nodiscard]] std::string to_display_string() const override;

    [[nodiscard]] std::string display_type_name() const override {
        return "key_value_store";
    }

    [[nodiscard]] EqualsKind equals_kind() const noexcept override {
        return EqualsKind::by_reference;
    }

    [[nodiscard]] Value deep_copy_value() const override;

    [[nodiscard]] std::size_t size() const override {
        const std::lock_guard lock{mutex};
        return entries.size();
    }
};

// ─── Node types ─────────────────────────────────────────────────────────────

struct ReferenceValue {
    explicit ReferenceValue(Value v) : value{std::make_shared<Value>(std::move(v))} {}

    std::shared_ptr<Value> value;
    // Protects get()/set() for cross-task access. deep_copy() intentionally
    // does NOT deep-copy the referenced cell (returns a shallow alias), so
    // it does not need the lock.
    mutable std::recursive_mutex mutex;

    [[nodiscard]] Value get() const {
        const std::lock_guard lock{mutex};
        return *value;
    }

    void set(Value v) {
        const std::lock_guard lock{mutex};
        *value = std::move(v);
    }
};

// ──────── Deferred Value constructor (requires complete CollectionObject hierarchy) ────────

template <CollectionSubtype T>
Value::Value(std::shared_ptr<T> v)
    : data_{std::shared_ptr<CollectionObject>{std::move(v)}},
      type_{detail::value_type_for<T>::value} {}

} // namespace luma

#endif // LUMA_INTERPRETER_VALUE_COLLECTIONS_HPP
