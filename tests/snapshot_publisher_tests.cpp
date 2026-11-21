#include <catch2/catch_test_macros.hpp>

#include "lifecycle_test_support.hpp"

#include "../src/runtime/RuntimeStatusBuilder.hpp"
#include "../src/runtime/SnapshotPublisher.hpp"

#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/telemetry/Telemetry.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <thread>
#include <variant>

namespace
{
    struct ReaderResult
    {
        bool monotonic_versions{true};
        bool non_null_after_first_publish{true};
        std::uint64_t last_seen_version{0};
    };

    struct StatusBuilderFixture
    {
        // Only timeout-specific tests need to depend on elapsed wall-clock time.
        explicit StatusBuilderFixture(
            std::uint64_t timeout_ms = std::numeric_limits<std::uint64_t>::max())
            : telemetry(bus, cfg), health_monitor(bus, timeout_ms), stats(bus), processor(bus)
        {
            health_monitor.onHeartbeat();
        }

        edgenetswitch::RuntimeStatus build(
            edgenetswitch::RuntimeState state = edgenetswitch::RuntimeState::Running)
        {
            return builder.build(telemetry, health_monitor, stats, processor, state, 1000);
        }

        void publishQueueOverflow(std::uint64_t lifecycle_id)
        {
            using namespace edgenetswitch;
            bus.publish(Message{.type = MessageType::PacketDropped,
                                .timestamp_ms = 1,
                                .payload = PacketDropped{.reason = PacketDropReason::QueueOverflow,
                                                         .timestamp_ms = 1,
                                                         .lifecycle_id = lifecycle_id}});
        }

        edgenetswitch::MessagingBus bus;
        edgenetswitch::core::Config cfg{};
        edgenetswitch::Telemetry telemetry;
        edgenetswitch::HealthMonitor health_monitor;
        edgenetswitch::PacketStats stats;
        edgenetswitch::PacketProcessor processor;
        edgenetswitch::RuntimeStatusBuilder builder;
    };
} // namespace

TEST_CASE("RuntimeStatusBuilder derives healthy and shutting-down operational health",
          "[SnapshotPublisher][RuntimeStatusBuilder][OperationalHealth]")
{
    using namespace edgenetswitch;
    StatusBuilderFixture fixture;

    const auto running = fixture.build();
    REQUIRE(running.state == RuntimeState::Running);
    REQUIRE(running.health.is_alive);
    REQUIRE(running.packet.queue_depth < running.packet.queue_capacity);
    REQUIRE(running.packet.drops_by_reason.empty());
    CHECK(running.operational_health == OperationalHealth::Healthy);

    const auto stopping = fixture.build(RuntimeState::Stopping);
    REQUIRE(stopping.state == RuntimeState::Stopping);
    CHECK(stopping.operational_health == OperationalHealth::ShuttingDown);
}

TEST_CASE("RuntimeStatusBuilder overload from a new queue overflow clears on the next build",
          "[SnapshotPublisher][RuntimeStatusBuilder][OperationalHealth]")
{
    using namespace edgenetswitch;
    StatusBuilderFixture fixture;
    REQUIRE(fixture.build().operational_health == OperationalHealth::Healthy);

    for (std::uint64_t overflow_count = 1; overflow_count <= 2; ++overflow_count)
    {
        CAPTURE(overflow_count);
        fixture.publishQueueOverflow(overflow_count);
        const auto overloaded = fixture.build();
        REQUIRE(overloaded.health.is_alive);
        REQUIRE(overloaded.packet.queue_depth < overloaded.packet.queue_capacity);
        REQUIRE(overloaded.packet.drops_by_reason.at(PacketDropReason::QueueOverflow) ==
                overflow_count);
        CHECK(overloaded.operational_health == OperationalHealth::Overloaded);

        const auto recovered = fixture.build();
        CHECK(recovered.packet.drops_by_reason == overloaded.packet.drops_by_reason);
        CHECK(recovered.packet.queue_depth < recovered.packet.queue_capacity);
        CHECK(recovered.operational_health == OperationalHealth::Healthy);
    }
}

