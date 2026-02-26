#include <catch2/catch_test_macros.hpp>

#include "telemetry/TelemetryExportManager.hpp"
#include "telemetry/InMemoryTelemetryExporter.hpp"
#include "telemetry/TelemetryExporter.hpp"
#include "edgenetswitch/RuntimeMetrics.hpp"

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
        }

        int call_count = 0;
        RuntimeMetrics last_metrics{0, 0};
        std::vector<RuntimeMetrics> seen;
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
