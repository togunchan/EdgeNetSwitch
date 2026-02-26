#include "InMemoryTelemetryExporter.hpp"

namespace edgenetswitch::telemetry
{
    void InMemoryTelemetryExporter::exportSample(const RuntimeMetrics &metrics)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(metrics);
    }

    std::vector<RuntimeMetrics> InMemoryTelemetryExporter::snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_;
    }
} // namespace edgenetswitch::telemetry