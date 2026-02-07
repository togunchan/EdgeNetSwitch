#pragma once

#include <memory>
#include <vector>

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
    };
} // namespace edgenetswitch::telemetry