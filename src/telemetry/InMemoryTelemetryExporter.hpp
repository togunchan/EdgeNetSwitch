#pragma once

#include "TelemetryExporter.hpp"
#include "edgenetswitch/Logger.hpp"
#include <vector>
#include <mutex>

namespace edgenetswitch::telemetry
{
    class InMemoryTelemetryExporter : public TelemetryExporter
    {
    public:
        void exportSample(const RuntimeMetrics &metrics) override;

        std::vector<RuntimeMetrics> snapshot() const;

    private:
        mutable std::mutex mutex_; // mutable: locking is not part of the logical object state
        std::vector<RuntimeMetrics> buffer_;
    };
} // namespace edgenetswitch::telemetry