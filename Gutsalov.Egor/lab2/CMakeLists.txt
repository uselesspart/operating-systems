cmake_minimum_required(VERSION 3.20)
project(lab2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(cmake/BuildOptions.cmake)
find_package(Threads REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# CONFIGURE_DEPENDS also notices newly added source/transport files during make.
file(GLOB_RECURSE sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE connections CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/conn_*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/conn_*.c")
list(REMOVE_ITEM sources ${connections}
  "${CMAKE_CURRENT_SOURCE_DIR}/src/host.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/client.cpp")
add_library(lab2_core STATIC ${sources})
target_include_directories(lab2_core PUBLIC include)
target_link_libraries(lab2_core PUBLIC lab2_build_options Threads::Threads Qt${QT_VERSION_MAJOR}::Widgets rt)

foreach(connection IN LISTS connections)
  get_filename_component(code "${connection}" NAME_WE)
  string(REGEX REPLACE "^conn_" "" code "${code}")
  add_library(lab2_conn_${code} STATIC "${connection}")
  target_link_libraries(lab2_conn_${code} PUBLIC lab2_core)
  foreach(role host client)
    add_executable(${role}_${code} src/${role}.cpp)
    target_link_libraries(${role}_${code} PRIVATE lab2_conn_${code} lab2_core)
    set_target_properties(${role}_${code} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  endforeach()
endforeach()

if(ENABLE_TESTING)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_subdirectory(tests)
endif()
