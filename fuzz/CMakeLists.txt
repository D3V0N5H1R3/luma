# Fuzz testing targets for the Luma interpreter pipeline.
# Requires Clang with -fsanitize=fuzzer support.
#
# Build via the top-level CMakeLists.txt with LUMA_BUILD_FUZZ=ON:
#   cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#         -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address" \
#         -DLUMA_BUILD_FUZZ=ON
#   cmake --build build-fuzz --parallel
#   ./build-fuzz/fuzz/fuzz_lexer corpus/lexer/ -max_total_time=300

if(NOT TARGET luma_analysis)
    message(FATAL_ERROR
        "Build from the top-level CMakeLists.txt with -DLUMA_BUILD_FUZZ=ON.")
endif()

# Fuzzing dictionary for guided input generation.
set(LUMA_FUZZ_DICTIONARY "${CMAKE_CURRENT_SOURCE_DIR}/dictionary.txt")

# Wall-clock seconds each *_quick CTest entry spends fuzzing.  Override with
# -DLUMA_FUZZ_QUICK_TIME=<secs> for longer smoke runs in CI.
set(LUMA_FUZZ_QUICK_TIME "10" CACHE STRING
    "Seconds each fuzz *_quick CTest entry runs")

# Detect whether the global flags already include address sanitizer.
# When they don't (e.g. on Windows where ASan requires the entire TU
# to be built consistently), omit it from the per-target flags.
if(CMAKE_CXX_FLAGS MATCHES "address")
    set(LUMA_FUZZ_SANITIZERS fuzzer,address)
else()
    set(LUMA_FUZZ_SANITIZERS fuzzer)
endif()

# On Windows with clang-cl, the Clang runtime libraries may not be on
# the default library search path.  Locate them via the compiler's
# resource directory so that lld-link can find the fuzzer runtime.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /clang:--print-resource-dir
        OUTPUT_VARIABLE CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CLANG_RT_LIB_DIR "${CLANG_RESOURCE_DIR}/lib/windows")
endif()

# Helper — create a fuzz target with sanitizer flags on the link line.
function(luma_add_fuzz_target name source lib)
    add_executable(${name} ${source})
    luma_set_compile_options(${name})
    # Match the production executables' enlarged stack so that deeply-nested
    # input reaches the parser's depth guard (a recoverable diagnostic) instead
    # of overflowing the default 1 MB Windows stack and reporting a spurious
    # crash that masks real findings.
    luma_set_stack_reserve(${name})
    target_link_libraries(${name} PRIVATE ${lib})
    target_compile_options(${name} PRIVATE -fsanitize=${LUMA_FUZZ_SANITIZERS})

    # On Windows with clang-cl, the linker (lld-link) does not understand
    # -fsanitize=fuzzer.  Link the Clang fuzzer runtime library explicitly.
    if(CLANG_RT_LIB_DIR)
        target_link_directories(${name} PRIVATE "${CLANG_RT_LIB_DIR}")
        target_link_libraries(${name} PRIVATE clang_rt.fuzzer-x86_64)
        if(LUMA_FUZZ_SANITIZERS MATCHES "address")
            target_link_libraries(${name} PRIVATE clang_rt.asan_dynamic-x86_64)
        endif()
    else()
        target_link_options(${name} PRIVATE -fsanitize=${LUMA_FUZZ_SANITIZERS})
    endif()

    # Each target seeds from its own corpus subdirectory (corpus/<stage>/),
    # derived from the target name by stripping the leading "fuzz_".  This
    # matches the layout documented in README.md, rather than feeding every
    # target the entire corpus tree.
    string(REGEX REPLACE "^fuzz_" "" corpus_subdir "${name}")
    set(corpus_dir "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${corpus_subdir}")

    # Register a convenience CTest entry that uses the dictionary and the
    # target's own seed corpus.
    add_test(
        NAME ${name}_quick
        COMMAND ${name}
            -dict=${LUMA_FUZZ_DICTIONARY}
            -max_total_time=${LUMA_FUZZ_QUICK_TIME}
            ${corpus_dir}
    )
endfunction()

