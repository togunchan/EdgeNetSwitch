#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/core/TimeUtils.hpp"
#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"

#include <chrono>
#include <optional>
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

    monitor.onTick();
    REQUIRE(publishCount == 1);
    REQUIRE(lastStatus.has_value());
    REQUIRE(lastStatus->is_alive);
    CHECK(lastStatus->silence_duration_ms <= 100);

    // Wait beyond timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    monitor.onTick();

    REQUIRE(publishCount == 2);
    REQUIRE(lastStatus.has_value());
    REQUIRE(lastStatus->is_alive == false);
    CHECK(lastStatus->silence_duration_ms > 100);
    CHECK(lastStatus->is_alive == (lastStatus->silence_duration_ms <= 100));
    const auto transition_silence_ms = lastStatus->silence_duration_ms;

    // Further ticks should NOT publish again
    monitor.onTick();
    monitor.onTick();

    REQUIRE(publishCount == 2);
    CHECK(lastStatus->silence_duration_ms == transition_silence_ms);
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

    REQUIRE(publishCount == 1);
    REQUIRE(lastStatus.has_value());
    REQUIRE(lastStatus->is_alive == false);
    CHECK(lastStatus->silence_duration_ms > 100);
    const auto expired_heartbeat_ms = lastStatus->last_heartbeat_ms;

    // Heartbeat restores alive
    const auto heartbeat_before_ms = nowMs();
    monitor.onHeartbeat();
    const auto heartbeat_after_ms = nowMs();
    CHECK(publishCount == 1);
    const auto tick_before_ms = nowMs();
    monitor.onTick();
    const auto tick_after_ms = nowMs();

    REQUIRE(publishCount == 2);
    REQUIRE(lastStatus->is_alive == true);
    CHECK(lastStatus->last_heartbeat_ms >= heartbeat_before_ms);
    CHECK(lastStatus->last_heartbeat_ms <= heartbeat_after_ms);
    CHECK(lastStatus->last_heartbeat_ms > expired_heartbeat_ms);
    CHECK(lastStatus->silence_duration_ms <= 100);
    CHECK(lastStatus->is_alive == (lastStatus->silence_duration_ms <= 100));
    const auto observed_ms = lastStatus->last_heartbeat_ms + lastStatus->silence_duration_ms;
    CHECK(observed_ms >= tick_before_ms);
    CHECK(observed_ms <= tick_after_ms);

    monitor.onTick();
    monitor.onHeartbeat();
    monitor.onTick();
    CHECK(publishCount == 2);
}
