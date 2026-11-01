# Fuzz Testing

Fuzz targets for the Luma interpreter pipeline, powered by [LibFuzzer](https://llvm.org/docs/LibFuzzer.html). Each target feeds random or structured input to a specific pipeline stage and checks that it does not crash or trigger undefined behaviour.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Building](#building)
3. [Running](#running)
4. [Targets](#targets)
5. [Corpus](#corpus)
6. [Shared Harness](#shared-harness)
7. [Investigating Crashes](#investigating-crashes)

## Prerequisites

- **Clang** with `-fsanitize=fuzzer` support (Clang 6+)
- CMake 3.21+

LibFuzzer is built into Clang — no separate installation is needed.

## Building

Configure and build from the repository root with `LUMA_BUILD_FUZZ=ON`:

```bash
cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined" \
      -DLUMA_BUILD_FUZZ=ON

cmake --build build-fuzz --target luma_fuzzers --parallel
```

`luma_fuzzers` builds every fuzz target and nothing else; the default target
would also compile every test, the language server and the debugger under the
sanitizers. Executables land at the build-tree root (`build-fuzz/fuzz_lexer`).

The fuzz targets mirror whichever of `address` and `undefined` the global flags
enable onto their own compile and link lines. UndefinedBehaviorSanitizer turns
silent undefined behaviour — signed overflow, invalid shifts, misaligned access
— into a crash the fuzzer can report (`fuzz_random` exists to catch exactly
that); `-fno-sanitize-recover=undefined` makes every finding abort.

### Platform notes

The suite targets Linux Clang with libFuzzer, ASan and UBSan (the configuration
shown above), which is what CI exercises. It also builds with the Visual Studio
`clang-cl` toolchain; the validated Windows configuration uses `RelWithDebInfo`,
the static runtime, and coverage-only instrumentation:

```powershell
cmake -B build-fuzz -G Ninja `
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY `
      -DCMAKE_BUILD_TYPE=RelWithDebInfo `
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
      -DLUMA_BUILD_FUZZ=ON `
      -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link"
```

ASan is omitted on Windows because the prebuilt libFuzzer runtime links the
static CRT, which the dynamic ASan runtime cannot be combined with — run the
fuzzers on Linux for full ASan coverage. One significant `clang-cl` caveat:
under SanitizerCoverage, C++ exception unwinding is miscompiled (an MSVC
EH-funclet codegen artifact), so any target whose input makes the code throw
and catch faults inside the unwinder within seconds — an access violation, or an
`int3` (`0x80000003`) at the throw site, with no crash artifact written. That
covers the parser's `ParseFailure` recovery (so every source-driven target, on
inputs as small as `a>`), `fuzz_xml`, `fuzz_json_stdlib`, `fuzz_path`,
`fuzz_http` and `fuzz_protocol`. These are not Luma defects — the same inputs
pass the MSVC-built unit tests — so investigate findings from those targets on
the Linux build. The non-throwing codec targets (`fuzz_string`, `fuzz_terminal`,
`fuzz_decimal`, `fuzz_datetime`, `fuzz_hash`, `fuzz_regex`, `fuzz_random`,
`fuzz_encoder`, `fuzz_compression`, `fuzz_keyvaluestore`, `fuzz_process`,
`fuzz_csv`, `fuzz_json`, `fuzz_bytecode_deserializer`) run cleanly on Windows.
Pass `-dict=` an absolute path there; libFuzzer on Windows fails to open a
relative dictionary path.

## Running

Run a fuzz target with its seed corpus directory and a time limit. Paths are
relative to the repository root, matching the build commands above:

```bash
./build-fuzz/fuzz_lexer fuzz/corpus/lexer/ -max_total_time=300
```

Use the included dictionary for better input generation, and bound each input's
run time so a hang is reported rather than silently consuming the session:

```bash
./build-fuzz/fuzz_lexer fuzz/corpus/lexer/ -dict=fuzz/dictionary.txt -timeout=25 -max_total_time=300
```

Useful LibFuzzer flags:

| Flag                      | Purpose                                    |
| ------------------------- | ------------------------------------------ |
| `-max_total_time=<secs>`  | Stop after the given number of seconds      |
| `-max_len=<bytes>`        | Limit the maximum input size               |
| `-jobs=<N>`               | Run N fuzzing jobs in parallel              |
| `-workers=<N>`            | Number of worker processes                 |
| `-dict=<file>`            | Use a fuzzing dictionary for guided inputs |
| `-timeout=<secs>`         | Report an input that runs longer as a hang |
| `-close_fd_mask=1`        | Close the target's stdout (programs print) |
| `-artifact_prefix=<dir>/` | Save crash inputs to the given directory   |
| `-runs=<N>`               | Stop after N inputs (deterministic replay) |
| `-rss_limit_mb=<N>`       | Abort if RSS exceeds N MB (0 disables)     |
| `-help=1`                 | List every available LibFuzzer flag        |

### Smoke-testing via CTest

Each target also registers a `<target>_quick` CTest entry that fuzzes for a few
seconds against its own seed corpus and the shared dictionary — a fast
regression gate rather than a thorough search. Run them all with:

```bash
ctest --test-dir build-fuzz -R _quick --output-on-failure
```

The per-run budget is `LUMA_FUZZ_QUICK_TIME` seconds (default 10); override it at
configure time with `-DLUMA_FUZZ_QUICK_TIME=<secs>`. Each entry also passes
`-timeout=${LUMA_FUZZ_UNIT_TIMEOUT}` (25 s, set in `fuzz/CMakeLists.txt` and
mirrored by the nightly workflow) and `-close_fd_mask=1`.

## Targets

| Target                  | Pipeline Stages                          | Description                                                            |
| ----------------------- | ---------------------------------------- | ---------------------------------------------------------------------- |
| `fuzz_lexer`            | Lexer                                    | Tokenises arbitrary byte sequences                                     |
| `fuzz_parser`           | Lexer → Parser                           | Constructs ASTs from random token streams                              |
| `fuzz_type_checker`     | Lexer → Parser → Type Checker            | Validates the type checker against arbitrary programs                  |
| `fuzz_linter`           | Lexer → Parser → Type Checker → Linter   | Lints arbitrary AST shapes after type-checking, exactly as production does |
| `fuzz_compiler`         | Lexer → Parser → Type Checker → Compiler → Optimizer → Verifier | Compiles programs through the production passes; compiler output must verify (oracle) |
| `fuzz_optimizer`        | Lexer → Parser → Type Checker → Compiler → Optimizer | Runs `OptimizerPass` at every level over every chunk (incl. inlining); optimised bytecode must still verify (oracle) |
| `fuzz_include_resolver` | Lexer → Parser → Include Resolver        | Handles pathological include paths and circular references             |
| `fuzz_vm`               | Full pipeline → VM                       | Executes programs end-to-end, optimised and verified per an input-derived profile, with sandboxed stdlib and resource limits |
| `fuzz_structured`       | Full pipeline → VM                       | Generates well-typed (VM-bound) or chaos (front-end) Luma source via `FuzzedDataProvider` |
| `fuzz_bytecode_deserializer` | `.lumc` deserializer → VM           | Decodes arbitrary bytes as bytecode, round-trips valid blobs (oracle) and executes them |
| `fuzz_json`             | `shared/json` parser                     | Parses untrusted JSON-RPC bodies; re-serialise must re-parse (oracle)  |
| `fuzz_json_stdlib`      | `Json` stdlib parser                     | Parses arbitrary bytes as `Json.deserialize` input; re-serialise must re-parse (oracle) |
| `fuzz_csv`              | `Csv` codec layer                        | Parses arbitrary bytes as RFC 4180 CSV; serialise→parse round-trips (oracle) |
| `fuzz_xml`              | `Xml` stdlib parser                      | Parses arbitrary bytes as `Xml.deserialize` input; canonical serialise→parse round-trips byte-for-byte (oracle) |
| `fuzz_datetime`         | `DateTime` ISO-8601 codec                | Parses arbitrary bytes as ISO-8601 timestamps; format→parse round-trips (oracle) |
| `fuzz_decimal`          | `Decimal` base-10 parser                 | Parses arbitrary bytes as decimal text (and coerces doubles); canonical text→re-parse round-trips (oracle) |
| `fuzz_protocol`         | `shared/protocol` transport              | Parses Content-Length framed LSP/DAP messages from a byte stream, continuing past recoverable errors |
| `fuzz_compression`      | `Compression` codec layer                | Decodes arbitrary bytes as deflate/gzip/RLE; round-trips valid data (oracle) |
| `fuzz_encoder`          | `Encoder` Base64 / URL codecs            | Decodes arbitrary bytes as Base64/Base64URL/percent-encoding; round-trips valid data (oracle) |
| `fuzz_hash`             | `Hash` CRC32 + hex codec                 | Checksums and hex-encodes arbitrary bytes; hex encode/decode round-trips and CRC32 known answers hold (oracle) |
| `fuzz_path`             | `FileSystem` path validator              | Resolves arbitrary bytes as sandbox-relative paths; accepted paths must stay inside the working directory (oracle) |
| `fuzz_random`           | `Random` bounded-integer core            | Draws a uniform integer in an untrusted `[lo, hi]` range; spans never overflow or divide by zero and draws stay in range (oracle) |
| `fuzz_http`             | `Http` URL parser                        | Parses arbitrary bytes as a URL; parse→reconstruct→parse must converge (oracle) |
| `fuzz_keyvaluestore`    | `KeyValueStore` `.kv` codec              | Parses arbitrary bytes as `.kv` store content; escape/unescape inverts and serialise→parse round-trips, and the glob matcher never crashes (oracle) |
| `fuzz_process`          | `Process` command tokenizer              | Tokenizes arbitrary bytes into argv for `Process.run`; re-quote→tokenize round-trips (oracle) |
| `fuzz_regex`            | `RegularExpression` ReDoS heuristic      | Walks arbitrary bytes as a regex pattern through the nested-quantifier guard; idempotent, never flags a group-free pattern, and fixed safe/dangerous known answers hold (oracle) |
| `fuzz_string`           | UTF-8 string codec                       | Walks arbitrary bytes as UTF-8 through the codepoint helpers; the partition reassembles the input, byte-offset/codepoint-index invert, and encode→decode round-trips (oracle) |
| `fuzz_terminal`         | `Terminal` key decoder                   | Decodes arbitrary bytes as keystrokes, UTF-8 code points and ANSI escape / SGR-mouse sequences; every byte must classify to a non-empty key name (oracle) |

### Trust boundaries and oracles

The last nineteen targets close gaps beyond the compile pipeline:

- **`fuzz_bytecode_deserializer`** feeds arbitrary bytes to
  `BytecodeSerializer::deserialize` — the hand-written binary reader behind the
  `.lumc` cache and pre-compiled module distribution. Buffers that decode are
  re-serialised and decoded again; the bytes must be identical (a deterministic
  round-trip oracle). The decoded program is then executed under the sandboxed
  stdlib and the fuzzing resource limits, because running it is what a loaded
  `.lumc` is for: the deserializer re-verifies every chunk, so verified chunks
  exercise the VM's unchecked fast path (a verifier soundness hole becomes an
  ASan report) and rejected chunks exercise its runtime bounds checks.
- **`fuzz_json`** drives the `shared/json` parser used for LSP/DAP message
  bodies. Any value it produces must serialise to text the parser accepts again.
- **`fuzz_json_stdlib`** drives the separate stdlib JSON parser
  (`core/runtime/stdlib/text/json_module_parser.cpp`) behind `Json.deserialize`,
  `Json.is_valid`, `Json.get`, `Json.set`, `Json.merge`, `Json.get_path` and
  `Json.set_path`. This is the trust boundary that decodes untrusted JSON handed
  to a Luma program (string literals, file contents, network payloads). Unlike
  the `shared/json` parser it builds runtime `Value`s, decodes `\uXXXX` UTF-16
  surrogate pairs by hand, rejects leading zeros and enforces array / object /
  string / depth resource limits, so arbitrary bytes must never crash it. A
  serialisation oracle additionally checks that any `Value` the parser produces
  serialises to text the parser accepts again. `fuzz_structured` only reaches
  this parser shallowly through sanitised string literals, so the direct target
  exercises the grammar far more deeply, mirroring `fuzz_csv`.
- **`fuzz_csv`** drives the `Csv` codec layer
  (`core/runtime/stdlib/text/csv_codec.hpp`) behind `Csv.deserialize`,
  `Csv.deserialize_records`, `Csv.read_file`, `Csv.header` and
  `Csv.count_rows`. `parse_csv` is a hand-written RFC 4180 state machine that
  tracks quote state, escaped quotes and CR / LF / CRLF row terminators over
  untrusted text, so arbitrary bytes must never crash it. An idempotence oracle
  additionally checks that any rows the parser accepts re-serialise to text that
  re-parses to identical rows.
- **`fuzz_xml`** drives the stdlib XML parser
  (`core/runtime/stdlib/text/xml_module_parser.cpp`) behind `Xml.deserialize`,
  `Xml.deserialize_file` and `Xml.is_valid`. `xml_parse_string` is a hand-written
  recursive-descent parser that walks untrusted text by hand: it skips an
  optional `<?xml …?>` declaration, **rejects `<!DOCTYPE …>` to block
  external-entity injection**, decodes the five predefined entities
  (`&lt; &gt; &amp; &apos; &quot;`), reads `<!-- comments -->` and
  `<![CDATA[ … ]]>` sections, parses attributes and matched start/end tags, and
  enforces nesting-depth, child-count and string resource limits, so arbitrary
  bytes must never crash it. A round-trip oracle additionally checks that any
  tree the parser produces serialises to canonical text the parser accepts again
  and that re-serialising that tree reproduces the same bytes — the compact
  serializer escapes content, sanitises comment `--` runs and splits CDATA `]]>`
  markers, so a parser-produced tree has a single stable canonical form.
  `fuzz_structured` only reaches this parser shallowly through sanitised string
  literals, so the direct target exercises the grammar far more deeply,
  mirroring `fuzz_csv` and `fuzz_json_stdlib`.
- **`fuzz_datetime`** drives the `DateTime` ISO-8601 codec
  (`core/runtime/stdlib/system/datetime_codec.hpp`) behind `DateTime.from_iso_string`,
  `DateTime.to_iso_string` and `DateTime.now_iso_string`. `parse_iso8601` is a
  hand-written reader that pulls a date, an optional `THH:MM:SS` time and an
  optional `Z` / `+HH:MM` / `-HH:MM` zone offset out of untrusted text, so
  arbitrary bytes must never crash it; `format_iso8601` is additionally driven
  with hostile doubles (including NaN and infinities) to exercise its range
  guards. A round-trip oracle checks that any instant the parser accepts and the
  formatter can represent renders to canonical text that re-parses unchanged.
- **`fuzz_decimal`** drives the `Decimal` base-10 parser
  (`core/common/decimal.hpp`) behind `Decimal.from_string`. `Decimal::parse` is a
  hand-written reader that pulls an optional sign, a digit run, an optional
  fractional part and an optional `eNN` exponent out of untrusted text, so
  arbitrary bytes must never crash it; `Decimal::from_double` is additionally
  driven with hostile doubles (including NaN and infinities) to exercise its
  guards. A round-trip oracle checks that any value the parser accepts renders to
  canonical text that re-parses to an equal decimal.
- **`fuzz_protocol`** streams arbitrary bytes through the shared `Content-Length`
  message-framing transport via an in-memory subclass. Like the LSP/DAP message
  loops it keeps reading after a recoverable error, so the resync and
  buffered-header paths that follow a malformed frame are exercised too.
- **`fuzz_compression`** drives the `Compression` codec layer
  (`core/runtime/stdlib/system/compression_codec.hpp`) behind `Compression.inflate`,
  `Compression.gunzip` and `Compression.decode_rle`. These decoders parse
  untrusted bytes — gunzip walks a hand-written gzip header (FEXTRA / FNAME /
  FCOMMENT / FHCRC) with manual offset arithmetic — so arbitrary input must
  never crash them. A round-trip oracle additionally checks that every encoder
  output decodes back to the exact input for deflate, gzip and RLE.
- **`fuzz_encoder`** drives the `Encoder` Base64 and URL codecs
  (`core/common/base64_codec.hpp` and `core/common/url_codec.hpp`)
  behind `Encoder.decode_base64`, `Encoder.decode_base64url` and
  `Encoder.decode_url`. `base64_decode_with` walks untrusted text four
  characters at a time — implicitly padding a short tail and rejecting stray
  alphabet characters — and `url_decode` scans for `%XX` escapes with manual
  two-character look-ahead while folding `+` to space, so arbitrary bytes must
  never crash them. A round-trip oracle checks that every encoder output decodes
  back to the exact input for Base64, Base64URL and percent-encoding, and an
  idempotence oracle pins down the lenient Base64 decoder by re-encoding and
  re-decoding any blob it accepts.
- **`fuzz_hash`** drives the `Hash` module's first-party primitives
  (`core/common/crc32.hpp` and `core/common/hex_codec.hpp`) behind `Hash.crc32`
  and the hex rendering of every digest (`Hash.md5`, `Hash.sha1`, `Hash.sha256`,
  `Hash.sha512`, the HMAC variants and the `*_file` helpers). `crc32_hash` is a
  table loop over arbitrary input bytes and `to_hex` / `from_hex_digit` are the
  hand-written hex codec, so arbitrary bytes must never crash them. The SHA/MD5
  digests themselves delegate to the vendored mbedtls library (fuzzed upstream)
  and are out of scope. A round-trip oracle checks that decoding `to_hex(input)`
  one nibble pair at a time reproduces the exact input, an output-shape oracle
  checks the two `to_hex` overloads agree and emit only valid hex of twice the
  input length, and fixed CRC32 known answers pin the polynomial, initial value
  and final XOR.
- **`fuzz_path`** drives the `FileSystem` path-validation trust boundary
  (`core/runtime/stdlib/common/path_validator.hpp` and the security primitives in
  `core/common/path_utils.hpp`) behind every `FileSystem.*` function and the
  include resolver. `validate_path` resolves untrusted path strings against the
  current working directory with hand-written `weakly_canonical` /
  `lexically_relative` arithmetic and a manual first-component `..` check, so
  arbitrary bytes must never crash it (rejections raise `RuntimeError`). An
  agreement oracle additionally checks that any path the validator accepts as
  in-sandbox is not flagged as escaping the working directory by the
  independent `canonical_escapes_root` check.
- **`fuzz_random`** drives the `Random` module's bounded-integer core
  (`core/runtime/stdlib/system/random_bounded.hpp`) behind `Random.generate_integer`
  and `Random.secure_integer`. `bounded_uniform` draws a uniform integer in a
  closed `[lo, hi]` range — both supplied by an untrusted Luma program — from a
  stream of 64-bit values, using rejection sampling to remove modulo bias. The
  fuzzer chooses `lo` and `hi` and feeds attacker-controlled draws into the
  rejection loop (falling back to `UINT64_MAX`, which is always accepted, once
  the input is spent so the loop terminates). All arithmetic runs in unsigned
  64-bit space, so even the full `[INT64_MIN, INT64_MAX]` span — which a naive
  `static_cast<uint64_t>(hi - lo)` would overflow and a `% (range + 1)` would
  divide by zero — must never crash. Oracles check that an empty range (`lo >
  hi`) is refused, that a valid range always yields a draw inside `[lo, hi]`,
  that the rejection threshold never exceeds the span, and that `bounded_map`
  keeps every raw value — not just accepted ones — within the range.
- **`fuzz_http`** drives the `Http` module's URL parser
  (`core/runtime/stdlib/io/http_url_parser.hpp`) behind `Http.parse_url` and every
  `Http.get` / `Http.post` / ... request method. `parse_url` splits an untrusted
  URL string into scheme, host, port, path and query by hand — lowercasing the
  scheme, carving the authority off at the first `/`, pulling a query off at the
  first `?`, and reading a port with `std::stoi` (guarded against
  `std::invalid_argument` / `std::out_of_range`), including the bracketed-IPv6
  `[2001:db8::1]:8080` form — so arbitrary bytes must never crash it. An
  idempotence oracle additionally re-serialises the parsed parts into a
  canonical URL and re-parses: because the parser normalises its input (dropping
  a scheme-default port, collapsing a missing path to `/`, folding an unparseable
  port back to the default), the second and third parses must agree exactly, so
  any non-convergence is a genuine parser inconsistency.
- **`fuzz_keyvaluestore`** drives the `KeyValueStore` `.kv` codec
  (`core/runtime/stdlib/collections/keyvaluestore_codec.hpp`) behind `KeyValueStore.open`,
  `KeyValueStore.open_read_only` and `KeyValueStore.reload` (through the
  module's `read_store`) plus the glob matcher behind
  `KeyValueStore.find_by_pattern`. `parse_store` is a hand-written line reader
  that splits each record of an untrusted store file on the first tab and
  unescapes the key and value through the matching escape/unescape codec, while
  `glob_match` walks an untrusted `*` / `?` pattern against every stored key, so
  arbitrary bytes must never crash them — content exceeding the dictionary-size
  or string-size limits raises `RuntimeError`, the expected outcome for hostile
  input. A codec-inverse oracle checks that `unescape(escape(input)) == input`
  for every input, and an idempotence oracle checks that any entries
  `parse_store` accepts re-serialise to text that re-parses to identical
  entries.

- **`fuzz_process`** drives the `Process` module's command tokenizer
  (`core/runtime/stdlib/system/process_module.hpp`) behind `Process.run`.
  `tokenize_command` is a hand-written parser that splits an untrusted command
  string into argv components — honouring double-quoted and single-quoted spans
  and backslash escapes — before the platform spawn path (`CreateProcessA` on
  Windows, `fork` + `execvp` on POSIX) hands the vector straight to the OS
  without a shell, so arbitrary bytes must never crash it; a mismatched quote
  raises `RuntimeError`, the expected outcome for malformed input. A re-quote
  oracle canonically re-quotes every emitted argument (wrapping it in `"` with
  each `"` and `\` backslash-escaped), rejoins them with single spaces and
  re-tokenizes: the result must reproduce the original argument vector exactly,
  because inside a double-quoted span only `\` and `"` are special and the
  tokenizer never emits an empty argument.

- **`fuzz_regex`** drives the `RegularExpression` module's ReDoS heuristic
  (`core/runtime/stdlib/text/regularexpression_module.hpp`) behind every entry point —
  `RegularExpression.matches`, `find`, `find_all`, `replace`, `replace_all`,
  `split` and `is_valid`. `has_dangerous_quantifier_nesting` is a hand-written
  walk that scans an untrusted pattern with a per-open-parenthesis stack to flag
  nested quantifiers that risk catastrophic backtracking (e.g. `(a+)+`, `((a+))+`,
  `(?:a+)+`) before `std::regex` ever compiles it, skipping character classes and
  group-modifier syntax and honouring backslash escapes, so arbitrary bytes must
  never crash it. `std::regex` itself is fuzzed upstream and is out of scope. Three
  oracles back the never-crash contract: the walk is pure, so it is idempotent; it
  can only raise its flag at a `)` followed by a quantifier, so a pattern with no
  `(` byte is never dangerous; and a fixed table of safe and dangerous patterns
  pins the contract against drift, mirroring `fuzz_hash`'s CRC-32 anchors.
- **`fuzz_string`** drives the UTF-8 string codec (`core/common/utf8.hpp` and
  `core/common/utf8_iterator.hpp`) behind nearly every `String.*`
  function — `String.length`, `byte_length`, `reverse`, `characters`,
  `character_at`, `substring`, `chunk`, `to_codepoints`, `from_codepoints`,
  `common_prefix` / `common_suffix`, `levenshtein_distance`, `truncate` and the
  pad / center helpers — as well as the VM's for-in iteration over a string and
  the `\uXXXX` surrogate-pair decode in the stdlib JSON parser. The byte-walking
  helpers (`utf8_count`, `utf8_advance`, `utf8_decode_at`, `utf8_char_at_byte`,
  `utf8_byte_offset`, `utf8_codepoint_index`, `utf8_codepoint_len`, `utf8_encode`
  and `decode_surrogate_pair`) decode untrusted text one codepoint at a time and
  treat a malformed sequence as a single replacement byte rather than throwing,
  so arbitrary — including non-UTF-8 — bytes must never crash them, read out of
  bounds, or loop forever. Four oracles back the never-crash contract: gluing the
  per-codepoint chunks back together reproduces the input exactly and the chunk
  count equals `utf8_count` (partition completeness); `utf8_byte_offset` and
  `utf8_codepoint_index` are mutual inverses and `byte_offset` lands exactly on
  the string length and then saturates; `utf8_encode` rejects surrogate halves
  and out-of-range scalars
  and otherwise round-trips through `utf8_decode_at` with a length matching
  `utf8_codepoint_len`, pinned by a fixed 1/2/3/4-byte known-answer table; and
  `decode_surrogate_pair` maps every valid half pair into a supplementary scalar
  that re-encodes. `fuzz_structured` only reaches these helpers shallowly through
  sanitised string literals, so the direct target exercises the codec far more
  deeply, mirroring `fuzz_csv` and `fuzz_encoder`.
- **`fuzz_terminal`** drives the `Terminal` key decoder
  (`core/runtime/stdlib/io/terminal_key_decoder.hpp`) behind `Terminal.read_key`
  and `Terminal.read_input`. `decode_key` turns the raw byte stream a terminal
  delivers in raw mode — single keystrokes, multi-byte UTF-8 code points and ANSI
  escape sequences for arrows, function keys, modifier combinations and SGR mouse
  reports — into a stable key-name string. The bytes are entirely controlled by
  whatever is connected to stdin (a real terminal, a pipe, a malicious program),
  so the parser must never crash, read out of bounds or loop unboundedly. The
  POSIX backend drives this exact code with live stdin, which no unit test can
  reach deterministically. The decoder is decoupled from blocking I/O via a
  `byte_reader` callback, and — like the live backend — the whole input is
  decoded as a stream of keys: each key's first byte seeds `decode_key`, the
  following bytes feed the callback (which yields `-1` once spent, mirroring an
  escape-sequence timeout), and decoding resumes where the previous key stopped,
  so consecutive sequences and their boundaries are exercised. Each input is
  decoded with mouse reporting both disabled and enabled so the SGR branch is
  reached. A non-empty-result oracle backs the never-crash contract:
  every byte must classify to a non-empty key name, so an empty result signals a
  regression.

## Corpus

Seed corpus files live in `corpus/` subdirectories, one per target, named after
the target with the `fuzz_` prefix stripped (so `fuzz_lexer` seeds from
`corpus/lexer/`). Good seeds help the fuzzer reach deeper code paths faster.

To add a new seed:

1. Write a small seed file in whatever the target consumes — Luma source for the
   pipeline stages, or the relevant format (JSON, CSV, XML, raw bytes, …) for the
   codec and trust-boundary targets.
2. Place it in the matching `corpus/<stage>/` directory (e.g. `corpus/lexer/` for
   `fuzz_lexer`).

Seeds for the compiling and executing targets (`corpus/compiler/`,
`corpus/optimizer/`, `corpus/vm/`) must type-check and run under
`luma --box`; a seed the type checker rejects never reaches the stage it is
meant to seed. Check a seed from a copy in a scratch directory: running `luma`
writes a `.lumc` cache next to the file, and only
`corpus/bytecode_deserializer/` may hold `.lumc` files.

## Shared Harness

All targets are built from a small set of shared headers:

- **[fuzz_harness.hpp](fuzz_harness.hpp)** — low-level utilities with no Luma
  pipeline dependencies: input size caps (`k_max_input_size` /
  `k_max_vm_input_size`), `to_string`, `leading_double` (reinterprets the first
  eight input bytes as a hostile double via `std::bit_cast`), `do_not_optimize`
  (forces a value to be observed so the work behind it is not optimised away),
  and `run` — the exception-handling wrapper. Every expected failure derives
  from `std::runtime_error` (`luma::RuntimeError`, the shared parsers'
  `ParseError`, transport errors, `std::filesystem_error`), so `run` tolerates
  exactly those plus `std::bad_alloc`, and traps on everything else:
  `std::logic_error` and its kin, `std::bad_variant_access`, foreign exceptions,
  and `luma::InternalError` — the `RuntimeError` subtype the VM raises when a
  built-in fails with an interpreter fault, which would otherwise look like an
  ordinary catchable error. `run_text` builds on `run`: it applies the size cap,
  converts the bytes to a `std::string`, and forwards it to a caller-supplied
  body — the single delegating call every string-based target uses in place of
  the repeated cap-check / `to_string` / `run` prologue.
- **[fuzz_oracle.hpp](fuzz_oracle.hpp)** — reusable codec oracles
  (`check_roundtrip`, `check_decoder_stable`) parameterised on a codec's
  `encode`/`decode` callables for `std::optional`-returning decoders. Shared by
  `fuzz_encoder` and `fuzz_compression`; depends only on `fuzz_harness.hpp`.
- **[fuzz_frontend.hpp](fuzz_frontend.hpp)** — analysis-stage helpers (`lex`,
  `parse`, `parse_clean`, `type_check`, `type_check_and_lint`) that depend only
  on `luma_analysis`. `type_check_and_lint` assembles the production
  `TypeCheckerPass` + `LintPass` pipeline, because the linter relies on the
  type checker's annotations. Used by the analysis-only targets.
- **[fuzz_pipeline.hpp](fuzz_pipeline.hpp)** — back-end helpers. `build`
  assembles the production `TypeCheckerPass` → `CompilerPass` →
  [`OptimizerPass`] → [`VerifierPass`] pipeline exactly as `compile_program()`
  does, so the fuzzers cannot drift from production; `build_options_for` derives
  the optimisation level and verification flag from the input size, so every
  production profile — including the VM's verified fast path, which skips its
  bounds checks — is covered deterministically. `execute` runs bytecode under
  the sandboxed stdlib, `run_full_pipeline` chains the three, and
  `resolve_includes` drives the include resolver. `apply_fuzz_resource_limits`
  tightens every container, string, task-queue and loop limit. The loop budgets
  are the main bound on execution time: the shared while budget, and the
  cumulative total budget (off by default outside fuzzing) that bounds nested
  `for` loops and crafted bytecode that the while budget cannot, since every
  `for` loop resets it. Pulls in the runtime/VM headers, so only
  targets that link `luma_core` include it.
- **[fuzz_source_generator.hpp](fuzz_source_generator.hpp)** — umbrella header
  for the templated grammar generator used by `fuzz_structured`. Each input
  yields one of two kinds of program, chosen up front because one ill-typed
  statement rejects a whole program: a **well-typed** program, which reaches the
  compiler, optimiser, verifier and VM, or a deliberately ill-typed **chaos**
  program, which stresses parser recovery and the type checker. The generator
  is split by concern across **[fuzz_gen_vocab.hpp](fuzz_gen_vocab.hpp)** (token
  vocabulary, `pick`, `pick_kind`, and literal helpers),
  **[fuzz_gen_features.hpp](fuzz_gen_features.hpp)** (type-correct record,
  tuple, alias, interface and namespace emitters),
  **[fuzz_gen_stdlib.hpp](fuzz_gen_stdlib.hpp)** (type-correct concurrency,
  `LinearAlgebra`, `Math`/`Statistics`, `Converter` and `Json` emitters),
  **[fuzz_gen_grammar.hpp](fuzz_gen_grammar.hpp)** (the chaos grammar),
  **[fuzz_gen_typed.hpp](fuzz_gen_typed.hpp)** (the well-typed grammar, which
  builds every expression for a requested type and avoids runtime failures by
  construction — bounded operands, non-zero divisors, literal loop bounds), and
  **[fuzz_gen_program.hpp](fuzz_gen_program.hpp)** (preludes, declarations and
  program assembly). Every grammar dispatches on an `enum class` with a `Count`
  sentinel drawn by `pick_kind`, so `-Wswitch` flags any kind a switch forgets.
  The generator is parameterised on a data provider rather than depending on
  LibFuzzer directly, and `tests/integration/fuzz_generator_test.cpp` drives it
  with a deterministic provider: every well-typed program and every well-typed
  statement kind must type-check without an unknown-stdlib warning, compile at
  the highest optimisation level, verify, and run cleanly in the sandbox. That
  matters because the type checker only *warns* about an unknown stdlib member,
  and `Csv`, `Xml` and the other OS-only modules are absent under the sandbox —
  either would type-check yet abort the program at run time.
Each `*_quick` CTest entry runs its target against the target's own
`corpus/<stage>/` directory with the shared dictionary for
`LUMA_FUZZ_QUICK_TIME` seconds (default 10; override with
`-DLUMA_FUZZ_QUICK_TIME=<secs>`).

## Investigating Crashes

When a fuzzer finds a crash, it writes the reproducing input to a file (e.g., `crash-<hash>`). To reproduce:

```bash
./build-fuzz/fuzz_lexer crash-abc123def
```

Run under a debugger for a stack trace:

```bash
lldb -- ./build-fuzz/fuzz_lexer crash-abc123def
```

Minimise the crashing input for a cleaner reproduction case:

```bash
./build-fuzz/fuzz_lexer -minimize_crash=1 -exact_artifact_path=minimized.txt crash-abc123def
```

A `timeout-<hash>` artifact means one input ran longer than `-timeout`. The loop
budgets in `apply_fuzz_resource_limits` bound every loop, so for most targets a
timeout is a genuine hang. For `fuzz_vm` and `fuzz_bytecode_deserializer`, which
run arbitrary programs, first check whether the program simply waits forever by
design — for example a blocking `Channel.receive` on a channel nothing sends to,
or an `await` on such a task. That is legitimate Luma behaviour, not an
interpreter bug. (The structured generator never emits such waits, so a timeout
from `fuzz_structured` is always worth investigating.)
