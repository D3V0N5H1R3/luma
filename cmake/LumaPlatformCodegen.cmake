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

    # The generated platform.ts / platform.rs are checked-in sources, so they must
    # not be declared as OUTPUT or BYPRODUCTS: the clean target deletes every
    # declared output, which would remove tracked files from the source tree. A
    # stamp file in the build tree tracks freshness instead.
    set(_platform_stamp "${CMAKE_CURRENT_BINARY_DIR}/generate_platform_code.stamp")

    add_custom_command(
        OUTPUT "${_platform_stamp}"
        COMMAND ${Python3_EXECUTABLE} "${_platform_gen}" --all
        COMMAND ${CMAKE_COMMAND} -E touch "${_platform_stamp}"
        DEPENDS "${_platform_map}" "${_platform_gen}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/extensions/shared"
        COMMENT "Regenerating platform code from platform-map.json"
    )

    add_custom_target(generate_platform_code
        DEPENDS "${_platform_stamp}"
    )
else()
    message(STATUS "Python3 not found — generate_platform_code target unavailable")
endif()
