# core/runtime/CMakeLists.txt
# Compiler, VM, standard library, and the main luma executable.

# ─── Stdlib OBJECT libraries (grouped for faster incremental builds) ─────────
# Macro to reduce boilerplate when adding stdlib OBJECT libraries.
# Usage: luma_add_stdlib_library(name SOURCES src1 src2 ... [INCLUDES dir...])

function(luma_add_stdlib_library name)
    cmake_parse_arguments(_STDLIB "" "" "SOURCES;INCLUDES" ${ARGN})
    luma_add_library(luma_stdlib_${name} OBJECT ${_STDLIB_SOURCES})
    if(_STDLIB_INCLUDES)
        # INCLUDES carries vendored third-party dependency directories (miniz,
        # mbedtls); mark them SYSTEM so their headers do not emit warnings under
        # the project's strict warning flags.
        target_include_directories(luma_stdlib_${name} SYSTEM PRIVATE ${_STDLIB_INCLUDES})
    endif()
endfunction()

# Emit the platform-specific source pair for a module split into
# <base>_win32.cpp (Windows) and <base>_posix.cpp (every other platform).
# Sets <out_var> in the caller's scope to the two generator expressions so the
# pair can be spliced into a SOURCES list.  <base> is a path relative to this
# directory without the _win32/_posix suffix or extension
# (e.g. stdlib/platform_process).
function(luma_platform_source_pair out_var base)
    set(${out_var}
        $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/${base}_win32.cpp>
        $<$<NOT:$<PLATFORM_ID:Windows>>:${CMAKE_CURRENT_SOURCE_DIR}/${base}_posix.cpp>
        PARENT_SCOPE)
endfunction()

luma_add_stdlib_library(collections
    SOURCES
        stdlib/collections/array_module.cpp
        stdlib/collections/array_functional.cpp
        stdlib/collections/array_transform.cpp
        stdlib/collections/array_transform_reordering.cpp
        stdlib/collections/array_transform_aggregate.cpp
        stdlib/collections/array_transform_query.cpp
        stdlib/collections/dictionary_module.cpp
        stdlib/collections/set_module.cpp
        stdlib/collections/queue_module.cpp
        stdlib/collections/stack_module.cpp
)

luma_platform_source_pair(_platform_process stdlib/system/platform_process)
luma_platform_source_pair(_platform_terminal stdlib/io/platform_terminal)
luma_platform_source_pair(_terminal_input stdlib/io/terminal_input)

luma_add_stdlib_library(io
    INCLUDES ${PROJECT_SOURCE_DIR}/external/mbedtls/include
    SOURCES
        stdlib/io/filesystem_module.cpp
        stdlib/io/filesystem_operations.cpp
        stdlib/io/filesystem_paths.cpp
        stdlib/io/filesystem_content.cpp
        stdlib/io/console_module.cpp
        stdlib/system/process_module.cpp
        ${_platform_process}
        stdlib/io/terminal_module.cpp
        ${_platform_terminal}
        stdlib/io/terminal_module_ansi.cpp
        stdlib/io/terminal_testing.cpp
        stdlib/io/socket_module.cpp
        stdlib/io/http_module.cpp
        stdlib/io/http_module_parsing.cpp
        stdlib/io/http_module_request.cpp
        stdlib/io/http_module_response.cpp
        stdlib/io/http_module_tls.cpp
        stdlib/io/http_security.cpp
        ${_terminal_input}
)

luma_add_stdlib_library(data
    INCLUDES ${PROJECT_SOURCE_DIR}/external/miniz
    SOURCES
        stdlib/text/string_module.cpp
        stdlib/text/string_module_search.cpp
        stdlib/text/string_module_transform.cpp
        stdlib/types/converter_module.cpp
        stdlib/text/json_module.cpp
        stdlib/text/json_module_parser.cpp
        stdlib/text/json_module_path.cpp
        stdlib/text/json_module_serializer.cpp
        stdlib/text/json_value_module.cpp
        stdlib/text/csv_module.cpp
        stdlib/text/csv_codec.cpp
        stdlib/text/xml_module.cpp
        stdlib/text/xml_module_parser.cpp
        stdlib/text/xml_module_serializer.cpp
        stdlib/system/encoder_module.cpp
        stdlib/system/compression_module.cpp
        stdlib/system/compression_codec.cpp
)

