cmake_minimum_required(VERSION 3.16)

project(cached_dynamic_cast_tests)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


add_executable(cached_dynamic_cast_tests
               cached_dynamic_cast_tests_main.cpp
               ../cached_dynamic_cast/cached_dynamic_cast.hpp
               ../cached_dynamic_cast/cached_dynamic_cast.cpp)

set_property(TARGET cached_dynamic_cast_tests PROPERTY CXX_STANDARD 17)

#add_custom_command(TARGET cached_dynamic_cast_tests
#                   POST_BUILD
#                   COMMAND "$<TARGET_FILE:cached_dynamic_cast_tests>")

if (CMAKE_GENERATOR MATCHES "Visual Studio")
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT cached_dynamic_cast_tests)
endif()
