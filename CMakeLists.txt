cmake_minimum_required(VERSION 3.16)
project(orderbook CXX)

set(CMAKE_CXX_STANDARD 20)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ZLIB REQUIRED)
include_directories(include)

add_executable(itch_parse tools/itch_parse.cpp)
target_link_libraries(itch_parse ZLIB::ZLIB)

add_executable(test_itch_parser tests/test_itch_parser.cpp)
target_link_libraries(test_itch_parser ZLIB::ZLIB)

add_executable(run_engine tools/run_engine.cpp)
add_executable(test_engine tests/test_engine.cpp)