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

# 1. Get the name of the current binary directory (last folder only)
get_filename_component(CURRENT_BINARY_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)

# 2. Use a regex to check if it starts with "build-" and capture the rest
if(CURRENT_BINARY_DIR_NAME MATCHES "^build-(.*)")
    # CMAKE_MATCH_1 contains the text captured by the parentheses (.*)
    set(MODEL_NAME ${CMAKE_MATCH_1})

    add_compile_definitions(BUILD_FREEHOUSE_MODEL=${MODEL_NAME})

    message(STATUS "Detected Build Model: ${MODEL_NAME}")
else()
    message(WARNING "Binary directory does not follow 'build-NAME' format. Skipping definition.")
endif()

# if(CMAKE_BINARY_DIR MATCHES "build-TRV1")
#     add_compile_definitions(BUILD_FREEHOUSE_MODEL=TRV1)
# elseif(CMAKE_BINARY_DIR MATCHES "build-TRV3")
#     add_compile_definitions(BUILD_FREEHOUSE_MODEL=TRV3)
# endif()
