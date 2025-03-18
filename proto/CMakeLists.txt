cmake_minimum_required(VERSION 3.14)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
project(ProtobufExample)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/proto.cmake)

find_package(Protobuf REQUIRED)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_library(proto STATIC ${PROTO_HDRS} ${PROTO_SRCS})

target_link_libraries(proto PRIVATE ${Protobuf_LIBRARIES})

target_include_directories(proto PUBLIC ${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})