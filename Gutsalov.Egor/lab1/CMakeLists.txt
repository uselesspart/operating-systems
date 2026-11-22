cmake_minimum_required(VERSION 3.20)
project(lab1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/BuildOptions.cmake)
add_subdirectory(src)

if(ENABLE_TESTING)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_subdirectory(tests)
endif()
