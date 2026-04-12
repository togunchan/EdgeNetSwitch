#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#define private public
#include "edgenetswitch/packet/PacketProcessor.hpp"
#undef private
#include "edgenetswitch/packet/PacketStats.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace edgenetswitch;

namespace
{
    constexpr int kMaxWaitIterations = 100;
    constexpr auto kWaitStep = std::chrono::milliseconds(1);

    template <typename Predicate>
    void waitUntil(Predicate &&predicate)
    {
        for (int i = 0; i < kMaxWaitIterations; ++i)
        {
            if (predicate())
                break;

            std::this_thread::sleep_for(kWaitStep);
        }
    }

    struct PacketPipelineFixture
    {
        MessagingBus bus;
        PacketProcessor processor;
        PacketStats stats;

        PacketPipelineFixture() : bus(), processor(bus), stats(bus) {}
    };
} // namespace

TEST_CASE("PacketProcessor emits PacketProcessed and PacketStats updates metrics", "[PacketPipeline]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    std::uint64_t now_ms = 1000;

    std::atomic<std::uint64_t> processedCount{0};
    bus.subscribe(MessageType::PacketProcessed, [&](const Message &)
                  { processedCount.fetch_add(1, std::memory_order_relaxed); });

    Packet packet{};
    packet.id = 42;
    packet.timestamp_ms = 1000;
    packet.payload = std::string(128, 'x');

    Message msg{};
    msg.type = MessageType::PacketRx;
    msg.timestamp_ms = packet.timestamp_ms;
    msg.payload = packet;

    bus.publish(msg);

    waitUntil([&]
              { return stats.rxPackets() >= 1 &&
                       processedCount.load(std::memory_order_relaxed) >= 1; });

    const PacketMetrics metrics = stats.snapshotAt(now_ms);

    REQUIRE(processedCount.load(std::memory_order_relaxed) == 1);
    REQUIRE(metrics.rx_packets == 1);
    REQUIRE(metrics.rx_bytes == packet.payload.size());
}

TEST_CASE("Packet pipeline accumulates metrics across multiple packets", "[PacketPipeline]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    std::uint64_t now_ms = 1000;

    std::atomic<std::uint64_t> processedCount{0};
    bus.subscribe(MessageType::PacketProcessed, [&](const Message &)
                  { processedCount.fetch_add(1, std::memory_order_relaxed); });

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

    waitUntil([&]
              { return stats.rxPackets() >= 3 &&
                       processedCount.load(std::memory_order_relaxed) >= 3; });

    const PacketMetrics metrics = stats.snapshotAt(now_ms);
    const std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(first.payload.size()) +
        static_cast<std::uint64_t>(second.payload.size()) +
        static_cast<std::uint64_t>(third.payload.size());

    REQUIRE(processedCount.load(std::memory_order_relaxed) == 3);
    REQUIRE(metrics.rx_packets == 3);
    REQUIRE(metrics.rx_bytes == expectedBytes);
}

TEST_CASE("PacketProcessor ignores invalid payload", "[PacketPipeline]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    std::uint64_t now_ms = 1000;

    Message msg{};
    msg.type = MessageType::PacketRx;
    msg.timestamp_ms = 1000;

    msg.payload = TelemetryData{
        .uptime_ms = 1,
        .tick_count = 1,
        .timestamp_ms = 1000};

    bus.publish(msg);

    waitUntil([&]
              { return stats.snapshotAt(now_ms).ingress_packets >= 1; });

    auto metrics = stats.snapshotAt(now_ms);

    REQUIRE(metrics.ingress_packets == 1);
    REQUIRE(metrics.processed_packets == 0);
    REQUIRE(metrics.rx_packets == 0);
    REQUIRE(metrics.rx_bytes == 0);
}
