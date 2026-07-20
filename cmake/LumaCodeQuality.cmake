# cmake/LumaCodeQuality.cmake — Convenience targets for code quality tools.
#
# Defines the `format` and `tidy` custom targets when clang-format / clang-tidy
# are found. Both delegate to cmake/LumaRunClangTool.cmake, which discovers the
# sources at build time so newly added files are covered without reconfiguring.
#
# Included from the root CMakeLists.txt. include() preserves the including file's
# directory scope, so CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR resolve to the project
# root and its binary dir exactly as they did inline.

include_guard(GLOBAL)

find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
    # Sources are discovered at build time by cmake/LumaRunClangTool.cmake so that
    # newly added files are covered without reconfiguring (file(GLOB_RECURSE) only
    # runs when that script runs).
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=format
            -DTOOL_EXE=${CLANG_FORMAT_EXE}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/LumaRunClangTool.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format on all source files...")
endif()

find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE)
    # Uses the same build-time discovery approach as the format target above.
    add_custom_target(tidy
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=tidy
            -DTOOL_EXE=${CLANG_TIDY_EXE}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/LumaRunClangTool.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-tidy on all source files...")
endif()
