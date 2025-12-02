file(GLOB_RECURSE app_sources FOLLOW_SYMLINKS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.c"
)

set(BUILD_INFO_SCRIPT "./update_build_info.sh")
set(BUILD_INFO_C "./build_info.c")

# Register component sources including the generated C file
idf_component_register(
    SRCS
        ${app_sources}
        "${BUILD_INFO_C}"
    INCLUDE_DIRS "."
)

# Ensure build_info.c is removed on clean
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES "${BUILD_INFO_C}")

# Generate build_info.c only when not in early expansion
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(
        OUTPUT "${BUILD_INFO_C}"
        # Use CMake to run bash to avoid token/whitespace issues
        COMMAND "${CMAKE_COMMAND}" -E env
                BASH_PATH="$ENV{BASH}"
                bash "${BUILD_INFO_SCRIPT}" "${BUILD_INFO_C}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        DEPENDS "${BUILD_INFO_SCRIPT}"
        COMMENT "Regenerating build_info.c"
        VERBATIM
    )

    # Mark as a generated source so CMake/IDF know it's created by a rule
    set_source_files_properties("${BUILD_INFO_C}" PROPERTIES GENERATED TRUE)
endif()
