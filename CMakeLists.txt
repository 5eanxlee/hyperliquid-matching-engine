cmake_minimum_required(VERSION 3.15)
project(HyperliquidEngine VERSION 1.0.0 LANGUAGES CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address -fsanitize=undefined")

# Options
option(HYPERLIQUID_BUILD_TESTS "Build tests" ON)
option(HYPERLIQUID_BUILD_BENCHMARKS "Build benchmarks" ON)
option(HYPERLIQUID_PROFILING "Enable profiling support" OFF)
option(HYPERLIQUID_WEBSOCKET "Enable WebSocket support (requires Boost)" OFF)

if(HYPERLIQUID_PROFILING)
    add_compile_definitions(HYPERLIQUID_PROFILING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -g")
    message(STATUS "Profiling enabled - latency tracking active")
endif()

if(HYPERLIQUID_WEBSOCKET)
    find_package(Boost REQUIRED COMPONENTS system)
    add_compile_definitions(HYPERLIQUID_WEBSOCKET)
    message(STATUS "WebSocket support enabled")
endif()

# Find threads package
find_package(Threads REQUIRED)

# Core library
add_library(hyperliquid INTERFACE)
target_include_directories(hyperliquid INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(hyperliquid INTERFACE Threads::Threads)

if(HYPERLIQUID_WEBSOCKET)
    target_link_libraries(hyperliquid INTERFACE Boost::system)
endif()

# Main executable
add_executable(hyperliquid_engine
    src/main.cpp
    src/matching_engine.cpp
    src/feed_handler.cpp
    src/publisher.cpp
    src/metrics.cpp
)
target_link_libraries(hyperliquid_engine PRIVATE hyperliquid)

# Data generator tool
add_executable(data_generator
    tools/data_generator.cpp
)
target_link_libraries(data_generator PRIVATE hyperliquid)

# Log converter tool
add_executable(log_converter
    tools/log_converter.cpp
)
target_link_libraries(log_converter PRIVATE hyperliquid)

# Interactive demo
add_executable(demo
    tools/demo.cpp
)
target_link_libraries(demo PRIVATE hyperliquid)

# CLI visualization
add_executable(cli_viz
    tools/cli_viz.cpp
)
target_link_libraries(cli_viz PRIVATE hyperliquid)

# CLI live market data
add_executable(cli_live
    tools/cli_live.cpp
)

# Engine JSON bridge (stdin/stdout)
add_executable(engine_bridge
    tools/engine_bridge.cpp
)
target_link_libraries(engine_bridge PRIVATE hyperliquid)

# Tests
if(HYPERLIQUID_BUILD_TESTS)
    enable_testing()
    
    # Fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    
    add_executable(hyperliquid_tests
        tests/test_order_book.cpp
        tests/test_matching.cpp
        tests/test_determinism.cpp
        tests/test_mempool.cpp
        tests/test_spsc_queue.cpp
        tests/test_price_levels.cpp
        tests/test_advanced_orders.cpp
    )
    target_link_libraries(hyperliquid_tests PRIVATE
        hyperliquid
        GTest::gtest_main
    )
    
    # Disable LTO for tests (conflicts with GTest RTTI requirements)
    set_target_properties(hyperliquid_tests PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF
    )
    
    include(GoogleTest)
    gtest_discover_tests(hyperliquid_tests)
endif()

# Benchmarks
if(HYPERLIQUID_BUILD_BENCHMARKS)
    add_executable(benchmark_engine
        benchmarks/bench_engine.cpp
    )
    target_link_libraries(benchmark_engine PRIVATE hyperliquid)
endif()

# Installation
install(TARGETS hyperliquid hyperliquid_engine data_generator
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