TEST_CASE("RuntimeStatusBuilder reports queue saturation until drain with stopping taking priority",
          "[SnapshotPublisher][RuntimeStatusBuilder][OperationalHealth]")
{
    using namespace edgenetswitch;
    lifecycle_test::Gate worker_gate;
    StatusBuilderFixture fixture;
    lifecycle_test::ReleaseGateOnExit release{worker_gate};
    const auto capacity = fixture.processor.queueCapacity();
    REQUIRE(capacity > 1);

    fixture.bus.subscribe(MessageType::PacketProcessed,
                          [&](const Message &) { worker_gate.arriveAndWait(); });
    Message message{
        .type = MessageType::PacketRx,
        .timestamp_ms = 1,
        .payload = Packet{.lifecycle_id = 1, .payload = "operational health", .timestamp_ms = 1}};
    fixture.bus.publish(message);
    REQUIRE(worker_gate.waitForArrival());

    for (std::size_t depth = 1; depth < capacity; ++depth)
    {
        ++std::get<Packet>(message.payload).lifecycle_id;
        fixture.bus.publish(message);
    }
    const auto below_capacity = fixture.build();
    REQUIRE(below_capacity.packet.queue_depth == capacity - 1);
    REQUIRE(below_capacity.health.is_alive);
    CHECK(below_capacity.operational_health == OperationalHealth::Healthy);

    ++std::get<Packet>(message.payload).lifecycle_id;
    fixture.bus.publish(message);
    const auto saturated = fixture.build();
    REQUIRE(saturated.packet.queue_depth == saturated.packet.queue_capacity);
    REQUIRE(saturated.packet.drops_by_reason.empty());
    CHECK(saturated.operational_health == OperationalHealth::Overloaded);
    CHECK(fixture.build().operational_health == OperationalHealth::Overloaded);
    CHECK(fixture.build(RuntimeState::Stopping).operational_health ==
          OperationalHealth::ShuttingDown);

    worker_gate.release();
    fixture.processor.stop();
    const auto drained = fixture.build();
    REQUIRE(drained.packet.queue_depth == 0);
    REQUIRE(drained.packet.queue_high_water_mark == capacity);
    REQUIRE(drained.packet.drops_by_reason.empty());
    CHECK(drained.operational_health == OperationalHealth::Healthy);
}

TEST_CASE("RuntimeStatusBuilder prioritizes stopping and overload over heartbeat degradation",
          "[SnapshotPublisher][RuntimeStatusBuilder][OperationalHealth]")
{
    using namespace edgenetswitch;
    StatusBuilderFixture fixture(100);

    // Match HealthMonitorTests: the monitor owns system_clock, so expiry uses a sleep margin.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto degraded = fixture.build();
    REQUIRE_FALSE(degraded.health.is_alive);
    REQUIRE(degraded.health.silence_duration_ms > 100);
    REQUIRE(degraded.packet.queue_depth < degraded.packet.queue_capacity);
    REQUIRE(degraded.packet.drops_by_reason.empty());
    CHECK(degraded.operational_health == OperationalHealth::Degraded);

    fixture.publishQueueOverflow(1);
    const auto overloaded = fixture.build();
    REQUIRE_FALSE(overloaded.health.is_alive);
    CHECK(overloaded.operational_health == OperationalHealth::Overloaded);
    CHECK(fixture.build().operational_health == OperationalHealth::Degraded);

    fixture.publishQueueOverflow(2);
    const auto stopping = fixture.build(RuntimeState::Stopping);
    REQUIRE_FALSE(stopping.health.is_alive);
    REQUIRE(stopping.packet.drops_by_reason.at(PacketDropReason::QueueOverflow) == 2);
    CHECK(stopping.operational_health == OperationalHealth::ShuttingDown);
}

