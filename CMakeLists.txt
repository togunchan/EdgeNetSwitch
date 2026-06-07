cmake_minimum_required(VERSION 3.21)
project(EdgeNetSwitch LANGUAGES CXX)

# -------------------------------------------------------
# Debug / Sanitizer Settings
# -------------------------------------------------------
option(EDGENETSWITCH_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)

option(EDGENETSWITCH_ENABLE_ASSERTIONS
    "Enable runtime assertions"
    ON
)

if(EDGENETSWITCH_ENABLE_SANITIZERS)

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "Sanitizers require Clang or GCC"
        )
    endif()

    message(STATUS
        "EdgeNetSwitch: sanitizers enabled"
    )

    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -g
    )

    add_link_options(
        -fsanitize=address,undefined
    )

endif()

if(NOT EDGENETSWITCH_ENABLE_ASSERTIONS)
    add_compile_definitions(NDEBUG)
endif()

# -------------------------------------------------------
# Global Settings
# -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ENABLE_DEBUG_READER "Enable debug snapshot reader thread" OFF)

include(CTest)

# -------------------------------------------------------
# Logger Module
# -------------------------------------------------------
add_library(Logger
    src/core/Logger.cpp
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
    src/core/Config.cpp
)

target_include_directories(Config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Config
    PUBLIC
        Logger
        nlohmann_json::nlohmann_json
)

target_compile_features(Config PUBLIC cxx_std_20)

# -------------------------------------------------------
# Telemetry Module
# -------------------------------------------------------
add_library(Telemetry
    src/telemetry/Telemetry.cpp
    src/telemetry/WindowedEwmaRateSmoother.cpp
    src/telemetry/TelemetryExportManager.cpp
    src/telemetry/InMemoryTelemetryExporter.cpp
    src/telemetry/FileTelemetryExporter.cpp
)

target_include_directories(Telemetry
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Telemetry
    PUBLIC
        Logger
        MessagingBus
        Config
)

target_compile_features(Telemetry PUBLIC cxx_std_20)

# -------------------------------------------------------
# HealthMonitor Module
# -------------------------------------------------------
add_library(HealthMonitor
    src/runtime/HealthMonitor.cpp
)

