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

} // namespace edgenetswitch
