#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"

using namespace edgenetswitch;

TEST_CASE("PacketProcessor emits PacketProcessed and PacketStats updates metrics", "[PacketPipeline]")
{
    MessagingBus bus;
    PacketProcessor processor(bus);
    PacketStats stats(bus);
    std::uint64_t now_ms = 1000;

    std::uint64_t processedCount = 0;
    bus.subscribe(MessageType::PacketProcessed, [&](const Message &)
                  { processedCount++; });

    Packet packet{};
    packet.id = 42;
    packet.timestamp_ms = 1000;
    packet.payload = std::string(128, 'x');

    Message msg{};
    msg.type = MessageType::PacketRx;
    msg.timestamp_ms = packet.timestamp_ms;
    msg.payload = packet;

    bus.publish(msg);

    const PacketMetrics metrics = stats.snapshotAt(now_ms);

    REQUIRE(processedCount == 1);
    REQUIRE(metrics.rx_packets == 1);
    REQUIRE(metrics.rx_bytes == packet.payload.size());
}

TEST_CASE("Packet pipeline accumulates metrics across multiple packets", "[PacketPipeline]")
{
    MessagingBus bus;
    PacketProcessor processor(bus);
    PacketStats stats(bus);
    std::uint64_t now_ms = 1000;

    std::uint64_t processedCount = 0;
    bus.subscribe(MessageType::PacketProcessed, [&](const Message &)
                  { processedCount++; });

    Packet first{};
    first.id = 1;
    first.timestamp_ms = 1000;
    first.payload = std::string(64, 'x');

    Packet second{};
    second.id = 2;
    second.timestamp_ms = 1100;
    second.payload = std::string(96, 'x');

    Packet third{};
    third.id = 3;
    third.timestamp_ms = 1200;
    third.payload = std::string(128, 'x');

    Message msg{};
    msg.type = MessageType::PacketRx;

    msg.timestamp_ms = first.timestamp_ms;
    msg.payload = first;
    bus.publish(msg);

    msg.timestamp_ms = second.timestamp_ms;
    msg.payload = second;
    bus.publish(msg);

    msg.timestamp_ms = third.timestamp_ms;
    msg.payload = third;
    bus.publish(msg);

    const PacketMetrics metrics = stats.snapshotAt(now_ms);
    const std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(first.payload.size()) +
        static_cast<std::uint64_t>(second.payload.size()) +
        static_cast<std::uint64_t>(third.payload.size());

    REQUIRE(processedCount == 3);
    REQUIRE(metrics.rx_packets == 3);
    REQUIRE(metrics.rx_bytes == expectedBytes);
}

TEST_CASE("PacketProcessor ignores invalid payload", "[PacketPipeline]")
{
    MessagingBus bus;
    PacketProcessor processor(bus);
    PacketStats stats(bus);
    std::uint64_t now_ms = 1000;

    Message msg{};
    msg.type = MessageType::PacketRx;
    msg.timestamp_ms = 1000;

    msg.payload = TelemetryData{
        .uptime_ms = 1,
        .tick_count = 1,
        .timestamp_ms = 1000};

    bus.publish(msg);

    auto metrics = stats.snapshotAt(now_ms);

    REQUIRE(metrics.rx_packets == 0);
    REQUIRE(metrics.rx_bytes == 0);
}
