cmake_minimum_required(VERSION 3.16)
project(os_lab2_variant13 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# All .cpp files from the project root are discovered automatically.
file(GLOB ALL_CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
file(GLOB CONN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/conn_*.cpp"
)

if(NOT CONN_SOURCES)
    message(FATAL_ERROR "No conn_*.cpp files found")
endif()

set(HOST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/host.cpp")
if(NOT EXISTS "${HOST_SOURCE}")
    message(FATAL_ERROR "host.cpp is required")
endif()

# Ordinary implementation files (common.cpp and any future ordinary .cpp)
# are picked up automatically. host.cpp is the designated main file and
# conn_*.cpp files are handled separately below.
set(REGULAR_SOURCES ${ALL_CPP_SOURCES})
list(REMOVE_ITEM REGULAR_SOURCES "${HOST_SOURCE}")
list(FILTER REGULAR_SOURCES EXCLUDE REGEX "/conn_[^/]+\\.cpp$")

foreach(CONN_SOURCE IN LISTS CONN_SOURCES)
    get_filename_component(CONN_NAME "${CONN_SOURCE}" NAME_WE)
    string(REGEX REPLACE "^conn_" "" TYPE_CODE "${CONN_NAME}")

    set(CONN_HEADER "conn_${TYPE_CODE}.hpp")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${CONN_HEADER}")
        message(FATAL_ERROR "Missing ${CONN_HEADER} for ${CONN_SOURCE}")
    endif()

    add_executable(host_${TYPE_CODE}
        ${HOST_SOURCE}
        ${REGULAR_SOURCES}
        ${CONN_SOURCE}
    )

    target_compile_options(host_${TYPE_CODE} PRIVATE -Wall -Werror)
    target_compile_definitions(host_${TYPE_CODE} PRIVATE
        CONN_HEADER="${CONN_HEADER}"
        CONN_TYPE_CODE="${TYPE_CODE}"
    )
    target_link_libraries(host_${TYPE_CODE} PRIVATE Qt6::Widgets pthread rt)
endforeach()
