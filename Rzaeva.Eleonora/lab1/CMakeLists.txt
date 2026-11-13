cmake_minimum_required(VERSION 3.16)
project(rzaeva_lab1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Ядро демона без main - переиспользуется и бинарником, и тестами.
add_library(rzaeva_core STATIC
    src/config.cpp
    src/cleaner.cpp
    src/pid_manager.cpp
    src/daemon.cpp
)
target_include_directories(rzaeva_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(rzaeva_core PUBLIC -Wall -Werror)

add_executable(daemon src/main.cpp)
target_link_libraries(daemon PRIVATE rzaeva_core)
target_compile_options(daemon PRIVATE -Wall -Werror)

option(BUILD_TESTS "Собрать тесты" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
