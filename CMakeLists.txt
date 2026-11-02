cmake_minimum_required(VERSION 3.21)

# CMAKE_CURRENT_* (not CMAKE_SOURCE_DIR) so the check targets this project even
# when it is pulled into a super-build via add_subdirectory().
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported. Remove CMakeCache.txt and CMakeFiles/ "
        "from the source root, then configure out of source, e.g. `cmake --preset default`.")
endif()

# Single source of truth for the project version: the top-level VERSION file.
# The value flows into project(), the generated common/version.hpp, the install
# package version, and CPack metadata.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LUMA_VERSION)
string(STRIP "${LUMA_VERSION}" LUMA_VERSION)

# Re-run configure when VERSION changes so version.hpp and package metadata never go stale.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(luma VERSION ${LUMA_VERSION} LANGUAGES CXX C)

find_package(Threads REQUIRED)

# ─────────── Position-independent code ───────────
# Executables are linked as PIE (-pie) for ASLR hardening (see
# cmake/LumaCompilerFlags.cmake). A PIE executable can only link objects that are
# themselves position-independent, so every static library it pulls in — both
# first-party and vendored — must be compiled -fPIC. Ubuntu's GCC defaults to PIE
# and so happens to satisfy this, but distros whose GCC does not (e.g. Fedora)
# fail to link with "relocation R_X86_64_32S ... can not be used when making a PIE
# object". Enabling PIC globally makes the requirement explicit and portable; it
# is a no-op on Windows and macOS, where code is already relocatable.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ─────────── MSVC runtime library ───────────
# Link the C/C++ runtime statically (/MT, /MTd for Debug) so the shipped
# luma.exe, luma_lsp.exe, and luma_dap.exe run on a machine without the Visual
# C++ Redistributable; the CMake default (/MD) would make them depend on
# MSVCP140.dll / VCRUNTIME140.dll, which the release archives do not carry.
# Set here, after project() and before any add_subdirectory() or add_library(),
# so every target — including the vendored mbedtls_lib and miniz_lib — is
# created with the same runtime (mixing runtimes fails to link). Requires policy
# CMP0091 (NEW under cmake_minimum_required 3.21). Ignored by non-MSVC-ABI
# compilers.
#
# An explicit -DCMAKE_MSVC_RUNTIME_LIBRARY=... always wins. AddressSanitizer
# under MSVC or clang-cl links the DLL ASan runtime (clang_rt.asan_dynamic),
# which requires the DLL C runtime, so a build whose global flags enable
# -fsanitize=address or /fsanitize=address keeps /MD. (The Windows libFuzzer
# build in fuzz/DIRECTORY.md is coverage-only and already uses the static runtime.)
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    if("${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS}" MATCHES "[-/]fsanitize=([^ ]*,)?address")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ─────────── Build-system regeneration (Visual Studio generator) ───────────
# The Visual Studio (MSBuild) generator attaches a build-system regeneration
# check — the ZERO_CHECK mechanism, shown as "Building Custom Rule .../
# CMakeLists.txt" — to every target. On the first build after a fresh configure
# it re-runs CMake, and because MSBuild builds targets in parallel the dozens of
# test targets fire that check simultaneously and race to restore the timestamp
# on generate.stamp, failing with "Cannot restore timestamp ... generate.stamp:
# access denied" (MSB8066), which breaks the build. Suppressing the in-build
# regeneration target removes the race. The build workflow — and the CI/pipeline
# gates — always run the configure step before building, so on-demand
# regeneration is unnecessary; re-run `cmake --preset <name>` after editing any
# CMakeLists.txt. Scoped to Visual Studio because that is where the race occurs;
# other generators are unaffected.
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_SUPPRESS_REGENERATION ON)
endif()

option(LUMA_BUILD_TESTS
    "Build and register unit tests" ON)
option(LUMA_FEATURE_SANITIZERS
    "Enable ASan and UBSan (GCC/Clang)" OFF)
option(LUMA_FEATURE_COVERAGE
    "Enable code coverage instrumentation (GCC/Clang)" OFF)
