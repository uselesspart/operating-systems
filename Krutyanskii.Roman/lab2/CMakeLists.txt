cmake_minimum_required(VERSION 3.16)
project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_compile_options(-Wall -Werror)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

# Any src/conn_<type>.cpp automatically yields host_<type> and client_<type>.
# Adding a new interaction type does not require touching this file.
file(GLOB CONN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/conn_*.cpp")

if(NOT CONN_SOURCES)
    message(FATAL_ERROR "No src/conn_*.cpp sources found")
endif()

foreach(conn_src IN LISTS CONN_SOURCES)
    get_filename_component(conn_name "${conn_src}" NAME_WE)      # conn_mmap
    string(REGEX REPLACE "^conn_" "" type_code "${conn_name}")   # mmap
    string(TOUPPER "${type_code}" type_upper)                    # MMAP

    add_executable(host_${type_code}
        src/host.cpp src/common.cpp "${conn_src}")
    target_compile_definitions(host_${type_code} PRIVATE TYPE_${type_upper})
    target_link_libraries(host_${type_code} Qt5::Widgets pthread)

    add_executable(client_${type_code}
        src/client.cpp src/common.cpp "${conn_src}")
    target_compile_definitions(client_${type_code} PRIVATE TYPE_${type_upper})
    target_link_libraries(client_${type_code} Qt5::Widgets pthread)
endforeach()
