cmake_minimum_required(VERSION 3.16)
project(variant3_linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WOLF_BUILD_GUI "Build the Qt host applications" ON)
option(BUILD_TESTING "Build tests" OFF)
include(CTest)
find_package(Threads REQUIRED)

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(CONNECTION_DIR "${SOURCE_DIR}/connection")

# CONFIGURE_DEPENDS makes CMake reconfigure automatically when a source is added.
file(GLOB_RECURSE PROJECT_CPP CONFIGURE_DEPENDS
    "${SOURCE_DIR}/*.cpp"
)
file(GLOB CONNECTION_SOURCES CONFIGURE_DEPENDS
    "${CONNECTION_DIR}/conn_*.cpp"
)

set(APP_COMMON_SOURCES ${PROJECT_CPP})
foreach(SOURCE_FILE IN LISTS CONNECTION_SOURCES)
    list(REMOVE_ITEM APP_COMMON_SOURCES "${SOURCE_FILE}")
endforeach()

set(AVAILABLE_CONNECTION_TYPES)
foreach(CONNECTION_SOURCE IN LISTS CONNECTION_SOURCES)
    get_filename_component(CONNECTION_NAME "${CONNECTION_SOURCE}" NAME_WE)
    string(REPLACE "conn_" "" CONNECTION_TYPE "${CONNECTION_NAME}")
    list(APPEND AVAILABLE_CONNECTION_TYPES "${CONNECTION_TYPE}")
endforeach()

if(WOLF_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    set(CMAKE_AUTOMOC ON)

    foreach(CONNECTION_TYPE IN LISTS AVAILABLE_CONNECTION_TYPES)
        set(CONNECTION_SOURCE "${CONNECTION_DIR}/conn_${CONNECTION_TYPE}.cpp")
        add_executable(host_${CONNECTION_TYPE}
            ${APP_COMMON_SOURCES}
            "${SOURCE_DIR}/host_worker.hpp"
            "${CONNECTION_SOURCE}"
        )
        target_include_directories(host_${CONNECTION_TYPE} PRIVATE "${SOURCE_DIR}")
        target_compile_definitions(host_${CONNECTION_TYPE} PRIVATE "WOLF_TRANSPORT=\"${CONNECTION_TYPE}\"")
        target_compile_options(host_${CONNECTION_TYPE} PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(host_${CONNECTION_TYPE} PRIVATE Qt6::Widgets Threads::Threads rt)
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(test_game_engine
        "${SOURCE_DIR}/game_engine.cpp"
        tests/test_game_engine.cpp
    )
    target_include_directories(test_game_engine PRIVATE "${SOURCE_DIR}")
    target_compile_options(test_game_engine PRIVATE -Wall -Wextra -Werror)
    add_test(NAME game_engine COMMAND test_game_engine)

    foreach(CONNECTION_TYPE IN LISTS AVAILABLE_CONNECTION_TYPES)
        add_executable(test_connection_${CONNECTION_TYPE}
            "${SOURCE_DIR}/shared_sync.cpp"
            "${CONNECTION_DIR}/base_conn.cpp"
            "${CONNECTION_DIR}/conn_${CONNECTION_TYPE}.cpp"
            tests/test_connection.cpp
        )
        target_include_directories(test_connection_${CONNECTION_TYPE} PRIVATE "${SOURCE_DIR}")
        target_compile_options(test_connection_${CONNECTION_TYPE} PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(test_connection_${CONNECTION_TYPE} PRIVATE Threads::Threads rt)
        if(CONNECTION_TYPE STREQUAL "sock")
            target_compile_definitions(test_connection_${CONNECTION_TYPE} PRIVATE WOLF_TEST_SOCKETPAIR)
        endif()
        add_test(NAME connection_${CONNECTION_TYPE} COMMAND test_connection_${CONNECTION_TYPE})
        set_tests_properties(connection_${CONNECTION_TYPE} PROPERTIES TIMEOUT 20)
    endforeach()
endif()
