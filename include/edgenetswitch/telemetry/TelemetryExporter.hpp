#pragma once

#include "edgenetswitch/runtime/RuntimeMetrics.hpp"

namespace edgenetswitch
{
    struct TelemetryExporter
    {
        virtual ~TelemetryExporter() = default;
        virtual void exportSample(const RuntimeMetrics &sample) = 0;
    };
} // namespace edgenetswitch