luma_add_stdlib_library(math
    INCLUDES ${PROJECT_SOURCE_DIR}/external/mbedtls/include
    SOURCES
        stdlib/math/math_module.cpp
        stdlib/math/bits_module.cpp
        stdlib/math/math_analysis.cpp
        stdlib/math/statistics_module.cpp
        stdlib/math/math_transcendental.cpp
        stdlib/math/decimal_module.cpp
        stdlib/system/random_module.cpp
        stdlib/math/linearalgebra_module.cpp
        stdlib/math/linearalgebra_vectors.cpp
        stdlib/math/linearalgebra_matrices.cpp
        stdlib/math/calculus_module.cpp
        stdlib/system/hash_module.cpp
        stdlib/system/hash_digest.cpp
        stdlib/system/hash_file.cpp
)

luma_add_stdlib_library(concurrency
    SOURCES
        stdlib/concurrency/channel_module.cpp
        stdlib/concurrency/task_module.cpp
)

luma_add_stdlib_library(utility
    INCLUDES ${PROJECT_SOURCE_DIR}/external/miniz
    SOURCES
        stdlib/system/datetime_module.cpp
        stdlib/system/datetime_arithmetic.cpp
        stdlib/system/datetime_parser.cpp
        stdlib/system/datetime_formatter.cpp
        stdlib/text/regularexpression_module.cpp
        stdlib/types/result_module.cpp
        stdlib/types/optional_module.cpp
        stdlib/types/reference_module.cpp
        stdlib/types/order_module.cpp
        stdlib/system/resource_module.cpp
        stdlib/system/log_module.cpp
        stdlib/collections/keyvaluestore_module.cpp
        stdlib/io/color_module.cpp
)

# OBJECT libraries don't inherit include directories from their consumers
# (luma_core), so vendored dependency include dirs must be set explicitly
# via the INCLUDES parameter of luma_add_stdlib_library above.

# ─── Core Library ────────────────────────────────────────────────────────────
# Compiler, VM, and standard library.  Stdlib modules are compiled as separate
# OBJECT libraries above and linked here for faster incremental builds.

luma_platform_source_pair(_line_editor repl/line_editor)

luma_add_library(luma_core STATIC
    cli/terminal.cpp
    cli/cli.cpp
    cli/cli_args.cpp
    cli/cli_commands.cpp
    cli/cli_runner.cpp
    cli/cli_tester.cpp
    include/include_resolver.cpp
    interpreter/value.cpp
    interpreter/value_equality.cpp
    interpreter/value_copying.cpp
    interpreter/value_formatting.cpp
    interpreter/value_hash.cpp
    repl/repl.cpp
    repl/line_editor.cpp
    ${_line_editor}
    concurrency/channel.cpp
    concurrency/task_scope.cpp
    concurrency/thread_pool.cpp
    compiler/compiler.cpp
    compiler/compiler_access.cpp
    compiler/statement_compiler.cpp
    compiler/expression_compiler.cpp
    compiler/assignment_compiler.cpp
    compiler/pipe_compiler.cpp
    compiler/declaration_compiler.cpp
    compiler/variable_resolver.cpp
    compiler/const_folder.cpp
    compiler/pattern_compiler.cpp
    compiler/loop_compiler.cpp
    compiler/binary_operator_compiler.cpp
    compiler/collection_compiler.cpp
    compiler/interpolation_handler.cpp
    compiler/chunk.cpp
    compiler/optimizer.cpp
    compiler/optimizer_peephole.cpp
    compiler/optimizer_constant_fold.cpp
    compiler/optimizer_dead_code.cpp
    compiler/optimizer_control_flow.cpp
    compiler/verifier.cpp
    compiler/bytecode_serializer.cpp
    compiler/compilation_cache.cpp
    compiler/compilation_pipeline.cpp
    compiler/program_loader.cpp
    vm/vm.cpp
    vm/vm_exception_manager.cpp
    vm/vm_frames.cpp
    vm/vm_calls.cpp
    vm/vm_debug.cpp
    vm/vm_helpers.cpp
    vm/vm_dispatch_arithmetic.cpp
    vm/vm_dispatch_collections.cpp
    vm/vm_dispatch_construction.cpp
    vm/vm_dispatch_concurrency.cpp
    vm/vm_dispatch_control_flow.cpp
    vm/vm_dispatch_table.cpp
    vm/vm_dispatch_types.cpp
    vm/vm_introspection.cpp

    # Stdlib — core and infrastructure
    stdlib/common/core_builtins_module.cpp
    stdlib/common/lazy_registry.cpp
)

