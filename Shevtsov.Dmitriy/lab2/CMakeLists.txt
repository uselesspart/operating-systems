cmake_minimum_required(VERSION 3.16)
project(wolf_and_goats LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
add_compile_options(-Wall -Werror)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE CONN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/conn_*.cpp)
set(HOST_MAIN ${CMAKE_SOURCE_DIR}/src/host.cpp)
set(TESTS_MAIN ${CMAKE_SOURCE_DIR}/tests/tests.cpp)

set(COMMON_SOURCES ${SOURCES})
list(REMOVE_ITEM COMMON_SOURCES ${CONN_SOURCES} ${HOST_MAIN})

add_library(common OBJECT ${COMMON_SOURCES})
target_include_directories(common PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(common PUBLIC Qt${QT_VERSION_MAJOR}::Widgets Threads::Threads rt)

foreach(CONN ${CONN_SOURCES})
	get_filename_component(NAME ${CONN} NAME_WE)
	string(REGEX REPLACE "^conn_" "" TYPE_CODE ${NAME})

	add_executable(host_${TYPE_CODE} ${HOST_MAIN} ${CONN})
	target_link_libraries(host_${TYPE_CODE} PRIVATE common)

	add_executable(tests_${TYPE_CODE} ${TESTS_MAIN} ${CONN})
	target_link_libraries(tests_${TYPE_CODE} PRIVATE common)
endforeach()
