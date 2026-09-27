cmake_minimum_required(VERSION 3.28)
project(DiskMonitorDaemon CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

add_compile_options(-Wall -Wextra -Werror)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/deployments/systemd/disk-monitor.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/disk-monitor.service
    @ONLY
)