target_sources(luma_core PRIVATE
    $<TARGET_OBJECTS:luma_stdlib_collections>
    $<TARGET_OBJECTS:luma_stdlib_io>
    $<TARGET_OBJECTS:luma_stdlib_data>
    $<TARGET_OBJECTS:luma_stdlib_math>
    $<TARGET_OBJECTS:luma_stdlib_concurrency>
    $<TARGET_OBJECTS:luma_stdlib_utility>
)

target_link_libraries(luma_core PUBLIC luma_analysis Threads::Threads PRIVATE luma_shared)

# Link Winsock on Windows for the Socket module.
if(WIN32)
    target_link_libraries(luma_core PUBLIC ws2_32)
endif()

# IDE source grouping mirrors the stdlib category subdirectories.
source_group("stdlib\\common"
    REGULAR_EXPRESSION "stdlib/common/")
source_group("stdlib\\types"
    REGULAR_EXPRESSION "stdlib/types/")
source_group("stdlib\\collections"
    REGULAR_EXPRESSION "stdlib/collections/")
source_group("stdlib\\text"
    REGULAR_EXPRESSION "stdlib/text/")
source_group("stdlib\\math"
    REGULAR_EXPRESSION "stdlib/math/")
source_group("stdlib\\io"
    REGULAR_EXPRESSION "stdlib/io/")
source_group("stdlib\\system"
    REGULAR_EXPRESSION "stdlib/system/")
source_group("stdlib\\concurrency"
    REGULAR_EXPRESSION "stdlib/concurrency/")

# Vendored miniz (compression) — always linked.
include(${PROJECT_SOURCE_DIR}/cmake/LumaMiniz.cmake)
target_link_libraries(luma_core PUBLIC miniz_lib)

# Vendored Mbed TLS — always linked (Hash module dependency) + HTTPS support.
#
# mbedtls_lib is linked unconditionally because the Hash stdlib module
# (hash_digest.cpp, hash_file.cpp) uses the Mbed TLS Message Digest API
# (mbedtls_md, mbedtls_md_hmac) for all cryptographic hashing — SHA-256,
# SHA-512, MD5, HMAC — with no TLS-off fallback. Hashing is not a TLS/HTTPS
# feature: it is an independent use of mbedtls as a crypto library, so it must
# be available regardless of LUMA_FEATURE_TLS.
#
# LUMA_HAS_TLS enables the TLS/HTTPS protocol layer on top:
#   * luma_core / luma_stdlib_io / luma_stdlib_math need LUMA_HAS_TLS=1 to
#     compile their TLS code paths (ssl/x509 for HTTPS, ctr_drbg/entropy for
#     Random.secure_*). Without the macro those paths are compiled out and
#     every https:// request / secure_* call returns a "requires TLS" failure.
#   * OBJECT libraries must define the macro themselves — a PUBLIC define on
#     luma_core does not reach objects consumed via $<TARGET_OBJECTS:>.
#
# Platform CA-cert libraries (crypt32, Security.framework) are TLS-only: they
# are used exclusively by load_system_ca_certs() in http_module_tls.cpp and
# are not needed for plain cryptographic hashing.
include(${PROJECT_SOURCE_DIR}/cmake/LumaMbedTLS.cmake)
target_link_libraries(luma_core PUBLIC mbedtls_lib)

if(LUMA_FEATURE_TLS)
    target_compile_definitions(luma_core PUBLIC LUMA_HAS_TLS=1)
    target_compile_definitions(luma_stdlib_math PRIVATE LUMA_HAS_TLS=1)
    target_compile_definitions(luma_stdlib_io PRIVATE LUMA_HAS_TLS=1)

    # Platform CA-cert libraries (only needed for HTTPS certificate loading).
    if(WIN32)
        target_link_libraries(luma_core PUBLIC crypt32)
    elseif(APPLE)
        find_library(SECURITY_FW Security)
        find_library(COREFOUNDATION_FW CoreFoundation)
        if(SECURITY_FW AND COREFOUNDATION_FW)
            target_link_libraries(luma_core PUBLIC ${SECURITY_FW} ${COREFOUNDATION_FW})
        endif()
    endif()

    message(STATUS "TLS enabled — Http module supports HTTPS via Mbed TLS")
else()
    message(STATUS "TLS disabled — Http module will only support plain HTTP")
endif()

# ─── Main executable ────────────────────────────────────────────────────────

luma_add_executable(luma ${PROJECT_SOURCE_DIR}/core/main.cpp)
target_link_libraries(luma PRIVATE luma_core)
