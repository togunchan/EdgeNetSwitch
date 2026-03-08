#pragma once

#include <memory>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "telemetry/StdoutTelemetryExporter.hpp"
#include <thread>

namespace edgenetswitch::telemetry
{
    using TelemetrySample = RuntimeMetrics;

    class TelemetryExportManager
    {
    public:
        explicit TelemetryExportManager(std::size_t capacity = 512) noexcept : capacity_(capacity) {}
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

        void start();

        void stop();

        std::size_t queueSize() const noexcept;

        uint64_t droppedCount() const noexcept;

    private:
        std::vector<std::unique_ptr<TelemetryExporter>> exporters_;
        std::deque<TelemetrySample> queue_;
        std::size_t capacity_;
        mutable std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        std::atomic<uint64_t> dropped_count_{0};
        std::thread export_thread_;
        std::atomic<bool> running_{false};

        void exportLoop();
    };
} // namespace edgenetswitch::telemetry
