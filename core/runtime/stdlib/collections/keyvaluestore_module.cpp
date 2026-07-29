#include "runtime/stdlib/collections/keyvaluestore_module.hpp"

#include <concepts>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/escape.hpp"
#include "common/resource_limits.hpp"
#include "common/string_utils.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/keyvaluestore_codec.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/native_function_containers.hpp"
#include "runtime/stdlib/common/path_validator.hpp"

namespace luma::kvs {

// ─── File format codec ───────────────────────────────────────────────────────
// Format: escape(key)\tescape(value)\n per entry.  Tabs, newlines and
// backslashes are escaped so each record stays on a single line; see
// keyvaluestore_codec.hpp for the full contract.

namespace {

// Escape policy for the single-line `.kv` record format: render the three bytes
// that would otherwise break the one-record-per-line framing ('\t', '\n', and
// the '\\' escape lead-in) as two-character sequences, and pass every other
// byte through unchanged.  unescape() is its exact inverse.
struct KeyValueStoreEscapePolicy {
    static void escape_char(char ch, std::string& out) {
        switch (ch) {
            case '\t':
                out += "\\t";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\\':
                out += "\\\\";
                break;
            default:
                out += ch;
                break;
        }
    }
};

} // namespace

void escape_into(std::string& out, std::string_view s) {
    escape_string_impl<KeyValueStoreEscapePolicy>(s, out);
}

std::string escape(const std::string& s) {
    std::string out{};
    out.reserve(s.size());
    escape_into(out, s);
    return out;
}

std::string unescape(const std::string& s) {
    std::string out{};
    out.reserve(s.size());

    for (std::size_t i{0}; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case 't':
                    out += '\t';
                    ++i;
                    break;
                case 'n':
                    out += '\n';
                    ++i;
                    break;
                case '\\':
                    out += '\\';
                    ++i;
                    break;
                default:
                    out += s[i];
                    break;
            }
        } else {
            out += s[i];
        }
    }

    return out;
}

StoreEntries parse_store(const std::string& text) {
    StoreEntries entries{};
    std::size_t start{0};

    while (start <= text.size()) {
        const auto newline = text.find('\n', start);
        const auto stop = (newline == std::string::npos) ? text.size() : newline;
        const std::string_view line{text.data() + start, stop - start};

        if (!line.empty()) {
            const auto tab = line.find('\t');

            if (tab != std::string_view::npos) {
                if (entries.size() >= ResourceLimits::max_dictionary_size) {
                    throw RuntimeError{
                        "KeyValueStore file exceeds maximum entry count", SourceLocation{},
                        "the store file contains more entries than max_dictionary_size allows"};
                }

                auto key = unescape(std::string{line.substr(0, tab)});
                auto value = unescape(std::string{line.substr(tab + 1)});

                if (key.size() > ResourceLimits::max_string_size ||
                    value.size() > ResourceLimits::max_string_size) {
                    throw RuntimeError{
                        "KeyValueStore entry exceeds maximum string size", SourceLocation{},
                        "a key or value in the store file is larger than max_string_size allows"};
                }

                entries[std::move(key)] = std::move(value);
            }
        }

        if (newline == std::string::npos) {
            break;
        }

        start = newline + 1;
    }

    return entries;
}

std::string serialize_store(const StoreEntries& entries) {
    // Pre-size the output to the unescaped total (escaping only grows it) plus
    // the per-entry '\t' and '\n' separators, so the append loop reallocates at
    // most a handful of times regardless of entry count.
    std::size_t estimate{0};
    for (const auto& [key, value] : entries) {
        estimate += key.size() + value.size() + 2;
    }

    std::string out{};
    out.reserve(estimate);

    for (const auto& [key, value] : entries) {
        escape_into(out, key);
        out += '\t';
        escape_into(out, value);
        out += '\n';
    }

    return out;
}

// Glob-style pattern matching supporting '*' (any sequence) and '?' (one char).
// Delegates to the shared linear matcher in common/string_utils.hpp so the
// backtracking-free algorithm lives in one place; kvs::glob_match remains the
// public, fuzzed entry point behind KeyValueStore.find_by_pattern.
bool glob_match(std::string_view pattern, std::string_view text) {
    return luma::glob_match(pattern, text);
}

} // namespace luma::kvs

