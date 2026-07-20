#ifndef LUMA_STDLIB_KEYVALUESTORE_CODEC_HPP
#define LUMA_STDLIB_KEYVALUESTORE_CODEC_HPP

#include <string>
#include <string_view>

#include "common/string_hash.hpp"

// Pure KeyValueStore codec layer, free of any Value, environment or filesystem
// dependencies.  These routines are the trust-boundary decoder behind
// KeyValueStore.open / open_read_only / reload (via read_store) and the glob
// matcher behind KeyValueStore.find_by_pattern.  They are exposed in this
// header — rather than kept file-local in keyvaluestore_module.cpp — so the
// fuzz target (fuzz/fuzz_keyvaluestore.cpp) can drive them directly, mirroring
// how the CSV, compression and DateTime codecs are fuzzed.
//
// The `.kv` on-disk format is one `escape(key)\tescape(value)\n` record per
// line.  escape renders '\t', '\n' and '\\' as the two-character sequences
// \t, \n and \\ so that every record stays on a single line and the first
// literal tab is always the key/value separator; unescape is its exact inverse,
// so unescape(escape(s)) == s for every byte string s.  Carriage returns are
// deliberately left untouched (not part of the escape set), so a value may
// contain a literal '\r' and still round-trip.
//
// parse_store never returns partial state silently: malformed lines (empty or
// lacking a tab) are skipped, but content exceeding the dictionary-size or
// string-size resource limits throws RuntimeError, exactly as the original
// file reader did.  serialize_store is the matching encoder and guarantees
// parse_store(serialize_store(entries)) == entries.

namespace luma::kvs {

using StoreEntries = StringMap<std::string>;

// Escape a key or value for the single-line `.kv` record format.
[[nodiscard]] std::string escape(const std::string& s);

// Append the escaped form of `s` directly onto `out`.  Equivalent to
// `out += escape(s)` but without the throwaway intermediate string — used by
// serialize_store to encode every key and value in place.
void escape_into(std::string& out, std::string_view s);

// Inverse of escape.  unescape(escape(s)) == s for every byte string s.
[[nodiscard]] std::string unescape(const std::string& s);

// Parse `.kv` file text into entries following the one-record-per-line format.
// Empty lines and lines without a tab separator are skipped.  Throws
// luma::RuntimeError when the content exceeds ResourceLimits::max_dictionary_size
// (entry count) or ResourceLimits::max_string_size (a single key or value).
[[nodiscard]] StoreEntries parse_store(const std::string& text);

// Serialise entries back to `.kv` file text.  Each entry becomes one
// `escape(key)\tescape(value)\n` line, so the result re-parses to the same
// entries via parse_store.
[[nodiscard]] std::string serialize_store(const StoreEntries& entries);

// Glob-style pattern matching supporting '*' (any sequence, including empty)
// and '?' (exactly one character).  All other characters match literally.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view text);

} // namespace luma::kvs

#endif // LUMA_STDLIB_KEYVALUESTORE_CODEC_HPP
