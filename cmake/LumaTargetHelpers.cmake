# cmake/LumaTargetHelpers.cmake
# ─────────────────────────────────────────────────────────────────────────
# Convenience wrappers that create a target AND apply luma_set_compile_options()
# (plus LTO) in one step, so every Luma target receives the project compile
# options and usage requirements at creation time.
# ─────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)

# luma_add_executable(<name> <sources...>)
# Creates an executable target with Luma compile options applied.
function(luma_add_executable name)
    add_executable(${name} ${ARGN})
    luma_set_compile_options(${name})
    luma_apply_lto(${name})
    luma_set_stack_reserve(${name})
endfunction()

# luma_add_library(<name> [STATIC|SHARED|OBJECT|INTERFACE] <sources...>)
# Creates a library target with Luma compile options applied.
# For INTERFACE libraries, options are added as INTERFACE properties.
function(luma_add_library name)
    add_library(${name} ${ARGN})
    # INTERFACE libraries cannot have PRIVATE compile options
    get_target_property(type ${name} TYPE)
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
        luma_set_compile_options(${name})
        luma_apply_lto(${name})
    endif()
endfunction()

# luma_add_tool_library(<name> SOURCES <src...> [DEPENDS <dep...>])
# Creates the (STATIC library + executable) pair shared by the language server
# and debugger. Call this from the tool's own directory; SOURCES and the
# implicit source/main.cpp are resolved relative to it.
#   luma_<name>_lib — STATIC library from SOURCES, linking luma_shared + DEPENDS,
#                     exposing <dir>/source as a PUBLIC include directory.
#   luma_<name>     — executable from source/main.cpp linking luma_<name>_lib.
# Compile options and LTO are applied to both via luma_add_library/executable.
function(luma_add_tool_library name)
    cmake_parse_arguments(_TOOL "" "" "SOURCES;DEPENDS" ${ARGN})

    luma_add_library(luma_${name}_lib STATIC ${_TOOL_SOURCES})
    target_link_libraries(luma_${name}_lib PUBLIC ${_TOOL_DEPENDS} luma_shared)
    target_include_directories(luma_${name}_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/source)

    luma_add_executable(luma_${name} source/main.cpp)
    target_link_libraries(luma_${name} PRIVATE luma_${name}_lib)
endfunction()

# luma_configure_vendored_c_target(<target>)
# Applies the shared policy for vendored third-party C libraries: build to C99
# and silence all warnings so the project's strict diagnostics never flag code we
# do not maintain. Per-library SYSTEM include treatment and compile definitions
# (MINIZ_NO_STDIO, etc.) stay at each call site, since those genuinely differ.
function(luma_configure_vendored_c_target target)
    set_target_properties(${target} PROPERTIES C_STANDARD 99)
    target_compile_options(${target} PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
    )
endfunction()
