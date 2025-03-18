cmake_minimum_required(VERSION 3.14)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

#add_compile_options(-fsanitize=thread)
#add_link_options(-fsanitize=thread)
#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

project(MyServer)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/boost.cmake)

find_package(Protobuf REQUIRED)
add_subdirectory(../proto ${CMAKE_BINARY_DIR}/proto)

set(MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/EventDispatcher.h
        ${CMAKE_CURRENT_SOURCE_DIR}/peer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine.h
        ${CMAKE_CURRENT_SOURCE_DIR}/utils.h
        ${CMAKE_CURRENT_SOURCE_DIR}/utils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

add_executable(server ${MAIN_SRC})

target_link_libraries(server PRIVATE
        proto
        Boost::headers
        Boost::asio
        Boost::system
        Boost::lockfree
)

target_include_directories(server PUBLIC
        ${Protobuf_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
)