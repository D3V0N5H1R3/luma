# cmake/LumaCompilerFlags.cmake — Compiler warning, security, and
# instrumentation flags for the Luma project.
#
# Include this file from the root CMakeLists.txt before calling
# luma_set_compile_options() on any target.
#
# Note: Warning flags defined here and naming/style checks in .clang-tidy
# serve complementary roles and should be kept in sync.  When adding a
# new warning flag here, verify that the corresponding clang-tidy check
# (if any) is not disabled in .clang-tidy, and vice versa.
#
# Run `python scripts/check_warning_sync.py` to verify consistency.
# Use --strict in CI to fail on mismatches.

include_guard(GLOBAL)

# ─────────── Warning flags ───────────

set(LUMA_MSVC_WARN_FLAGS
    /W4 /permissive- /utf-8 /EHsc
    /we4715   # Treat "not all paths return a value" as error.
    /we4062   # Treat unhandled enum in switch as error.
)

set(LUMA_GCC_WARN_FLAGS
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference
    -Wformat=2 -Wformat-security
    -Wimplicit-fallthrough
    -Wdouble-promotion
    -Wundef
    -Wno-missing-field-initializers
)

# ─────────── Security hardening flags ───────────

set(LUMA_MSVC_SECURITY_FLAGS
    /sdl      # Security Development Lifecycle checks.
    /GS       # Buffer security checks (stack canaries).
    /guard:cf # Control Flow Guard.
)

set(LUMA_GCC_SECURITY_FLAGS
    -fstack-protector-strong
)

set(LUMA_MSVC_LINKER_SECURITY_FLAGS
    /DYNAMICBASE # Address Space Layout Randomization (ASLR).
    /NXCOMPAT    # Data Execution Prevention (DEP).
    /CETCOMPAT   # Intel CET shadow stack compatibility.
)

set(LUMA_LINUX_LINKER_SECURITY_FLAGS
    -Wl,-z,relro,-z,now    # Full RELRO (GOT hardening).
    -Wl,-z,noexecstack     # Non-executable stack.
)

# ─────────── Project-wide usage requirements ───────────
# A single INTERFACE target carrying the include directories and language
# standard shared by every Luma target. luma_set_compile_options() links it so
# these settings live on a target rather than being re-applied imperatively to
# each one. LUMA_GENERATED_INCLUDE_DIR is set by the root CMakeLists.txt and
# holds the configured headers (e.g. common/version.hpp).

add_library(luma_project_options INTERFACE)
target_compile_features(luma_project_options INTERFACE cxx_std_20)
target_include_directories(luma_project_options INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/core>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/shared>
    $<BUILD_INTERFACE:${LUMA_GENERATED_INCLUDE_DIR}>
)

# ─────────── Link-time optimisation (LTO) ───────────
# Support is detected once here; luma_apply_lto() applies it per-target during
# Release builds, skipping any target listed in LUMA_DISABLE_LTO_TARGETS.

if(LUMA_FEATURE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _lto_supported OUTPUT _lto_error)

    # Promote the detection result to module-level state so luma_apply_lto()
    # reads it explicitly instead of inheriting the directory-scoped
    # _lto_supported from its call site (mirrors LUMA_WEBVIEW_AVAILABLE in
    # cmake/LumaWebView.cmake).
    set(LUMA_LTO_SUPPORTED ${_lto_supported} CACHE INTERNAL
        "Whether the toolchain supports interprocedural (link-time) optimisation")

    if(LUMA_LTO_SUPPORTED)
        message(STATUS "LTO enabled (excluding: ${LUMA_DISABLE_LTO_TARGETS})")
    else()
        message(WARNING "LTO requested but not supported: ${_lto_error}")
    endif()
endif()

