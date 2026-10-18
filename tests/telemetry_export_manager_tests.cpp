#include <catch2/catch_test_macros.hpp>

#include "lifecycle_test_support.hpp"

#include "edgenetswitch/telemetry/TelemetryExportManager.hpp"
#include "edgenetswitch/telemetry/InMemoryTelemetryExporter.hpp"
#include "edgenetswitch/telemetry/TelemetryExporter.hpp"
#include "edgenetswitch/runtime/RuntimeMetrics.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace edgenetswitch;

namespace
{
    class CountingExporter final : public TelemetryExporter
    {
    public:
        void exportSample(const RuntimeMetrics &sample) override
        {
            ++call_count;
            last_metrics = sample;
            seen.push_back(sample);
            if (export_gate)
                export_gate->arriveAndWait();
        }

        int call_count = 0;
        RuntimeMetrics last_metrics{0, 0};
        std::vector<RuntimeMetrics> seen;
        lifecycle_test::Gate *export_gate = nullptr;
    };

    class ThrowingExporter final : public TelemetryExporter
    {
    public:
        void exportSample(const RuntimeMetrics &) override
        {
            ++call_count;
            throw std::runtime_error("export failure");
        }

        int call_count = 0;
    };

    void requireMetricsEq(const RuntimeMetrics &actual, const RuntimeMetrics &expected)
    {
        REQUIRE(actual.uptime_ms == expected.uptime_ms);
        REQUIRE(actual.tick_count == expected.tick_count);
    }
} // namespace

TEST_CASE("TelemetryExportManager routes samples to all registered exporters", "[TelemetryExportManager]")
{
    telemetry::TelemetryExportManager manager;

    auto first = std::make_unique<CountingExporter>();
    auto second = std::make_unique<CountingExporter>();

    auto *first_ptr = first.get();
    auto *second_ptr = second.get();

    manager.addExporter(std::move(first));
    manager.addExporter(std::move(second));

    const RuntimeMetrics metrics{123, 7};
    manager.exportSample(metrics);

    REQUIRE(first_ptr->call_count == 1);
    REQUIRE(second_ptr->call_count == 1);
    requireMetricsEq(first_ptr->last_metrics, metrics);
    requireMetricsEq(second_ptr->last_metrics, metrics);
}

TEST_CASE("TelemetryExportManager delivers to multiple in-memory exporters", "[TelemetryExportManager]")
{
    telemetry::TelemetryExportManager manager;

    auto first = std::make_unique<telemetry::InMemoryTelemetryExporter>();
    auto second = std::make_unique<telemetry::InMemoryTelemetryExporter>();

    auto *first_ptr = first.get();
    auto *second_ptr = second.get();

    manager.addExporter(std::move(first));
    manager.addExporter(std::move(second));

    const RuntimeMetrics metrics{44, 9};
    manager.exportSample(metrics);

    const auto first_snapshot = first_ptr->snapshot();
    const auto second_snapshot = second_ptr->snapshot();

    REQUIRE(first_snapshot.size() == 1);
    REQUIRE(second_snapshot.size() == 1);
    requireMetricsEq(first_snapshot[0], metrics);
    requireMetricsEq(second_snapshot[0], metrics);
}

TEST_CASE("InMemoryTelemetryExporter snapshot returns a stable copy", "[InMemoryTelemetryExporter]")
{
    telemetry::InMemoryTelemetryExporter exporter;

    const RuntimeMetrics metrics{10, 2};
    exporter.exportSample(metrics);

    auto snapshot_once = exporter.snapshot();
    REQUIRE(snapshot_once.size() == 1);
    requireMetricsEq(snapshot_once[0], metrics);

    snapshot_once[0].uptime_ms = 999;
    snapshot_once[0].tick_count = 999;

    auto snapshot_twice = exporter.snapshot();
    REQUIRE(snapshot_twice.size() == 1);
    requireMetricsEq(snapshot_twice[0], metrics);
}

