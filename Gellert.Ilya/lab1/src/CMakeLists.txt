add_library(daemon_core
    config/config.cpp
    core/daemon.cpp
    worker/worker.cpp
)

target_include_directories(daemon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(disk_monitor main.cpp)
target_link_libraries(disk_monitor PRIVATE daemon_core)