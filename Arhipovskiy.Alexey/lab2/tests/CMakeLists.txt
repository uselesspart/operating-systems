find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

function(chat_add_test name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_include_directories(${name} PRIVATE support)
    target_link_libraries(${name} PRIVATE chat_gui Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# One executable per unit/test_*.cpp
file(GLOB UNIT_TESTS CONFIGURE_DEPENDS unit/test_*.cpp)
foreach(test_source ${UNIT_TESTS})
    get_filename_component(name ${test_source} NAME_WE)
    chat_add_test(${name} ${test_source})
endforeach()

# The same connection and server tests for every conn_<type>.cpp
foreach(type ${CHAT_TYPES})
    set(conn_source ${PROJECT_SOURCE_DIR}/src/conn/conn_${type}.cpp)

    chat_add_test(test_conn_${type} conn/test_conn.cpp ${conn_source})
    target_compile_definitions(test_conn_${type} PRIVATE CHAT_CONN_TYPE="${type}")

    chat_add_test(test_server_${type} server/test_server.cpp ${conn_source})
    target_compile_definitions(test_server_${type} PRIVATE CHAT_CLIENT_BINARY="$<TARGET_FILE:client_${type}>")
    add_dependencies(test_server_${type} client_${type})
endforeach()
