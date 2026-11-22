cmake_minimum_required(VERSION 3.21)
project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
add_compile_options(-Wall -Werror)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
enable_testing()

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB CONN_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/conn_*.cpp)
list(REMOVE_ITEM CORE_SOURCES ${CONN_SOURCES} ${PROJECT_SOURCE_DIR}/src/host.cpp)

add_library(chat_core STATIC ${CORE_SOURCES})
target_include_directories(chat_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(chat_core PUBLIC Qt6::Widgets)

add_executable(test_message tests/test_message.cpp)
target_link_libraries(test_message PRIVATE chat_core)
add_test(NAME message COMMAND test_message)

foreach(conn_source IN LISTS CONN_SOURCES)
    get_filename_component(code ${conn_source} NAME_WE)
    string(REGEX REPLACE "^conn_" "" code ${code})

    add_executable(host_${code} src/host.cpp ${conn_source})
    target_link_libraries(host_${code} PRIVATE chat_core)
    set_target_properties(host_${code} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

    add_executable(test_transport_${code} tests/test_transport.cpp ${conn_source})
    target_link_libraries(test_transport_${code} PRIVATE chat_core)
    target_compile_definitions(test_transport_${code} PRIVATE TRANSPORT_CODE="${code}")
    add_test(NAME transport_${code} COMMAND test_transport_${code})
endforeach()
