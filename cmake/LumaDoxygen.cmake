# cmake/LumaDoxygen.cmake — API documentation via Doxygen.
#
# Defines the `docs` custom target when Doxygen is available, generating the
# Doxyfile from Doxyfile.in so PROJECT_NUMBER tracks the VERSION-derived project
# version and HAVE_DOT matches the detected toolchain.
#
# Included from the root CMakeLists.txt. include() preserves the including file's
# directory scope, so CMAKE_CURRENT_SOURCE_DIR / CMAKE_CURRENT_BINARY_DIR resolve
# to the project root and its binary dir exactly as they did inline.

include_guard(GLOBAL)

find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    # Enable Graphviz diagrams only when the optional `dot` tool is present;
    # otherwise Doxygen warns once per graph it cannot render.
    if(TARGET Doxygen::dot)
        set(LUMA_DOXYGEN_HAVE_DOT YES)
    else()
        set(LUMA_DOXYGEN_HAVE_DOT NO)
    endif()

    # Generate the Doxyfile so PROJECT_NUMBER tracks the VERSION-derived project
    # version and HAVE_DOT matches the detected toolchain (single source of truth).
    # Output goes under the active binary dir to honour out-of-source builds.
    set(LUMA_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
