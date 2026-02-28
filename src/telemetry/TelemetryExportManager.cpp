#include "TelemetryExportManager.hpp"
#include "edgenetswitch/Logger.hpp"

namespace edgenetswitch::telemetry
{
    void TelemetryExportManager::addExporter(std::unique_ptr<TelemetryExporter> exporter)
    {
        if (!exporter)
        {
            Logger::warn("telemetry_export: null exporter ignored");
            return;
        }
        exporters_.push_back(std::move(exporter));
    }

    void TelemetryExportManager::exportSample(const RuntimeMetrics &snapshot) noexcept
    {
        for (auto &exporter : exporters_)
        {
            try
            {
                exporter->exportSample(snapshot);
            }
            catch (const std::exception &e)
            {
                Logger::error("Telemetry exporter failed: " + std::string(e.what()));
            }
            catch (...)
            {
                Logger::error("Telemetry exporter failed with unknown exception");
            }
        }
    }

    void TelemetryExportManager::enqueue(TelemetrySample sample) noexcept
    {
        bool notify = false;

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            if (capacity_ == 0)
            {
                // this is an edge-case. 0 is used for disabled-queue which means drop everything
                ++dropped_count_;
                return;
            }

            if (queue_.size() >= capacity_)
            {
                queue_.pop_front(); // drop the oldest one
                ++dropped_count_;
            }

            queue_.push_back(std::move(sample));
            notify = true;
        }

        if (notify)
        {
            queue_cv_.notify_one();
        }
    }

    std::size_t TelemetryExportManager::queueSizeForTest() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.size();
    }

    uint64_t TelemetryExportManager::droppedCountForTest() const
    {
        return dropped_count_.load();
    }

} // namespace edgenetswitch::telemetry
