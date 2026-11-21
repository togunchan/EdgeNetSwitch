#include <catch2/catch_test_macros.hpp>

#include "lifecycle_test_support.hpp"

#include "../src/runtime/RuntimeStatusBuilder.hpp"
#include "../src/runtime/SnapshotPublisher.hpp"

#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/telemetry/Telemetry.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
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
} // namespace

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
