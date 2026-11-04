cmake_minimum_required(VERSION 3.10)
project(lab2_mpi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Werror)

find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

# Все *.cpp в src/ компилируются автоматически
# Исключение - conn_*.cpp: они обрабатываются отдельно ниже, по одному target'у на каждый тип IPC
file(GLOB ALL_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)
set(COMMON_SOURCES ${ALL_SRC})
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*/conn_.*\\.cpp$")

file(GLOB CONN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/conn_*.cpp")

foreach(conn_src ${CONN_SOURCES})
    get_filename_component(conn_name ${conn_src} NAME_WE)
    string(SUBSTRING ${conn_name} 5 -1 type_code)

    string(SUBSTRING ${type_code} 0 1 first)
    string(TOUPPER ${first} first_up)
    string(SUBSTRING ${type_code} 1 -1 rest)
    set(class_name "Conn${first_up}${rest}")

    set(target_name "host_${type_code}")

    add_executable(${target_name} ${COMMON_SOURCES} ${conn_src})
    target_include_directories(${target_name} PRIVATE include)
    target_compile_definitions(${target_name} PRIVATE
        CONN_HEADER=\"${class_name}.h\"
        CONN_CLASS=${class_name})
    target_link_libraries(${target_name} PRIVATE
        Qt5::Widgets Threads::Threads rt)
endforeach()