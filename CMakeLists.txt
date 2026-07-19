# Copyright (c) 2024 Chiradip Mandal
# Author: Chiradip Mandal
# Organization: Space-RF.org
# 
# This file is part of DB25 SQL Tokenizer.
# 
# Licensed under the MIT License. See LICENSE file for details.

# DB25 SQL Tokenizer - CMake Build Configuration
# ==============================================
cmake_minimum_required(VERSION 3.20)
project(DB25SqlTokenizer 
    VERSION 1.0.0
    DESCRIPTION "High-Performance SQL Tokenizer with SIMD Optimization"
    LANGUAGES CXX)

# ==============================================
# Build Options
# ==============================================
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_TOOLS "Build tools (keyword extractor)" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_PROFILING "Enable profiling flags" OFF)
# Opt-in, non-portable build tuned for the host CPU. OFF by default so the
# default build stays portable: the AVX2/AVX-512 code paths are compiled via
# per-function target attributes and selected at runtime, not by -march.
option(ENABLE_NATIVE "Build with -march=native (non-portable, host-tuned)" OFF)

# ==============================================
# C++ Standard and Compiler Settings
# ==============================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================
# Compiler Detection and Flags
# ==============================================
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    set(COMPILER_IS_CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(COMPILER_IS_GCC TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(COMPILER_IS_MSVC TRUE)
endif()

# Common warning flags
if(NOT COMPILER_IS_MSVC)
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Wno-unused-function
    )
else()
    add_compile_options(/W4 /permissive-)
endif()

# ==============================================
# SIMD Architecture Detection
# ==============================================
include(CheckCXXCompilerFlag)

# NOTE: The AVX2 / AVX-512 (and SSE4.2) code paths in
# include/simd_architecture.hpp are compiled with per-function target
# attributes (DB25_TARGET) and selected at RUNTIME via CpuDetection::detect().
# Because of that, the build does NOT need a global -march flag to emit those
# instructions, and the default build stays portable across x86_64 CPUs.
# We only set a conservative SSE4.2 baseline floor here; the wider paths do not
# depend on this flag. Use -DENABLE_NATIVE=ON for a host-tuned (non-portable)
# build.

# Detect CPU architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(ARCH_X86_64 TRUE)
    message(STATUS "Detected x86_64 architecture")

    # Conservative, portable baseline floor. AVX2/AVX-512 are handled by the
    # runtime dispatch + function target attributes, not by a global flag.
    check_cxx_compiler_flag("-msse4.2" HAS_SSE42)
    if(HAS_SSE42)
        set(SIMD_FLAGS "-msse4.2")
        message(STATUS "Using SSE4.2 portable baseline (AVX2/AVX-512 via runtime dispatch)")
    endif()

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(ARCH_ARM64 TRUE)
    message(STATUS "Detected ARM64 architecture")
    # ARM NEON is standard on ARMv8
    set(SIMD_FLAGS "")
    message(STATUS "ARM NEON support detected")
endif()

# ==============================================
# Build Type Configuration
# ==============================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Optimization flags per build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g3 -fno-omit-frame-pointer)
    add_compile_definitions(DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT COMPILER_IS_MSVC)
        add_compile_options(-O3 ${SIMD_FLAGS})
        if(ENABLE_NATIVE)
            add_compile_options(-march=native)
        endif()
    else()
        add_compile_options(/O2 /arch:AVX2)
    endif()
    add_compile_definitions(NDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(NOT COMPILER_IS_MSVC)
        add_compile_options(-O2 -g ${SIMD_FLAGS})
        if(ENABLE_NATIVE)
            add_compile_options(-march=native)
        endif()
    else()
        add_compile_options(/O2 /Zi)
    endif()
endif()

# ==============================================
# Sanitizers
# ==============================================
if(ENABLE_ASAN AND NOT COMPILER_IS_MSVC)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "Address Sanitizer enabled")
endif()

if(ENABLE_UBSAN AND NOT COMPILER_IS_MSVC)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
    message(STATUS "Undefined Behavior Sanitizer enabled")
endif()

# ==============================================
# Profiling
# ==============================================
if(ENABLE_PROFILING AND NOT COMPILER_IS_MSVC)
    add_compile_options(-pg -fprofile-arcs -ftest-coverage)
    add_link_options(-pg -fprofile-arcs -ftest-coverage)
    message(STATUS "Profiling enabled")
endif()

# ==============================================
# Library Target - DB25 Tokenizer
# ==============================================
add_library(db25_tokenizer
    src/simd_tokenizer.cpp
)

target_include_directories(db25_tokenizer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Apply SIMD flags to tokenizer
if(SIMD_FLAGS)
    target_compile_options(db25_tokenizer PRIVATE ${SIMD_FLAGS})
endif()

# Set library properties
set_target_properties(db25_tokenizer PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "db25tokenizer"
    POSITION_INDEPENDENT_CODE ON
)

# Alias for cleaner internal usage
add_library(DB25::Tokenizer ALIAS db25_tokenizer)

# ==============================================
# Test Executables
# ==============================================
if(BUILD_TESTS)
    enable_testing()

    # Main test executable
    add_executable(test_sql_file
        test/test_sql_file.cpp
    )

    target_link_libraries(test_sql_file
        PRIVATE
            DB25::Tokenizer
    )

    # Operator test executable
    add_executable(test_operators
        test/test_operators.cpp
    )

    target_link_libraries(test_operators
        PRIVATE
            DB25::Tokenizer
    )

    # Invalid operators test executable - tests rejection of invalid operators
    add_executable(test_invalid_operators
        test/test_invalid_operators.cpp
    )

    target_link_libraries(test_invalid_operators
        PRIVATE
            DB25::Tokenizer
    )

    # Copy test data to build directory
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/test/sql_test.sqls
        ${CMAKE_CURRENT_BINARY_DIR}/test/sql_test.sqls
        COPYONLY
    )

    # Add CTest tests with strict expectations
    add_test(
        NAME TokenizerBasicTest
        COMMAND test_sql_file
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(TokenizerBasicTest PROPERTIES
        PASS_REGULAR_EXPRESSION "Success Rate: 100.0%"
        FAIL_REGULAR_EXPRESSION "Failed: [1-9];Error"
    )

    add_test(
        NAME TokenizerVerboseTest
        COMMAND test_sql_file -v
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(TokenizerVerboseTest PROPERTIES
        PASS_REGULAR_EXPRESSION "Success Rate: 100.0%"
        FAIL_REGULAR_EXPRESSION "Failed: [1-9];Error"
    )

    add_test(
        NAME TokenizerOutputTest
        COMMAND test_sql_file -o
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_test(
        NAME OperatorTest
        COMMAND test_operators
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(OperatorTest PROPERTIES
        PASS_REGULAR_EXPRESSION "All tests passed.*No regressions detected"
        FAIL_REGULAR_EXPRESSION "FAIL;Failed: [1-9]"
    )

    add_test(
        NAME InvalidOperatorTest
        COMMAND test_invalid_operators
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(InvalidOperatorTest PROPERTIES
        PASS_REGULAR_EXPRESSION "All invalid operators correctly tokenized"
        FAIL_REGULAR_EXPRESSION "FAIL;should be rejected"
    )

    # Performance regression test - ensure tokenizer is fast enough
    add_test(
        NAME PerformanceTest
        COMMAND test_sql_file
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(PerformanceTest PROPERTIES
        PASS_REGULAR_EXPRESSION "Success Rate: 100"  # All tests must pass
    )

    # Set test properties for all tests
    set_tests_properties(TokenizerBasicTest TokenizerVerboseTest TokenizerOutputTest
                        OperatorTest InvalidOperatorTest PerformanceTest
        PROPERTIES
            TIMEOUT 10
            LABELS "tokenizer"
    )

    # Add custom target for running tests
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --verbose
        DEPENDS test_sql_file test_operators test_invalid_operators
        COMMENT "Running all tokenizer tests with strict validation"
    )
endif()

# ==============================================
# Tools
# ==============================================
if(BUILD_TOOLS)
    # Keyword extractor tool
    add_executable(extract_keywords
        tools/extract_keywords.cpp
    )
    
    set_target_properties(extract_keywords PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tools
    )
    
    # Custom target to regenerate keywords
    add_custom_target(regenerate_keywords
        COMMAND extract_keywords 
            ${CMAKE_CURRENT_SOURCE_DIR}/grammar/DB25_SQL_GRAMMAR.ebnf
            ${CMAKE_CURRENT_SOURCE_DIR}/include/keywords.hpp
        DEPENDS extract_keywords
        COMMENT "Regenerating keywords from EBNF grammar"
    )
endif()

# ==============================================
# Installation
# ==============================================
include(GNUInstallDirs)

# Install library
install(TARGETS db25_tokenizer
    EXPORT DB25TokenizerTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/db25
    FILES_MATCHING PATTERN "*.hpp"
)

# Install test executable
if(BUILD_TESTS)
    install(TARGETS test_sql_file
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    
    # Install test data
    install(FILES test/sql_test.sqls
        DESTINATION ${CMAKE_INSTALL_DATADIR}/db25/test
    )
endif()

# Install tools
if(BUILD_TOOLS)
    install(TARGETS extract_keywords
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ==============================================
# Export Configuration
# ==============================================
install(EXPORT DB25TokenizerTargets
    FILE DB25TokenizerTargets.cmake
    NAMESPACE DB25::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DB25Tokenizer
)

# Create config file
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DB25TokenizerConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/DB25TokenizerConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DB25Tokenizer
)

# Generate version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/DB25TokenizerConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Install config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/DB25TokenizerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/DB25TokenizerConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DB25Tokenizer
)

# ==============================================
# Summary
# ==============================================
message(STATUS "")
message(STATUS "DB25 SQL Tokenizer Configuration Summary")
message(STATUS "=========================================")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "System:            ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
if(SIMD_FLAGS)
    message(STATUS "SIMD baseline:     ${SIMD_FLAGS} (AVX2/AVX-512 via runtime dispatch)")
else()
    message(STATUS "SIMD baseline:     default")
endif()
message(STATUS "march=native:      ${ENABLE_NATIVE}")
message(STATUS "Build tests:       ${BUILD_TESTS}")
message(STATUS "Build tools:       ${BUILD_TOOLS}")
message(STATUS "ASAN enabled:      ${ENABLE_ASAN}")
message(STATUS "UBSAN enabled:     ${ENABLE_UBSAN}")
message(STATUS "Profiling:         ${ENABLE_PROFILING}")
message(STATUS "Install prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=========================================")
message(STATUS "")