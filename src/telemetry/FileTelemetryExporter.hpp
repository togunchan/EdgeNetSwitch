#pragma once

#include "TelemetryExporter.hpp"

#include <fstream>
#include <mutex>
#include <string>

namespace edgenetswitch::telemetry
{
    class FileTelemetryExporter : public TelemetryExporter
    {
    public:
        explicit FileTelemetryExporter(std::string path);

        void exportSample(const RuntimeMetrics &sample) override;

    private:
        std::string filePath_;
        std::ofstream out_;
        mutable std::mutex mutex_;
    };
} // namespace edgenetswitch::telemetry
