cmake_minimum_required(VERSION 3.16)

project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_compile_options(-Wall -Wextra -Werror)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)
include(CTest)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB CONNECTION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/conn_*.cpp"
)

file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/*.cpp"
)
set(COMMON_SOURCES ${SOURCE_FILES})
list(REMOVE_ITEM COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/host.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client.cpp"
    ${CONNECTION_SOURCES}
    ${GUI_SOURCES}
)

if(NOT CONNECTION_SOURCES)
    message(FATAL_ERROR "No IPC implementations found")
endif()

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp"
    )
    # Общие тесты не зависят от выбранного транспорта.
    set(TRANSPORT_UNIT_TESTS ${TEST_SOURCES})
    list(FILTER TRANSPORT_UNIT_TESTS INCLUDE REGEX "/test_connection\\.cpp$")
    set(HOST_TEST_SOURCES ${TEST_SOURCES})
    list(FILTER HOST_TEST_SOURCES INCLUDE REGEX "/test_host_controller\\.cpp$")
    list(REMOVE_ITEM TEST_SOURCES ${TRANSPORT_UNIT_TESTS} ${HOST_TEST_SOURCES})
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

foreach(CONNECTION_SOURCE IN LISTS CONNECTION_SOURCES)
    get_filename_component(CONNECTION_NAME "${CONNECTION_SOURCE}" NAME_WE)
    string(REGEX REPLACE "^conn_" "" TYPE_CODE "${CONNECTION_NAME}")

    add_library(connection_${TYPE_CODE} OBJECT
        "${CONNECTION_SOURCE}"
        ${COMMON_SOURCES}
    )
    target_include_directories(connection_${TYPE_CODE} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(connection_${TYPE_CODE} PUBLIC Threads::Threads)

    add_executable(host_${TYPE_CODE} src/host.cpp ${GUI_SOURCES})
    target_compile_definitions(host_${TYPE_CODE} PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(host_${TYPE_CODE} PRIVATE
        connection_${TYPE_CODE}
        Qt6::Widgets
    )

    add_executable(client_${TYPE_CODE} src/client.cpp)
    target_link_libraries(client_${TYPE_CODE} PRIVATE connection_${TYPE_CODE})

    if(BUILD_TESTING)
        # Тесты конкретного транспорта не должны ссылаться на классы других conn_*.cpp.
        file(GLOB_RECURSE TRANSPORT_TEST_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/${TYPE_CODE}/*.cpp"
        )
        if(TRANSPORT_UNIT_TESTS OR TRANSPORT_TEST_SOURCES)
            add_executable(test_${TYPE_CODE} ${TRANSPORT_UNIT_TESTS} ${TRANSPORT_TEST_SOURCES})
            target_link_libraries(test_${TYPE_CODE} PRIVATE
                connection_${TYPE_CODE}
                GTest::gtest_main
            )
            target_compile_definitions(test_${TYPE_CODE} PRIVATE
                EXPECTED_TRANSPORT="${TYPE_CODE}"
            )
            gtest_discover_tests(test_${TYPE_CODE}
                TEST_PREFIX "${TYPE_CODE}."
                PROPERTIES TIMEOUT 15 LABELS transport
            )
        endif()
        if(HOST_TEST_SOURCES)
            add_executable(test_host_${TYPE_CODE} ${HOST_TEST_SOURCES})
            target_link_libraries(test_host_${TYPE_CODE} PRIVATE
                connection_${TYPE_CODE} GTest::gtest_main)
            gtest_discover_tests(test_host_${TYPE_CODE}
                TEST_PREFIX "controller.${TYPE_CODE}."
                PROPERTIES TIMEOUT 15 LABELS controller)
        endif()
        add_test(NAME e2e.${TYPE_CODE}
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/test_application.py"
                "$<TARGET_FILE:host_${TYPE_CODE}>" "$<TARGET_FILE:client_${TYPE_CODE}>" "${TYPE_CODE}")
        set_tests_properties(e2e.${TYPE_CODE} PROPERTIES TIMEOUT 100 LABELS e2e)
    endif()
endforeach()

if(BUILD_TESTING)
    set(GAME_TEST_SOURCES ${TEST_SOURCES})
    list(FILTER GAME_TEST_SOURCES INCLUDE REGEX "/test_game[^/]*\\.cpp$")
    list(REMOVE_ITEM TEST_SOURCES ${GAME_TEST_SOURCES})
    if(GAME_TEST_SOURCES)
        add_executable(test_game ${GAME_TEST_SOURCES})
        target_link_libraries(test_game PRIVATE connection_fifo GTest::gtest_main)
        gtest_discover_tests(test_game TEST_PREFIX "game." PROPERTIES TIMEOUT 15 LABELS game)
    endif()
    set(CLIENT_TEST_SOURCES ${TEST_SOURCES})
    list(FILTER CLIENT_TEST_SOURCES INCLUDE REGEX "/test_client_controller\\.cpp$")
    list(REMOVE_ITEM TEST_SOURCES ${CLIENT_TEST_SOURCES})
    if(CLIENT_TEST_SOURCES)
        add_executable(test_client ${CLIENT_TEST_SOURCES})
        target_link_libraries(test_client PRIVATE connection_fifo GTest::gtest_main)
        gtest_discover_tests(test_client TEST_PREFIX "client." PROPERTIES TIMEOUT 15 LABELS client)
    endif()
    if(TEST_SOURCES)
        add_executable(test_common ${TEST_SOURCES})
        target_link_libraries(test_common PRIVATE connection_fifo GTest::gtest_main)
        gtest_discover_tests(test_common TEST_PREFIX "common." PROPERTIES TIMEOUT 15 LABELS technical)
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    file(GLOB_RECURSE GUI_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/gui/*.cpp"
    )
    if(GUI_TEST_SOURCES)
        add_executable(test_gui ${GUI_TEST_SOURCES} ${GUI_SOURCES})
        target_compile_definitions(test_gui PRIVATE QT_NO_KEYWORDS)
        target_link_libraries(test_gui PRIVATE connection_fifo Qt6::Widgets Qt6::Test)
        add_dependencies(test_gui client_fifo)
        add_test(NAME gui.MainWindow COMMAND test_gui)
        set_tests_properties(gui.MainWindow PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40 LABELS gui)
    endif()
endif()
