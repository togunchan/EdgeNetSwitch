#include <string>
#include <unordered_map>

#include "ControlContext.hpp"
#include "ControlDispatch.hpp"
#include "RuntimeStatusBuilder.hpp"
#include "edgenetswitch/RuntimeStatus.hpp"

namespace edgenetswitch::control
{
    using CommandTable = std::unordered_map<std::string, CommandDescriptor>;

    static const CommandTable &commandTable();

    static ControlResponse handleStatus(const ControlContext &ctx)
    {
        auto status = buildRuntimeStatus(ctx.telemetry, ctx.runtimeState);
        return ControlResponse{
            .success = true,
            .payload =
                "state=" + stateToString(status.state) + "\n" +
                "uptime_ms=" + std::to_string(status.metrics.uptime_ms) + "\n" +
                "tick_count=" + std::to_string(status.metrics.tick_count)};
    }

    static ControlResponse handleHealth(const ControlContext &ctx)
    {
        auto snap = ctx.healthMotitor.snapshot();

        return ControlResponse{
            .success = true,
            .payload =
                "alive=" + std::string(snap.alive ? "true" : "false") + "\n" +
                "timeout_ms=" + std::to_string(snap.timeout_ms)};
    }

    static ControlResponse handleMetrics(const ControlContext &ctx)
    {
        auto metrics = ctx.telemetry.snapshot();

        return ControlResponse{
            .success = true,
            .payload =
                "uptime_ms=" + std::to_string(metrics.uptime_ms) + "\n" +
                "tick_count=" + std::to_string(metrics.tick_count)};
    }

    static ControlResponse handleVersion(const ControlContext &)
    {
        return ControlResponse{
            .success = true,
            .payload =
                "version=1.2.0\n"
                "protocol=1.2\n"
                "build=debug"};
    }

    static ControlResponse handleHelp(const ControlContext &)
    {
        std::string out = "commands:\n";

        for (const auto &[_, desc] : commandTable())
        {
            out += "  " + desc.name + " - " + desc.description + "\n";
        }

        return ControlResponse{
            .success = true,
            .payload = out};
    }

    static const CommandTable &commandTable()
    {
        // Static dispatch table:
        // - Initialized once on first call (not per request)
        // - Lives for the lifetime of the program
        // - Scoped to this function to avoid global exposure
        // - Maps control commands to their corresponding handlers
        static const CommandTable table = {
            {"status",
             {.name = "status",
              .description = "runtime state and core metrics",
              .fields = {"state", "uptime_ms", "tick_count"},
              .handler = handleStatus}},

            {"health",
             {.name = "health",
              .description = "liveness monitoring",
              .fields = {"alive", "timeout_ms"},
              .handler = handleHealth}},

            {"metrics",
             {.name = "metrics",
              .description = "telemetry snapshot",
              .fields = {"uptime_ms", "tick_count"},
              .handler = handleMetrics}},

            {"version",
             {.name = "version",
              .description = "daemon and protocol identification",
              .fields = {"version", "protocol", "build"},
              .handler = handleVersion}},

            {"help",
             {.name = "help",
              .description = "command listing",
              .fields = {"commands"},
              .handler = handleHelp}}};
        return table;
    }

    ControlResponse dispatchControlRequest(
        const ControlRequest &req,
        const ControlContext &ctx)
    {
        const auto &table = commandTable();
        auto it = table.find(req.command);
        if (it == table.end())
        {
            return ControlResponse{
                .success = false,
                .error = "unknown command"};
        }
        return it->second.handler(ctx);
    }
} // namespace edgenetswitch::control
