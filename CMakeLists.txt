cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(trv-1)
idf_build_set_property(COMPILE_OPTIONS "-Wmissing-field-initializers" APPEND)
