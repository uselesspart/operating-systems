cmake_minimum_required(VERSION 3.16)
project(lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_compile_options(-Wall -Werror)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

# Any src/conn_<type>.cpp automatically yields host_<type> and client_<type>.
# Adding a new interaction type does not require touching this file.
file(GLOB CONN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/conn_*.cpp")
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/host.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client.cpp" ${CONN_SOURCES})

if(NOT CONN_SOURCES)
    message(FATAL_ERROR "No src/conn_*.cpp sources found")
endif()

foreach(conn_src IN LISTS CONN_SOURCES)
    get_filename_component(conn_name "${conn_src}" NAME_WE)      # conn_mmap
    string(REGEX REPLACE "^conn_" "" type_code "${conn_name}")   # mmap
    string(TOUPPER "${type_code}" type_upper)                    # MMAP

    add_executable(host_${type_code}
        src/host.cpp ${COMMON_SOURCES} "${conn_src}")
    target_compile_definitions(host_${type_code} PRIVATE TYPE_${type_upper})
    target_link_libraries(host_${type_code} Qt5::Widgets pthread)

    add_executable(client_${type_code}
        src/client.cpp ${COMMON_SOURCES} "${conn_src}")
    target_compile_definitions(client_${type_code} PRIVATE TYPE_${type_upper})
    target_link_libraries(client_${type_code} Qt5::Widgets pthread)
endforeach()

option(LAB2_BUILD_TESTS "Build GUI shutdown regression tests" OFF)
if(LAB2_BUILD_TESTS)
    enable_testing()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    qt5_generate_moc(src/host.cpp "${CMAKE_CURRENT_BINARY_DIR}/tests/host.moc")
    foreach(type_code mmap shm pipe)
        string(TOUPPER "${type_code}" type_upper)
        add_executable(shutdown_${type_code} tests/host_shutdown.cpp
            ${COMMON_SOURCES} src/conn_${type_code}.cpp
            "${CMAKE_CURRENT_BINARY_DIR}/tests/host.moc")
        set_target_properties(shutdown_${type_code} PROPERTIES AUTOMOC OFF)
        target_include_directories(shutdown_${type_code} PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/tests")
        target_compile_definitions(shutdown_${type_code} PRIVATE TYPE_${type_upper})
        target_link_libraries(shutdown_${type_code} Qt5::Widgets pthread)
        foreach(delay 0 100 600 3500 4200 5200)
            add_test(NAME shutdown_${type_code}_${delay}
                COMMAND shutdown_${type_code} ${delay})
            set_tests_properties(shutdown_${type_code}_${delay} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
endif()
