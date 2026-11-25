
cmake_minimum_required(VERSION 3.20)

project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

set(COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/host.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/client.cpp"
)

file(GLOB CONNECTION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/conn_*.cpp"
)

file(GLOB ALL_CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

foreach(SOURCE_FILE IN LISTS ALL_CPP_SOURCES)
    get_filename_component(SOURCE_NAME "${SOURCE_FILE}" NAME_WE)

    if(NOT SOURCE_NAME STREQUAL "host"
       AND NOT SOURCE_NAME STREQUAL "client"
       AND NOT SOURCE_NAME MATCHES "^conn_")

        add_library(source_${SOURCE_NAME} OBJECT
            "${SOURCE_FILE}"
        )

        target_compile_options(source_${SOURCE_NAME} PRIVATE
            -Wall
            -Werror
            -Wextra
            -Wpedantic
        )
    endif()
endforeach()

foreach(CONNECTION_SOURCE IN LISTS CONNECTION_SOURCES)
    get_filename_component(CONNECTION_NAME
        "${CONNECTION_SOURCE}" NAME_WE
    )

    string(REPLACE "conn_" "" IPC_NAME "${CONNECTION_NAME}")

    add_executable(host_${IPC_NAME}
        ${COMMON_SOURCES}
        "${CONNECTION_SOURCE}"
    )

    target_link_libraries(host_${IPC_NAME}
        PRIVATE
        Qt6::Widgets
        Threads::Threads
        rt
    )

    target_compile_options(host_${IPC_NAME}
        PRIVATE
        -Wall
        -Werror
        -Wextra
        -Wpedantic
    )
endforeach()

include(CTest)

if(BUILD_TESTING)
    foreach(IPC_NAME IN ITEMS mq fifo sock)
        add_executable(test_${IPC_NAME}
            tests/connection_test.cpp
            "conn_${IPC_NAME}.cpp"
        )
        
        target_include_directories(test_${IPC_NAME}
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
        )

        target_link_libraries(test_${IPC_NAME}
            PRIVATE
            Threads::Threads
            rt
        )

        target_compile_options(test_${IPC_NAME}
            PRIVATE
            -Wall
            -Werror
            -Wextra
            -Wpedantic
        )

        if(IPC_NAME STREQUAL "mq")
            target_compile_definitions(test_${IPC_NAME}
                PRIVATE TEST_MQ
            )
        endif()

        add_test(
            NAME ${IPC_NAME}_connection_test
            COMMAND test_${IPC_NAME}
        )
    endforeach()
endif()
