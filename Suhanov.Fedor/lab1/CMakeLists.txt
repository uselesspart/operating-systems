cmake_minimum_required(VERSION 3.24)
project(disk-monitor LANGUAGES CXX)

add_executable(
    disk-monitor
        src/AppConfig.cpp
        src/ArgsParser.cpp
        src/Daemon.cpp
        src/Monitor.cpp
        src/main.cpp
)

target_compile_features(
    disk-monitor
    PRIVATE
        cxx_std_17
)

target_compile_options(
    disk-monitor
    PRIVATE
        -Werror
        -Wall
        -Wextra
)

target_include_directories(
    disk-monitor
    PRIVATE
        include
)
