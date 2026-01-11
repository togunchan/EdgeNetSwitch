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


#-------------------------------------------------------
# MessagingBus Module
# -------------------------------------------------------
add_library(MessagingBus
    src/messaging/MessagingBus.cpp
)

target_include_directories(MessagingBus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(MessagingBus PUBLIC cxx_std_20)

# -------------------------------------------------------
# Config Module
# -------------------------------------------------------
add_library(Config
    src/utils/Config.cpp
)

target_include_directories(Config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Config
    PUBLIC
        nlohmann_json::nlohmann_json
)

target_compile_features(Config PUBLIC cxx_std_20)

# -------------------------------------------------------
# Telemetry Module
# -------------------------------------------------------
add_library(Telemetry
    src/telemetry/Telemetry.cpp
)

target_include_directories(Telemetry
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Telemetry
    PUBLIC
        MessagingBus
        Config
)

target_compile_features(Telemetry PUBLIC cxx_std_20)

# -------------------------------------------------------
# HealthMonitor Module
# -------------------------------------------------------
add_library(HealthMonitor
    src/health/HealthMonitor.cpp
)

target_include_directories(HealthMonitor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(HealthMonitor PUBLIC cxx_std_20)

# -------------------------------------------------------
# Daemon Executable
# -------------------------------------------------------
add_executable(EdgeNetSwitchDaemon
    src/daemon/main.cpp
    src/daemon/ControlDispatch.cpp
)

target_link_libraries(EdgeNetSwitchDaemon
    PRIVATE
        Logger
        MessagingBus
        Config
        Telemetry
        HealthMonitor
)

target_include_directories(EdgeNetSwitchDaemon
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(EdgeNetSwitchDaemon PRIVATE cxx_std_20)

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
# nlohmann_json Submodule
# -------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/CMakeLists.txt")
    message(STATUS "Using nlohmann_json submodule")
    add_subdirectory(third_party/nlohmann_json)
else()
    message(FATAL_ERROR "nlohmann_json submodule not found! Run: git submodule update --init --recursive")
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

# -------------------------------------------------------
# Unit Tests for MessagingBus
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(MessagingBusTests
        tests/messagingbus_tests.cpp
    )

    target_link_libraries(MessagingBusTests
        PRIVATE
            MessagingBus
            Catch2::Catch2WithMain
    )

    target_include_directories(MessagingBusTests PRIVATE include)

    add_test(NAME MessagingBusTests COMMAND MessagingBusTests)

endif()

# -------------------------------------------------------
# Unit Tests for ConfigLoader
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(ConfigTests
        tests/config_tests.cpp
    )

    target_link_libraries(ConfigTests
        PRIVATE
            Config
            Catch2::Catch2WithMain
    )

    target_include_directories(ConfigTests PRIVATE include)

    add_test(NAME ConfigTests COMMAND ConfigTests)

endif()

# -------------------------------------------------------
# Unit Tests for Telemetry
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(TelemetryTests
        tests/telemetry_tests.cpp
    )

    target_link_libraries(TelemetryTests
        PRIVATE
            Telemetry
            MessagingBus
            Config
            Catch2::Catch2WithMain
    )

    target_include_directories(TelemetryTests PRIVATE include)

    add_test(NAME TelemetryTests COMMAND TelemetryTests)

endif()

# -------------------------------------------------------
# Unit Tests for HealthMonitor
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(HealthMonitorTests
        tests/health_monitor_tests.cpp
    )

    target_link_libraries(HealthMonitorTests
        PRIVATE
            HealthMonitor
            MessagingBus
            Logger
            Catch2::Catch2WithMain
    )

    target_include_directories(HealthMonitorTests
        PRIVATE
            include
    )

    add_test(NAME HealthMonitorTests COMMAND HealthMonitorTests)

endif()

# -------------------------------------------------------
# Unit Tests for HealthMonitor transitions
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(HealthMonitorTransitionTests
        tests/health_monitor_transition_tests.cpp
    )

    target_link_libraries(HealthMonitorTransitionTests
        PRIVATE
            HealthMonitor
            MessagingBus
            Catch2::Catch2WithMain
    )

    target_include_directories(HealthMonitorTransitionTests PRIVATE include)

    add_test(NAME HealthMonitorTransitionTests COMMAND HealthMonitorTransitionTests)

endif()
