# Тесты на GoogleTest. Если системный GTest не найден, он скачивается через
# FetchContent (требуется доступ к сети на этапе конфигурации).
include(FetchContent)
find_package(Threads REQUIRED)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest не найден локально, скачиваю через FetchContent")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---- Unit-тесты -------------------------------------------------------------
add_executable(unit_tests
    ut/config_test.cpp
    ut/cleaner_test.cpp
    ut/pid_manager_test.cpp
    ut/daemon_test.cpp
)
target_include_directories(unit_tests PRIVATE common)
target_link_libraries(unit_tests PRIVATE
    rzaeva_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
target_compile_options(unit_tests PRIVATE -Wall -Werror)
add_test(NAME unit COMMAND unit_tests)
set_tests_properties(unit PROPERTIES LABELS unit)

# ---- Компонентные тесты -----------------------------------------------------
add_executable(component_tests
    ct/daemon_ct_test.cpp
)
target_include_directories(component_tests PRIVATE common)
target_link_libraries(component_tests PRIVATE
    rzaeva_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
target_compile_options(component_tests PRIVATE -Wall -Werror)
target_compile_definitions(component_tests PRIVATE DAEMON_BIN="$<TARGET_FILE:daemon>")
add_dependencies(component_tests daemon)
add_test(NAME component COMMAND component_tests)
set_tests_properties(component PROPERTIES LABELS component)
