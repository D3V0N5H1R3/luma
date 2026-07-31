# shared/CMakeLists.txt
# JSON utilities and protocol transport shared by the language server and debugger.
# Also builds the stdlib catalog shared between luma_analysis and luma_core.

# ─── Shared utilities (JSON, protocol transport) ─────────────────────────────

luma_add_library(luma_shared OBJECT
    json/json.cpp
    json/json_parser.cpp
    protocol/error_recovery.cpp
    protocol/transport.cpp
    protocol/buffered_transport.cpp
    protocol/stdio_transport.cpp
    protocol/uri_utils.cpp
)

target_include_directories(luma_shared PUBLIC
    ${PROJECT_SOURCE_DIR}/shared
)

# ─── Stdlib catalog ──────────────────────────────────────────────────────────
# Compiled as a separate OBJECT library to prevent duplicate symbol issues when
# both luma_analysis and luma_core embed the same translation unit.

luma_add_library(luma_stdlib_catalog OBJECT
    stdlib/stdlib_catalog.cpp
    stdlib/stdlib_catalog_math.cpp
    stdlib/stdlib_catalog_statistics.cpp
    stdlib/stdlib_catalog_string.cpp
    stdlib/stdlib_catalog_array.cpp
    stdlib/stdlib_catalog_dictionary.cpp
    stdlib/stdlib_catalog_containers.cpp

    stdlib/stdlib_catalog_io.cpp
    stdlib/stdlib_catalog_serialization.cpp
    stdlib/stdlib_catalog_encoding.cpp
    stdlib/stdlib_catalog_numerics.cpp
    stdlib/stdlib_catalog_log_terminal.cpp
    stdlib/stdlib_catalog_graphical_ui.cpp
    stdlib/stdlib_catalog_error_handling.cpp
    stdlib/stdlib_catalog_task.cpp
    stdlib/stdlib_catalog_channel.cpp
    stdlib/stdlib_catalog_datetime.cpp
    stdlib/stdlib_catalog_order.cpp
    stdlib/stdlib_catalog_color.cpp
)
