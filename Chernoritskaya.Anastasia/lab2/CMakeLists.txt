cmake_minimum_required(VERSION 3.16)
project(chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Werror)

find_package(Qt6 COMPONENTS Widgets QUIET)
if (NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
endif()

# Библиотека логики
add_library(chat_logic STATIC src/chat_logic.cpp)
target_include_directories(chat_logic PUBLIC include)

# Соединения
add_library(conn_mq STATIC src/conn_mq.cpp)
add_library(conn_fifo STATIC src/conn_fifo.cpp)
add_library(conn_sock STATIC src/conn_sock.cpp)
foreach(c conn_mq conn_fifo conn_sock)
    target_include_directories(${c} PUBLIC include)
    target_link_libraries(${c} PUBLIC chat_logic rt)
endforeach()

# Сборка хостов и клиентов
foreach(code mq fifo sock)
    add_executable(host_${code} src/host.cpp)
    target_link_libraries(host_${code} PRIVATE conn_${code} Qt::Widgets)

    add_executable(client_${code} src/client.cpp)
    target_link_libraries(client_${code} PRIVATE conn_${code})
endforeach()

# Тесты
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(unit_tests
    tests/tests.cpp
)

target_link_libraries(unit_tests
    PRIVATE
    chat_logic
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests)