# Compile the same main/core as the production binary, replacing only syslog.
# Linking the capture avoids LD_PRELOAD ordering conflicts with sanitizer runtimes.
add_executable(lab1_test_driver ../src/main.cpp syslog_capture.cpp)
target_link_libraries(lab1_test_driver PRIVATE lab1_core)

add_executable(test_singleton singleton.cpp)
find_package(Threads REQUIRED)
target_link_libraries(test_singleton PRIVATE lab1_core Threads::Threads)
add_test(NAME lab1.singleton COMMAND test_singleton)
add_test(NAME lab1.cli COMMAND lab1 --help)
add_test(NAME lab1.build_options
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_build_options.py")
set_tests_properties(lab1.build_options PROPERTIES TIMEOUT 90)

add_test(NAME lab1.integration
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_lab1.py"
          "$<TARGET_FILE:lab1_test_driver>")
set_tests_properties(lab1.integration PROPERTIES TIMEOUT 180
  ENVIRONMENT "LAB1_SANITIZER=${LAB1_SANITIZER}")
