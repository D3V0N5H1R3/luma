# cmake/LumaPackaging.cmake — CPack packaging configuration.
#
# Configures CPack metadata, install components, and the per-platform generators
# (ZIP/TGZ everywhere, DEB+RPM on Linux, NSIS on Windows), then includes CPack.
#
# Included as the final step of the root CMakeLists.txt. include() preserves the
# including file's directory scope, so PROJECT_VERSION, CMAKE_SOURCE_DIR, and the
# install components defined earlier resolve exactly as they did inline.

include_guard(GLOBAL)

set(CPACK_PACKAGE_NAME "luma")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Luma programming language interpreter, language server, and debugger")
set(CPACK_PACKAGE_VENDOR "Luma Contributors")
set(CPACK_PACKAGE_CONTACT "d3v0n5h1r3@users.noreply.github.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/d3v0n5h1r3/luma")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/DIRECTORY.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/DIRECTORY.md")

# Component definitions.
set(CPACK_COMPONENTS_ALL Runtime Tools Examples Documentation Development)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Luma Interpreter")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "The Luma interpreter executable.")
set(CPACK_COMPONENT_RUNTIME_REQUIRED TRUE)

set(CPACK_COMPONENT_TOOLS_DISPLAY_NAME "Developer Tools")
set(CPACK_COMPONENT_TOOLS_DESCRIPTION "Language server (luma_lsp) and debugger (luma_dap).")
set(CPACK_COMPONENT_TOOLS_DEPENDS Runtime)

set(CPACK_COMPONENT_EXAMPLES_DISPLAY_NAME "Example Programs")
set(CPACK_COMPONENT_EXAMPLES_DESCRIPTION "Example Luma programs and language feature demonstrations.")

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
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31)")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/d3v0n5h1r3/luma")

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

include(CPack)
