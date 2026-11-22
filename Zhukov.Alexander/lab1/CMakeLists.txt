cmake_minimum_required(VERSION 3.16)
project(disk_monitor LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Build this project on Linux.")
endif()

find_package(yaml-cpp CONFIG REQUIRED)

add_executable(disk_monitor
    src/main.cpp
    src/daemon.cpp
    src/pid_file.cpp
    src/disk_monitor.cpp
    src/config.cpp
    src/logger.cpp
    src/file_watcher.cpp
)

set_target_properties(disk_monitor PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(disk_monitor PRIVATE include)
target_compile_options(disk_monitor PRIVATE -Wall -Werror)

if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(disk_monitor PRIVATE yaml-cpp::yaml-cpp)
else()
    target_link_libraries(disk_monitor PRIVATE yaml-cpp)
endif()
