cmake_minimum_required(VERSION 3.16)
project(wolf_goats VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WOLF_BUILD_GUI "Собирать Qt-приложения хоста" ON)
option(WOLF_BUILD_TESTS "Собирать тесты" OFF)

find_package(Threads REQUIRED)

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS "${SOURCE_DIR}/*.cpp")
file(GLOB CONNECTION_SOURCES CONFIGURE_DEPENDS "${SOURCE_DIR}/conn_*.cpp")
file(GLOB GUI_SOURCES CONFIGURE_DEPENDS "${SOURCE_DIR}/gui/*.cpp")

set(GUI_HEADERS
    "${INCLUDE_DIR}/wolf/host_window.hpp"
    "${INCLUDE_DIR}/wolf/host_worker.hpp"
)

set(CORE_SOURCES ${PROJECT_SOURCES})
list(REMOVE_ITEM CORE_SOURCES "${SOURCE_DIR}/host.cpp")
foreach(GUI_SOURCE IN LISTS GUI_SOURCES)
    list(REMOVE_ITEM CORE_SOURCES "${GUI_SOURCE}")
endforeach()
foreach(CONNECTION_SOURCE IN LISTS CONNECTION_SOURCES)
    list(REMOVE_ITEM CORE_SOURCES "${CONNECTION_SOURCE}")
endforeach()

if(WOLF_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        set(WOLF_QT_TARGET Qt6::Widgets)
    else()
        find_package(Qt5 QUIET COMPONENTS Widgets)
        if(Qt5_FOUND)
            set(WOLF_QT_TARGET Qt5::Widgets)
        endif()
    endif()

    if(NOT WOLF_QT_TARGET)
        message(FATAL_ERROR "Не найден Qt с модулем Widgets. Установите qt6-base-dev или qtbase5-dev.")
    endif()

    foreach(CONNECTION_SOURCE IN LISTS CONNECTION_SOURCES)
        get_filename_component(CONNECTION_NAME "${CONNECTION_SOURCE}" NAME_WE)
        string(SUBSTRING "${CONNECTION_NAME}" 5 -1 TRANSPORT)

        add_executable(host_${TRANSPORT}
            ${CORE_SOURCES}
            ${GUI_SOURCES}
            ${GUI_HEADERS}
            "${SOURCE_DIR}/host.cpp"
            "${CONNECTION_SOURCE}"
        )
        target_include_directories(host_${TRANSPORT} PRIVATE "${INCLUDE_DIR}")
        set_target_properties(host_${TRANSPORT} PROPERTIES AUTOMOC ON)
        target_compile_options(host_${TRANSPORT} PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(host_${TRANSPORT} PRIVATE
            ${WOLF_QT_TARGET}
            Threads::Threads
            rt
        )
    endforeach()
endif()

if(WOLF_BUILD_TESTS)
    enable_testing()

    add_executable(wolf_game_tests
        "${SOURCE_DIR}/game.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_game.cpp"
    )
    target_include_directories(wolf_game_tests PRIVATE "${INCLUDE_DIR}")
    target_compile_options(wolf_game_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME game COMMAND wolf_game_tests)

    foreach(CONNECTION_SOURCE IN LISTS CONNECTION_SOURCES)
        get_filename_component(CONNECTION_NAME "${CONNECTION_SOURCE}" NAME_WE)
        string(SUBSTRING "${CONNECTION_NAME}" 5 -1 TRANSPORT)

        add_executable(conn_${TRANSPORT}_tests
            "${SOURCE_DIR}/shared_state.cpp"
            "${SOURCE_DIR}/conn.cpp"
            "${CONNECTION_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_conn.cpp"
        )
        target_include_directories(conn_${TRANSPORT}_tests PRIVATE "${INCLUDE_DIR}")
        target_compile_options(conn_${TRANSPORT}_tests PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(conn_${TRANSPORT}_tests PRIVATE Threads::Threads rt)
        add_test(NAME conn_${TRANSPORT} COMMAND conn_${TRANSPORT}_tests)
        set_tests_properties(conn_${TRANSPORT} PROPERTIES TIMEOUT 40)
    endforeach()

    if(WOLF_BUILD_GUI)
        add_executable(wolf_gui_tests
            "${SOURCE_DIR}/game.cpp"
            "${SOURCE_DIR}/shared_state.cpp"
            "${SOURCE_DIR}/conn.cpp"
            ${GUI_SOURCES}
            ${GUI_HEADERS}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui.cpp"
        )
        target_include_directories(wolf_gui_tests PRIVATE "${INCLUDE_DIR}")
        set_target_properties(wolf_gui_tests PROPERTIES AUTOMOC ON)
        target_compile_options(wolf_gui_tests PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(wolf_gui_tests PRIVATE
            ${WOLF_QT_TARGET}
            Threads::Threads
            rt
        )
        add_test(NAME gui COMMAND wolf_gui_tests)
        set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

        foreach(CONNECTION_SOURCE IN LISTS CONNECTION_SOURCES)
            get_filename_component(CONNECTION_NAME "${CONNECTION_SOURCE}" NAME_WE)
            string(SUBSTRING "${CONNECTION_NAME}" 5 -1 TRANSPORT)

            add_executable(e2e_${TRANSPORT}_tests
                "${SOURCE_DIR}/game.cpp"
                "${SOURCE_DIR}/shared_state.cpp"
                "${SOURCE_DIR}/conn.cpp"
                "${SOURCE_DIR}/client.cpp"
                ${GUI_SOURCES}
                ${GUI_HEADERS}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_e2e.cpp"
                "${CONNECTION_SOURCE}"
            )
            target_include_directories(e2e_${TRANSPORT}_tests PRIVATE "${INCLUDE_DIR}")
            set_target_properties(e2e_${TRANSPORT}_tests PROPERTIES AUTOMOC ON)
            target_compile_options(e2e_${TRANSPORT}_tests PRIVATE -Wall -Wextra -Werror)
            target_link_libraries(e2e_${TRANSPORT}_tests PRIVATE
                ${WOLF_QT_TARGET}
                Threads::Threads
                rt
            )
            add_test(NAME e2e_${TRANSPORT} COMMAND e2e_${TRANSPORT}_tests)
            set_tests_properties(e2e_${TRANSPORT} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                TIMEOUT 60
            )
        endforeach()
    endif()
endif()
