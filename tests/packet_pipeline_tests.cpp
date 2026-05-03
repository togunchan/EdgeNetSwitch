#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

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
    constexpr std::uint64_t packet_count = 1;

    std::atomic<std::uint64_t> processedCount{0};
    bus.subscribe(MessageType::PacketProcessed, [&](const Message &)
                  { processedCount.fetch_add(1, std::memory_order_relaxed); });

    Packet packet{};
    packet.lifecycle_id = 1;
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

    const PacketMetrics metrics = stats.snapshotAt(now_ms + packet_count);

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
    constexpr std::uint64_t packet_count = 3;

    std::atomic<std::uint64_t> processedCount{0};
    bus.subscribe(MessageType::PacketProcessed, [&](const Message &)
                  { processedCount.fetch_add(1, std::memory_order_relaxed); });

    Packet first{};
    first.lifecycle_id = 1;
    first.id = 1;
    first.timestamp_ms = 1000;
    first.payload = std::string(64, 'x');

    Packet second{};
    second.lifecycle_id = 2;
    second.id = 2;
    second.timestamp_ms = 1100;
    second.payload = std::string(96, 'x');

    Packet third{};
    third.lifecycle_id = 3;
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

    const PacketMetrics metrics = stats.snapshotAt(now_ms + packet_count);
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
    constexpr std::uint64_t packet_count = 0;

    Message msg{};
    msg.type = MessageType::PacketRx;
    msg.timestamp_ms = 1000;

    msg.payload = TelemetryData{
        .uptime_ms = 1,
        .tick_count = 1,
        .timestamp_ms = 1000};

    REQUIRE_NOTHROW(bus.publish(msg));

    auto metrics = stats.snapshotAt(now_ms + packet_count);

    REQUIRE(metrics.ingress_packets == 0);
    REQUIRE(metrics.processed_packets == 0);
    REQUIRE(metrics.terminal_events == 0);
    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.pending_terminal_events == 0);
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
        packet.lifecycle_id = i + 1;
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
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.ingress_packets == packet_count;
                      }));

    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(metrics);

    REQUIRE(metrics.ingress_packets == packet_count);
    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.ingress_packets == metrics.terminal_events + metrics.pending_terminal_events);
    REQUIRE(metrics.terminal_events == metrics.processed_packets + drops_total);
}

TEST_CASE("Repeated packet ids are not duplicate lifecycle events", "[PacketPipeline]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    constexpr std::uint64_t now_ms = 2500;
    constexpr std::uint64_t packet_count = 2;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < 2; ++i)
    {
        Packet packet{};
        packet.lifecycle_id = i + 1;
        packet.id = 700;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(64, 'r');
        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.ingress_packets == 2 &&
                                 metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(metrics);

    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.processed_packets == 2);
    REQUIRE(metrics.terminal_events == 2);
    REQUIRE(metrics.ingress_packets == metrics.terminal_events + metrics.pending_terminal_events);
    REQUIRE(metrics.terminal_events == metrics.processed_packets + drops_total);
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
        oversized.lifecycle_id = i + 1;
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
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          const auto validation_it = metrics.drops_by_reason.find(PacketDropReason::ValidationError);
                          return metrics.ingress_packets == packet_count &&
                                 validation_it != metrics.drops_by_reason.end() &&
                                 validation_it->second == packet_count;
                      }));

    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(metrics);

    REQUIRE(metrics.ingress_packets == packet_count);
    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.ingress_packets == metrics.terminal_events + metrics.pending_terminal_events);
    REQUIRE(metrics.terminal_events == metrics.processed_packets + drops_total);
}

