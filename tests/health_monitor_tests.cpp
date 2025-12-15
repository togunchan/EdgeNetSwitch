#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/HealthMonitor.hpp"
#include "edgenetswitch/MessagingBus.hpp"

#include <optional>
#include <thread>
#include <chrono>

using namespace edgenetswitch;

TEST_CASE("HealthMonitor publishes alive status initially", "[HealthMonitor]")
{
    MessagingBus bus;
    HealthMonitor monitor(bus, 1000);

    std::optional<Message> lastMsg;

    bus.subscribe(MessageType::HealthStatus, [&](const Message &msg)
                  { lastMsg = msg; });

    monitor.onTick();

    REQUIRE(lastMsg.has_value());

    const auto *status = std::get_if<HealthStatus>(&lastMsg->payload);
    REQUIRE(status != nullptr);
    REQUIRE(status->is_alive == true);
}

TEST_CASE("HealthMonitor stays alive when heartbeat is received", "[HealthMonitor]")
{
    MessagingBus bus;
    HealthMonitor monitor(bus, 200);

    std::optional<HealthStatus> lastStatus;

    bus.subscribe(MessageType::HealthStatus, [&](const Message &msg)
                  { lastStatus = std::get<HealthStatus>(msg.payload); });

    monitor.onHeartbeat();
    monitor.onTick();

    REQUIRE(lastStatus.has_value());
    REQUIRE(lastStatus->is_alive == true);
}

TEST_CASE("HealthMonitor reports not alive after timeout", "[HealthMonitor]")
{
    MessagingBus bus;
    HealthMonitor monitor(bus, 100); // very short timeout

    std::optional<HealthStatus> lastStatus;

    bus.subscribe(MessageType::HealthStatus, [&](const Message &msg)
                  { lastStatus = std::get<HealthStatus>(msg.payload); });

    // Wait longer than timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    monitor.onTick();

    REQUIRE(lastStatus.has_value());
    REQUIRE(lastStatus->is_alive == false);
}