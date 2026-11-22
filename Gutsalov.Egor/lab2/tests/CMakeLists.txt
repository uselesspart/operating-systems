add_executable(lab2_unit unit.cpp)
target_link_libraries(lab2_unit PRIVATE lab2_conn_sock lab2_core)
add_test(NAME lab2.unit COMMAND lab2_unit)

foreach(code pipe fifo sock)
  add_test(NAME lab2.component.${code}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/component.py"
      "${CMAKE_BINARY_DIR}/bin" "${code}")
  set_tests_properties(lab2.component.${code} PROPERTIES TIMEOUT 75 LABELS component)
endforeach()
add_test(NAME lab2.inactivity
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/component.py"
    "${CMAKE_BINARY_DIR}/bin" idle)
set_tests_properties(lab2.inactivity PROPERTIES TIMEOUT 85 LABELS component)

add_test(NAME lab2.build_options
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_options.py")
set_tests_properties(lab2.build_options PROPERTIES TIMEOUT 90)
