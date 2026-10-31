cmake_minimum_required(VERSION 3.16)
project(os_lab2_variant13 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

file(GLOB CONN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conn_*.cpp")

if(NOT CONN_SOURCES)
    message(FATAL_ERROR "No conn_*.cpp files found")
endif()

foreach(CONN_SOURCE IN LISTS CONN_SOURCES)
    get_filename_component(CONN_NAME "${CONN_SOURCE}" NAME_WE)
    string(REGEX REPLACE "^conn_" "" TYPE_CODE "${CONN_NAME}")

    set(CONN_HEADER "conn_${TYPE_CODE}.hpp")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${CONN_HEADER}")
        message(FATAL_ERROR "Missing ${CONN_HEADER} for ${CONN_SOURCE}")
    endif()

    add_executable(host_${TYPE_CODE}
        host.cpp
        common.cpp
        ${CONN_SOURCE}
    )

    target_compile_options(host_${TYPE_CODE} PRIVATE -Wall -Werror)
    target_compile_definitions(host_${TYPE_CODE} PRIVATE
        CONN_HEADER="${CONN_HEADER}"
        CONN_TYPE_CODE="${TYPE_CODE}"
    )
    target_link_libraries(host_${TYPE_CODE} PRIVATE Qt6::Widgets pthread rt)
endforeach()
