# cmake/LumaWindowsResources.cmake — Windows file metadata for shipped binaries.
#
# Embeds a VERSIONINFO resource (product, company, description, version, and
# copyright — what Explorer shows under Properties → Details) and an application
# manifest into an executable. Antivirus heuristics, Microsoft Defender's
# machine-learning models in particular, weigh an unsigned executable with no
# version information and no manifest as suspicious, so the bare binaries were
# regularly quarantined as false positives (e.g. "Trojan:Win32/Wacatac!ml").
# Proper metadata does not replace Authenticode signing (see the release
# workflow), but it removes one of the strongest "anonymous binary" signals and
# lets a false-positive report identify the file.
#
# The manifest is embedded through the resource script rather than the linker,
# so the same path works for MSVC link.exe, clang-cl's lld-link, and MinGW's
# windres; MSVC-style linkers are told not to synthesise a second one.

include_guard(GLOBAL)

if(WIN32)
    enable_language(RC)
endif()

# luma_add_windows_metadata(<target> DESCRIPTION <text>)
# No-op on non-Windows platforms.
function(luma_add_windows_metadata target)
    if(NOT WIN32)
        return()
    endif()

    cmake_parse_arguments(_META "" "DESCRIPTION" "" ${ARGN})
    if(NOT _META_DESCRIPTION)
        message(FATAL_ERROR "luma_add_windows_metadata(${target}): DESCRIPTION is required")
    endif()

    # VERSIONINFO needs four numeric components; absent ones default to 0.
    set(LUMA_RC_VERSION_MAJOR 0)
    set(LUMA_RC_VERSION_MINOR 0)
    set(LUMA_RC_VERSION_PATCH 0)
    if(NOT "${PROJECT_VERSION_MAJOR}" STREQUAL "")
        set(LUMA_RC_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    endif()
    if(NOT "${PROJECT_VERSION_MINOR}" STREQUAL "")
        set(LUMA_RC_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    endif()
    if(NOT "${PROJECT_VERSION_PATCH}" STREQUAL "")
        set(LUMA_RC_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    endif()

    set(LUMA_RC_TARGET "${target}")
    set(LUMA_RC_DESCRIPTION "${_META_DESCRIPTION}")
    set(LUMA_RC_MANIFEST "${PROJECT_SOURCE_DIR}/cmake/luma.manifest")

    set(_rc_file "${PROJECT_BINARY_DIR}/generated/resources/${target}.rc")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/luma_version.rc.in" "${_rc_file}" @ONLY)
    target_sources(${target} PRIVATE "${_rc_file}")

    # Not every generator's dependency scanning sees files a resource script
    # references (only #include), so rebuild the resource when the manifest changes.
    set_source_files_properties("${_rc_file}" PROPERTIES
        OBJECT_DEPENDS "${LUMA_RC_MANIFEST}")

    if(MSVC)
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endif()
endfunction()
