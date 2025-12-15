file(GLOB_RECURSE app_sources FOLLOW_SYMLINKS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.c"
)

# Register component sources including the generated C file
idf_component_register(
    SRCS
        ${app_sources}
        "${BUILD_INFO_C}"
    INCLUDE_DIRS "."
)
