#include "TelemetryExportManager.hpp"
#include "edgenetswitch/Logger.hpp"

namespace edgenetswitch::telemetry
{
    TelemetryExportManager::TelemetryExportManager() = default;

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
        for (const auto &exporter : exporters_)
        {
            exporter->exportSample(snapshot);
        }
    }

} // namespace edgenetswitch