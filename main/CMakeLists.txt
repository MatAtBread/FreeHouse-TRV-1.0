file(GLOB_RECURSE app_sources FOLLOW_SYMLINKS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../common/*.c"
)

set(BUILD_INFO_SCRIPT "./update_build_info.sh")
set(BUILD_INFO_C "./build_info.c")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(
        OUTPUT ${BUILD_INFO_C}
        COMMAND bash ${BUILD_INFO_SCRIPT} build_info.c
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        DEPENDS ${BUILD_INFO_SCRIPT}
        COMMENT "Regenerating build_info.c"
        VERBATIM
    )
    add_custom_target(build_info_gen DEPENDS ${BUILD_INFO_C})
endif()

# Add the generated file to your sources
list(APPEND app_sources ${BUILD_INFO_C})

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_dependencies(${COMPONENT_LIB} build_info_gen)
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${BUILD_INFO_C})
endif()
