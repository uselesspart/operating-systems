cmake_minimum_required(VERSION 3.16)
project(dirclean_daemon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build unit and component tests" ON)

# Всё, кроме main.cpp, - в библиотеку, чтобы её могли линковать тесты.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(dirclean_core STATIC ${CORE_SOURCES})
target_include_directories(dirclean_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(dirclean_core PRIVATE -Wall -Werror)

add_executable(daemon src/main.cpp)
target_link_libraries(daemon PRIVATE dirclean_core)
target_compile_options(daemon PRIVATE -Wall -Werror)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
