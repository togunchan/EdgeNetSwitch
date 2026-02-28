#pragma once

#include <memory>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "telemetry/StdoutTelemetryExporter.hpp"

namespace edgenetswitch::telemetry
{
    using TelemetrySample = RuntimeMetrics;

    class TelemetryExportManager
    {
    public:
        explicit TelemetryExportManager(std::size_t capacity = 1024) noexcept : capacity_(capacity) {}
        ~TelemetryExportManager() = default;

        // Non-copyable: owns exporters via unique_ptr.
        // Copying would duplicate ownership and break lifetime guarantees.
        TelemetryExportManager(const TelemetryExportManager &) = delete;
        TelemetryExportManager &operator=(const TelemetryExportManager &) = delete;

        // Ownership is transferred to the manager.
        void addExporter(std::unique_ptr<TelemetryExporter> exporter);

        // Delivers a snapshot to all registered exporters.
        void exportSample(const RuntimeMetrics &snapshot) noexcept;

        void enqueue(TelemetrySample sample) noexcept;

        std::size_t queueSizeForTest() const;
        uint64_t droppedCountForTest() const;

    private:
        std::vector<std::unique_ptr<TelemetryExporter>> exporters_;
        std::deque<TelemetrySample> queue_;
        std::size_t capacity_;
        mutable std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        std::atomic<uint64_t> dropped_count_{0};
    };
} // namespace edgenetswitch::telemetry
