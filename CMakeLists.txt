cmake_minimum_required(VERSION 3.21)
project(EdgeNetSwitch LANGUAGES CXX)

# -------------------------------------------------------
# Global Settings
# -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# -------------------------------------------------------
# Logger Module
# -------------------------------------------------------
add_library(Logger
    src/utils/Logger.cpp
)

target_include_directories(Logger
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(Logger PUBLIC cxx_std_20)

# -------------------------------------------------------
# Catch2 Submodule
# -------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Catch2/CMakeLists.txt")
    message(STATUS "Using Catch2 submodule")
    set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/Catch2)
else()
    message(FATAL_ERROR "Catch2 submodule not found! Run: git submodule update --init --recursive")
endif()

# -------------------------------------------------------
# Unit Tests for Logger
# -------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()

    add_executable(LoggerTests
        tests/logger_tests.cpp
    )

    target_link_libraries(LoggerTests
        PRIVATE
            Logger
            Catch2::Catch2WithMain
    )

    target_include_directories(LoggerTests PRIVATE include)
    add_test(NAME LoggerTests COMMAND LoggerTests)
endif()