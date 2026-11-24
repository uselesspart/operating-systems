cmake_minimum_required(VERSION 3.24)
project(wolf LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-Wall -Werror)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(QT REQUIRED IMPORTED_TARGET Qt6Widgets Qt5Widgets)

include(CTest)
if(BUILD_TESTING)
    include(cmake/criterion.cmake)
endif()

file(GLOB sources CONFIGURE_DEPENDS src/*.cpp src/*.c)
file(GLOB transports CONFIGURE_DEPENDS src/conn_*.cpp src/conn_*.c)
list(REMOVE_ITEM sources ${transports}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client.cpp)

add_library(common STATIC ${sources})
target_include_directories(common PUBLIC src)
target_link_libraries(common PUBLIC Threads::Threads rt)

foreach(source IN LISTS transports)
    get_filename_component(name ${source} NAME_WE)
    string(REGEX REPLACE "^conn_" "" type ${name})
    add_library(${name} STATIC ${source})
    target_link_libraries(${name} PUBLIC common)
    target_compile_definitions(${name} PUBLIC CONN_HEADER="${name}.h")

    foreach(role host client)
        add_executable(${role}_${type} src/${role}.cpp)
        target_link_libraries(${role}_${type} PRIVATE ${name})
        install(TARGETS ${role}_${type} RUNTIME DESTINATION bin)
    endforeach()
    target_link_libraries(host_${type} PRIVATE PkgConfig::QT)
    target_compile_options(host_${type} PRIVATE -fPIC)
endforeach()
