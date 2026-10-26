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

# Visual Studio's "C++ Clang tools for Windows" component ships clang-format but
# puts it on PATH only inside a Developer prompt. With a Visual Studio generator,
# CMAKE_GENERATOR_INSTANCE is that installation, so search its LLVM directory
# after PATH (PATHS, not HINTS, keeps a clang-format on PATH preferred). The host
# architecture selects the binary: an ARM64 build does not start on x64.
set(_luma_vs_llvm_bin "")
if(CMAKE_GENERATOR_INSTANCE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(_luma_vs_llvm_bin "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/ARM64/bin")
    else()
        set(_luma_vs_llvm_bin "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/x64/bin")
    endif()
endif()

find_program(LUMA_CLANG_FORMAT_EXE clang-format PATHS ${_luma_vs_llvm_bin})
if(LUMA_CLANG_FORMAT_EXE)
    # Sources are discovered at build time by cmake/LumaRunClangTool.cmake so that
    # newly added files are covered without reconfiguring (file(GLOB_RECURSE) only
    # runs when that script runs).
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=format
            -DTOOL_EXE=${LUMA_CLANG_FORMAT_EXE}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/LumaRunClangTool.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format on all source files...")
endif()

find_program(LUMA_CLANG_TIDY_EXE clang-tidy)
if(LUMA_CLANG_TIDY_EXE)
    # Uses the same build-time discovery approach as the format target above.
    add_custom_target(tidy
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=tidy
            -DTOOL_EXE=${LUMA_CLANG_TIDY_EXE}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/LumaRunClangTool.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-tidy on all source files...")
endif()
