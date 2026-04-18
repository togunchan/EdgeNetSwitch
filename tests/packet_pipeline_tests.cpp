#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace edgenetswitch;

namespace
{
    constexpr int kMaxWaitIterations = 1000;
    constexpr auto kWaitStep = std::chrono::milliseconds(1);

    template <typename Predicate>
    bool waitUntil(const Predicate &predicate)
    {
        for (int i = 0; i < kMaxWaitIterations; ++i)
        {
            if (predicate())
                return true;

            std::this_thread::sleep_for(kWaitStep);
        }

        return false;
    }

    std::uint64_t dropsTotal(const PacketMetrics &metrics)
    {
        std::uint64_t total = 0;
        for (const auto &[_, count] : metrics.drops_by_reason)
        {
            total += count;
        }

        return total;
    }

    struct PacketPipelineFixture
    {
        MessagingBus bus;
        PacketStats stats;
        PacketProcessor processor;

        PacketPipelineFixture() : bus(), stats(bus), processor(bus) {}
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

    REQUIRE(waitUntil([&]
                      { return stats.rxPackets() >= 1 &&
                               processedCount.load(std::memory_order_relaxed) >= 1; }));

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

    REQUIRE(waitUntil([&]
                      { return stats.rxPackets() >= 3 &&
                               processedCount.load(std::memory_order_relaxed) >= 3; }));

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

    REQUIRE(waitUntil([&]
                      { return stats.snapshotAt(now_ms).ingress_packets >= 1; }));

    auto metrics = stats.snapshotAt(now_ms);

    REQUIRE(metrics.ingress_packets == 1);
    REQUIRE(metrics.processed_packets == 0);
    REQUIRE(metrics.rx_packets == 0);
    REQUIRE(metrics.rx_bytes == 0);
}

TEST_CASE("Packet lifecycle reaches terminal state under normal processing", "[PacketPipeline]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    constexpr std::uint64_t now_ms = 2000;
    constexpr std::uint64_t packet_count = 4;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < packet_count; ++i)
    {
        Packet packet{};
        packet.id = 100 + i;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(64 + static_cast<std::size_t>(i), 'n');
        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms);
                          const std::uint64_t drops_total = dropsTotal(metrics);
                          return metrics.ingress_packets >= packet_count &&
                                 metrics.terminal_events >= packet_count &&
                                 metrics.duplicate_events == 0 &&
                                 metrics.ingress_packets == metrics.terminal_events + metrics.pending_terminal_events &&
                                 metrics.terminal_events == metrics.processed_packets + drops_total; }));

    REQUIRE(metrics.ingress_packets >= packet_count);
}

TEST_CASE("Packet lifecycle accounting remains consistent for validation drops", "[PacketPipeline]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    constexpr std::uint64_t now_ms = 3000;
    constexpr std::uint64_t packet_count = 3;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < packet_count; ++i)
    {
        Packet oversized{};
        oversized.id = 500 + i;
        oversized.timestamp_ms = now_ms + i;
        oversized.payload = std::string(600, 'x');

        msg.timestamp_ms = oversized.timestamp_ms;
        msg.payload = oversized;
        bus.publish(msg);
    }

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms);
                          const auto validation_it = metrics.drops_by_reason.find(PacketDropReason::ValidationError);
                          const std::uint64_t drops_total = dropsTotal(metrics);
                          return metrics.ingress_packets >= packet_count &&
                                 validation_it != metrics.drops_by_reason.end() &&
                                 validation_it->second == packet_count &&
                                 metrics.duplicate_events == 0 &&
                                 metrics.ingress_packets == metrics.terminal_events + metrics.pending_terminal_events &&
                                 metrics.terminal_events == metrics.processed_packets + drops_total; }));

    REQUIRE(metrics.ingress_packets >= packet_count);
}

TEST_CASE("Packet lifecycle accounting remains consistent under real overload", "[PacketPipeline][Overload]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    constexpr std::uint64_t now_ms = 4000;
    constexpr std::uint64_t overload_count = 20000;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < overload_count; ++i)
    {
        Packet packet{};
        packet.id = 9000 + i;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(128, 'o');
        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    REQUIRE(waitUntil([&]
                      {
                        auto m = stats.snapshotAt(now_ms);
                        const auto overflow_it = m.drops_by_reason.find(PacketDropReason::QueueOverflow);

                        return m.ingress_packets == overload_count &&
                                overflow_it != m.drops_by_reason.end() &&
                                overflow_it->second > 0 &&
                                m.pending_terminal_events == 0 && 
                                m.duplicate_events == 0; }));

    auto m = stats.snapshotAt(now_ms);
    const std::uint64_t drops_total = dropsTotal(m);
    REQUIRE(m.ingress_packets == overload_count);
    REQUIRE(m.ingress_packets == m.terminal_events);
    REQUIRE(m.terminal_events == m.processed_packets + drops_total);
}