target_include_directories(HealthMonitor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(HealthMonitor PUBLIC cxx_std_20)

# -------------------------------------------------------
# Failure Injection Module
# -------------------------------------------------------
add_library(FailureInjection
    src/failure/FailureInjector.cpp
)

target_include_directories(FailureInjection
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(FailureInjection PUBLIC cxx_std_20)

# -------------------------------------------------------
# Switching Module
# -------------------------------------------------------
add_library(Switching
    src/switching/InterfaceRegistry.cpp
    src/switching/MacAddress.cpp
    src/switching/MacTable.cpp
    src/switching/SwitchPort.cpp
    src/switching/SwitchForwardingEngine.cpp
)

target_include_directories(Switching
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(Switching PUBLIC cxx_std_20)

# -------------------------------------------------------
# Daemon Executable
# -------------------------------------------------------
add_executable(EdgeNetSwitchDaemon
    src/daemon/main.cpp
    src/control/ControlDispatch.cpp
    src/runtime/RuntimeStatusBuilder.cpp
    src/runtime/SnapshotPublisher.cpp
    src/packet/PacketParser.cpp
    src/packet/PacketGenerator.cpp
    src/packet/PacketProcessor.cpp
    src/packet/PacketStats.cpp
    src/packet/PacketValidator.cpp
    src/network/UdpReceiver.cpp
    src/replay/ReplayRecorder.cpp
    src/replay/ReplayPlayer.cpp
    src/system/FileDescriptor.cpp
    src/system/FdRegistry.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(EdgeNetSwitchDaemon
        PRIVATE
            src/system/EventFd.cpp
            src/system/EpollManager.cpp
            src/system/EpollEventLoop.cpp
    )
endif()

target_link_libraries(EdgeNetSwitchDaemon
    PRIVATE
        Telemetry
        HealthMonitor
        FailureInjection
        Switching
)

target_include_directories(EdgeNetSwitchDaemon
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(EdgeNetSwitchDaemon PRIVATE cxx_std_20)
if(ENABLE_DEBUG_READER)
    target_compile_definitions(EdgeNetSwitchDaemon PRIVATE EDGENETSWITCH_DEBUG_READER)
endif()

# -------------------------------------------------------
# Unit Tests for Packet parser
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(PacketParserTests
        tests/packet_parser_tests.cpp
        src/packet/PacketParser.cpp
    )

    target_link_libraries(PacketParserTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_include_directories(PacketParserTests PRIVATE include)

    add_test(NAME PacketParserTests COMMAND PacketParserTests)

endif()

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
# Unit Tests for ReplayRecorder
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(ReplayRecorderTests
        tests/replay_recorder_tests.cpp
        src/replay/ReplayRecorder.cpp
    )

    target_link_libraries(ReplayRecorderTests
        PRIVATE
            MessagingBus
            Catch2::Catch2WithMain
    )

    target_include_directories(ReplayRecorderTests PRIVATE include)

    add_test(NAME ReplayRecorderTests COMMAND ReplayRecorderTests)

endif()

# -------------------------------------------------------
# Unit Tests for ReplayPlayer
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(ReplayPlayerTests
        tests/replay_player_tests.cpp
        src/replay/ReplayPlayer.cpp
    )

    target_link_libraries(ReplayPlayerTests
        PRIVATE
            MessagingBus
            Catch2::Catch2WithMain
    )

    target_include_directories(ReplayPlayerTests PRIVATE include)

    add_test(NAME ReplayPlayerTests COMMAND ReplayPlayerTests)

endif()

# -------------------------------------------------------
# Unit Tests for replay equivalence
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(ReplayEquivalenceTests
        tests/replay_equivalence_tests.cpp
        src/packet/PacketProcessor.cpp
        src/packet/PacketStats.cpp
        src/replay/ReplayRecorder.cpp
        src/replay/ReplayPlayer.cpp
    )

    target_link_libraries(ReplayEquivalenceTests
        PRIVATE
            MessagingBus
            FailureInjection
            Switching
            Catch2::Catch2WithMain
    )

    target_include_directories(ReplayEquivalenceTests PRIVATE include)

    add_test(NAME ReplayEquivalenceTests COMMAND ReplayEquivalenceTests)

endif()

# -------------------------------------------------------
# Unit Tests for replay outcome equivalence
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(ReplayOutcomeEquivalenceTests
        tests/replay_outcome_equivalence_tests.cpp
        src/packet/PacketProcessor.cpp
        src/packet/PacketStats.cpp
        src/replay/ReplayRecorder.cpp
        src/replay/ReplayPlayer.cpp
        src/replay/ReplayOutcomeCollector.cpp
    )

    target_link_libraries(ReplayOutcomeEquivalenceTests
        PRIVATE
            MessagingBus
            FailureInjection
            Switching
            Catch2::Catch2WithMain
    )

    target_include_directories(ReplayOutcomeEquivalenceTests PRIVATE include)

    add_test(NAME ReplayOutcomeEquivalenceTests COMMAND ReplayOutcomeEquivalenceTests)

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
        tests/telemetry_export_manager_tests.cpp
    )

    target_link_libraries(TelemetryTests
        PRIVATE
            Telemetry
            MessagingBus
            Config
            Catch2::Catch2WithMain
    )

    target_include_directories(TelemetryTests PRIVATE include src)

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
# Unit Tests for SnapshotPublisher
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(SnapshotPublisherTests
        tests/snapshot_publisher_tests.cpp
        src/runtime/SnapshotPublisher.cpp
    )

    target_link_libraries(SnapshotPublisherTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_include_directories(SnapshotPublisherTests
        PRIVATE
            include
            src
    )

    add_test(NAME SnapshotPublisherTests COMMAND SnapshotPublisherTests)

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

# -------------------------------------------------------
# Unit Tests for Packet pipeline
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(PacketPipelineTests
        tests/packet_pipeline_tests.cpp
        tests/rate_smoother_tests.cpp
        src/packet/PacketProcessor.cpp
        src/packet/PacketStats.cpp
    )

    target_link_libraries(PacketPipelineTests
        PRIVATE
            Telemetry
            MessagingBus
            FailureInjection
            Switching
            Catch2::Catch2WithMain
    )

    target_include_directories(PacketPipelineTests PRIVATE include)

    add_test(NAME PacketPipelineTests COMMAND PacketPipelineTests)

endif()

# -------------------------------------------------------
# Unit Tests for packet forwarding runtime integration
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(PacketForwardingRuntimeTests
        tests/packet_forwarding_runtime_tests.cpp
        src/packet/PacketProcessor.cpp
    )

    target_link_libraries(PacketForwardingRuntimeTests
        PRIVATE
            MessagingBus
            FailureInjection
            Switching
            Catch2::Catch2WithMain
    )

    target_include_directories(PacketForwardingRuntimeTests PRIVATE include)

    add_test(NAME PacketForwardingRuntimeTests COMMAND PacketForwardingRuntimeTests)

endif()

# -------------------------------------------------------
# Unit Tests for interface registry
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(InterfaceRegistryTests
        tests/interface_registry_tests.cpp
        src/switching/InterfaceRegistry.cpp
        src/switching/SwitchPort.cpp
    )

    target_link_libraries(InterfaceRegistryTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_include_directories(InterfaceRegistryTests PRIVATE include)

    add_test(NAME InterfaceRegistryTests COMMAND InterfaceRegistryTests)

endif()

# -------------------------------------------------------
# Unit Tests for MAC table
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(MacTableTests
        tests/mac_table_tests.cpp
        src/switching/MacAddress.cpp
        src/switching/MacTable.cpp
    )

    target_link_libraries(MacTableTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_include_directories(MacTableTests PRIVATE include)

    add_test(NAME MacTableTests COMMAND MacTableTests)

endif()

# -------------------------------------------------------
# Unit Tests for Switch forwarding engine
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(SwitchForwardingEngineTests
        tests/switch_forwarding_engine_tests.cpp
        src/switching/InterfaceRegistry.cpp
        src/switching/MacAddress.cpp
        src/switching/MacTable.cpp
        src/switching/SwitchPort.cpp
        src/switching/SwitchForwardingEngine.cpp
    )

    target_link_libraries(SwitchForwardingEngineTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_include_directories(SwitchForwardingEngineTests PRIVATE include)

    add_test(NAME SwitchForwardingEngineTests COMMAND SwitchForwardingEngineTests)

endif()

# -------------------------------------------------------
# Unit Tests for FileDescriptor
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(FileDescriptorTests
        tests/file_descriptor_tests.cpp
        src/system/FileDescriptor.cpp
        src/system/FdRegistry.cpp
    )

    target_link_libraries(FileDescriptorTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_include_directories(FileDescriptorTests PRIVATE include)

    add_test(NAME FileDescriptorTests COMMAND FileDescriptorTests)

endif()

# -------------------------------------------------------
# Unit Tests for EpollManager and EventFd integration
# -------------------------------------------------------
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")

    add_executable(EpollManagerTests
        tests/epoll_manager_tests.cpp
        src/system/EpollManager.cpp
        src/system/EventFd.cpp
        src/system/FileDescriptor.cpp
        src/system/FdRegistry.cpp
    )

    target_link_libraries(EpollManagerTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_include_directories(EpollManagerTests
        PRIVATE
            include
            src
    )

    add_test(NAME EpollManagerTests COMMAND EpollManagerTests)

endif()

# -------------------------------------------------------
# Unit Tests for Control dispatch
# -------------------------------------------------------
if(BUILD_TESTING)

    add_executable(ControlTests
        tests/control_tests.cpp
        src/control/ControlDispatch.cpp
        src/runtime/SnapshotPublisher.cpp
        src/system/FdRegistry.cpp
    )

    target_link_libraries(ControlTests
        PRIVATE
            Telemetry
            MessagingBus
            Config
            HealthMonitor
            Logger
            Switching
            Catch2::Catch2WithMain
    )

    target_include_directories(ControlTests
        PRIVATE
            include
            src
    )

    add_test(NAME ControlTests COMMAND ControlTests)

endif()
