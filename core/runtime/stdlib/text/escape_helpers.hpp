#ifndef LUMA_STDLIB_ESCAPE_HELPERS_HPP
#define LUMA_STDLIB_ESCAPE_HELPERS_HPP

// NOTE: This file is intentionally documentation-only — it serves as a
// directory of escape/unescape helpers across the stdlib. No code is defined here.
//
// ═══════════════════════════════════════════════════════════
// String Escaping Helpers — Directory
// ═══════════════════════════════════════════════════════════
//
// Format-specific escaping functions used by stdlib modules.
// Most escaping is centralised in common/escape.hpp via a
// policy-based template engine.  This header documents where
// each escape helper lives so that new modules can reuse an
// existing function instead of rolling their own.
//
// ── Centralised (common/escape.hpp) ─────────────────────
//
// JSON escaping:
//   json_escape(input)             — returns new string (escapes /)
//   json_escape_string(input, out) — appends to buffer  (no / escape)
//   Policy: JsonEscapePolicy<bool EscapeForwardSlash>
//   Escapes \\, \n, \r, \", \t, \b, \f, and control chars as \uXXXX.
//
// XML escaping:
//   xml_escape_string(input, out)  — appends to buffer
//   Policy: XmlEscapePolicy
//   Escapes &, <, >, ", '.
//
// HTML escaping:
//   html_escape(input)             — returns new string
//   html_escape_string(input, out) — appends to buffer
//   Policy: HtmlEscapePolicy
//   Escapes &, <, >, ", ' (uses &#x27; for apostrophe).
//
// JavaScript escaping:
//   js_string_escape(input)        — returns new string
//   Policy: JsEscapePolicy
//   Escapes \\, \n, \r, ' for single-quoted JS string literals.
//
// Low-level engine:
//   escape_string_impl<Policy>(input, out)  — append with custom policy
//   escape_string_as<Policy>(input)         — return with custom policy
//
// ── URL encoding (common/url_codec.hpp) ─────────────────
//
// url_encode(input)   — percent-encodes non-unreserved chars (RFC 3986)
// url_decode(input)   — decodes percent-encoded string (+ → space)
//
// ── CSV escaping (runtime/stdlib/text/csv_module.cpp) ────────
//
// Handled inline during serialisation.  Fields containing
// delimiters, quotes, or newlines are double-quoted with
// internal quotes escaped by doubling.
//
// ── KV store escaping (runtime/stdlib/collections/keyvaluestore_module.cpp) ──
//
// escape() / unescape() — local anonymous-namespace helpers.
// Escapes tabs, newlines, and backslashes for a flat-file
// key\tvalue\n storage format.  Format-specific; not shared.
//
// ═══════════════════════════════════════════════════════════
// Before implementing a new escape function, check the list
// above.  If the format is new, consider adding a policy to
// common/escape.hpp rather than writing a one-off loop.
// ═══════════════════════════════════════════════════════════

#endif // LUMA_STDLIB_ESCAPE_HELPERS_HPP
