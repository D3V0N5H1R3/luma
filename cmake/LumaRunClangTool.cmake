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

# Multi-config Visual Studio / Xcode generators ignore CMAKE_EXPORT_COMPILE_COMMANDS,
# so fail with an actionable message instead of a wall of clang-tidy errors.
if(TOOL STREQUAL "tidy" AND NOT EXISTS "${BINARY_DIR}/compile_commands.json")
    message(FATAL_ERROR
        "LumaRunClangTool: ${BINARY_DIR}/compile_commands.json not found. "
        "Configure with a Ninja or Makefile generator (e.g. `cmake --preset ninja`) "
        "and CMAKE_EXPORT_COMPILE_COMMANDS=ON.")
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
else()
    message(STATUS "Running clang-tidy on ${_count} files...")
endif()

# Windows caps a process command line at 32,767 characters and the full source
# list is well beyond that, so run the tool in batches. Every batch runs even
# after a failure so a single invocation still reports all findings.
set(_batch_size 100)
set(_failures "")
math(EXPR _last_index "${_count} - 1")
foreach(_start RANGE 0 ${_last_index} ${_batch_size})
    list(SUBLIST _all_sources ${_start} ${_batch_size} _batch)
    if(TOOL STREQUAL "format")
        execute_process(COMMAND ${TOOL_EXE} -i ${_batch} RESULT_VARIABLE _result)
    else()
        execute_process(
            COMMAND ${TOOL_EXE} -p ${BINARY_DIR} ${_batch}
            RESULT_VARIABLE _result
        )
    endif()
    if(NOT _result EQUAL 0)
        list(APPEND _failures "${_result}")
    endif()
endforeach()

if(_failures)
    message(FATAL_ERROR "${TOOL_EXE} failed (exit codes: ${_failures})")
endif()