# Targets are listed in the same order as the README "Targets" table.
# Analysis-only stages link luma_analysis; stages that compile or execute
# bytecode link luma_core.
luma_add_fuzz_target(fuzz_lexer            fuzz_lexer.cpp            luma_analysis)
luma_add_fuzz_target(fuzz_parser           fuzz_parser.cpp           luma_analysis)
luma_add_fuzz_target(fuzz_resolver         fuzz_resolver.cpp         luma_analysis)
luma_add_fuzz_target(fuzz_type_checker     fuzz_type_checker.cpp     luma_analysis)
luma_add_fuzz_target(fuzz_linter           fuzz_linter.cpp           luma_analysis)
luma_add_fuzz_target(fuzz_compiler         fuzz_compiler.cpp         luma_core)
luma_add_fuzz_target(fuzz_optimizer        fuzz_optimizer.cpp        luma_core)
luma_add_fuzz_target(fuzz_include_resolver fuzz_include_resolver.cpp luma_core)
luma_add_fuzz_target(fuzz_vm               fuzz_vm.cpp               luma_core)
luma_add_fuzz_target(fuzz_structured       fuzz_structured.cpp       luma_core)

# Trust-boundary and oracle targets.  These link luma_core, which archives the
# luma_shared objects (JSON parser, protocol transport) and the bytecode
# serializer, so no additional libraries or include directories are required —
# luma_project_options already exposes core/ and shared/ on the include path.
luma_add_fuzz_target(fuzz_bytecode_deserializer fuzz_bytecode_deserializer.cpp luma_core)
luma_add_fuzz_target(fuzz_json                   fuzz_json.cpp                   luma_core)
luma_add_fuzz_target(fuzz_json_stdlib            fuzz_json_stdlib.cpp            luma_core)
luma_add_fuzz_target(fuzz_csv                    fuzz_csv.cpp                    luma_core)

# Xml parser trust boundary.  xml_parse_string / xml_serialize_value
# (core/runtime/stdlib/text/xml_module.hpp) behind Xml.deserialize,
# Xml.deserialize_file and Xml.is_valid form a hand-written recursive-descent
# parser that decodes untrusted XML text — entities, comments, CDATA, attributes
# and matched tags — under nesting-depth and resource limits, and reject
# <!DOCTYPE> to block external-entity injection.  They are defined in
# xml_module_parser.cpp / xml_module_serializer.cpp, which luma_core archives — so,
# like fuzz_csv and fuzz_json_stdlib, this target links luma_core.  A re-parse
# and byte-idempotence oracle checks the canonical serialisation round-trips.
luma_add_fuzz_target(fuzz_xml                    fuzz_xml.cpp                    luma_core)
luma_add_fuzz_target(fuzz_datetime               fuzz_datetime.cpp               luma_core)
luma_add_fuzz_target(fuzz_protocol               fuzz_protocol.cpp               luma_core)
luma_add_fuzz_target(fuzz_compression            fuzz_compression.cpp            luma_core)
luma_add_fuzz_target(fuzz_encoder                fuzz_encoder.cpp                luma_core)
luma_add_fuzz_target(fuzz_graphicalui_css        fuzz_graphicalui_css.cpp        luma_core)

# KeyValueStore `.kv` codec trust boundary.  parse_store / serialize_store, the
# escape/unescape line codec and glob_match are declared in
# core/runtime/stdlib/collections/keyvaluestore_codec.hpp and defined in
# keyvaluestore_module.cpp, which luma_core archives — so, like fuzz_csv, this
# target links luma_core and needs no extra libraries.
luma_add_fuzz_target(fuzz_keyvaluestore          fuzz_keyvaluestore.cpp          luma_core)

# CRC32 + hex codec trust boundary.  crc32_hash (core/common/crc32.hpp) and the
# hex codec (core/common/hex_codec.hpp) behind the Hash module are header-only
# and depend only on the standard library, so this target links the analysis
# library like fuzz_path above.  The SHA/MD5/HMAC digests delegate to the
# vendored mbedtls (fuzzed upstream) and are intentionally out of scope.
luma_add_fuzz_target(fuzz_hash                    fuzz_hash.cpp                   luma_analysis)

