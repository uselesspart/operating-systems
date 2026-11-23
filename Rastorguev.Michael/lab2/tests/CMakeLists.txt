set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_AUTOMOC OFF)  # тестам Qt не нужен

add_executable(test_rules rules_test.cpp ${IMPL_DIR}/rules.cpp)
target_include_directories(test_rules PRIVATE ${INCLUDE_DIRS})
set(ALL_TEST_TARGETS test_rules)

foreach(conn_src ${CONN_SOURCES})
    get_filename_component(conn_name ${conn_src} NAME_WE)
    string(REGEX REPLACE "^conn_" "" type ${conn_name})

    add_executable(test_conn_${type} conn_test.cpp ${conn_src}
                   ${IMPL_DIR}/kid.cpp ${IMPL_DIR}/logger.cpp ${IMPL_DIR}/conns/channel.cpp)
    target_include_directories(test_conn_${type} PRIVATE ${INCLUDE_DIRS})
    target_link_libraries(test_conn_${type} PRIVATE Threads::Threads rt)
    list(APPEND ALL_TEST_TARGETS test_conn_${type})
endforeach()

add_custom_target(tests DEPENDS ${ALL_TEST_TARGETS})