namespace luma {

namespace {

// Read a .kv file into a map via the shared parser.  parse_store enforces the
// dictionary-size and per-string limits, but only AFTER the whole file has been
// slurped into memory, so without an up-front bound a multi-gigabyte store file
// would be fully materialised first.  Cap the on-disk size before reading a
// single byte (the same guard FileSystem.read_file and Hash.*_file use) and
// treat an oversize or size-indeterminate file as unreadable — returning an
// empty map, exactly like a file that cannot be opened.
[[nodiscard]] kvs::StoreEntries read_store(const std::filesystem::path& path) {
    std::error_code size_ec;
    const auto file_bytes = std::filesystem::file_size(path, size_ec);

    if (size_ec || file_bytes > ResourceLimits::max_string_size) {
        return {};
    }

    std::ifstream file{path};

    if (!file.is_open()) {
        return {};
    }

    const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

    return kvs::parse_store(text);
}

// Write a map to a .kv file (atomic: write to temp, then rename)
[[nodiscard]] bool write_store(const std::filesystem::path& path,
                               const kvs::StoreEntries& entries) {
    namespace fs = std::filesystem;

    // Temp path is derived from the already-validated path by appending ".tmp",
    // which keeps it in the same directory — no additional path validation needed.
    const auto temp_path = path.string() + ".tmp";

    {
        std::ofstream file{temp_path};

        if (!file.is_open()) {
            return false;
        }

        file << kvs::serialize_store(entries);

        if (file.fail()) {
            return false;
        }
    }

    std::error_code ec{};

    fs::rename(temp_path, path, ec);

    if (ec) {
        // Fallback: try direct copy
        try {
            fs::copy_file(temp_path, path, fs::copy_options::overwrite_existing, ec);

            if (ec) {
                return false;
            }

            // Clean up temp file (best effort)
            std::error_code cleanup_ec{};
            fs::remove(temp_path, cleanup_ec);
        } catch (const std::exception&) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::shared_ptr<KeyValueStoreValue>
clone_store(const std::shared_ptr<KeyValueStoreValue>& src) {
    const std::scoped_lock lock{src->mutex};

    auto copy = std::make_shared<KeyValueStoreValue>();
    copy->entries = src->entries;
    copy->file_path = src->file_path;
    copy->read_only = src->read_only;

    return copy;
}

// Failure result for a mutating or persistence call on a read-only store.
[[nodiscard]] Value read_only_failure(std::string_view function) {
    return make_failure_value(error_msg("KeyValueStore", function, "store is read-only"));
}

// Failure result for a persistence call on a store with no backing file path.
[[nodiscard]] Value no_path_failure(std::string_view function) {
    return make_failure_value(error_msg("KeyValueStore", function, "no file path set"));
}

// Apply an in-place mutation to a private copy-on-write clone of a writable
// store and return it as a success result.  Centralises the read-only guard,
// clone and success wrapping shared by set/remove/set_many/clear.
template <typename Mutate>
    requires std::invocable<Mutate, kvs::StoreEntries&>
[[nodiscard]] Value mutate_store(std::string_view function,
                                 const std::shared_ptr<KeyValueStoreValue>& store,
                                 Mutate&& mutate) {
    if (store->read_only) {
        return read_only_failure(function);
    }

    auto copy = clone_store(store);
    std::forward<Mutate>(mutate)(copy->entries);

    return make_success_value(Value{std::move(copy)});
}

} // anonymous namespace

// ─── Registration ────────────────────────────────────────────────────────────

void register_keyvaluestore_ns(const EnvPtr& env) {
    ModuleBuilder{"KeyValueStore", env} // KeyValueStore.open(path) -> result<key_value_store>
        .func("open", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "KeyValueStore.open", loc);

            const auto safe = validate_path(args[0].as_string(), loc);

            auto store = std::make_shared<KeyValueStoreValue>();
            store->file_path = safe.string();

            if (std::filesystem::exists(safe)) {
                store->entries = read_store(safe);
            }

            return make_success_value(Value{std::move(store)});
        })
        // KeyValueStore.open_read_only(path) -> result<key_value_store>
        .func("open_read_only", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "KeyValueStore.open_read_only", loc);

            const auto safe = validate_path(args[0].as_string(), loc);

            auto store = std::make_shared<KeyValueStoreValue>();
            store->file_path = safe.string();
            store->read_only = true;

            if (std::filesystem::exists(safe)) {
                store->entries = read_store(safe);
            }