# Path-validation trust boundary.  validate_path and the path_utils security
# primitives are header-only and depend only on luma_analysis (RuntimeError and
# SourceLocation), so this target links the analysis library like the
# analysis-stage fuzzers above.
luma_add_fuzz_target(fuzz_path                    fuzz_path.cpp                   luma_analysis)

# Exact base-10 decimal parser trust boundary.  Decimal::parse and from_double
# (core/common/decimal.hpp) behind Decimal.from_string / Decimal.from_number
# scan untrusted text and hostile doubles into an arbitrary-precision base-10
# value under a digit-count limit.  decimal.cpp is compiled into the luma_common
# OBJECT library that luma_analysis embeds, so this target links the analysis
# library like fuzz_path and fuzz_hash above.  A canonical-text round-trip oracle
# checks parse(to_string(x)) == x.
luma_add_fuzz_target(fuzz_decimal                 fuzz_decimal.cpp                luma_analysis)

# Bounded-integer trust boundary.  bounded_uniform and its UB-free helpers
# (core/runtime/stdlib/system/random_bounded.hpp) behind Random.generate_integer and
# Random.secure_integer draw a uniform value in an untrusted [lo, hi] range via
# rejection sampling.  The header depends only on the standard library, so this
# target links the analysis library like fuzz_path and fuzz_hash above.
luma_add_fuzz_target(fuzz_random                  fuzz_random.cpp                 luma_analysis)

# URL-parsing trust boundary.  parse_url (core/runtime/stdlib/io/http_url_parser.hpp)
# behind Http.parse_url and every Http request method is header-only and depends
# only on the standard library, so this target links the analysis library like
# fuzz_path and fuzz_hash above.
luma_add_fuzz_target(fuzz_http                    fuzz_http.cpp                   luma_analysis)

# Command-tokenizer trust boundary.  luma::tokenize_command
# (core/runtime/stdlib/system/process_module.hpp) splits an untrusted command string
# into argv for Process.run before the platform spawn path (CreateProcessA /
# execvp) hands it to the OS without a shell.  It is defined in
# process_module.cpp, which luma_core archives — so, like fuzz_keyvaluestore and
# fuzz_csv, this target links luma_core.
luma_add_fuzz_target(fuzz_process                 fuzz_process.cpp                luma_core)

# ReDoS-heuristic trust boundary.  has_dangerous_quantifier_nesting
# (core/runtime/stdlib/text/regularexpression_module.hpp) is the hand-written pattern
# walk that guards every RegularExpression entry point against catastrophic
# backtracking before std::regex compiles an untrusted pattern.  It is defined in
# regularexpression_module.cpp, which luma_core archives — so, like fuzz_process
# and fuzz_keyvaluestore, this target links luma_core.
luma_add_fuzz_target(fuzz_regex                   fuzz_regex.cpp                  luma_core)

# UTF-8 string-codec trust boundary.  The byte-walking helpers in
# core/common/utf8.hpp and core/common/utf8_iterator.hpp (utf8_count,
# utf8_advance, utf8_decode_at, utf8_byte_offset, utf8_codepoint_index,
# utf8_encode, decode_surrogate_pair, …) sit behind nearly every String.*
# function and the VM's for-in over strings, decoding untrusted text.  They are
# header-only and depend only on the standard library, so this target links the
# analysis library like fuzz_path, fuzz_hash and fuzz_random above.
luma_add_fuzz_target(fuzz_string                  fuzz_string.cpp                 luma_analysis)

# Terminal key-decoder trust boundary.  luma::terminal_detail::decode_key
# (core/runtime/stdlib/io/terminal_key_decoder.hpp) turns the raw byte stream a
# terminal delivers in raw mode — keystrokes, UTF-8 code points and ANSI
# escape/SGR-mouse sequences — into key-name strings behind Terminal.read_key
# and Terminal.read_input.  It is header-only and depends only on the standard
# library, so this target links the analysis library like fuzz_path, fuzz_hash
# and fuzz_string above.
luma_add_fuzz_target(fuzz_terminal                fuzz_terminal.cpp               luma_analysis)
