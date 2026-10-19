cmake_minimum_required(VERSION 3.16)
project(reminderd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Werror)

add_library(reminder STATIC
    src/event.cpp
)
target_include_directories(reminder PUBLIC include)
