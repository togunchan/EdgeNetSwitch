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
    class TelemetryExportManager
    {
    public:
        TelemetryExportManager() = default;
        ~TelemetryExportManager() = default;

        // Non-copyable: owns exporters via unique_ptr.
        // Copying would duplicate ownership and break lifetime guarantees.
        TelemetryExportManager(const TelemetryExportManager &) = delete;
        TelemetryExportManager &operator=(const TelemetryExportManager &) = delete;

        // Ownership is transferred to the manager.
        void addExporter(std::unique_ptr<TelemetryExporter> exporter);

        // Delivers a snapshot to all registered exporters.
        void exportSample(const RuntimeMetrics &snapshot) noexcept;

    private:
        std::vector<std::unique_ptr<TelemetryExporter>> exporters_;
        std::deque<RuntimeMetrics> queue_;
        std::size_t capacity_{1024};
        std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        std::atomic<uint64_t> dropped_count_{0};
    };
} // namespace edgenetswitch::telemetry