TEST_CASE("TelemetryExportManager accepts exporters added after samples", "[TelemetryExportManager]")
{
    telemetry::TelemetryExportManager manager;

    const RuntimeMetrics early{1, 1};
    REQUIRE_NOTHROW(manager.exportSample(early));

    auto exporter = std::make_unique<telemetry::InMemoryTelemetryExporter>();
    auto *exporter_ptr = exporter.get();
    manager.addExporter(std::move(exporter));

    const RuntimeMetrics later{2, 2};
    manager.exportSample(later);

    const auto snapshot = exporter_ptr->snapshot();
    REQUIRE(snapshot.size() == 1);
    requireMetricsEq(snapshot[0], later);
}

TEST_CASE("TelemetryExportManager continues after an exporter throws", "[TelemetryExportManager]")
{
    if constexpr (noexcept(std::declval<telemetry::TelemetryExportManager &>().exportSample(
                      std::declval<const RuntimeMetrics &>())))
    {
        SKIP("exportSample is noexcept; throwing would terminate the process. Remove noexcept or catch internally to enable this test.");
    }

    telemetry::TelemetryExportManager manager;

    auto first = std::make_unique<CountingExporter>();
    auto throwing = std::make_unique<ThrowingExporter>();
    auto second = std::make_unique<CountingExporter>();

    auto *first_ptr = first.get();
    auto *second_ptr = second.get();

    manager.addExporter(std::move(first));
    manager.addExporter(std::move(throwing));
    manager.addExporter(std::move(second));

    const RuntimeMetrics metrics{5, 5};

    REQUIRE_NOTHROW(manager.exportSample(metrics));
    REQUIRE(first_ptr->call_count == 1);
    REQUIRE(second_ptr->call_count == 1);

    REQUIRE_NOTHROW(manager.exportSample(metrics));
    REQUIRE(first_ptr->call_count == 2);
    REQUIRE(second_ptr->call_count == 2);
}

TEST_CASE("InMemoryTelemetryExporter snapshot preserves call order", "[InMemoryTelemetryExporter]")
{
    telemetry::InMemoryTelemetryExporter exporter;

    const RuntimeMetrics first{1, 10};
    const RuntimeMetrics second{2, 20};
    const RuntimeMetrics third{3, 30};

    exporter.exportSample(first);
    exporter.exportSample(second);
    exporter.exportSample(third);

    const auto snapshot = exporter.snapshot();
    REQUIRE(snapshot.size() == 3);
    requireMetricsEq(snapshot[0], first);
    requireMetricsEq(snapshot[1], second);
    requireMetricsEq(snapshot[2], third);
}

TEST_CASE("TelemetryExportManager boundary cases", "[TelemetryExportManager]")
{
    telemetry::TelemetryExportManager manager;
    const RuntimeMetrics empty{0, 0};

    SECTION("zero exporters is a no-op")
    {
        REQUIRE_NOTHROW(manager.exportSample(empty));
    }

    SECTION("null exporter is ignored")
    {
        manager.addExporter(std::unique_ptr<TelemetryExporter>{});

        auto exporter = std::make_unique<telemetry::InMemoryTelemetryExporter>();
        auto *exporter_ptr = exporter.get();
        manager.addExporter(std::move(exporter));

        manager.exportSample(empty);

        const auto snapshot = exporter_ptr->snapshot();
        REQUIRE(snapshot.size() == 1);
        requireMetricsEq(snapshot[0], empty);
    }

    SECTION("repeated adds deliver to all exporters")
    {
        auto first = std::make_unique<CountingExporter>();
        auto second = std::make_unique<CountingExporter>();
        auto third = std::make_unique<CountingExporter>();

        auto *first_ptr = first.get();
        auto *second_ptr = second.get();
        auto *third_ptr = third.get();

        manager.addExporter(std::move(first));
        manager.addExporter(std::move(second));
        manager.addExporter(std::move(third));

        const RuntimeMetrics metrics{11, 22};
        manager.exportSample(metrics);

        REQUIRE(first_ptr->call_count == 1);
        REQUIRE(second_ptr->call_count == 1);
        REQUIRE(third_ptr->call_count == 1);
    }
}

