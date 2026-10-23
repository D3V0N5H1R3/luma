# cmake/LumaPackaging.cmake — CPack packaging configuration.
#
# Configures CPack metadata, install components, and the per-platform generators
# (ZIP/TGZ everywhere, DEB+RPM on Linux, NSIS on Windows), then includes CPack.
#
# Included as the final step of the root CMakeLists.txt. include() preserves the
# including file's directory scope, so PROJECT_VERSION, CMAKE_SOURCE_DIR, and the
# install components defined earlier resolve exactly as they did inline.

include_guard(GLOBAL)

# Escape CPACK_* values when writing CPackConfig.cmake / CPackSourceConfig.cmake so
# cpack receives them exactly as set here. Without it, cpack re-parses the raw
# values and drops backslashes, turning each regex "\." in
# CPACK_SOURCE_IGNORE_FILES into ".", which matches any character.
set(CPACK_VERBATIM_VARIABLES TRUE)

set(CPACK_PACKAGE_NAME "luma")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Luma programming language interpreter, language server, and debugger")
set(CPACK_PACKAGE_VENDOR "Luma Contributors")
set(CPACK_PACKAGE_CONTACT "d3v0n5h1r3@users.noreply.github.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/d3v0n5h1r3/luma")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# Component definitions.
set(CPACK_COMPONENTS_ALL Runtime Tools Examples Documentation Development)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Luma Interpreter")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "The Luma interpreter executable.")
set(CPACK_COMPONENT_RUNTIME_REQUIRED TRUE)

set(CPACK_COMPONENT_TOOLS_DISPLAY_NAME "Developer Tools")
set(CPACK_COMPONENT_TOOLS_DESCRIPTION "Language server (luma_lsp) and debugger (luma_dap).")
set(CPACK_COMPONENT_TOOLS_DEPENDS Runtime)

set(CPACK_COMPONENT_EXAMPLES_DISPLAY_NAME "Example Programs")
set(CPACK_COMPONENT_EXAMPLES_DESCRIPTION
    "Example Luma programs and language feature demonstrations.")

set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")
set(CPACK_COMPONENT_DOCUMENTATION_DESCRIPTION "README, license, and project documentation.")

set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "CMake Package Config")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "CMake config files for find_package(Luma).")
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime)

# NSIS config (Windows).
set(CPACK_NSIS_DISPLAY_NAME "Luma Programming Language")
set(CPACK_NSIS_PACKAGE_NAME "Luma")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_HELP_LINK "https://github.com/d3v0n5h1r3/luma")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/d3v0n5h1r3/luma")

# DEB config (Debian).
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Luma Contributors <d3v0n5h1r3@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/d3v0n5h1r3/luma")

# Derive the exact libc6 / libstdc++6 / libgcc-s1 minimum versions from the
# binaries actually built: a hand-written floor goes stale with every toolchain
# bump (GCC 13+ binaries need far newer glibc and libstdc++ than any fixed value).
# dpkg-shlibdeps only exists on Debian-family hosts and cannot resolve the
# libraries of a foreign architecture (e.g. the arm64-cross preset), so in those
# cases fall back to unversioned runtime dependencies — incomplete, but never wrong.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(LUMA_DPKG_SHLIBDEPS_EXE dpkg-shlibdeps)
    if(LUMA_DPKG_SHLIBDEPS_EXE AND NOT CMAKE_CROSSCOMPILING)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    else()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libgcc-s1")
        message(VERBOSE "DEB package will declare unversioned dependencies "
            "(dpkg-shlibdeps unavailable or cross-compiling)")
    endif()
endif()

# RPM config (Red Hat).
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
set(CPACK_RPM_PACKAGE_URL "https://github.com/d3v0n5h1r3/luma")

# Generator selection.
set(CPACK_GENERATOR "ZIP;TGZ")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CPACK_GENERATOR "DEB" "RPM")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND CPACK_GENERATOR "NSIS")
endif()

# Source package (`package_source` target). CPack archives the whole source tree,
# so exclude the build trees, IDE state, caches, and fetched checkouts that the
# root and extensions/zed .gitignore files list — otherwise any in-tree build-*/
# directory, or the full repository clone Zed fetches into
# extensions/zed/grammars/luma/, is packed. Setting this variable replaces CPack's
# default list, so its VCS and editor-temporary patterns are restated. CPack
# matches the patterns against absolute paths, so each is anchored to the source
# root: an unanchored "/target/" would drop the entire tree whenever the checkout
# itself lives under such a directory. Build trees match only at the root, because
# tracked directories such as .github/actions/build-vscode-extension/ share the
# build-* prefix.
string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" _luma_source_regex
    "${PROJECT_SOURCE_DIR}")
set(_luma_any_dir "^${_luma_source_regex}/(.*/)?")
set(CPACK_SOURCE_IGNORE_FILES
    "^${_luma_source_regex}/(build|build-[^/]*|cmake-build-[^/]*|out|install)/"
    "^${_luma_source_regex}/extensions/zed/grammars/(luma/|[^/]*\\.wasm$)"
    "^${_luma_source_regex}/extensions/zed/extension\\.wasm$"
    # Compiled-bytecode caches; the fuzz/corpus .lumc seeds are tracked and kept.
    "^${_luma_source_regex}/([^/]*|(benchmarks|examples|tests)/.*)\\.lumc$"
    "${_luma_any_dir}(CVS|\\.svn|\\.bzr|\\.hg|\\.git)(/|$)"
    "${_luma_any_dir}(\\.vs|\\.idea|\\.luma|\\.cache|\\.ccache|\\.ruff_cache)/"
    "${_luma_any_dir}(target|node_modules|__pycache__)/"
    "${_luma_any_dir}(CMakeUserPresets|compile_commands)\\.json$"
    "${_luma_any_dir}([^/]*\\.swp|\\.#[^/]*|#[^/]*)$"
)
unset(_luma_any_dir)
unset(_luma_source_regex)

include(CPack)
