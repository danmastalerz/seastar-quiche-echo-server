cmake_minimum_required(VERSION 3.10)

project(seastar-quiche-echo-demo)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)


file(COPY ${PROJECT_SOURCE_DIR}/cert.crt
        DESTINATION ${PROJECT_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/cert.key
        DESTINATION ${PROJECT_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/cert-big.crt
        DESTINATION ${PROJECT_BINARY_DIR})


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
find_library(QUICHE_LIB NAMES quiche HINTS /home/danielmastalerz/Pulpit/zpp/quiche/target/debug REQUIRED)
if(QUICHE_LIB)
    message(STATUS "Found quiche library - ${QUICHE_LIB}")
endif()


list(APPEND LIBS Seastar::seastar ${FMT_LIB} ${QUICHE_LIB})
list(APPEND INCLUDE_DIRS ${QUICHE_INCLUDE_DIR} include)


list(APPEND
        SERVER_SRC
        src/quiche_echo_server.cc
        src/server/server.cc
        src/quic_connection/quic_connection.cc
        )
add_executable(echo_server ${SERVER_SRC})
target_include_directories(echo_server PRIVATE ${INCLUDE_DIRS})
target_link_libraries(echo_server PRIVATE ${LIBS})


list(APPEND
        CLIENT_SRC
        src/quiche_echo_client.cc
        src/client/client.cpp
        )
add_executable(echo_client ${CLIENT_SRC})
target_include_directories(echo_client PRIVATE ${INCLUDE_DIRS})
target_link_libraries(echo_client PRIVATE ${LIBS})