cmake_minimum_required(VERSION 3.14)
project(dirclean_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)
include(GoogleTest)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Всё, кроме main.cpp, собираем в библиотеку для тестов
add_library(dirclean_core STATIC
    ${SRC}/config.cpp ${SRC}/cleaner.cpp ${SRC}/pid_manager.cpp ${SRC}/daemon.cpp)
target_include_directories(dirclean_core PUBLIC ${SRC})
target_compile_options(dirclean_core PRIVATE -Wall -Werror)

# Сам демон нужен компонентным тестам
add_executable(daemon_bin ${SRC}/main.cpp)
target_link_libraries(daemon_bin dirclean_core)
set_target_properties(daemon_bin PROPERTIES OUTPUT_NAME daemon)

# Модульные тесты
add_executable(ut_tests
    ut/config_test.cpp ut/cleaner_test.cpp ut/pid_manager_test.cpp ut/daemon_test.cpp)
target_link_libraries(ut_tests dirclean_core GTest::gtest_main)
gtest_discover_tests(ut_tests PROPERTIES LABELS unit)

# Компонентные тесты
add_executable(ct_tests ct/daemon_ct.cpp)
target_compile_definitions(ct_tests PRIVATE DAEMON_BIN="$<TARGET_FILE:daemon_bin>")
target_link_libraries(ct_tests dirclean_core GTest::gtest_main)
add_dependencies(ct_tests daemon_bin)
gtest_discover_tests(ct_tests PROPERTIES LABELS component TIMEOUT 60)