TEST_CASE("RuntimeStatusBuilder copies packet queue metrics before and after drain",
          "[SnapshotPublisher][RuntimeStatusBuilder]")
{
    using namespace edgenetswitch;

    lifecycle_test::Gate worker_gate;
    MessagingBus bus;
    core::Config cfg{};
    Telemetry telemetry(bus, cfg);
    HealthMonitor health_monitor(bus, 1000);
    PacketStats stats(bus);
    PacketProcessor processor(bus);
    lifecycle_test::ReleaseGateOnExit release{worker_gate};
    RuntimeStatusBuilder builder;

    constexpr std::size_t queued_packets = 3;
    REQUIRE(processor.queueCapacity() > queued_packets);

    const auto check_snapshot = [&](std::size_t expected_depth, std::size_t expected_peak)
    {
        REQUIRE(processor.queueDepth() == expected_depth);
        REQUIRE(processor.queueHighWaterMark() == expected_peak);

        const auto status = builder.build(telemetry, health_monitor, stats, processor,
                                          RuntimeState::Running, 1000);
        CHECK(status.packet.queue_depth == processor.queueDepth());
        CHECK(status.packet.queue_capacity == processor.queueCapacity());
        CHECK(status.packet.queue_high_water_mark == processor.queueHighWaterMark());
    };

    check_snapshot(0, 0);

    // Keep the worker outside the queue while the main thread admits a known backlog.
    bus.subscribe(MessageType::PacketProcessed,
                  [&](const Message &) { worker_gate.arriveAndWait(); });
    Message message{
        .type = MessageType::PacketRx,
        .timestamp_ms = 1,
        .payload = Packet{.lifecycle_id = 1, .payload = "queue visibility", .timestamp_ms = 1}};
    bus.publish(message);
    REQUIRE(worker_gate.waitForArrival());

    for (std::size_t i = 0; i < queued_packets; ++i)
    {
        ++std::get<Packet>(message.payload).lifecycle_id;
        bus.publish(message);
    }
    check_snapshot(queued_packets, queued_packets);

    worker_gate.release();
    processor.stop();
    check_snapshot(0, queued_packets);
}

TEST_CASE("SnapshotPublisher publishes and loads snapshots correctly", "[SnapshotPublisher]")
{
    using edgenetswitch::RuntimeStatus;
    using edgenetswitch::daemon::SnapshotPublisher;

    SnapshotPublisher publisher;

    RuntimeStatus first{};
    first.snapshot_timestamp_ms = 1000;

    const std::uint64_t version1 = publisher.publish(first);
    const auto snapshot1 = publisher.load();

    REQUIRE(snapshot1 != nullptr);
    CHECK(snapshot1->snapshot_timestamp_ms == 1000);
    CHECK(snapshot1->snapshot_version == version1);
    CHECK(publisher.version() == version1);

    RuntimeStatus second{};
    second.snapshot_timestamp_ms = 2000;

    const std::uint64_t version2 = publisher.publish(second);
    const auto snapshot2 = publisher.load();

    REQUIRE(snapshot2 != nullptr);
    // Invariant: publish() version is strictly monotonic for a single writer.
    CHECK(version2 > version1);
    CHECK(snapshot2->snapshot_version == version2);
    CHECK(snapshot2->snapshot_timestamp_ms == 2000);
    CHECK(publisher.version() == version2);
}

