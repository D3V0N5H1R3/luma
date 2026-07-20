# cmake/LumaWebView.cmake — Detect the platform WebView backend and expose it as
# the `luma_webview` INTERFACE target.
#
# Linking `luma_webview` (PRIVATE) provides the compile-time usage requirements:
#   - the bundled external/webview header include directory (always),
#   - LUMA_HAS_WEBVIEW=1 plus any required compile options (only when a backend
#     was found).
# The platform link libraries are exposed separately via LUMA_WEBVIEW_LINK_LIBRARIES
# so the runtime can link them PUBLIC on luma_core (reaching the final binaries)
# without leaking the compile-time requirements to luma_core's consumers.
#
# Options:
#   LUMA_FEATURE_WEBVIEW — when OFF, skip backend detection entirely and compile
#                          the GraphicalUi stub regardless of platform. ON by default.
#   LUMA_REQUIRE_WEBVIEW — when ON, raise a fatal error if no backend is found
#                          instead of silently disabling the GraphicalUi module.

include_guard(GLOBAL)

option(LUMA_REQUIRE_WEBVIEW "Require WebView support (fatal error if not found)" OFF)

set(_webview_found FALSE)
set(_webview_libraries "")
set(_webview_compile_options "")

if(NOT LUMA_FEATURE_WEBVIEW)
    # Explicitly disabled — leave _webview_found FALSE so the GraphicalUi stub is
    # compiled. No platform detection or link libraries are needed.
elseif(WIN32)
    # WebView2 is lazy-loaded at runtime via the WebView2 Loader DLL; only basic
    # Win32 libs are needed at link time. The runtime self-updates (Evergreen)
    # and is pre-installed on Windows 10 1803+ and all Windows 11 builds, so no
    # minimum version is checked here.
    set(_webview_libraries advapi32 ole32 shell32 shlwapi user32 version)
    set(_webview_found TRUE)
elseif(APPLE)
    find_library(WEBKIT_LIB WebKit)
    find_library(COCOA_LIB Cocoa)
    if(WEBKIT_LIB AND COCOA_LIB)
        set(_webview_libraries ${WEBKIT_LIB} ${COCOA_LIB})
        set(_webview_compile_options -fobjc-arc)
        set(_webview_found TRUE)
    endif()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        # IMPORTED_TARGET bundles all pkg-config metadata (include dirs, library
        # dirs, link flags) into a single CMake target. GLOBAL promotes it to
        # global scope: luma_core links it PUBLIC (see core/runtime/CMakeLists.txt),
        # so the imported target must be visible in every directory that consumes
        # luma_core, not just the core/runtime scope where this file is included.
        pkg_check_modules(WEBKITGTK IMPORTED_TARGET GLOBAL webkit2gtk-4.1)
    endif()
    if(WEBKITGTK_FOUND)
        set(_webview_libraries PkgConfig::webkit2gtk-4.1)
        set(_webview_found TRUE)
    endif()
endif()

add_library(luma_webview INTERFACE)
# SYSTEM so the bundled third-party header is treated as a system include and its
# compiler warnings are suppressed, matching the project policy of only linting
# first-party code. Note: clang-tidy's static-analyzer diagnostics ignore system,
# external and header-filter settings, so the two analyzer checks that fire inside
# webview.h are instead carved out by name in the root .clang-tidy.
target_include_directories(luma_webview SYSTEM INTERFACE
    ${PROJECT_SOURCE_DIR}/external/webview)

if(_webview_found)
    target_compile_definitions(luma_webview INTERFACE LUMA_HAS_WEBVIEW=1)
    if(_webview_compile_options)
        target_compile_options(luma_webview INTERFACE ${_webview_compile_options})
    endif()
elseif(LUMA_REQUIRE_WEBVIEW)
    message(FATAL_ERROR
        "WebView not found but LUMA_REQUIRE_WEBVIEW is ON.\n"
        "  Windows:  WebView2 should be available automatically.\n"
        "  macOS:    Requires WebKit.framework and Cocoa.framework.\n"
        "  Linux:    Install webkit2gtk-4.1 (e.g. libwebkit2gtk-4.1-dev).")
elseif(NOT LUMA_FEATURE_WEBVIEW)
    message(STATUS "WebView disabled (LUMA_FEATURE_WEBVIEW=OFF) — GraphicalUi module will be disabled")
elseif(APPLE)
    message(STATUS "WebKit/Cocoa not found — GraphicalUi module will be disabled")
elseif(NOT WIN32)
    message(STATUS "WebKitGTK not found — GraphicalUi module will be disabled")
endif()

# Expose the resolved backend state so other parts of the build (e.g. the test
# suite) can branch on whether the live GraphicalUi module or its stub is active.
set(LUMA_WEBVIEW_AVAILABLE ${_webview_found} CACHE INTERNAL
    "Whether a WebView backend was found and the GraphicalUi module is active")

# Platform link libraries are exposed separately (rather than as INTERFACE link
# libraries on luma_webview) so the runtime can link them PUBLIC on luma_core —
# reaching the final binaries — while the compile-time usage requirements above
# stay PRIVATE to the targets that actually compile WebView code.
set(LUMA_WEBVIEW_LINK_LIBRARIES "${_webview_libraries}" CACHE INTERNAL
    "Platform libraries required to link the WebView backend")
