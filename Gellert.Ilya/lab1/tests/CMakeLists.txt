include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(daemon_test main_test.cpp)
target_link_libraries(daemon_test PRIVATE daemon_core gtest_main)

add_test(NAME DaemonTest COMMAND daemon_test)