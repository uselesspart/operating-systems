cmake_minimum_required(VERSION 3.16)
project(lab2_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LAB2_TESTS "Build unit tests" OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_compile_options(-Wall -Werror)

file(GLOB_RECURSE EVERY_SOURCE CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE CONN_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/conn_*.cpp)
file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/gui/*.cpp)
set(MAIN_SOURCES ${PROJECT_SOURCE_DIR}/src/host.cpp ${PROJECT_SOURCE_DIR}/src/client.cpp)

set(CORE_SOURCES ${EVERY_SOURCE})
list(REMOVE_ITEM CORE_SOURCES ${CONN_SOURCES} ${GUI_SOURCES} ${MAIN_SOURCES})

add_library(lab2_core STATIC ${CORE_SOURCES})
target_include_directories(lab2_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lab2_core PUBLIC Threads::Threads)

add_library(lab2_gui STATIC ${GUI_SOURCES})
target_link_libraries(lab2_gui PUBLIC lab2_core Qt${QT_VERSION_MAJOR}::Widgets)

set(LAB2_KINDS "")
foreach(conn_file ${CONN_SOURCES})
    get_filename_component(conn_stem ${conn_file} NAME_WE)
    string(REGEX REPLACE "^conn_" "" kind ${conn_stem})
    list(APPEND LAB2_KINDS ${kind})

    add_executable(host_${kind} ${PROJECT_SOURCE_DIR}/src/host.cpp ${conn_file})
    target_link_libraries(host_${kind} PRIVATE lab2_gui)

    add_executable(client_${kind} ${PROJECT_SOURCE_DIR}/src/client.cpp ${conn_file})
    target_link_libraries(client_${kind} PRIVATE lab2_core)
endforeach()

if(LAB2_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