function(luma_apply_lto target)
    if(NOT LUMA_FEATURE_LTO OR NOT LUMA_LTO_SUPPORTED)
        return()
    endif()

    if(target IN_LIST LUMA_DISABLE_LTO_TARGETS)
        message(STATUS "  LTO disabled for target: ${target}")
        return()
    endif()

    set_target_properties(${target} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endfunction()

# ─────────── Stack reservation ───────────

# Enlarge the reserved stack for executables so the parser's max_parse_depth
# limit (see core/common/resource_limits.hpp) keeps generous head-room before
# recursive-descent parsing could exhaust the native stack.  Windows threads
# default to a 1 MB reserve — far below the ~8 MB POSIX main-thread default the
# limit was calibrated against — so raise it to 8 MB there.  On Windows this
# reserve also becomes the default for std::thread workers (e.g. the language
# server's analysis thread), which inherit the PE-header value.  POSIX main
# threads already default to 8 MB, so no change is needed elsewhere.
function(luma_set_stack_reserve target_name)
    if(MSVC)
        # /STACK:<reserve> — accepted by both the MSVC linker and clang-cl's
        # lld-link.
        set(_luma_stack_reserve_bytes 8388608)  # 8 MiB = 8 * 1024 * 1024 bytes.
        target_link_options(${target_name} PRIVATE /STACK:${_luma_stack_reserve_bytes})
    endif()
endfunction()

# ─────────── Common compile options ───────────

function(luma_set_compile_options target_name)
    # Link the project-wide usage requirements (include dirs + C++20). Libraries
    # propagate them (PUBLIC) so consumers inherit the standard; executables keep
    # them PRIVATE as they expose no compile interface.
    get_target_property(_type ${target_name} TYPE)

    if(_type MATCHES "_LIBRARY")
        target_link_libraries(${target_name} PUBLIC luma_project_options)
    else()
        target_link_libraries(${target_name} PRIVATE luma_project_options)
    endif()

    set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            ${LUMA_MSVC_WARN_FLAGS} ${LUMA_MSVC_SECURITY_FLAGS})
        # clang-cl supports GCC-style warning flags that plain MSVC does not.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target_name} PRIVATE
                # ?? is a valid Luma operator; string literals containing ??X
                # sequences are not intended as trigraphs.
                -Wno-trigraphs
                # Aggregate initializers that intentionally omit fields with
                # default values (e.g. optional<Diagnostic>, std::string).
                -Wno-missing-field-initializers
                -Wno-unused-private-field
                -Wno-unused-local-typedef
            )
        endif()
    else()
        target_compile_options(${target_name} PRIVATE
            ${LUMA_GCC_WARN_FLAGS} ${LUMA_GCC_SECURITY_FLAGS})
    endif()

    # Linker security hardening.
    if(_type STREQUAL "EXECUTABLE")
        if(MSVC)
            target_link_options(${target_name} PRIVATE
                ${LUMA_MSVC_LINKER_SECURITY_FLAGS})
        else()
            target_compile_options(${target_name} PRIVATE -fPIE)
            target_link_options(${target_name} PRIVATE -pie)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                target_link_options(${target_name} PRIVATE
                    ${LUMA_LINUX_LINKER_SECURITY_FLAGS})
            endif()
        endif()
    endif()

    # Preprocessor hardening — enable checked iterators and fortified
    # libc wrappers in Release builds.
    if(NOT MSVC)
        target_compile_definitions(${target_name} PRIVATE
            $<$<CONFIG:Release>:_FORTIFY_SOURCE=2>
        )
    endif()

    luma_apply_sanitizers(${target_name})
    luma_apply_coverage(${target_name})
endfunction()

# ─────────── Instrumentation helpers (sanitizers / coverage) ───────────

set(LUMA_SANITIZE_COMPILE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
# Link sanitizer executables as non-PIE. The project builds position-independent
# code globally (CMAKE_POSITION_INDEPENDENT_CODE ON), which otherwise links the
# instrumented binaries as PIE. On CI runners with high ASLR entropy the
# randomized PIE load base can collide with the sanitizer's fixed shadow-memory
# region, crashing every binary at startup before any diagnostic is printed. A
# non-PIE executable loads at a fixed low address that never overlaps the shadow
# — the canonical fix for "instrumented binary segfaults at startup". PIC objects
# link into a non-PIE executable without issue.
set(LUMA_SANITIZE_LINK_FLAGS    -fsanitize=address,undefined -no-pie)
set(LUMA_COVERAGE_COMPILE_FLAGS --coverage -fprofile-arcs -ftest-coverage)
set(LUMA_COVERAGE_LINK_FLAGS    --coverage)

# Shared implementation for the sanitizer and coverage passes. Both are no-ops
# under MSVC, apply their compile flags to the target, and add link flags only to
# targets that actually link: object libraries are compiled and then linked into
# their consumers, so linker flags placed on them would be dropped with a warning.
function(_luma_apply_instrumentation target_name feature compile_flags link_flags)
    if(NOT feature OR MSVC)
        return()
    endif()

    target_compile_options(${target_name} PRIVATE ${compile_flags})

    get_target_property(_type ${target_name} TYPE)
    if(NOT _type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target_name} PRIVATE ${link_flags})
    endif()
endfunction()

function(luma_apply_sanitizers target_name)
    _luma_apply_instrumentation(${target_name} "${LUMA_FEATURE_SANITIZERS}"
        "${LUMA_SANITIZE_COMPILE_FLAGS}" "${LUMA_SANITIZE_LINK_FLAGS}")
endfunction()

function(luma_apply_coverage target_name)
    _luma_apply_instrumentation(${target_name} "${LUMA_FEATURE_COVERAGE}"
        "${LUMA_COVERAGE_COMPILE_FLAGS}" "${LUMA_COVERAGE_LINK_FLAGS}")
endfunction()

# Target-creation helpers (luma_add_executable / luma_add_library /
# luma_add_tool_library) live in cmake/LumaTargetHelpers.cmake. WebView support
# is provided by the luma_webview INTERFACE target from cmake/LumaWebView.cmake.
