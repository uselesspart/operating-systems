cmake_minimum_required(VERSION 3.16)
project(reminder_daemon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Werror)

add_library(reminder_core STATIC
    src/Event.cpp
    src/Config.cpp
    src/PidFile.cpp
    src/Notifier.cpp
)
target_include_directories(reminder_core PUBLIC include)

add_executable(reminder_daemon
    src/main.cpp
    src/Daemon.cpp
)
target_link_libraries(reminder_daemon PRIVATE reminder_core)

option(REMINDER_BUILD_TESTS "Собирать тесты" ON)

if(REMINDER_BUILD_TESTS)
    enable_testing()

    add_executable(test_event tests/unit/test_event.cpp)
    target_link_libraries(test_event PRIVATE reminder_core)
    add_test(NAME unit_event COMMAND test_event)

    add_executable(test_config tests/unit/test_config.cpp)
    target_link_libraries(test_config PRIVATE reminder_core)
    add_test(NAME unit_config COMMAND test_config)

    add_test(NAME integration_add_event
        COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration/test_add_event.sh $<TARGET_FILE:reminder_daemon>)
    add_test(NAME integration_daemon
        COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration/test_daemon.sh $<TARGET_FILE:reminder_daemon>)

    set_tests_properties(integration_add_event integration_daemon PROPERTIES RUN_SERIAL TRUE)
endif()
