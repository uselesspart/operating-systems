cmake_minimum_required(VERSION 3.16)
project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Werror)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)
include(CTest)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE transports CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/conn_*.cpp")
set(host_source "${CMAKE_CURRENT_SOURCE_DIR}/src/host.cpp")
list(REMOVE_ITEM sources ${transports} "${host_source}")

add_library(lab2_core STATIC ${sources})
target_include_directories(lab2_core PUBLIC src)
target_link_libraries(lab2_core PUBLIC Threads::Threads rt)

set(HOST_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" CACHE PATH "Каталог программ хоста")

if(BUILD_TESTING)
    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
endif()

foreach(transport IN LISTS transports)
    get_filename_component(code "${transport}" NAME_WE)
    string(REGEX REPLACE "^conn_" "" code "${code}")

    add_library(conn_${code} OBJECT "${transport}")
    target_link_libraries(conn_${code} PUBLIC lab2_core)

    add_executable(host_${code} "${host_source}")
    target_link_libraries(host_${code} PRIVATE conn_${code} Qt${QT_VERSION_MAJOR}::Widgets)
    set_target_properties(host_${code} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${HOST_OUTPUT_DIRECTORY}")

    foreach(test_source IN LISTS test_sources)
        get_filename_component(test_name "${test_source}" NAME_WE)
        set(test_target "${test_name}_${code}")
        add_executable(${test_target} "${test_source}")
        target_link_libraries(${test_target} PRIVATE conn_${code})
        add_test(NAME ${test_target} COMMAND ${test_target})
        set_tests_properties(${test_target} PROPERTIES TIMEOUT 15)
    endforeach()

    if(TARGET test_connection_${code})
        add_test(NAME test_timeout_${code} COMMAND test_connection_${code} timeout)
        set_tests_properties(test_timeout_${code} PROPERTIES TIMEOUT 10)
    endif()
endforeach()
