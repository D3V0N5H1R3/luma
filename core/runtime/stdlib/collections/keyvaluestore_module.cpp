#include "runtime/stdlib/collections/keyvaluestore_module.hpp"

#include <atomic>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/escape.hpp"
#include "common/path_utils.hpp"
#include "common/resource_limits.hpp"
#include "common/result.hpp"
#include "common/string_utils.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/keyvaluestore_codec.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/file_helpers.hpp"
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

// Read an EXISTING .kv file into a map, or explain why it cannot be read
// completely.  Every failure is reported — never mapped to an empty store:
// open/reload used to treat an oversize, unreadable or over-limit file as
// empty, and the next save/commit then silently overwrote the real data.
//
//   * The read is bounded at read time (read_file_bounded), so an oversize
//     file is rejected without being materialised.
//   * A non-empty line without the key/value tab separator is corruption (the
//     encoder never writes one), so the file is rejected rather than having
//     that line silently dropped on the next save.  Blank lines carry no data
//     and are ignored, exactly as parse_store does.
//   * parse_store's entry-count / string-size limit errors become failures.
[[nodiscard]] Result<kvs::StoreEntries, std::string> read_store(const std::filesystem::path& path) {
    using ReadResult = Result<kvs::StoreEntries, std::string>;

    const auto display = path_to_utf8(path);
    auto read = file_helpers::read_file_bounded(path, ResourceLimits::max_string_size);

    switch (read.status) {
        case file_helpers::BoundedReadStatus::Ok:
            break;
        case file_helpers::BoundedReadStatus::TooLarge:
            return ReadResult::err(
                std::format("store file '{}' exceeds the maximum size of {} bytes", display,
                            ResourceLimits::max_string_size));
        case file_helpers::BoundedReadStatus::OpenFailed:
            return ReadResult::err(std::format("cannot open store file '{}'", display));
        case file_helpers::BoundedReadStatus::ReadError:
            return ReadResult::err(std::format("error reading store file '{}'", display));
    }

    const std::string_view text{read.data};
    std::size_t line_number{1};

    for (std::size_t start{0}; start < text.size(); ++line_number) {
        const auto newline = text.find('\n', start);
        const auto stop = (newline == std::string_view::npos) ? text.size() : newline;
        const auto line = text.substr(start, stop - start);

        if (!line.empty() && line.find('\t') == std::string_view::npos) {
            return ReadResult::err(
                std::format("store file '{}' is corrupt: line {} has no key/value separator",
                            display, line_number));
        }

        start = stop + 1;
    }

    try {
        return ReadResult::ok(kvs::parse_store(read.data));
    } catch (const RuntimeError& e) {
        return ReadResult::err(std::format("store file '{}' is invalid: {}", display, e.what()));
    }
}

// Load the backing file of a store being opened / reloaded.  A missing file is
// the only case that yields an empty store; every other problem is a failure
// result naming `function`, so the caller never gets (and later saves over) an
// empty stand-in for data it could not read.
[[nodiscard]] Result<kvs::StoreEntries, Value>
load_existing_store(const std::filesystem::path& path, std::string_view function) {
    using LoadResult = Result<kvs::StoreEntries, Value>;

    std::error_code ec;

    if (!std::filesystem::exists(path, ec) && !ec) {
        return LoadResult::ok(kvs::StoreEntries{});
    }

    auto read = read_store(path);

    if (!read.is_ok()) {
        return LoadResult::err(
            make_failure_value(error_msg("KeyValueStore", function, std::move(read).error())));
    }

    return LoadResult::ok(std::move(read).value());
}

// A temp-file path next to `path` that no other writer — another task in this
// interpreter or another process — will pick: the shared "<path>.tmp" name let
// two concurrent saves of the same store write into (and rename) one file.  A
// per-process random nonce, the writing thread, and a process-wide counter make
// the name unique.  Being in the same directory keeps the final rename atomic
// and needs no extra path validation (the store path is already validated).
[[nodiscard]] std::filesystem::path unique_temp_path(const std::filesystem::path& path) {
    static const std::uint64_t process_nonce = [] {
        std::random_device device;

        return (static_cast<std::uint64_t>(device()) << 32) ^ device();
    }();
    static std::atomic<std::uint64_t> counter{0};

    const auto thread_tag = std::hash<std::thread::id>{}(std::this_thread::get_id());

    auto temp = path;
    temp += path_from_utf8(std::format(".{:x}.{:x}.{}.tmp", process_nonce, thread_tag,
                                       counter.fetch_add(1, std::memory_order_relaxed)));

    return temp;
}

