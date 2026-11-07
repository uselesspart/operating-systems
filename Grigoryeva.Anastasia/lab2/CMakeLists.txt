cmake_minimum_required(VERSION 3.16)
project(wolf_lab2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-Wall -Werror)
include_directories("${CMAKE_SOURCE_DIR}/src")

option(NO_GUI "Собрать хост без Qt (консольный режим, для автотестов)" OFF)
option(BUILD_TESTS "Собирать модульные тесты (unit_*)" ON)
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Куда класть исполняемые файлы")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

find_package(Threads REQUIRED)
set(SYSTEM_LIBS Threads::Threads)
find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
  list(APPEND SYSTEM_LIBS ${RT_LIBRARY})
endif()

if(NO_GUI)
  set(HOST_LIBS "")
else()
  find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
  find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
  set(HOST_LIBS Qt${QT_VERSION_MAJOR}::Widgets)
endif()

# ---- Источники ---------------------------------------------------------------------------
# Любой новый *.cpp в src/ подхватывается автоматически (CMakeLists менять не нужно):
#   conn_<тип>.cpp  -> порождает пару исполняемых файлов host_<тип> и client_<тип>;
#   host.cpp / client.cpp - функции main хоста и клиента;
#   остальные файлы - общий код, линкуется в каждый исполняемый файл.
file(GLOB ALL_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB CONN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/conn_*.cpp")
set(COMMON_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM COMMON_SOURCES
     "${CMAKE_SOURCE_DIR}/src/host.cpp"
     "${CMAKE_SOURCE_DIR}/src/client.cpp"
     ${CONN_SOURCES})

foreach(conn_src IN LISTS CONN_SOURCES)
  get_filename_component(conn_name "${conn_src}" NAME_WE)       # conn_pipe
  string(REGEX REPLACE "^conn_" "" TYPE_CODE "${conn_name}")     # pipe

  add_executable(host_${TYPE_CODE} "${CMAKE_SOURCE_DIR}/src/host.cpp" ${COMMON_SOURCES} "${conn_src}")
  target_link_libraries(host_${TYPE_CODE} PRIVATE ${SYSTEM_LIBS} ${HOST_LIBS})
  if(NO_GUI)
    target_compile_definitions(host_${TYPE_CODE} PRIVATE WOLF_NO_GUI)
  endif()

  add_executable(client_${TYPE_CODE} "${CMAKE_SOURCE_DIR}/src/client.cpp" ${COMMON_SOURCES} "${conn_src}")
  target_link_libraries(client_${TYPE_CODE} PRIVATE ${SYSTEM_LIBS})

  if(BUILD_TESTS)
    add_executable(unit_conn_${TYPE_CODE} "${CMAKE_SOURCE_DIR}/tests/unit_conn.cpp"
                   "${CMAKE_SOURCE_DIR}/src/sync_sem.cpp" "${CMAKE_SOURCE_DIR}/src/fd_io.cpp" "${conn_src}")
    target_link_libraries(unit_conn_${TYPE_CODE} PRIVATE ${SYSTEM_LIBS})
  endif()
endforeach()

if(BUILD_TESTS)
  add_executable(unit_rules "${CMAKE_SOURCE_DIR}/tests/unit_rules.cpp" "${CMAKE_SOURCE_DIR}/src/game_rules.cpp")
endif()
