#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/Telemetry.hpp"
#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/Config.hpp"

#include <optional>

using namespace edgenetswitch;

TEST_CASE("Telemetry publishes TelemetryData on each tick", "[Telemetry]")
{
    MessagingBus bus;
    Config cfg{};

    Telemetry telemetry(bus, cfg);

    std::optional<Message> lastMessage;

    bus.subscribe(MessageType::Telemetry, [&](const Message &msg)
                  { lastMessage = msg; });

    telemetry.onTick();

    REQUIRE(lastMessage.has_value());

    const Message &msg = *lastMessage;
    REQUIRE(msg.type == MessageType::Telemetry);

    const auto *data = std::get_if<TelemetryData>(&msg.payload);
    REQUIRE(data != nullptr);

    REQUIRE(data->tick_count == 1);
    REQUIRE(data->uptime_ms >= 0);
}

TEST_CASE("Telemetry tick counter increments on subsequent ticks", "[Telemetry]")
{
    MessagingBus bus;
    Config cfg{};

    Telemetry telemetry(bus, cfg);

    std::uint64_t lastTick = 0;

    bus.subscribe(MessageType::Telemetry, [&](const Message &msg)
                  {
        const auto* data = std::get_if<TelemetryData>(&msg.payload);
        REQUIRE(data != nullptr);
        lastTick = data->tick_count; });

    telemetry.onTick();
    REQUIRE(lastTick == 1);

    telemetry.onTick();
    REQUIRE(lastTick == 2);
}