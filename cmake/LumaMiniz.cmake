# cmake/LumaMiniz.cmake — Vendored miniz (deflate/inflate/gzip) static library.
#
# miniz 3.1.0 — vendored 2026-05-15 from
# https://github.com/richgel999/miniz/releases/tag/3.1.0
#
# Defines the `miniz_lib` target only. Linking it into luma_core is the caller's
# responsibility (see core/runtime/CMakeLists.txt).

include_guard(GLOBAL)

add_library(miniz_lib STATIC
    ${PROJECT_SOURCE_DIR}/external/miniz/miniz.c
    ${PROJECT_SOURCE_DIR}/external/miniz/miniz_tdef.c
    ${PROJECT_SOURCE_DIR}/external/miniz/miniz_tinfl.c
)

target_include_directories(miniz_lib SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/external/miniz)

target_compile_definitions(miniz_lib PRIVATE
    MINIZ_NO_STDIO
    MINIZ_NO_ARCHIVE_APIS
)

# Vendored C: build to C99 and silence our strict warnings (see LumaTargetHelpers).
luma_configure_vendored_c_target(miniz_lib)
