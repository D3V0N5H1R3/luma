# cmake/LumaRunClangTool.cmake — Build-time driver for clang-format / clang-tidy.
#
# Discovers all sources under the project source directories at build time (not
# configure time) so that newly added files are always covered without
# reconfiguring. file(GLOB_RECURSE) only runs when this script runs.
#
# Expected variables (passed via -D on the command line):
#   TOOL        — "format" or "tidy"
#   TOOL_EXE    — path to the clang-format / clang-tidy executable
#   SOURCE_DIR  — path to the project source root
#   BINARY_DIR  — path to the build directory (clang-tidy only; for
#                 compile_commands.json)

set(_tool_dirs
    "${SOURCE_DIR}/core"
    "${SOURCE_DIR}/shared"
    "${SOURCE_DIR}/language-server/source"
    "${SOURCE_DIR}/debugger/source"
)

if(TOOL STREQUAL "format")
    set(_globs "*.cpp" "*.hpp")
    # clang-format also covers the test and fuzz sources, matching the
    # lint-and-format prompt. clang-tidy intentionally skips them: they are not
    # part of compile_commands.json and follow looser conventions.
    list(APPEND _tool_dirs
        "${SOURCE_DIR}/tests"
        "${SOURCE_DIR}/fuzz"
    )
elseif(TOOL STREQUAL "tidy")
    set(_globs "*.cpp")
else()
    message(FATAL_ERROR
        "LumaRunClangTool: unknown TOOL '${TOOL}' (expected 'format' or 'tidy')")
endif()

# clang-tidy needs -p <build dir> to locate compile_commands.json; without a
# non-empty BINARY_DIR it would be invoked with an empty path and fail with
# misleading errors instead of a clear "missing required parameter" message.
if(TOOL STREQUAL "tidy" AND NOT BINARY_DIR)
    message(FATAL_ERROR
        "LumaRunClangTool: BINARY_DIR is required when TOOL is 'tidy' "
        "(path to the build directory containing compile_commands.json)")
endif()

set(_all_sources "")
foreach(_dir IN LISTS _tool_dirs)
    foreach(_glob IN LISTS _globs)
        file(GLOB_RECURSE _sources "${_dir}/${_glob}")
        list(APPEND _all_sources ${_sources})
    endforeach()
endforeach()

list(LENGTH _all_sources _count)
if(_count EQUAL 0)
    message(STATUS "No source files found — nothing to do")
    return()
endif()

if(TOOL STREQUAL "format")
    message(STATUS "Formatting ${_count} files...")
    execute_process(
        COMMAND ${TOOL_EXE} -i ${_all_sources}
        RESULT_VARIABLE _result
    )
else()
    message(STATUS "Running clang-tidy on ${_count} files...")
    execute_process(
        COMMAND ${TOOL_EXE} -p ${BINARY_DIR} ${_all_sources}
        RESULT_VARIABLE _result
    )
endif()

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "${TOOL_EXE} failed with exit code ${_result}")
endif()
