# Builds a program against an installed keel: find_package(keel) and keel::keel.
#
#   cmake -S tests/install_consumer -B <dir> -DCMAKE_PREFIX_PATH=<keel install prefix>
cmake_minimum_required(VERSION 3.21)
project(keel_install_consumer LANGUAGES CXX)

find_package(keel 0.1 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE keel::keel)
