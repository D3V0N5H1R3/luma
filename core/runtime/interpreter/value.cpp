#include <atomic>
#include <cassert>
#include <optional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#else
#include <unistd.h>
#endif

#include "analysis/errors/error.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"

// ── Memory ownership ──
// Value uses two memory models: inline value semantics for primitives
// (null, bool, integer, number, string) stored directly in the variant,
// and shared_ptr with copy-on-write for compound values (arrays,
// dictionaries, records, ...), which are shared until mutated then cloned.

namespace luma {

// ─────────── ResultValue factory methods ───────────

std::shared_ptr<ResultValue> ResultValue::success(Value val) {
    auto rv = std::make_shared<ResultValue>();
    rv->is_success = true;
    rv->owned_inner = std::make_shared<Value>(std::move(val));
    return rv;
}

std::shared_ptr<ResultValue> ResultValue::failure(Value val, std::string error_code,
                                                  std::string source_function,
                                                  std::optional<SourceLocation> location) {
    auto rv = std::make_shared<ResultValue>();
    rv->is_success = false;
    rv->owned_inner = std::make_shared<Value>(std::move(val));
    if (location.has_value()) {
        rv->has_failure_location = true;
        rv->failure_location = *location;
    }
    rv->error_code = std::move(error_code);
    rv->source_function = std::move(source_function);
    return rv;
}

// ─────────── Value::is_truthy ───────────

bool Value::is_truthy() const {
    if (is_bool()) {
        return as_bool();
    }

    if (is_integer()) {
        return as_integer() != 0;
    }

    if (is_null()) {
        return false;
    }

    if (is_number()) {
        return as_number() != 0.0;
    }

    if (is_string()) {
        return !as_string().empty();
    }

    return true; // all other types are truthy
}

// ─────────── SocketValue RAII ───────────

namespace {

std::atomic<int> g_open_socket_count{0};

} // namespace

void SocketValue::increment_count() {
    const int prev = g_open_socket_count.fetch_add(1, std::memory_order_acq_rel);
    if (prev >= ResourceLimits::max_open_sockets) {
        g_open_socket_count.fetch_sub(1, std::memory_order_relaxed);
        throw RuntimeError{"socket limit exceeded: too many open sockets", {}};
    }
}

void SocketValue::decrement_count() noexcept {
    g_open_socket_count.fetch_sub(1, std::memory_order_relaxed);
}

int SocketValue::open_count() noexcept {
    return g_open_socket_count.load(std::memory_order_relaxed);
}

void SocketValue::close_handle() noexcept {
    // Atomically take ownership of the handle so exactly one thread closes the
    // underlying fd and decrements the open-socket count, even when the same
    // SocketValue is shared across tasks (e.g. sent over a channel) and closed
    // concurrently.  A double ::close would be unsafe: the fd number may have
    // been reused by another socket in the meantime.
    const SocketHandle previous = handle.exchange(invalid_socket_handle, std::memory_order_acq_rel);
    if (previous == invalid_socket_handle) {
        return;
    }

#ifdef _WIN32
    closesocket(previous);
#else
    ::close(previous);
#endif
    decrement_count();
}

SocketValue::~SocketValue() noexcept {
    if (is_valid()) {
        close_handle();
    }
}

// ─────────── Debug-only type consistency check ───────────

#ifndef NDEBUG
void Value::verify_type_consistency() const {
    // Map the active variant index to the expected ValueType.
    // The order must match the Value::Variant alternative list.
    static constexpr ValueType k_index_to_type[] = {
        ValueType::Null,           // 0: NullValue
        ValueType::Bool,           // 1: bool
        ValueType::Integer,        // 2: int64_t
        ValueType::Number,         // 3: double
        ValueType::String,         // 4: string
        ValueType::Array,          // 5: shared_ptr<ArrayValue>
        ValueType::Dictionary,     // 6: shared_ptr<DictionaryValue>
        ValueType::Tuple,          // 7: shared_ptr<TupleValue>
        ValueType::Result,         // 8: shared_ptr<ResultValue>
        ValueType::Record,         // 9: shared_ptr<RecordValue>
        ValueType::Range,          // 10: shared_ptr<RangeValue>
        ValueType::Choice,         // 11: shared_ptr<ChoiceValue>
        ValueType::Function,       // 12: shared_ptr<FunctionValue>
        ValueType::NativeFunction, // 13: shared_ptr<NativeFunctionValue>
        ValueType::Task,           // 14: shared_ptr<TaskValue>
        ValueType::Channel,        // 15: shared_ptr<ChannelValue>
        ValueType::Socket,         // 16: shared_ptr<SocketValue>
        // 17: shared_ptr<CollectionObject> — resolved via CollectionKind below
        ValueType::Null,      // 17: placeholder for CollectionObject
        ValueType::Reference, // 18: shared_ptr<ReferenceValue>
        ValueType::Decimal,   // 19: shared_ptr<DecimalValue>
    };

    const auto idx = data_.index();

    if (idx == 17) {
        // CollectionObject — type_ should match the concrete subtype's kind.
        // We trust type_ here because the constructor sets it from
        // value_type_for<T>::value which is known correct at compile time.
        return;
    }

    assert(idx < std::size(k_index_to_type) &&
           "Value variant index out of range for type consistency check");
    assert(type_ == k_index_to_type[idx] && "Value type_ out of sync with variant data");
}
#endif

} // namespace luma
