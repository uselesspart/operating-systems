add_library(lab1_core STATIC
  config.cpp
  daemon.cpp
  file_manager.cpp
  pid_file.cpp
  worker.cpp
)
target_include_directories(lab1_core PUBLIC ../include)
target_link_libraries(lab1_core PUBLIC lab1_build_options)

add_executable(lab1 main.cpp)
target_link_libraries(lab1 PRIVATE lab1_core)
set_target_properties(lab1 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
