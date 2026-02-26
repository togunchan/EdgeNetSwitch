#pragma once

#include "edgenetswitch/Telemetry.hpp"

namespace edgenetswitch
{
    struct TelemetryExporter
    {
        virtual ~TelemetryExporter() = default;
        virtual void exportSample(const RuntimeMetrics &sample) = 0;
    };
} // namespace edgenetswitch