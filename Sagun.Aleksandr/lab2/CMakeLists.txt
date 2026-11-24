cmake_minimum_required(VERSION 3.16)
project(linux_ipc_chat LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_library(chat_frame STATIC src/chat/frame.cpp)

target_include_directories(chat_frame PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(chat_frame PRIVATE
    -Wall -Wextra -Werror
)

file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

file(GLOB transports CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/conn_*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/conn_*.c"
)

list(REMOVE_ITEM app_sources ${transports}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chat/frame.cpp"
)

foreach(transport IN LISTS transports)
    get_filename_component(stem "${transport}" NAME_WE)
    string(REGEX REPLACE "^conn_" "" code "${stem}")
    set(target "host_${code}")

    add_executable(${target} ${app_sources} "${transport}")

    set_target_properties(${target} PROPERTIES
        AUTOMOC ON
    )

    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Werror
    )

    target_link_libraries(${target} PRIVATE
        Qt6::Widgets
        chat_frame
        Threads::Threads
    )
endforeach()

enable_testing()

add_executable(frame_check tests/frame_check.cpp)

target_compile_options(frame_check PRIVATE
    -Wall -Wextra -Werror
)

target_link_libraries(frame_check PRIVATE
    chat_frame
)

add_test(NAME frame_check COMMAND frame_check)