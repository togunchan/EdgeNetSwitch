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

    static ControlResponse handleStatus(
        const ControlContext &ctx,
        const std::string &)
    {
        auto status = buildRuntimeStatus(ctx.telemetry, ctx.runtimeState);
        return ControlResponse{
            .success = true,
            .payload =
                "state=" + stateToString(status.state) + "\n" +
                "uptime_ms=" + std::to_string(status.metrics.uptime_ms) + "\n" +
                "tick_count=" + std::to_string(status.metrics.tick_count)};
    }

    static ControlResponse handleHealth(
        const ControlContext &ctx,
        const std::string &)
    {
        auto snap = ctx.healthMonitor.snapshot();

        return ControlResponse{
            .success = true,
            .payload =
                "alive=" + std::string(snap.alive ? "true" : "false") + "\n" +
                "timeout_ms=" + std::to_string(snap.timeout_ms)};
    }

    static ControlResponse handleMetrics(
        const ControlContext &ctx,
        const std::string &)
    {
        auto metrics = ctx.telemetry.snapshot();

        return ControlResponse{
            .success = true,
            .payload =
                "uptime_ms=" + std::to_string(metrics.uptime_ms) + "\n" +
                "tick_count=" + std::to_string(metrics.tick_count)};
    }

    static ControlResponse handleVersion(
        const ControlContext &,
        const std::string &)
    {
        return ControlResponse{
            .success = true,
            .payload =
                "version=1.2.0\n"
                "protocol=1.2\n"
                "build=debug"};
    }

    static ControlResponse handleHelp(
        const ControlContext &,
        const std::string &target)
    {
        const auto &handlers = commandTable();

        // help <command>
        if (!target.empty())
        {
            auto it = handlers.find(target);
            if (it == handlers.end())
            {
                return ControlResponse{
                    .success = false,
                    .error = "unknown command: " + target};
            }

            const auto &cmd = it->second;

            std::string payload;
            payload += "command=" + cmd.name + "\n";
            payload += "description=" + cmd.description + "\n";
            payload += "fields:\n";
            for (const auto &f : cmd.fields)
            {
                payload += "  - " + f + "\n";
            }

            return ControlResponse{.success = true, .payload = payload};
        }

        // help (general)
        std::string payload = "commands:\n";
        for (const auto &[name, desc] : handlers)
        {
            payload += "  " + name + " - " + desc.description + "\n";
        }

        return ControlResponse{.success = true, .payload = payload};
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
        std::string command = req.command;
        std::string arg;

        auto sep = command.find(':');
        if (sep != std::string::npos)
        {
            arg = command.substr(sep + 1);
            command = command.substr(0, sep);
        }

        const auto &table = commandTable();
        auto it = table.find(command);
        if (it == table.end())
        {
            return ControlResponse{
                .success = false,
                .error = "unknown command"};
        }
        return it->second.handler(ctx, arg);
    }
} // namespace edgenetswitch::control
