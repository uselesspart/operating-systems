set(TEST_LOG_DIR ${PROJECT_SOURCE_DIR}/test-logs)

add_library(test_runner OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/unit/runner.cpp)

function(wolf_unit_test name)
    add_test(NAME unit_${name} COMMAND ${name})
    set_tests_properties(unit_${name} PROPERTIES
        LABELS unit
        ENVIRONMENT "TEST_LOG_DIR=${TEST_LOG_DIR};QT_QPA_PLATFORM=offscreen")
endfunction()

foreach(name test_game test_protocol test_semaphore)
    add_executable(${name} unit/${name}.cpp)
    target_link_libraries(${name} PRIVATE wolf_core test_runner)
    wolf_unit_test(${name})
endforeach()

add_executable(test_host unit/test_host.cpp ${PROJECT_SOURCE_DIR}/src/conn/conn_pipe.cpp)
target_link_libraries(test_host PRIVATE wolf_core test_runner)
wolf_unit_test(test_host)

foreach(conn_src ${CONN_SOURCES})
    get_filename_component(conn_name ${conn_src} NAME_WE)
    string(REGEX REPLACE "^conn_" "" type ${conn_name})
    add_executable(test_conn_${type} unit/test_conn.cpp ${conn_src})
    target_link_libraries(test_conn_${type} PRIVATE wolf_core test_runner)
    wolf_unit_test(test_conn_${type})
endforeach()

add_executable(test_hostwindow unit/test_hostwindow.cpp)
target_link_libraries(test_hostwindow PRIVATE wolf_gui test_runner)
wolf_unit_test(test_hostwindow)

file(GLOB INTEGRATION_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_*.sh)
foreach(test_sh ${INTEGRATION_TESTS})
    get_filename_component(name ${test_sh} NAME_WE)
    add_test(NAME integration_${name} COMMAND bash ${test_sh} ${CMAKE_BINARY_DIR})
    set_tests_properties(integration_${name} PROPERTIES
        LABELS integration
        RUN_SERIAL TRUE
        TIMEOUT 600
        ENVIRONMENT "TEST_LOG_DIR=${TEST_LOG_DIR};QT_QPA_PLATFORM=offscreen")
endforeach()
