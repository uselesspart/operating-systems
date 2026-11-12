cmake_minimum_required(VERSION 3.16)
project(wolf_and_kids LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Werror -Wextra)

option(BUILD_TESTS "Собирать тесты" OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB CONN_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/conn/conn_*.cpp)
file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/gui/*.cpp)
set(MAIN_SOURCES ${PROJECT_SOURCE_DIR}/src/host.cpp)

set(CORE_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM CORE_SOURCES ${CONN_SOURCES} ${GUI_SOURCES} ${MAIN_SOURCES})

add_library(wolf_core STATIC ${CORE_SOURCES})
target_include_directories(wolf_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wolf_core PUBLIC Threads::Threads)

add_library(wolf_gui STATIC ${GUI_SOURCES})
set_target_properties(wolf_gui PROPERTIES AUTOMOC ON)
target_link_libraries(wolf_gui PUBLIC wolf_core Qt${QT_VERSION_MAJOR}::Widgets)

set(CONN_TYPES)
foreach(conn_src ${CONN_SOURCES})
    get_filename_component(conn_name ${conn_src} NAME_WE)
    string(REGEX REPLACE "^conn_" "" type ${conn_name})
    list(APPEND CONN_TYPES ${type})

    add_executable(host_${type} src/host.cpp ${conn_src})
    target_link_libraries(host_${type} PRIVATE wolf_gui)
endforeach()
message(STATUS "Типы взаимодействия: ${CONN_TYPES}")

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
