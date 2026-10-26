cmake_minimum_required(VERSION 3.16)
project(treecleaner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(treecleaner_core
    src/config.cpp
    src/cleaner.cpp
)
target_include_directories(treecleaner_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(treecleaner_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

add_executable(treecleaner src/main.cpp src/logger.cpp src/daemon.cpp)
target_link_libraries(treecleaner PRIVATE treecleaner_core)
target_compile_options(treecleaner PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

include(CTest)
if(BUILD_TESTING)
    add_executable(treecleaner_tests tests/test_main.cpp)
    target_link_libraries(treecleaner_tests PRIVATE treecleaner_core)
    target_compile_options(treecleaner_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    add_test(NAME treecleaner_tests COMMAND treecleaner_tests)
endif()
