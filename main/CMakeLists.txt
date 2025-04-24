file(GLOB_RECURSE app_sources FOLLOW_SYMLINKS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.c"
)

set(BUILD_INFO_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/update_build_info.sh")
set(BUILD_INFO_C "${CMAKE_CURRENT_LIST_DIR}/build_info.c")

# Always run the script before each build by using a custom target added to ALL
add_custom_target(
    generate_build_info ALL
    COMMAND bash ${BUILD_INFO_SCRIPT} > ${BUILD_INFO_C}
    BYPRODUCTS ${BUILD_INFO_C}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Regenerating build_info.c"
    VERBATIM
)

# Add the generated file to your sources
list(APPEND app_sources ${BUILD_INFO_C})

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
)

# Ensure the component depends on the build info being generated
add_dependencies(${COMPONENT_LIB} generate_build_info)

