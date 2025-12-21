#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/HealthMonitor.hpp"
#include "edgenetswitch/MessagingBus.hpp"

#include <thread>

using namespace edgenetswitch;

TEST_CASE("HealthMonitor does not publish when state does not change", "[HealthMonitor]")
{
    MessagingBus bus;
    HealthMonitor monitor(bus, 500);

    int publishCount = 0;

    bus.subscribe(MessageType::HealthStatus, [&](const Message &)
                  { publishCount++; });

    // Initial tick publishes once (unknown -> alive), subsequent identical state should not republish
    monitor.onTick();
    monitor.onTick();

    REQUIRE(publishCount == 1);
}

TEST_CASE("HealthMonitor publishes once when transitioning to not alive", "[HealthMonitor]")
{
    MessagingBus bus;
    HealthMonitor monitor(bus, 100); // short timeout

    int publishCount = 0;
    std::optional<HealthStatus> lastStatus;

    bus.subscribe(MessageType::HealthStatus, [&](const Message &msg)
                  {
        publishCount++;
        lastStatus = std::get<HealthStatus>(msg.payload); });

    // Wait beyond timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    monitor.onTick();

    REQUIRE(publishCount == 1);
    REQUIRE(lastStatus.has_value());
    REQUIRE(lastStatus->is_alive == false);

    // Further ticks should NOT publish again
    monitor.onTick();
    monitor.onTick();

    REQUIRE(publishCount == 1);
}

TEST_CASE("HealthMonitor publishes once when transitioning back to alive", "[HealthMonitor]")
{
    MessagingBus bus;
    HealthMonitor monitor(bus, 100);

    int publishCount = 0;
    std::optional<HealthStatus> lastStatus;

    bus.subscribe(MessageType::HealthStatus, [&](const Message &msg)
                  {
        publishCount++;
        lastStatus = std::get<HealthStatus>(msg.payload); });

    // Go to not alive
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    monitor.onTick();

    REQUIRE(lastStatus->is_alive == false);

    // Heartbeat restores alive
    monitor.onHeartbeat();
    monitor.onTick();

    REQUIRE(publishCount == 2);
    REQUIRE(lastStatus->is_alive == true);
}
