cmake_minimum_required(VERSION 3.16)
project(ipc_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHAT_BUILD_TESTS "Build unit and connection tests" OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_compile_options(-Wall -Werror)

# Every source is picked up automatically: adding a file never requires editing this list
file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
file(GLOB CONN_SOURCES CONFIGURE_DEPENDS src/conn/conn_*.cpp)
file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp src/gui/*.h)
set(MAIN_SOURCES ${CMAKE_SOURCE_DIR}/src/host.cpp ${CMAKE_SOURCE_DIR}/src/client.cpp)

set(CORE_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM CORE_SOURCES ${CONN_SOURCES} ${GUI_SOURCES} ${MAIN_SOURCES})

add_library(chat_core STATIC ${CORE_SOURCES})
target_include_directories(chat_core PUBLIC src)
target_link_libraries(chat_core PUBLIC Threads::Threads)

add_library(chat_gui STATIC ${GUI_SOURCES})
set_target_properties(chat_gui PROPERTIES AUTOMOC ON)
target_link_libraries(chat_gui PUBLIC chat_core Qt${QT_VERSION_MAJOR}::Widgets)

# host_<type> and client_<type> for every conn_<type>.cpp
set(CHAT_TYPES)
foreach(conn_source ${CONN_SOURCES})
    get_filename_component(conn_name ${conn_source} NAME_WE)
    string(REGEX REPLACE "^conn_" "" type ${conn_name})
    list(APPEND CHAT_TYPES ${type})

    add_executable(host_${type} src/host.cpp ${conn_source})
    target_link_libraries(host_${type} PRIVATE chat_gui)

    add_executable(client_${type} src/client.cpp ${conn_source})
    target_link_libraries(client_${type} PRIVATE chat_core)
endforeach()

if(CHAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