            return make_success_value(Value{std::move(store)});
        })
        // KeyValueStore.get(store, key) -> result<string>
        .func("get", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.get", loc);
            const auto& key = expect_string(args[1], "KeyValueStore.get", loc);
            const std::scoped_lock lock{store->mutex};

            auto it = store->entries.find(key);

            if (it == store->entries.end()) {
                return make_failure_value(
                    ErrorMessages::key_not_found("KeyValueStore", "get", key));
            }

            return make_success_value(Value{it->second});
        })
        // KeyValueStore.get_or(store, key, default) -> string
        .func("get_or", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.get_or", loc);
            const auto& key = expect_string(args[1], "KeyValueStore.get_or", loc);
            const auto& fallback = expect_string(args[2], "KeyValueStore.get_or", loc);
            const std::scoped_lock lock{store->mutex};

            auto it = store->entries.find(key);

            if (it == store->entries.end()) {
                return Value{fallback};
            }

            return Value{it->second};
        })
        // KeyValueStore.set(store, key, value) -> result<key_value_store>
        .func("set", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.set", loc);
            const auto& key = expect_string(args[1], "KeyValueStore.set", loc);
            const auto& value = expect_string(args[2], "KeyValueStore.set", loc);

            return mutate_store("set", store, [&](kvs::StoreEntries& entries) {
                if (!entries.contains(key) &&
                    entries.size() >= ResourceLimits::max_dictionary_size) {
                    throw RuntimeError{"KeyValueStore exceeds maximum entry count", loc,
                                       "the store already holds max_dictionary_size entries"};
                }

                entries[key] = value;
            });
        })
        // KeyValueStore.remove(store, key) -> result<key_value_store>
        .func("remove", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.remove", loc);
            const auto& key = expect_string(args[1], "KeyValueStore.remove", loc);

            return mutate_store("remove", store,
                                [&](kvs::StoreEntries& entries) { entries.erase(key); });
        })
        // KeyValueStore.has(store, key) -> boolean
        .func("has", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.has", loc);
            const auto& key = expect_string(args[1], "KeyValueStore.has", loc);
            const std::scoped_lock lock{store->mutex};

            return Value{store->entries.contains(key)};
        })
        // KeyValueStore.is_empty(store) -> boolean
        .func("is_empty", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.is_empty", loc);
            const std::scoped_lock lock{store->mutex};

            return Value{store->entries.empty()};
        })
        // KeyValueStore.update(store, key, fn(optional<string>) -> string) -> result<key_value_store>
        // Read-modify-write for a single key: the updater receives the current
        // value, or none when the key is absent, and returns the new value.
        .func("update", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.update", loc);
            const auto& key = expect_string(args[1], "KeyValueStore.update", loc);
            expect_callable(args[2], "KeyValueStore.update", loc);

            if (store->read_only) {
                return read_only_failure("update");
            }

            // Read the current value (or none) under the lock, then release it
            // before invoking the callback so a re-entrant store call cannot
            // deadlock on the same mutex.
            std::optional<std::string> current;
            {
                const std::scoped_lock lock{store->mutex};
                auto it = store->entries.find(key);

                if (it != store->entries.end()) {
                    current = it->second;
                }
            }

            std::vector<Value> call_args(1);
            call_args[0] = current.has_value() ? Value{*current} : Value{NullValue{}};
            auto new_value = invoke_callable(args[2], call_args, loc);
            const auto& new_string = expect_string(new_value, "KeyValueStore.update", loc);

            return mutate_store("update", store, [&](kvs::StoreEntries& entries) {
                if (!entries.contains(key) &&
                    entries.size() >= ResourceLimits::max_dictionary_size) {
                    throw RuntimeError{"KeyValueStore exceeds maximum entry count", loc,
                                       "the store already holds max_dictionary_size entries"};
                }

                entries[key] = new_string;
            });
        })
        // KeyValueStore.set_many(store, dictionary) -> result<key_value_store>
        .func("set_many", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.set_many", loc);
            const auto& dict = expect_dict(args[1], "KeyValueStore.set_many", loc);

            return mutate_store("set_many", store, [&](kvs::StoreEntries& entries) {
                for (const auto& [k, v] : dict->entries) {
                    if (!entries.contains(k) &&
                        entries.size() >= ResourceLimits::max_dictionary_size) {
                        throw RuntimeError{"KeyValueStore exceeds maximum entry count", loc,
                                           "the store already holds max_dictionary_size entries"};
                    }

                    entries[k] = v.to_string();
                }
            });
        })
        // KeyValueStore.get_many(store, keys_array) -> dictionary<string>
        .func("get_many", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.get_many", loc);
            const auto& keys = expect_array(args[1], "KeyValueStore.get_many", loc);
            const std::scoped_lock lock{store->mutex};

            auto dict = std::make_shared<DictionaryValue>();
            // Pre-build the empty hash index so each set() below is O(1), keeping
            // the build O(n) rather than O(n^2).
            dict->rebuild_index();

            for (const auto& key_val : *keys->elements) {
                if (!key_val.is_string()) {
                    continue;
                }

                const auto& key = key_val.as_string();

                auto it = store->entries.find(key);

                if (it != store->entries.end()) {
                    dict->set(key, Value{it->second});
                }
            }

            return Value{std::move(dict)};
        })
        // KeyValueStore.keys(store) -> array<string>
        .func("keys", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.keys", loc);
            const std::scoped_lock lock{store->mutex};

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& [k, _] : store->entries) {
                arr->elements->emplace_back(k);
            }

            return Value{std::move(arr)};
        })
        // KeyValueStore.values(store) -> array<string>
        .func("values", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.values", loc);
            const std::scoped_lock lock{store->mutex};

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& [_, v] : store->entries) {
                arr->elements->emplace_back(v);
            }

            return Value{std::move(arr)};
        })
        // KeyValueStore.to_dictionary(store) -> dictionary<string>
        .func("to_dictionary", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.to_dictionary", loc);
            const std::scoped_lock lock{store->mutex};

            auto dict = std::make_shared<DictionaryValue>();
            // Pre-build the empty hash index so each set() below is O(1), keeping
            // the build O(n) rather than O(n^2).
            dict->rebuild_index();

            for (const auto& [k, v] : store->entries) {
                dict->set(k, Value{v});
            }

            return Value{std::move(dict)};
        })
        // KeyValueStore.count(store) -> integer
        .func("count", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.count", loc);
            const std::scoped_lock lock{store->mutex};

            return Value{static_cast<std::int64_t>(store->entries.size())};
        })
        // KeyValueStore.save(store) -> result<string>
        .func("save", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.save", loc);

            if (store->read_only) {
                return read_only_failure("save");
            }

            const std::scoped_lock lock{store->mutex};

            if (store->file_path.empty()) {
                return no_path_failure("save");
            }

            if (!write_store(store->file_path, store->entries)) {
                return make_failure_value(error_msg(
                    "KeyValueStore", "save", std::format("cannot write '{}'", store->file_path)));
            }

            return make_success_value(Value{store->file_path});
        })
        // KeyValueStore.reload(store) -> result<key_value_store>
        .func("reload", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& old = expect_key_value_store(args[0], "KeyValueStore.reload", loc);

            if (old->file_path.empty()) {
                return no_path_failure("reload");
            }

            auto store = std::make_shared<KeyValueStoreValue>();
            store->file_path = old->file_path;
            store->read_only = old->read_only;
            store->entries = read_store(old->file_path);

            return make_success_value(Value{std::move(store)});
        })
        // KeyValueStore.clear(store) -> result<key_value_store>
        .func("clear", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.clear", loc);

            return mutate_store("clear", store,
                                [](kvs::StoreEntries& entries) { entries.clear(); });
        })
        // KeyValueStore.destroy(store) -> result<string>
        .func("destroy", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.destroy", loc);

            if (store->read_only) {
                return read_only_failure("destroy");
            }

            if (store->file_path.empty()) {
                return no_path_failure("destroy");
            }

            std::error_code ec{};

            std::filesystem::remove(store->file_path, ec);

            if (ec) {
                return make_failure_value(error_msg(
                    "KeyValueStore", "destroy",
                    std::format("cannot delete '{}': {}", store->file_path, ec.message())));
            }

            return make_success_value(Value{store->file_path});
        })
        // KeyValueStore.find_by_pattern(store, pattern) -> dictionary<string>
        .func("find_by_pattern", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& store =
                expect_key_value_store(args[0], "KeyValueStore.find_by_pattern", loc);
            const auto& pattern = expect_string(args[1], "KeyValueStore.find_by_pattern", loc);
            const std::scoped_lock lock{store->mutex};

            auto dict = std::make_shared<DictionaryValue>();
            // Pre-build the empty hash index so each set() below is O(1), keeping
            // the build O(n) rather than O(n^2).
            dict->rebuild_index();

            for (const auto& [k, v] : store->entries) {
                if (kvs::glob_match(pattern, k)) {
                    dict->set(k, Value{v});
                }
            }

            return Value{std::move(dict)};
        })
        // KeyValueStore.is_read_only(store) -> boolean
        .func("is_read_only", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.is_read_only", loc);

            return Value{store->read_only};
        });
}

} // namespace luma