TEST_CASE("SnapshotPublisher supports one writer with concurrent readers",
          "[SnapshotPublisher][concurrency]")
{
    using edgenetswitch::RuntimeStatus;
    using edgenetswitch::daemon::SnapshotPublisher;

    constexpr std::uint64_t kPublishCount = 5000;

    SnapshotPublisher publisher;

    std::atomic<bool> start{false};
    std::atomic<bool> first_publish_done{false};
    std::atomic<bool> writer_done{false};
    std::atomic<bool> writer_version_mismatch{false};
    std::atomic<bool> thread_exception{false};

    ReaderResult reader1_result;
    ReaderResult reader2_result;

    auto reader_worker = [&](ReaderResult &result)
    {
        try
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            std::uint64_t last_seen = 0;

            while (!writer_done.load(std::memory_order_acquire))
            {
                const bool after_first_publish =
                    first_publish_done.load(std::memory_order_acquire);
                const auto snapshot = publisher.load();

                // Concurrency guarantee under test:
                // once first_publish_done is visible (acquire), the release-store
                // snapshot publication must also be visible to readers.
                if (after_first_publish && snapshot == nullptr)
                {
                    result.non_null_after_first_publish = false;
                    break;
                }

                // Invariant: each reader must observe non-decreasing versions.
                if (snapshot != nullptr)
                {
                    if (snapshot->snapshot_version < last_seen)
                    {
                        result.monotonic_versions = false;
                        break;
                    }
                    last_seen = snapshot->snapshot_version;
                }

                std::this_thread::yield();
            }

            const bool after_first_publish =
                first_publish_done.load(std::memory_order_acquire);
            const auto final_snapshot = publisher.load();
            if (after_first_publish && final_snapshot == nullptr)
            {
                result.non_null_after_first_publish = false;
            }
            else if (final_snapshot != nullptr)
            {
                if (final_snapshot->snapshot_version < last_seen)
                {
                    result.monotonic_versions = false;
                }
                else
                {
                    last_seen = final_snapshot->snapshot_version;
                }
            }

            result.last_seen_version = last_seen;
        }
        catch (...)
        {
            thread_exception.store(true, std::memory_order_relaxed);
            result.monotonic_versions = false;
            result.non_null_after_first_publish = false;
        }
    };

    std::thread writer([&]
                       {
                           try
                           {
                               while (!start.load(std::memory_order_acquire))
                               {
                                   std::this_thread::yield();
                               }

                               RuntimeStatus status{};
                               for (std::uint64_t i = 1; i <= kPublishCount; ++i)
                               {
                                   status.snapshot_timestamp_ms = i;
                                   const std::uint64_t published_version = publisher.publish(status);
                                   if (published_version != i)
                                   {
                                       writer_version_mismatch.store(true, std::memory_order_relaxed);
                                   }

                                   if (i == 1)
                                   {
                                       first_publish_done.store(true, std::memory_order_release);
                                   }

                                   if ((i % 64) == 0)
                                   {
                                       std::this_thread::yield();
                                   }
                               }
                           }
                           catch (...)
                           {
                               thread_exception.store(true, std::memory_order_relaxed);
                           }

                           writer_done.store(true, std::memory_order_release); });

    std::thread reader1(reader_worker, std::ref(reader1_result));
    std::thread reader2(reader_worker, std::ref(reader2_result));

    start.store(true, std::memory_order_release);

    writer.join();
    reader1.join();
    reader2.join();

    REQUIRE_FALSE(thread_exception.load(std::memory_order_relaxed));
    REQUIRE_FALSE(writer_version_mismatch.load(std::memory_order_relaxed));
    REQUIRE(reader1_result.non_null_after_first_publish);
    REQUIRE(reader2_result.non_null_after_first_publish);
    REQUIRE(reader1_result.monotonic_versions);
    REQUIRE(reader2_result.monotonic_versions);

    CHECK(reader1_result.last_seen_version > 0);
    CHECK(reader2_result.last_seen_version > 0);
    CHECK(reader1_result.last_seen_version <= kPublishCount);
    CHECK(reader2_result.last_seen_version <= kPublishCount);
    CHECK(publisher.version() == kPublishCount);

    const auto final_snapshot = publisher.load();
    REQUIRE(final_snapshot != nullptr);
    CHECK(final_snapshot->snapshot_version == kPublishCount);
    CHECK(final_snapshot->snapshot_timestamp_ms == kPublishCount);
}
