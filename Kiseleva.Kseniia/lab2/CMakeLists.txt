cmake_minimum_required(VERSION 3.20)

project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

file(GLOB CONNECTION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/conn_*.cpp"
)

foreach(CONNECTION_SOURCE ${CONNECTION_SOURCES})
    get_filename_component(CONNECTION_NAME
        "${CONNECTION_SOURCE}" NAME_WE
    )

    string(REPLACE "conn_" "" IPC_NAME "${CONNECTION_NAME}")

    add_executable(host_${IPC_NAME}
        host.cpp
        client.cpp
        "${CONNECTION_SOURCE}"
    )

    target_link_libraries(host_${IPC_NAME}
        PRIVATE
        Qt6::Widgets
        Threads::Threads
        rt
    )

    target_compile_options(host_${IPC_NAME}
        PRIVATE
        -Wall
        -Werror
        -Wextra
        -Wpedantic
    )
endforeach()