TEST_CASE("TelemetryExportManager enqueue bounded queue behavior",
          "[TelemetryExportManager][enqueue]")
{
    constexpr std::size_t capacity = 2;
    lifecycle_test::Gate gate;
    telemetry::TelemetryExportManager manager{capacity};
    lifecycle_test::ReleaseGateOnExit release{gate};
    auto exporter = std::make_unique<CountingExporter>();
    auto *recorded = exporter.get();
    exporter->export_gate = &gate;
    manager.addExporter(std::move(exporter));

    // Admission is closed before start; lifecycle rejection is not a capacity drop.
    manager.enqueue(RuntimeMetrics{.tick_count = 999});
    REQUIRE(manager.queueSize() == 0);
    REQUIRE(manager.droppedCount() == 0);

    manager.start();
    manager.enqueue(RuntimeMetrics{.tick_count = 0});
    REQUIRE(gate.waitForArrival());
    // The first sample is in flight. Block its exporter so queue assertions cannot race drain.
    REQUIRE(manager.queueSize() == 0);
    std::vector<std::uint64_t> expected_ticks;

    SECTION("enqueue adds samples up to capacity")
    {
        manager.enqueue(RuntimeMetrics{.tick_count = 1});
        REQUIRE(manager.queueSize() == 1);
        REQUIRE(manager.droppedCount() == 0);

        manager.enqueue(RuntimeMetrics{.tick_count = 2});
        REQUIRE(manager.queueSize() == capacity);
        REQUIRE(manager.droppedCount() == 0);
        expected_ticks = {0, 1, 2};
    }

    SECTION("when capacity is exceeded, the oldest sample is dropped")
    {
        manager.enqueue(RuntimeMetrics{.tick_count = 1});
        manager.enqueue(RuntimeMetrics{.tick_count = 2});
        manager.enqueue(RuntimeMetrics{.tick_count = 3});

        REQUIRE(manager.queueSize() == capacity);
        REQUIRE(manager.droppedCount() == 1);
        expected_ticks = {0, 2, 3};
    }

    SECTION("droppedCount increments correctly")
    {
        manager.enqueue(RuntimeMetrics{.tick_count = 1});
        manager.enqueue(RuntimeMetrics{.tick_count = 2});
        manager.enqueue(RuntimeMetrics{.tick_count = 3});
        manager.enqueue(RuntimeMetrics{.tick_count = 4});
        manager.enqueue(RuntimeMetrics{.tick_count = 5});

        REQUIRE(manager.droppedCount() == 3);
        REQUIRE(manager.queueSize() == capacity);
        expected_ticks = {0, 4, 5};
    }

    SECTION("queueSize never exceeds capacity")
    {
        for (std::uint64_t i = 1; i <= 64; ++i)
        {
            manager.enqueue(RuntimeMetrics{.tick_count = i});
            CHECK(manager.queueSize() <= capacity);
        }

        REQUIRE(manager.queueSize() == capacity);
        REQUIRE(manager.droppedCount() == 64 - capacity);
        expected_ticks = {0, 63, 64};
    }

    SECTION("enqueue does not throw")
    {
        REQUIRE_NOTHROW(manager.enqueue(RuntimeMetrics{.tick_count = 1}));
        REQUIRE_NOTHROW(manager.enqueue(RuntimeMetrics{.tick_count = 2}));
        REQUIRE_NOTHROW(manager.enqueue(RuntimeMetrics{.tick_count = 3}));
        CHECK(noexcept(manager.enqueue(telemetry::TelemetrySample{})));
        expected_ticks = {0, 2, 3};
    }

    gate.release();
    manager.stop();
    REQUIRE(manager.queueSize() == 0);
    REQUIRE(recorded->seen.size() == expected_ticks.size());
    for (std::size_t i = 0; i < expected_ticks.size(); ++i)
        REQUIRE(recorded->seen[i].tick_count == expected_ticks[i]);

    const auto dropped = manager.droppedCount();
    manager.enqueue(RuntimeMetrics{.tick_count = 999});
    manager.stop();
    REQUIRE(manager.queueSize() == 0);
    REQUIRE(manager.droppedCount() == dropped);
    REQUIRE(recorded->seen.size() == expected_ticks.size());
}