option(LUMA_FEATURE_TLS
    "Enable HTTPS support via Mbed TLS" ON)

# Note: Fuzz targets (fuzz/) are not included in the standard build.
# They require Clang with -fsanitize=fuzzer support. Build them by
# enabling LUMA_BUILD_FUZZ at configure time:
#   cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#         -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address" \
#         -DLUMA_BUILD_FUZZ=ON
option(LUMA_BUILD_FUZZ
    "Build LibFuzzer targets (requires Clang with -fsanitize=fuzzer)" OFF)
option(LUMA_FEATURE_LTO
    "Enable link-time optimisation for Release builds" OFF)
set(LUMA_DISABLE_LTO_TARGETS "" CACHE STRING
    "Semicolon-separated list of targets to exclude from LTO")
set(LUMA_FEATURE_TEST_TIMEOUT 30 CACHE STRING
    "Timeout in seconds for each Luma feature test (LUMA_BUILD_TESTS)")
set(LUMA_FUZZ_QUICK_TIME 10 CACHE STRING
    "Seconds each fuzz *_quick CTest entry runs (LUMA_BUILD_FUZZ)")

# ─────────── Target naming convention ───────────
#   Executables:  luma, luma_lsp, luma_dap
#   Libraries:    luma_<component>        — luma_core, luma_analysis, luma_shared
#   Tool libs:    luma_<tool>_lib         — luma_lsp_lib, luma_dap_lib (testable lib per tool exe)
#   Stdlib objs:  luma_stdlib_<group>     — luma_stdlib_collections, luma_stdlib_io, …
#   Test infra:   luma_test_framework     — header-only test support (INTERFACE)
#   Test exes:    <component>_test        — lexer_test, compiler_test, …

# ─────────── Generated headers ───────────
# Configure common/version.hpp from the VERSION-derived project version into a
# generated include directory placed on every target's include path via
# luma_project_options (see cmake/LumaCompilerFlags.cmake). The checked-in
# core/common/version.hpp has been removed so this generated copy is the only one.

set(LUMA_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/core/common/version.hpp.in"
    "${LUMA_GENERATED_INCLUDE_DIR}/common/version.hpp"
    @ONLY
)

# ─────────── Compiler flags and compile options ───────────
# Warning flags, security hardening, LTO detection (luma_apply_lto), the
# luma_project_options usage-requirements target, and luma_set_compile_options()
# are defined in cmake/LumaCompilerFlags.cmake. Override any LUMA_*_FLAGS
# variable before the first luma_set_compile_options() call to adjust flags
# project-wide. LUMA_GENERATED_INCLUDE_DIR must be set before this include.

include(cmake/LumaCompilerFlags.cmake)
include(cmake/LumaTargetHelpers.cmake)

# ─────────── Build configuration summary ───────────

message(STATUS "")
message(STATUS "=== Luma Build Configuration ===")
message(STATUS "  Tests:        ${LUMA_BUILD_TESTS}")
message(STATUS "  Sanitizers:   ${LUMA_FEATURE_SANITIZERS}")
message(STATUS "  Coverage:     ${LUMA_FEATURE_COVERAGE}")
message(STATUS "  LTO:          ${LUMA_FEATURE_LTO}")
message(STATUS "  TLS:          ${LUMA_FEATURE_TLS}")
message(STATUS "================================")
message(STATUS "")

# ─── Library Dependency Graph ───
# luma_analysis  →  lexer, parser, type_checker, linter, resolver, pipeline,
#                   diagnostics, source, errors, ast, stdlib_catalog
# luma_core      →  luma_analysis + compiler, vm, interpreter, stdlib,
#                   cli, repl, include, concurrency, stdlib_* OBJECT libs,
#                   luma_tls (mbedtls_lib + LUMA_HAS_TLS), miniz_lib
# luma_lsp_lib   →  luma_analysis + luma_shared (json, protocol)
# luma_dap_lib   →  luma_core + luma_shared (json, protocol)
# luma_shared    →  json, protocol transport (standalone OBJECT library)

