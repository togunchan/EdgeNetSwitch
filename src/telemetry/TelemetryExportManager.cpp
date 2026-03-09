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

            auto &back = queue_.back();
            back.telemetry_queue_size = queue_.size();
            back.telemetry_dropped_samples = dropped_count_.load(std::memory_order_relaxed);
            notify = true;
        }

        if (notify)
        {
            queue_cv_.notify_one();
        }
    }

    void TelemetryExportManager::start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true))
            return; // already running

        export_thread_ = std::thread([this]()
                                     { exportLoop(); });
    }

    void TelemetryExportManager::stop()
    {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false))
            return; // not running

        queue_cv_.notify_all();

        if (export_thread_.joinable())
            export_thread_.join();
    }

    void TelemetryExportManager::exportLoop()
    {
        while (true)
        {
            TelemetrySample sample;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                queue_cv_.wait(lock, [this]()
                               { return !queue_.empty() || !running_.load(std::memory_order_relaxed); });

                if (queue_.empty() && !running_.load(std::memory_order_relaxed))
                {
                    return; // graceful exit
                }

                sample = std::move(queue_.front());
                queue_.pop_front();
            }

            exportSample(sample);

            // std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    std::size_t TelemetryExportManager::queueSize() const noexcept
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.size();
    }

    uint64_t TelemetryExportManager::droppedCount() const noexcept
    {
        return dropped_count_.load(std::memory_order_relaxed);
    }
} // namespace edgenetswitch::telemetry