// Write a map to a .kv file atomically: write and flush a uniquely named temp
// file, then rename it over the target.  A failure at any step removes the temp
// file and reports false, leaving the previous store untouched.  There is
// deliberately NO copy fallback when the rename fails: copy_file overwrites the
// target in place, so a crash mid-copy would leave a truncated store — exactly
// what the temp-and-rename dance exists to prevent.
[[nodiscard]] bool write_store(const std::filesystem::path& path,
                               const kvs::StoreEntries& entries) {
    namespace fs = std::filesystem;

    const auto temp_path = unique_temp_path(path);

    const auto discard_temp = [&temp_path]() {
        std::error_code cleanup_ec{};
        fs::remove(temp_path, cleanup_ec);
    };

    {
        std::ofstream file{temp_path};

        if (!file.is_open()) {
            return false;
        }

        file << kvs::serialize_store(entries);
        file.flush();

        if (!file) {
            file.close();
            discard_temp();

            return false;
        }

        file.close();

        if (file.fail()) {
            discard_temp();

            return false;
        }
    }

    std::error_code ec{};

    fs::rename(temp_path, path, ec);

    if (ec) {
        discard_temp();

        return false;
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

            // A missing file starts an empty store; an existing file that cannot
            // be read completely is a failure (see load_existing_store).
            auto loaded = load_existing_store(safe, "open");

            if (!loaded.is_ok()) {
                return std::move(loaded).error();
            }

            auto store = std::make_shared<KeyValueStoreValue>();
            store->file_path = path_to_utf8(safe);
            store->entries = std::move(loaded).value();

            return make_success_value(Value{std::move(store)});
        })
        // KeyValueStore.open_read_only(path) -> result<key_value_store>
        .func("open_read_only", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "KeyValueStore.open_read_only", loc);

            const auto safe = validate_path(args[0].as_string(), loc);

            // A read-only store can never gain entries, so opening a missing
            // file (usually a mistyped path) fails instead of yielding an empty
            // store.
            if (!std::filesystem::exists(safe)) {
                return make_failure_value(
                    error_msg("KeyValueStore", "open_read_only",
                              std::format("store file not found: {}", args[0].as_string())));
            }

            auto loaded = read_store(safe);

            if (!loaded.is_ok()) {
                return make_failure_value(
                    error_msg("KeyValueStore", "open_read_only", std::move(loaded).error()));
            }

            auto store = std::make_shared<KeyValueStoreValue>();
            store->file_path = path_to_utf8(safe);
            store->read_only = true;
            store->entries = std::move(loaded).value();

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

            if (!write_store(path_from_utf8(store->file_path), store->entries)) {
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

            // Same rule as open: a missing file reloads as empty, an unreadable
            // one is a failure (the caller keeps its current store).
            auto loaded = load_existing_store(path_from_utf8(old->file_path), "reload");

            if (!loaded.is_ok()) {
                return std::move(loaded).error();
            }

            auto store = std::make_shared<KeyValueStoreValue>();
            store->file_path = old->file_path;
            store->read_only = old->read_only;
            store->entries = std::move(loaded).value();

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

            std::filesystem::remove(path_from_utf8(store->file_path), ec);

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
        // KeyValueStore.transaction(store, apply) -> result<key_value_store>
        // Runs a transaction body that threads a sequence of operations over the
        // store and returns success(final_store) to commit or failure(msg) to
        // roll back.  Because every mutating operation returns a new store
        // (copy-on-write), the body must thread the store through each step
        // (typically with the ? operator) and return the final one.  On commit a
        // file-backed writable store is persisted atomically; on rollback nothing
        // is written and the original store and its file are left untouched.
        .func("transaction", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_key_value_store(args[0], "KeyValueStore.transaction", loc);
            expect_callable(args[1], "KeyValueStore.transaction", loc);

            // Run the transaction body with the current store as its argument.
            std::vector<Value> call_args(1);
            call_args[0] = args[0];
            Value outcome = invoke_callable(args[1], call_args, loc);

            if (!outcome.is_result()) {
                return make_failure_value(error_msg("KeyValueStore", "transaction",
                                                    "transaction body must return "
                                                    "result<key_value_store>"));
            }

            const auto& result = outcome.as_result();

            // Rollback: the body failed — nothing is persisted and the original
            // store is untouched (all operations are copy-on-write).
            if (!result->is_success) {
                return outcome;
            }

            // Commit: the body returned success(final_store).
            const Value& committed_value = *result->owned_inner;
            const auto& committed =
                expect_key_value_store(committed_value, "KeyValueStore.transaction", loc);

            // Persist atomically when the committed store is file-backed and writable.
            if (!committed->read_only && !committed->file_path.empty()) {
                const std::scoped_lock lock{committed->mutex};

                if (!write_store(path_from_utf8(committed->file_path), committed->entries)) {
                    return make_failure_value(
                        error_msg("KeyValueStore", "transaction",
                                  std::format("cannot write '{}'", committed->file_path)));
                }
            }

            return make_success_value(committed_value);
        })
        // KeyValueStore.is_read_only(store) -> boolean
        .func("is_read_only", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& store = expect_key_value_store(args[0], "KeyValueStore.is_read_only", loc);

            return Value{store->read_only};
        });
}

} // namespace luma
