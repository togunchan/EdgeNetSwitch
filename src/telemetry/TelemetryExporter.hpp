#pragma once

#include "edgenetswitch/telemetry/Telemetry.hpp"

namespace edgenetswitch
{
    struct TelemetryExporter
    {
        virtual ~TelemetryExporter() = default;
        virtual void exportSample(const RuntimeMetrics &sample) = 0;
    };
} // namespace edgenetswitch