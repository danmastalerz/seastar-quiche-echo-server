cmake_minimum_required(VERSION 3.10)

project(seastar-quiche-echo-demo)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)

message(STATUS "Searching for seastar package...")
find_package(Seastar REQUIRED)

set(FMT_V8_DIR $ENV{FMT_V8_LIB_HOME})
set(FMT_V8_LIB_DIR ${FMT_V8_DIR}/build)
message(STATUS "Searching for fmt v8 library... (optional)")
find_library(FMT_LIB NAMES fmt HINTS ${FMT_V8_LIB_DIR})
if(FMT_LIB)
    message(STATUS "Found fmt library - ${FMT_LIB}")
endif()


set(QUICHE_DIR $ENV{QUICHE_LIB_HOME})
set(QUICHE_INCLUDE_DIR ${QUICHE_DIR}/include)
set(QUICHE_LIB_DIR ${QUICHE_DIR}/target/debug)
message(STATUS "Searching for quiche library...")
find_library(QUICHE_LIB NAMES quiche HINTS ${QUICHE_LIB_DIR} REQUIRED)
if(QUICHE_LIB)
    message(STATUS "Found quiche library - ${QUICHE_LIB}")
endif()

list(APPEND LIBS Seastar::seastar ${FMT_LIB} ${QUICHE_LIB})
list(APPEND INCLUDE_DIRS ${QUICHE_INCLUDE_DIR})

add_executable(echo_server quiche_echo_server.cc)
target_include_directories(echo_server PRIVATE ${INCLUDE_DIRS})
target_link_libraries(echo_server PRIVATE ${LIBS})

add_executable(echo_client quiche_echo_client.cc)
target_include_directories(echo_client PRIVATE ${INCLUDE_DIRS})
target_link_libraries(echo_client PRIVATE ${LIBS})

