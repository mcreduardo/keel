cmake_minimum_required(VERSION 3.21)  # PROJECT_IS_TOP_LEVEL
project(keel VERSION 0.2.0 LANGUAGES CXX)

# keel is a single header: the target only carries the include path, C++20, and the
# threads library (keel::AsyncInit and keel::AsyncStart run components on threads).
# keel.hpp is generated from the development headers in include/keel/ by
# tools/amalgamate.py; the target, and the install, use the generated keel.hpp.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_library(keel INTERFACE)
add_library(keel::keel ALIAS keel)
include(GNUInstallDirs)
target_include_directories(keel INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(keel INTERFACE cxx_std_20)
target_link_libraries(keel INTERFACE Threads::Threads)

option(KEEL_BUILD_TESTS "Build keel's tests" ${PROJECT_IS_TOP_LEVEL})
option(KEEL_BUILD_EXAMPLES "Build keel's examples (and run them as tests)" ${PROJECT_IS_TOP_LEVEL})
option(KEEL_INSTALL "Generate keel's install rules (find_package(keel) support)" ${PROJECT_IS_TOP_LEVEL})

if(KEEL_BUILD_TESTS OR KEEL_BUILD_EXAMPLES)
    enable_testing()
endif()
if(KEEL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(KEEL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install: the header, the keel::keel target, and a package config for
# find_package(keel CONFIG). keel is header-only, so the package is architecture-independent.
# Version compatibility is SameMinorVersion: before 1.0, a new minor version may change the
# API, so find_package(keel 0.2) accepts 0.2.x only.
if(KEEL_INSTALL)
    include(CMakePackageConfigHelpers)
    set(KEEL_CONFIG_DIR ${CMAKE_INSTALL_DATADIR}/cmake/keel)

    install(TARGETS keel EXPORT keelTargets)
    install(FILES keel.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT keelTargets NAMESPACE keel:: DESTINATION ${KEEL_CONFIG_DIR})

    configure_package_config_file(cmake/keelConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/keelConfig.cmake
        INSTALL_DESTINATION ${KEEL_CONFIG_DIR})
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/keelConfigVersion.cmake
        COMPATIBILITY SameMinorVersion
        ARCH_INDEPENDENT)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/keelConfig.cmake
                  ${CMAKE_CURRENT_BINARY_DIR}/keelConfigVersion.cmake
            DESTINATION ${KEEL_CONFIG_DIR})
endif()
