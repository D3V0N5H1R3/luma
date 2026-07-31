# tests/CMakeLists.txt
# Unit tests, integration tests, and Luma feature test registration.

# Interface library for the test framework — changes to
# test_framework.hpp trigger recompilation of every test target.
add_library(luma_test_framework INTERFACE)
target_include_directories(luma_test_framework INTERFACE ${PROJECT_SOURCE_DIR}/tests)

# ─── Test helper macro ───────────────────────────────────────────────────────
# Reduces boilerplate for each test executable.
# Usage: luma_add_test(NAME <name> SOURCES <src...> LIBS <lib...>
#                      [INCLUDES <dir...>] [WORKING_DIR <dir>]
#                      [TIMEOUT <seconds>] [LABELS <label...>])
#
# Test naming convention: <component>_test.cpp
# Exception: stdlib tests use stdlib_test_<module>.cpp for clarity.
# Exception: type checker tests use type_checker_test_<aspect>.cpp.
function(luma_add_test)
    cmake_parse_arguments(ARG "" "NAME;WORKING_DIR;TIMEOUT" "SOURCES;LIBS;INCLUDES;LABELS" ${ARGN})
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    luma_set_compile_options(${ARG_NAME})
    target_link_libraries(${ARG_NAME} PRIVATE ${ARG_LIBS} luma_test_framework)
    if(ARG_INCLUDES)
        target_include_directories(${ARG_NAME} PRIVATE ${ARG_INCLUDES})
    endif()
    if(ARG_WORKING_DIR)
        add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME}
            WORKING_DIRECTORY "${ARG_WORKING_DIR}")
    else()
        add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
    endif()
    set_tests_properties(${ARG_NAME} PROPERTIES
        ENVIRONMENT "LUMA_TEST_ROOT=${PROJECT_SOURCE_DIR}"
    )
    if(ARG_TIMEOUT)
        set_tests_properties(${ARG_NAME} PROPERTIES TIMEOUT "${ARG_TIMEOUT}")
    endif()
    if(ARG_LABELS)
        set_tests_properties(${ARG_NAME} PROPERTIES LABELS "${ARG_LABELS}")
    endif()
endfunction()

# ─── Test Working Directory Convention ───
# Tests that read source files (source_test, cli_test, feature tests) use
# WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} so they can find example programs.
# Unit tests with no file dependencies use the default (build) directory.

# ─── Test label mapping ──────────────────────────────────────────────────────
# Data-driven prefix-to-label mapping replaces repetitive if-elseif chains.
# Format: "prefix=label1|label2" (pipes become semicolons, i.e. CMake list
# separators, so each pipe-delimited token is a separate CTest label).

set(_analysis_label_map
    "lexer_=analysis|lexer"
    "parser_=analysis|parser"
    "type_checker_=analysis|types"
    "resolver_=analysis|resolver"
    "linter_=analysis|linter"
    "error_recovery_=analysis|parser"
)

set(_runtime_label_map
    "stdlib_=runtime|stdlib"
    "compiler_=runtime|compiler"
    "optimizer_=runtime|compiler"
    "verifier_=runtime|compiler"
    "compilation_cache_=runtime|compiler"
    "vm_=runtime|vm"
    "concurrency_=runtime|concurrency"
)

