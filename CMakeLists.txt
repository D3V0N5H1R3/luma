cmake_minimum_required(VERSION 3.21)

# Single source of truth for the project version: the top-level VERSION file.
# The value flows into project(), the generated common/version.hpp, the install
# package version, and CPack metadata.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LUMA_VERSION)
string(STRIP "${LUMA_VERSION}" LUMA_VERSION)

project(luma VERSION ${LUMA_VERSION} LANGUAGES CXX C)

find_package(Threads REQUIRED)

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
option(LUMA_FEATURE_WEBVIEW
    "Enable the GraphicalUi module via the platform WebView backend" ON)

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

# ─────────── Backward compatibility for renamed options ───────────
function(luma_migrate_option old_name new_name)
    if(DEFINED ${old_name})
        set(${new_name} ${${old_name}} CACHE BOOL "" FORCE)
        message(DEPRECATION "Option ${old_name} is deprecated; use ${new_name} instead")
    endif()
endfunction()

luma_migrate_option(LUMA_ENABLE_SANITIZERS LUMA_FEATURE_SANITIZERS)
luma_migrate_option(LUMA_ENABLE_COVERAGE LUMA_FEATURE_COVERAGE)
luma_migrate_option(LUMA_ENABLE_LTO LUMA_FEATURE_LTO)
luma_migrate_option(LUMA_ENABLE_TLS LUMA_FEATURE_TLS)

# ─────────── Target naming convention ───────────
#   Executables:  luma, luma_lsp, luma_dap
#   Libraries:    luma_<component>        — luma_core, luma_analysis, luma_shared
#   Tool libs:    luma_<tool>_lib         — luma_lsp_lib, luma_dap_lib (testable lib per tool exe)
#   Stdlib objs:  luma_stdlib_<group>     — luma_stdlib_collections, luma_stdlib_io, …
#   Test infra:   luma_test_framework     — header-only test support (INTERFACE)
#   Test exes:    <component>_test        — lexer_test, parser_test, …

# ─────────── Generated headers ───────────
# Configure common/version.hpp from the VERSION-derived project version into a
# generated include directory placed on every target's include path via
# luma_project_options (see cmake/LumaCompilerFlags.cmake). The checked-in
# core/common/version.hpp has been removed so this generated copy is the only one.

set(LUMA_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
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
message(STATUS "  WebView:      ${LUMA_FEATURE_WEBVIEW}")
message(STATUS "================================")
message(STATUS "")

# ─── Library Dependency Graph ───
# luma_analysis  →  lexer, parser, type_checker, linter, resolver, pipeline,
#                   diagnostics, source, errors, ast, stdlib_catalog
# luma_core      →  luma_analysis + compiler, vm, interpreter, stdlib,
#                   cli, repl, include, concurrency, stdlib_* OBJECT libs
# luma_lsp_lib   →  luma_analysis + luma_shared (json, protocol)
# luma_dap_lib   →  luma_core + luma_shared (json, protocol)
# luma_shared    →  json, protocol transport (standalone OBJECT library)

# ─────────── Output directories ───────────
# Place every executable at the build-tree root so all consumers — CI, release
# packaging, the editor configs (VS Code, Zed), the benchmark and test
# harnesses, and the documentation — find them at one predictable location:
#   single-config generators (Ninja, Makefiles):    <build>/luma
#   multi-config generators (Visual Studio, Xcode):  <build>/<Config>/luma(.exe)
# Targets are defined in subdirectories (e.g. core/runtime), which would
# otherwise place their binaries under <build>/core/runtime/… where nothing
# expects them. This is set before add_subdirectory so every target inherits it.
# Only RUNTIME outputs (executables) are redirected; static libraries keep their
# default per-directory locations.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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

# Install public-facing headers for downstream C++ consumers.
# Note: core/common/ headers are intentionally not installed. They are internal
# utilities (base64, string helpers, hashing, etc.) used only within the Luma
# build. The public API surface is limited to the types, AST nodes, and
# interpreter forward declarations listed below.
install(DIRECTORY core/analysis/types/ DESTINATION include/luma/analysis/types
    COMPONENT Development
    FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY core/runtime/interpreter/ DESTINATION include/luma/runtime/interpreter
    COMPONENT Development
    FILES_MATCHING PATTERN "*fwd.hpp")
install(DIRECTORY core/analysis/ast/ DESTINATION include/luma/analysis/ast
    COMPONENT Development
    FILES_MATCHING PATTERN "*.hpp")

# Install example programs.
install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/luma/examples
    COMPONENT Examples
    FILES_MATCHING PATTERN "*.luma"
)

# Install documentation.
install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT Documentation
)

# ─────────── Doxygen documentation ───────────
# Defines the docs target (Doxygen) when the toolchain is available. See module.

include(cmake/LumaDoxygen.cmake)

# ─────────── Convenience targets for code quality tools ───────────
# Defines the format and tidy targets when clang-format / clang-tidy are found.

include(cmake/LumaCodeQuality.cmake)

# ─────────── CPack packaging ───────────
# Configures CPack metadata, components, and per-platform generators, then
# includes CPack. Must come after the install() rules so the components exist.

include(cmake/LumaPackaging.cmake)
