# cmake/LumaPlatformCodegen.cmake — Editor-extension platform code generation.
#
# Regenerates the TypeScript and Rust platform mappings from the shared
# extensions/shared/platform-map.json. Defines the `generate_platform_code`
# custom target when a Python 3 interpreter is available. Run with:
#   cmake --build <dir> --target generate_platform_code
#
# Included from the root CMakeLists.txt after the component subdirectories.
# include() runs in the including file's directory scope, so CMAKE_SOURCE_DIR
# resolves to the project root exactly as it did inline.

include_guard(GLOBAL)

find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_FOUND)
    set(_platform_map "${CMAKE_SOURCE_DIR}/extensions/shared/platform-map.json")
    set(_platform_gen "${CMAKE_SOURCE_DIR}/extensions/shared/generate-platform-code.py")
    set(_platform_outputs
        "${CMAKE_SOURCE_DIR}/extensions/vscode/src/generated/platform.ts"
        "${CMAKE_SOURCE_DIR}/extensions/zed/src/generated/platform.rs"
    )

    add_custom_command(
        OUTPUT ${_platform_outputs}
        COMMAND ${Python3_EXECUTABLE} "${_platform_gen}" --all
        DEPENDS "${_platform_map}" "${_platform_gen}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/extensions/shared"
        COMMENT "Regenerating platform code from platform-map.json"
    )

    add_custom_target(generate_platform_code
        DEPENDS ${_platform_outputs}
    )
else()
    message(STATUS "Python3 not found — generate_platform_code target unavailable")
endif()