function(luma_get_test_label test_name label_map default_label out_var)
    foreach(_entry IN LISTS ${label_map})
        string(FIND "${_entry}" "=" _sep_pos)
        string(SUBSTRING "${_entry}" 0 ${_sep_pos} _prefix)
        math(EXPR _val_start "${_sep_pos} + 1")
        string(SUBSTRING "${_entry}" ${_val_start} -1 _raw_label)
        string(REPLACE "|" ";" _label "${_raw_label}")
        if("${test_name}" MATCHES "^${_prefix}")
            set(${out_var} "${_label}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "${default_label}" PARENT_SCOPE)
endfunction()

# ── Analysis tests (link luma_analysis) ──
# Type checker tests are split into multiple files to reduce heap pressure.

set(LUMA_ANALYSIS_TESTS
    lexer_test
    parser_test
    gui_prelude_test
    type_checker_test_basics
    type_checker_test_functions
    type_checker_test_advanced
    type_checker_test_types
    type_checker_test_linear
    type_checker_test_lint
    type_checker_test_generics
    type_checker_test_symbols
    resolver_test
    linter_test
    linter_registry_test
    diagnostic_test
    error_test
    error_recovery_test
    string_interner_test
    pipeline_test
)

foreach(test IN LISTS LUMA_ANALYSIS_TESTS)
    luma_get_test_label(${test} _analysis_label_map "analysis" _label)
    luma_add_test(NAME ${test}
        SOURCES analysis/${test}.cpp
        LIBS    luma_analysis
        LABELS  ${_label})
endforeach()

# Analysis tests that link luma_core (need runtime components).
set(LUMA_ANALYSIS_CORE_TESTS
    include_resolver_test
)

foreach(test IN LISTS LUMA_ANALYSIS_CORE_TESTS)
    luma_add_test(NAME ${test}
        SOURCES analysis/${test}.cpp
        LIBS    luma_core
        LABELS  analysis)
endforeach()

# source_test needs a custom working directory.
luma_add_test(NAME source_test
    SOURCES analysis/source_test.cpp
    LIBS    luma_analysis
    WORKING_DIR "${PROJECT_SOURCE_DIR}"
    LABELS  analysis)

# ── Header-only utility tests (no library linkage required) ──

set(LUMA_COMMON_HEADER_TESTS
    byte_utils_test
    format_number_test
    escape_utils_test
    path_security_test
    utf8_test
    qualified_name_test
    symbol_kind_test
    stdlib_return_type_test
)

foreach(test IN LISTS LUMA_COMMON_HEADER_TESTS)
    luma_add_test(NAME ${test}
        SOURCES analysis/${test}.cpp
        LIBS    ""
        LABELS  analysis)
endforeach()

# decimal_test exercises the Decimal implementation directly (compiled in) so it
# stays independent of the runtime libraries.
luma_add_test(NAME decimal_test
    SOURCES analysis/decimal_test.cpp ${PROJECT_SOURCE_DIR}/core/common/decimal.cpp
    LIBS    ""
    LABELS  analysis)

# ── Platform tests ──

# safe_getenv is cross-platform (std::getenv / _dupenv_s), so it runs on every OS.
luma_add_test(NAME safe_getenv_test
    SOURCES platform/safe_getenv_test.cpp
    LIBS    ""
    LABELS  platform)

# wstring_to_utf8 wraps the Win32 API and only exists on Windows.
if(WIN32)
    luma_add_test(NAME win32_utf8_test
        SOURCES platform/win32_utf8_test.cpp
        LIBS    ""
        LABELS  platform)

    # build_windows_cmdline is a Windows-only helper in luma_core; the round-trip
    # assertions call CommandLineToArgvW, which lives in shell32.
    luma_add_test(NAME win32_cmdline_test
        SOURCES platform/win32_cmdline_test.cpp
        LIBS    luma_core shell32
        LABELS  platform)
endif()

# ── Runtime tests (link luma_core) ──

set(LUMA_RUNTIME_TESTS
    stdlib_test_string
    stdlib_test_array
    stdlib_test_dictionary
    stdlib_test_math
    stdlib_test_calculus
    stdlib_test_linearalgebra
    stdlib_test_decimal
    stdlib_test_binarytree
    stdlib_test_graph
    stdlib_test_hashset
    stdlib_test_keyvaluestore
    stdlib_test_linkedlist
    stdlib_test_queue
    stdlib_test_reference
    stdlib_test_set
    stdlib_test_stack
    stdlib_test_filesystem
    stdlib_test_http
    stdlib_test_console
    stdlib_test_process
    stdlib_test_socket
    stdlib_test_terminal
    stdlib_test_compression
    stdlib_test_converter
    stdlib_test_csv
    stdlib_test_datetime
    stdlib_test_order
    stdlib_test_encoder
    stdlib_test_color
    stdlib_test_hash
    stdlib_test_json
    stdlib_test_log
    stdlib_test_random
    stdlib_test_regex
    stdlib_test_xml
    stdlib_test_result_optional
    stdlib_test_channel_task
    stdlib_test_builtins
    stdlib_catalog_conformance_test
    concurrency_test
    repl_test
    compiler_test
    compiler_opcode_test
    compiler_disassemble_test
    optimizer_test
    verifier_test
    compilation_cache_test
    bytecode_serializer_test
    vm_test_core
    vm_test_types
    vm_test_generics
    vm_test_concurrency
    vm_test_robustness
    vm_stack_api_test
    resource_limits_test
    value_hash_test
    value_collections_test
)

foreach(test IN LISTS LUMA_RUNTIME_TESTS)
    luma_get_test_label(${test} _runtime_label_map "runtime" _label)
    luma_add_test(NAME ${test}
        SOURCES runtime/${test}.cpp
        LIBS    luma_core
        LABELS  ${_label})
endforeach()

# GraphicalUi: the comprehensive suite drives the live webview-backed module and
# is registered only when a WebView backend is available. When webview is
# disabled (LUMA_FEATURE_WEBVIEW=OFF) or no backend is found, luma_core compiles
# the GraphicalUi stub instead, so a dedicated stub test verifies the
# "module unavailable" behaviour (constants still resolve; every function throws).
if(LUMA_WEBVIEW_AVAILABLE)
    luma_add_test(NAME stdlib_test_graphicalui_widgets
        SOURCES runtime/stdlib_test_graphicalui_widgets.cpp
        LIBS    luma_core
        LABELS  runtime stdlib)
    luma_add_test(NAME stdlib_test_graphicalui_styling
        SOURCES runtime/stdlib_test_graphicalui_styling.cpp
        LIBS    luma_core
        LABELS  runtime stdlib)
    luma_add_test(NAME stdlib_test_graphicalui_app
        SOURCES runtime/stdlib_test_graphicalui_app.cpp
        LIBS    luma_core
        LABELS  runtime stdlib)
else()
    luma_add_test(NAME stdlib_test_graphicalui_stub
        SOURCES runtime/stdlib_test_graphicalui_stub.cpp
        LIBS    luma_core
        LABELS  runtime stdlib)
endif()

# cli_test needs a custom working directory.
luma_add_test(NAME cli_test
    SOURCES     runtime/cli_test.cpp
    LIBS        luma_core
    WORKING_DIR "${PROJECT_SOURCE_DIR}"
    LABELS      runtime)

# json_test links luma_shared (not luma_core).
luma_add_test(NAME json_test
    SOURCES runtime/json_test.cpp
    LIBS    luma_shared
    LABELS  runtime)

# protocol_test covers the shared protocol layer (error codes, handler
# registry, transport framing, URI utilities, exception hierarchy).
luma_add_test(NAME protocol_test
    SOURCES runtime/protocol_test.cpp
    LIBS    luma_shared
    LABELS  runtime)

# stdio_transport_test drives the concrete stdin/stdout transport (the real
# production transport for both LSP and DAP) by redirecting the process file
# descriptors to temporary files.  Links luma_shared (not luma_core).
luma_add_test(NAME stdio_transport_test
    SOURCES runtime/stdio_transport_test.cpp
    LIBS    luma_shared
    LABELS  runtime)

# platform_socket_test exercises the shared socket primitives (WinsockGuard,
# sockopt_ptr, close_socket, invalid_socket).  The Winsock calls need ws2_32 on
# Windows; POSIX sockets need no extra library.
if(WIN32)
    luma_add_test(NAME platform_socket_test
        SOURCES runtime/platform_socket_test.cpp
        LIBS    luma_shared ws2_32
        LABELS  runtime)
else()
    luma_add_test(NAME platform_socket_test
        SOURCES runtime/platform_socket_test.cpp
        LIBS    luma_shared
        LABELS  runtime)
endif()

# ── Integration test ──

luma_add_test(NAME integration_test
    SOURCES integration/integration_test.cpp
    LIBS    luma_core
    LABELS  integration)

# ── Language server tests ──

set(LUMA_LSP_TESTS
    lsp_test_protocol
    lsp_test_completion
    lsp_test_navigation
    lsp_test_features
    lsp_test_inlay
    lsp_test_unit
    lsp_test_text_formatter
    lsp_test_analysis_pipeline
    lsp_test_analysis_service
    lsp_test_analysis_cache
    lsp_test_persisted_index
    lsp_test_document_store
)

foreach(test IN LISTS LUMA_LSP_TESTS)
    luma_add_test(NAME ${test}
        SOURCES  ${PROJECT_SOURCE_DIR}/language-server/tests/${test}.cpp
        LIBS     luma_lsp_lib
        INCLUDES ${PROJECT_SOURCE_DIR}/language-server/tests
        LABELS   lsp)
endforeach()

# ── Debugger tests ──

set(LUMA_DAP_TESTS
    dap_test_types
    dap_test_protocol
    dap_test_thread_state
    dap_test_breakpoints
    dap_test_compiled_cache
    dap_test_variable_inspector
    dap_test_breakpoint_manager
    dap_test_misc
    dap_test_time_travel
    dap_test_custom_visualizer
    dap_test_hot_reloader
    dap_test_expression_compiler
)

foreach(test IN LISTS LUMA_DAP_TESTS)
    luma_add_test(NAME ${test}
        SOURCES  ${PROJECT_SOURCE_DIR}/debugger/tests/${test}.cpp
        LIBS     luma_dap_lib
        LABELS   dap)
endforeach()

# TCP transport test — drives accept_client() from a client thread over a
# loopback socket. Registered separately with a timeout so a failed handshake
# can never hang CI on the blocking ::accept call.
luma_add_test(NAME dap_test_tcp_transport
    SOURCES  ${PROJECT_SOURCE_DIR}/debugger/tests/dap_test_tcp_transport.cpp
    LIBS     luma_dap_lib
    TIMEOUT  60
    LABELS   dap)

# DAP integration test — spawns luma_dap as a subprocess and sends
# scripted DAP messages over stdio to verify end-to-end behavior.
# This suite launches the debuggee dozens of times over loopback, so it is
# inherently slow (~145s on the Linux CI runners). macOS runners add
# per-exec Gatekeeper overhead on each spawn and exceed the previous 180s
# ceiling, so the timeout is generous to keep the suite reliable rather than
# flaky. A genuine hang still fails the job well within the 35-minute budget.
luma_add_test(NAME dap_integration_test
    SOURCES  ${PROJECT_SOURCE_DIR}/debugger/tests/dap_integration_test.cpp
    LIBS     luma_shared
    INCLUDES ${PROJECT_SOURCE_DIR}/debugger/source
    TIMEOUT  360
    LABELS   dap integration)

# ── Luma feature test suites ──
# Each .luma file under tests/features/ is registered as a CTest test case.
# Tests are organised into subdirectories:
#   language/  — core language feature tests
#   stdlib/    — standard library module tests
#
# Files are listed explicitly rather than discovered with file(GLOB): a
# CONFIGURE_DEPENDS glob installs a per-build glob-verification stamp that, under
# the Visual Studio (MSBuild) generator, makes every test project trigger its own
# concurrent CMake reconfigure on the first build, and those parallel reconfigures
# race and fail. Explicit lists also follow the project guideline (no file(GLOB))
# and make CMake re-run when the list changes. Add a new .luma suite here.

set(LUMA_FEATURE_TEST_TIMEOUT 30 CACHE STRING
    "Timeout in seconds for each Luma feature test")
message(STATUS "Feature test timeout: ${LUMA_FEATURE_TEST_TIMEOUT}s")

set(LUMA_FEATURE_TESTS_LANGUAGE
    arrays.luma
    choice_types.luma
    closures.luma
    concurrency.luma
    control_flow.luma
    diagnostics.luma
    dictionaries.luma
    downcast.luma
    enums.luma
    error_pipe.luma
    functions.luma
    generics.luma
    include_helper.luma
    include.luma
    interfaces.luma
    is_operator.luma
    lambdas.luma
    linter_clean.luma
    match.luma
    mutability.luma
    named_arguments.luma
    namespaces.luma
    operators.luma
    optional.luma
    ownership.luma
    pipe.luma
    record_destructuring.luma
    records.luma
    reference.luma
    sandbox.luma
    test_annotation.luma
    tuples.luma
    type_aliases.luma
    typed_errors.luma
    types.luma
    value_block_assignment.luma
    value_block_operands.luma
)

set(LUMA_FEATURE_TESTS_STDLIB
    array_functions.luma
    binarytree_functions.luma
    calculus_functions.luma
    channel_functions.luma
    color_functions.luma
    compression_functions.luma
    console_functions.luma
    converter_functions.luma
    csv_functions.luma
    datetime_functions.luma
    decimal_functions.luma
    encoder_functions.luma
    filesystem_functions.luma
    graph_functions.luma
    graphicalui_functions.luma
    graphicalui_interaction.luma
    hash_functions.luma
    hashset_functions.luma
    http_functions.luma
    json_functions.luma
    keyvaluestore_functions.luma
    linearalgebra_functions.luma
    linkedlist_functions.luma
    log_functions.luma
    math_functions.luma
    optional_functions.luma
    order_functions.luma
    process_functions.luma
    queue_functions.luma
    random_functions.luma
    reference_functions.luma
    regularexpression_functions.luma
    resource_functions.luma
    result_functions.luma
    set_functions.luma
    socket_functions.luma
    solaris_functions.luma
    stack_functions.luma
    string_functions.luma
    task_functions.luma
    terminal_functions.luma
    terminal_interaction.luma
    xml_functions.luma
)

# Helper function: register the named .luma files in a subdirectory as CTest
# tests, applying a label and timeout to each. File names are passed in ARGN.
function(luma_register_feature_tests subdir label)
    foreach(_test_file_name ${ARGN})
        set(_test_rel "tests/features/${subdir}/${_test_file_name}")
        get_filename_component(_test_name ${_test_file_name} NAME_WE)

        # The GraphicalUi feature tests — and the Solaris surface tests, which
        # render through the same engine — drive the live webview module; skip
        # them when only the stub is compiled (no LUMA_HAS_WEBVIEW), since every
        # widget call throws "not available" there. The stub path is covered
        # separately by the stdlib_test_graphicalui_stub C++ test.
        if(_test_name MATCHES "^(graphicalui|solaris)" AND NOT LUMA_WEBVIEW_AVAILABLE)
            continue()
        endif()

        if(_test_name STREQUAL "sandbox")
            # Sandbox tests require restricted environment (no filesystem access).
            add_test(
                NAME luma_${_test_name}
                COMMAND $<TARGET_FILE:luma> --box --strict --test ${_test_rel}
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            )
        else()
            add_test(
                NAME luma_${_test_name}
                COMMAND $<TARGET_FILE:luma> --strict --test ${_test_rel}
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            )
        endif()

        set_tests_properties(luma_${_test_name} PROPERTIES
            TIMEOUT ${LUMA_FEATURE_TEST_TIMEOUT}
            LABELS  "feature;${label}"
            ENVIRONMENT "LUMA_TEST_ROOT=${PROJECT_SOURCE_DIR}"
        )
    endforeach()
endfunction()

luma_register_feature_tests(language feature_language ${LUMA_FEATURE_TESTS_LANGUAGE})
luma_register_feature_tests(stdlib   feature_stdlib   ${LUMA_FEATURE_TESTS_STDLIB})
