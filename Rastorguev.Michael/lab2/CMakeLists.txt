cmake_minimum_required(VERSION 3.16)
project(wolf_and_kids CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-Wall -Werror)

option(BUILD_TESTS "Собирать тесты (см. run_tests.sh)" OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

set(IMPL_DIR ${CMAKE_SOURCE_DIR}/src/implementions)
set(INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src/headers ${CMAKE_SOURCE_DIR}/src/headers/conns)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${IMPL_DIR}/*.cpp)
file(GLOB_RECURSE ALL_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/headers/*.h)

set(CONN_SOURCES ${ALL_SOURCES})
list(FILTER CONN_SOURCES INCLUDE REGEX ".*/conn_[^/]+\\.cpp$")

set(COMMON_SOURCES ${ALL_SOURCES})
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*/(conn_[^/]+|host|client)\\.cpp$")

add_library(common OBJECT ${COMMON_SOURCES} ${ALL_HEADERS})
target_include_directories(common PUBLIC ${INCLUDE_DIRS})
target_link_libraries(common PUBLIC Qt${QT_VERSION_MAJOR}::Widgets)

foreach(conn_src ${CONN_SOURCES})
    get_filename_component(conn_name ${conn_src} NAME_WE)
    string(REGEX REPLACE "^conn_" "" type ${conn_name})

    add_executable(host_${type} ${IMPL_DIR}/host.cpp ${conn_src})
    target_link_libraries(host_${type} PRIVATE common Qt${QT_VERSION_MAJOR}::Widgets Threads::Threads rt)

    if(EXISTS ${IMPL_DIR}/client.cpp)
        add_executable(client_${type} ${IMPL_DIR}/client.cpp ${conn_src})
        target_link_libraries(client_${type} PRIVATE common Qt${QT_VERSION_MAJOR}::Widgets Threads::Threads rt)
    endif()
endforeach()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