TEST_CASE("Packet lifecycle accounting remains consistent under real overload", "[PacketPipeline][Overload]")
{
    PacketPipelineFixture fixture;
    MessagingBus &bus = fixture.bus;
    PacketStats &stats = fixture.stats;
    constexpr std::uint64_t now_ms = 4000;
    constexpr std::size_t producer_count = 8;
    constexpr std::uint64_t packets_per_producer = 5000;
    constexpr std::uint64_t overload_count = producer_count * packets_per_producer;
    constexpr std::uint64_t packet_count = overload_count;
    const std::string payload(128, 'o');

    std::vector<std::thread> producers;
    producers.reserve(producer_count);

    for (std::size_t producer = 0; producer < producer_count; ++producer)
    {
        producers.emplace_back([&, producer]
                               {
                                   Message msg{};
                                   msg.type = MessageType::PacketRx;

                                   for (std::uint64_t i = 0; i < packets_per_producer; ++i)
                                   {
                                       Packet packet{};
                                       packet.lifecycle_id = (producer * packets_per_producer) + i + 1;
                                       packet.id = 9000 + (producer * packets_per_producer) + i;
                                       packet.timestamp_ms = now_ms + packet.id;
                                       packet.payload = payload;
                                       msg.timestamp_ms = packet.timestamp_ms;
                                       msg.payload = packet;
                                       bus.publish(msg);
                                   } });
    }

    for (auto &producer : producers)
    {
        producer.join();
    }

    REQUIRE(waitUntil([&]
                      {
                          auto m = stats.snapshotAt(now_ms + packet_count);
                          const auto overflow_it = m.drops_by_reason.find(PacketDropReason::QueueOverflow);
                          return m.ingress_packets >= overload_count &&
                                 overflow_it != m.drops_by_reason.end() &&
                                 overflow_it->second > 0;
                      }));

    REQUIRE(waitUntil([&]
                      {
                          auto m = stats.snapshotAt(now_ms + packet_count);
                          return m.pending_terminal_events == 0 &&
                                 m.terminal_events == packet_count;
                      }));

    auto m = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(m);
    const auto overflow_it = m.drops_by_reason.find(PacketDropReason::QueueOverflow);

    REQUIRE(overflow_it != m.drops_by_reason.end());
    REQUIRE(overflow_it->second > 0);
    REQUIRE(m.duplicate_events == 0);
    REQUIRE(m.ingress_packets >= overload_count);
    REQUIRE(m.ingress_packets == m.terminal_events);
    REQUIRE(m.terminal_events == m.processed_packets + drops_total);
}

TEST_CASE("deterministic simulated loss every 2 packets", "[PacketPipeline][FailureInjection]")
{
    MessagingBus bus;
    PacketStats stats(bus);

    const failure::FailureConfig failure_config{
        .type = failure::FailureType::SimulatedLoss,
        .enabled = true,
        .every_n_packets = 2};
    PacketProcessor processor(bus, failure::FailureInjector{failure_config});

    constexpr std::uint64_t now_ms = 5000;
    constexpr std::uint64_t packet_count = 4;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < packet_count; ++i)
    {
        Packet packet{};
        packet.lifecycle_id = i + 1;
        packet.id = 1000 + i;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(64, 'x');

        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.ingress_packets == packet_count &&
                                 metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(metrics);

    REQUIRE(metrics.processed_packets == 2);
    REQUIRE(drops_total == 2);
    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.pending_terminal_events == 0);
    REQUIRE(metrics.ingress_packets == metrics.terminal_events);
}

TEST_CASE("each lifecycle produces exactly one terminal event", "[PacketPipeline][FailureInjection]")
{
    MessagingBus bus;
    PacketStats stats(bus);

    const failure::FailureConfig failure_config{
        .type = failure::FailureType::SimulatedLoss,
        .enabled = true,
        .every_n_packets = 2};
    PacketProcessor processor(bus, failure::FailureInjector{failure_config});

    constexpr std::uint64_t now_ms = 6000;
    constexpr std::uint64_t packet_count = 10;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < packet_count; ++i)
    {
        Packet packet{};
        packet.lifecycle_id = i + 1;
        packet.id = 2000 + i;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(64, 'x');

        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.ingress_packets == packet_count &&
                                 metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);

    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.pending_terminal_events == 0);
    REQUIRE(metrics.ingress_packets == metrics.terminal_events);

    const auto drops_total = dropsTotal(metrics);

    REQUIRE(metrics.processed_packets == 5);
    REQUIRE(drops_total == 5);
    REQUIRE(metrics.processed_packets + drops_total == metrics.terminal_events);
}

TEST_CASE("artificial delay does not drop packets", "[PacketPipeline][FailureInjection]")
{
    MessagingBus bus;
    PacketStats stats(bus);

    const failure::FailureConfig failure_config{
        .delay_ms = 1.0,
        .type = failure::FailureType::ArtificialDelay,
        .enabled = true,
        .every_n_packets = 2};
    PacketProcessor processor(bus, failure::FailureInjector{failure_config});

    constexpr std::uint64_t now_ms = 7000;
    constexpr std::uint64_t packet_count = 5;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < packet_count; ++i)
    {
        Packet packet{};
        packet.lifecycle_id = i + 1;
        packet.id = 3000 + i;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(64, 'x');

        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.ingress_packets == packet_count &&
                                 metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(metrics);

    REQUIRE(metrics.processed_packets == 5);
    REQUIRE(drops_total == 0);
    REQUIRE(metrics.pending_terminal_events == 0);
    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.ingress_packets == metrics.terminal_events);
}

