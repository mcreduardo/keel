cmake_minimum_required(VERSION 3.21)  # PROJECT_IS_TOP_LEVEL
project(keel VERSION 0.1.0 LANGUAGES CXX)

# keel is a single header: the target only carries the include path, C++20, and the
# threads library (keel::AsyncInit and keel::AsyncStart run components on threads).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_library(keel INTERFACE)
add_library(keel::keel ALIAS keel)
target_include_directories(keel INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(keel INTERFACE cxx_std_20)
target_link_libraries(keel INTERFACE Threads::Threads)

option(KEEL_BUILD_TESTS "Build keel's tests" ${PROJECT_IS_TOP_LEVEL})

if(KEEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
