#pragma once

#include "TelemetryExporter.hpp"
#include "edgenetswitch/Logger.hpp"

namespace edgenetswitch
{
    class StdoutTelemetryExporter final : public TelemetryExporter
    {
    public:
        void exportSample(const RuntimeMetrics &sample) override
        {
            Logger::info(
                "telemetry_export: uptime_ms=" + std::to_string(sample.uptime_ms) +
                " tick_count=" + std::to_string(sample.tick_count));
        }
    };
} // namespace edgenetswitch