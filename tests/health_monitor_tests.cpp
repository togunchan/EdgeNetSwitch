#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/core/TimeUtils.hpp"
#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"

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
    CHECK(status->silence_duration_ms <= 1000);
    CHECK(status->is_alive == (status->silence_duration_ms <= 1000));
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
    CHECK(lastStatus->silence_duration_ms <= 200);
    CHECK(lastStatus->is_alive == (lastStatus->silence_duration_ms <= 200));
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

    const auto tick_before_ms = nowMs();
    monitor.onTick();
    const auto tick_after_ms = nowMs();

    REQUIRE(lastStatus.has_value());
    REQUIRE(lastStatus->is_alive == false);
    CHECK(lastStatus->silence_duration_ms > 100);
    CHECK(lastStatus->is_alive == (lastStatus->silence_duration_ms <= 100));
    const auto observed_ms = lastStatus->last_heartbeat_ms + lastStatus->silence_duration_ms;
    CHECK(observed_ms >= tick_before_ms);
    CHECK(observed_ms <= tick_after_ms);
}

TEST_CASE("HealthMonitor currentStatus measures silence from the latest heartbeat", "[HealthMonitor]")
{
    MessagingBus bus;
    constexpr std::uint64_t timeout_ms = 100;
    HealthMonitor monitor(bus, timeout_ms);
    int publish_count = 0;
    bus.subscribe(MessageType::HealthStatus, [&](const Message &) { ++publish_count; });

    const auto heartbeat_before_ms = nowMs();
    monitor.onHeartbeat();
    const auto heartbeat_after_ms = nowMs();

    // Reuse the existing timeout mechanism; the production clock is not injectable.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto expired_before_ms = nowMs();
    const auto expired = monitor.currentStatus();
    const auto expired_after_ms = nowMs();

    CHECK(expired.last_heartbeat_ms >= heartbeat_before_ms);
    CHECK(expired.last_heartbeat_ms <= heartbeat_after_ms);
    CHECK(expired.silence_duration_ms > timeout_ms);
    CHECK_FALSE(expired.is_alive);
    CHECK(expired.is_alive == (expired.silence_duration_ms <= timeout_ms));
    const auto expired_observed_ms = expired.last_heartbeat_ms + expired.silence_duration_ms;
    CHECK(expired_observed_ms >= expired_before_ms);
    CHECK(expired_observed_ms <= expired_after_ms);

    const auto recovery_before_ms = nowMs();
    monitor.onHeartbeat();
    const auto recovery_after_ms = nowMs();
    const auto recovered = monitor.currentStatus();
    const auto recovered_after_ms = nowMs();

    CHECK(recovered.last_heartbeat_ms >= recovery_before_ms);
    CHECK(recovered.last_heartbeat_ms <= recovery_after_ms);
    CHECK(recovered.last_heartbeat_ms > expired.last_heartbeat_ms);
    CHECK(recovered.is_alive);
    CHECK(recovered.silence_duration_ms <= timeout_ms);
    CHECK(recovered.is_alive == (recovered.silence_duration_ms <= timeout_ms));
    const auto recovered_observed_ms = recovered.last_heartbeat_ms + recovered.silence_duration_ms;
    CHECK(recovered_observed_ms >= recovery_after_ms);
    CHECK(recovered_observed_ms <= recovered_after_ms);
    CHECK(publish_count == 0);
}
