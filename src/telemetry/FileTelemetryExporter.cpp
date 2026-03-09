#include "telemetry/FileTelemetryExporter.hpp"

#include "edgenetswitch/Logger.hpp"
#include <utility>

namespace edgenetswitch::telemetry
{
    FileTelemetryExporter::FileTelemetryExporter(std::string path)
        : filePath_(std::move(path)),
          out_(filePath_, std::ios::out | std::ios::app)
    {
        if (!out_.is_open())
        {
            Logger::error("FileTelemetryExporter: failed to open file: " + filePath_);
        }
    }

    FileTelemetryExporter::~FileTelemetryExporter()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (out_.is_open())
        {
            out_.flush();
            out_.close();
        }
    }

    void FileTelemetryExporter::exportSample(const RuntimeMetrics &sample)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!out_.is_open())
        {
            return;
        }

        out_ << "uptime_ms=" << sample.uptime_ms
             << ",tick_count=" << sample.tick_count
             << ",queue_size=" << sample.telemetry_queue_size
             << ",dropped=" << sample.telemetry_dropped_samples
             << '\n';

        if (!out_)
        {
            Logger::error("FileTelemetryExporter: failed to write sample to file: " + filePath_);
            out_.clear();
        }
    }
} // namespace edgenetswitch::telemetry
