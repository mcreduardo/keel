cmake_minimum_required(VERSION 3.20)
project(keel VERSION 0.1.0 LANGUAGES CXX)

# keel is a single header: the target only carries the include path and C++20.
add_library(keel INTERFACE)
add_library(keel::keel ALIAS keel)
target_include_directories(keel INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(keel INTERFACE cxx_std_20)

option(KEEL_BUILD_TESTS "Build keel's tests" ${PROJECT_IS_TOP_LEVEL})

if(KEEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
