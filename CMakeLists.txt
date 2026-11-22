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

# Correctness checker + benchmark harness for parallel engines
find_package(Threads REQUIRED)

add_executable(check_engine tests/checker/check_engine.cpp)
target_link_libraries(check_engine Threads::Threads)

add_executable(test_checker tests/checker/test_checker.cpp)
target_link_libraries(test_checker Threads::Threads)

# sequential/sequential-engine.hpp includes "event.hpp" from the repo root
target_include_directories(check_engine PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(test_checker PRIVATE ${CMAKE_SOURCE_DIR})

enable_testing()
add_test(NAME test_itch_parser COMMAND test_itch_parser)
add_test(NAME test_engine COMMAND test_engine)
add_test(NAME test_checker COMMAND test_checker)
add_test(NAME check_engine_random COMMAND check_engine --random 200000 --rule both --no-bench)