# ─────────── Output directories ───────────
# Place every executable at the build-tree root — <build>/luma, or
# <build>/luma.exe on Windows — on every platform and generator, so all
# consumers (CI, release packaging, the editor configs, the benchmark and test
# harnesses, and the documentation) use one path. Targets are defined in
# subdirectories (e.g. core/runtime), which would otherwise place their binaries
# under <build>/core/runtime/… where nothing expects them.
#
# Multi-config generators (Visual Studio, Xcode, Ninja Multi-Config) would
# append a per-configuration subdirectory. Instead, the tree's primary
# configuration, LUMA_PRIMARY_CONFIG, goes at the root, and any other
# configuration built in the same tree goes to <build>/<Config>/. Sharing the
# root between configurations would let one overwrite the other and fool
# MSBuild's up-to-date check into keeping the wrong binary. A generator
# expression in the path suppresses the automatic subdirectory.
#
# Multi-config generators ignore CMAKE_BUILD_TYPE when building, and not every
# preset sets it (msvc, xcode, and windows-arm64 do not), so LUMA_PRIMARY_CONFIG
# is its own cache variable. It defaults to CMAKE_DEFAULT_BUILD_TYPE (Ninja
# Multi-Config's default configuration), then CMAKE_BUILD_TYPE, then Release.
# Ninja Multi-Config is told to build that configuration by default, so a bare
# `cmake --build <dir>` refreshes the root. Visual Studio and Xcode always
# default to Debug, so pass `--config <primary>` (or use a build preset) there;
# without it, the build lands in <build>/Debug/ and the root is left untouched.
#
# Set before add_subdirectory so every target inherits it. Only RUNTIME outputs
# (executables) are redirected; static libraries keep their default locations.
# PROJECT_BINARY_DIR rather than CMAKE_BINARY_DIR keeps the binaries in this
# project's tree when Luma is pulled into a super-build via add_subdirectory(),
# and an output directory the including project has already chosen is respected.
get_property(LUMA_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(LUMA_IS_MULTI_CONFIG)
    if(CMAKE_DEFAULT_BUILD_TYPE)
        set(_luma_primary_config_default "${CMAKE_DEFAULT_BUILD_TYPE}")
    elseif(CMAKE_BUILD_TYPE)
        set(_luma_primary_config_default "${CMAKE_BUILD_TYPE}")
    else()
        set(_luma_primary_config_default "Release")
    endif()
    set(LUMA_PRIMARY_CONFIG "${_luma_primary_config_default}" CACHE STRING
        "Configuration whose executables a multi-config build places at the build-tree root")
    unset(_luma_primary_config_default)
    set_property(CACHE LUMA_PRIMARY_CONFIG PROPERTY STRINGS ${CMAKE_CONFIGURATION_TYPES})
    if(NOT LUMA_PRIMARY_CONFIG IN_LIST CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR
            "LUMA_PRIMARY_CONFIG is '${LUMA_PRIMARY_CONFIG}', which is not one of "
            "CMAKE_CONFIGURATION_TYPES (${CMAKE_CONFIGURATION_TYPES}).")
    endif()
    if(CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
        if(NOT CMAKE_DEFAULT_BUILD_TYPE)
            set(CMAKE_DEFAULT_BUILD_TYPE "${LUMA_PRIMARY_CONFIG}")
        elseif(NOT CMAKE_DEFAULT_BUILD_TYPE STREQUAL LUMA_PRIMARY_CONFIG)
            message(WARNING
                "CMAKE_DEFAULT_BUILD_TYPE (${CMAKE_DEFAULT_BUILD_TYPE}) differs from "
                "LUMA_PRIMARY_CONFIG (${LUMA_PRIMARY_CONFIG}): a build without --config "
                "places its executables in ${PROJECT_BINARY_DIR}/${CMAKE_DEFAULT_BUILD_TYPE}/.")
        endif()
    endif()
    message(STATUS "Primary configuration (at the build-tree root): ${LUMA_PRIMARY_CONFIG}")
endif()

if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    if(LUMA_IS_MULTI_CONFIG)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
            "${PROJECT_BINARY_DIR}$<$<NOT:$<CONFIG:${LUMA_PRIMARY_CONFIG}>>:/$<CONFIG>>")
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    endif()
endif()

# ─────────── Component subdirectories ───────────
# Each subdirectory creates its targets via the luma_add_* helpers, which apply
# compile options and LTO at creation time. Order matters: dependencies must be
# defined before dependents.

add_subdirectory(core/common)       # luma_common
add_subdirectory(shared)            # luma_shared, luma_stdlib_catalog
add_subdirectory(core/analysis)     # luma_analysis (+ luma_common, luma_stdlib_catalog objects)
add_subdirectory(core/runtime)      # luma_stdlib_*, luma_core, luma
add_subdirectory(language-server)   # luma_lsp_lib, luma_lsp
add_subdirectory(debugger)          # luma_dap_lib, luma_dap

# ─────────── Windows file metadata ───────────
# Version information and an application manifest for the shipped executables,
# so antivirus heuristics do not treat them as anonymous binaries. No-op
# elsewhere. See the module for details.

include(cmake/LumaWindowsResources.cmake)
luma_add_windows_metadata(luma     DESCRIPTION "Luma Programming Language Interpreter")
luma_add_windows_metadata(luma_lsp DESCRIPTION "Luma Language Server")
luma_add_windows_metadata(luma_dap DESCRIPTION "Luma Debug Adapter")

# ─────────── Testing ───────────
# enable_testing() is called once here so both the fuzz and unit-test trees can
# register tests with CTest.

enable_testing()

if(LUMA_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()

if(LUMA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ─────────── Platform code generation (extensions) ───────────
# Defines the generate_platform_code target that regenerates the TypeScript
# and Rust platform mappings from the shared platform-map.json. See the module.

include(cmake/LumaPlatformCodegen.cmake)

# ─────────── Installation ───────────

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(LUMA_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/luma")
set(LUMA_INSTALL_EXAMPLES_DIR "${CMAKE_INSTALL_DATADIR}/luma/examples")

install(TARGETS luma
    EXPORT LumaTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
)

# Only install the LSP and DAP tools if they were built.
foreach(_tool luma_lsp luma_dap)
    if(TARGET ${_tool})
        install(TARGETS ${_tool}
            EXPORT LumaTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Tools
        )
    endif()
endforeach()

# Export targets file so downstream projects can use Luma::luma etc.
install(EXPORT LumaTargets
    NAMESPACE Luma::
    DESTINATION ${LUMA_CMAKE_INSTALL_DIR}
    COMPONENT Development
)

# Generate package config and version files.
configure_package_config_file(
    cmake/LumaConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/LumaConfig.cmake"
    INSTALL_DESTINATION ${LUMA_CMAKE_INSTALL_DIR}
    PATH_VARS LUMA_INSTALL_EXAMPLES_DIR
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LumaConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LumaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LumaConfigVersion.cmake"
    DESTINATION ${LUMA_CMAKE_INSTALL_DIR}
    COMPONENT Development
)

# No C++ headers are installed: Luma ships executables only and exports no
# library to link against, and the internal headers are not self-contained
# (they reach into core/common, lexer, source, …), so installing a subset would
# hand downstream consumers headers that cannot compile.

# Install example programs.
install(DIRECTORY examples/
    DESTINATION ${LUMA_INSTALL_EXAMPLES_DIR}
    COMPONENT Examples
    FILES_MATCHING PATTERN "*.luma"
)

# Install documentation.
install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT Documentation
)

# ─────────── Convenience targets for code quality tools ───────────
# Defines the format and tidy targets when clang-format / clang-tidy are found.

include(cmake/LumaCodeQuality.cmake)

# ─────────── CPack packaging ───────────
# Configures CPack metadata, components, and per-platform generators, then
# includes CPack. Must come after the install() rules so the components exist.

include(cmake/LumaPackaging.cmake)
