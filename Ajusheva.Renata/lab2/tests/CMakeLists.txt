file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.cpp)

add_executable(unit_tests ${CMAKE_CURRENT_SOURCE_DIR}/unit/check.cpp ${UNIT_SOURCES})
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(unit_tests PRIVATE lab2_gui)
add_test(NAME unit_tests COMMAND unit_tests)
set_tests_properties(unit_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

foreach(kind ${LAB2_KINDS})
    add_executable(link_tests_${kind}
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/check.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/link/test_link.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/link/test_server.cpp
        ${PROJECT_SOURCE_DIR}/src/conn/conn_${kind}.cpp)
    target_include_directories(link_tests_${kind} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_compile_definitions(link_tests_${kind} PRIVATE LAB2_KIND="${kind}")
    target_link_libraries(link_tests_${kind} PRIVATE lab2_core)
    add_test(NAME link_tests_${kind} COMMAND link_tests_${kind})
    set_tests_properties(link_tests_${kind} PROPERTIES TIMEOUT 120)
endforeach()