TEST_CASE("failure injection triggers exactly every N packets", "[PacketPipeline][FailureInjection]")
{
    MessagingBus bus;
    PacketStats stats(bus);

    // NOTE: assumes synchronous MessagingBus dispatch (not thread-safe if async)
    std::vector<std::uint64_t> dropped_lifecycle_ids;
    bus.subscribe(MessageType::PacketDropped, [&](const Message &msg)
                  {
                      const auto drop = std::get<PacketDropped>(msg.payload);
                      dropped_lifecycle_ids.push_back(drop.lifecycle_id);
                  });

    const failure::FailureConfig failure_config{
        .type = failure::FailureType::SimulatedLoss,
        .enabled = true,
        .every_n_packets = 3};
    PacketProcessor processor(bus, failure::FailureInjector{failure_config});

    constexpr std::uint64_t now_ms = 8000;
    constexpr std::uint64_t packet_count = 6;

    Message msg{};
    msg.type = MessageType::PacketRx;

    for (std::uint64_t i = 0; i < packet_count; ++i)
    {
        Packet packet{};
        packet.lifecycle_id = i + 1;
        packet.id = 4000 + i;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(64, 'x');

        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.ingress_packets == packet_count &&
                                 metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(metrics);

    REQUIRE(metrics.processed_packets == 4);
    REQUIRE(metrics.pending_terminal_events == 0);
    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(drops_total == 2);
    REQUIRE(dropped_lifecycle_ids == std::vector<std::uint64_t>{3, 6});
}

TEST_CASE("failure injection under queue pressure produces correct drops", "[PacketPipeline][FailureInjection][Overload]")
{
    MessagingBus bus;
    PacketStats stats(bus);

    std::atomic<bool> hold_worker{true};
    std::atomic<bool> worker_held{false};
    // NOTE: blocks worker after processing begins to simulate queue buildup (not perfect queue isolation)
    bus.subscribe(MessageType::PacketProcessed, [&](const Message &)
                  {
                      worker_held.store(true, std::memory_order_release);
                      while (hold_worker.load(std::memory_order_acquire))
                      {
                          std::this_thread::yield();
                      }
                  });

    const failure::FailureConfig failure_config{
        .type = failure::FailureType::SimulatedLoss,
        .enabled = true,
        .every_n_packets = 2};
    PacketProcessor processor(bus, failure::FailureInjector{failure_config});

    constexpr std::uint64_t now_ms = 9000;
    constexpr std::uint64_t packet_count = 3000;

    Message msg{};
    msg.type = MessageType::PacketRx;

    Packet first_packet{};
    first_packet.lifecycle_id = 1;
    first_packet.id = 5000;
    first_packet.timestamp_ms = now_ms;
    first_packet.payload = std::string(64, 'x');

    msg.timestamp_ms = first_packet.timestamp_ms;
    msg.payload = first_packet;
    bus.publish(msg);

    REQUIRE(waitUntil([&]
                      { return worker_held.load(std::memory_order_acquire); }));

    for (std::uint64_t i = 1; i < packet_count; ++i)
    {
        Packet packet{};
        packet.lifecycle_id = i + 1;
        packet.id = 5000 + i;
        packet.timestamp_ms = now_ms + i;
        packet.payload = std::string(64, 'x');

        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;
        bus.publish(msg);
    }

    hold_worker.store(false, std::memory_order_release);

    PacketMetrics metrics{};
    REQUIRE(waitUntil([&]
                      {
                          metrics = stats.snapshotAt(now_ms + packet_count);
                          return metrics.ingress_packets == packet_count &&
                                 metrics.pending_terminal_events == 0 &&
                                 metrics.terminal_events == packet_count;
                      }));

    metrics = stats.snapshotAt(now_ms + packet_count);
    const std::uint64_t drops_total = dropsTotal(metrics);
    const auto simulated_loss_it = metrics.drops_by_reason.find(PacketDropReason::SimulatedLoss);
    const auto queue_overflow_it = metrics.drops_by_reason.find(PacketDropReason::QueueOverflow);

    REQUIRE(metrics.pending_terminal_events == 0);
    REQUIRE(metrics.duplicate_events == 0);
    REQUIRE(metrics.ingress_packets == metrics.terminal_events);
    REQUIRE(metrics.processed_packets > 0);
    REQUIRE(metrics.processed_packets + drops_total == metrics.terminal_events);
    REQUIRE(drops_total > 0);
    REQUIRE(simulated_loss_it != metrics.drops_by_reason.end());
    REQUIRE(simulated_loss_it->second > 0);
    REQUIRE(queue_overflow_it != metrics.drops_by_reason.end());
    REQUIRE(queue_overflow_it->second > 0);
    REQUIRE(simulated_loss_it->second + queue_overflow_it->second <= drops_total);
}
