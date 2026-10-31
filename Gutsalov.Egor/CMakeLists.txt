cmake_minimum_required(VERSION 3.20)
project(operating_systems LANGUAGES CXX)

include(lab1/cmake/BuildOptions.cmake)
if(ENABLE_TESTING)
  enable_testing()
endif()
add_subdirectory(lab1)
