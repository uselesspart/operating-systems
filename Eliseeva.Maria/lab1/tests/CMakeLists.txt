# GoogleTest: берём из системы, а если его нет - скачиваем при конфигурации.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

# --- unit-тесты: классы по отдельности ---
add_executable(unit_tests
    ut/config_test.cpp
    ut/cleaner_test.cpp
    ut/pid_manager_test.cpp
    ut/daemon_test.cpp
)
target_include_directories(unit_tests PRIVATE common)
target_link_libraries(unit_tests PRIVATE dirclean_core GTest::gtest_main)
target_compile_options(unit_tests PRIVATE -Wall -Werror)
gtest_discover_tests(unit_tests PROPERTIES LABELS unit DISCOVERY_TIMEOUT 60)

# --- компонентные тесты: настоящий бинарник как чёрный ящик ---
add_executable(component_tests ct/daemon_ct_test.cpp)
target_include_directories(component_tests PRIVATE common ct)
target_link_libraries(component_tests PRIVATE GTest::gtest_main)
target_compile_definitions(component_tests PRIVATE DAEMON_BIN=\"$<TARGET_FILE:daemon>\")
target_compile_options(component_tests PRIVATE -Wall -Werror)
add_dependencies(component_tests daemon)
gtest_discover_tests(component_tests PROPERTIES LABELS component TIMEOUT 120 DISCOVERY_TIMEOUT 60)
