#include <unordered_map>
#include <functional>

#include "edgenetswitch/control/ControlProtocol.hpp"
#include "ControlContext.hpp"
#include "RuntimeStatusBuilder.hpp"
#include "edgenetswitch/RuntimeStatus.hpp"

namespace edgenetswitch::control
{
    static ControlResponse handleStatus(const ControlContext &ctx)
    {
        auto status = buildRuntimeStatus(ctx.telemetry, ctx.runtimeState);
        return control::ControlResponse{
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
        return ControlResponse{
            .success = true,
            .payload =
                "commands:\n"
                "  status   - runtime state and core metrics\n"
                "  health   - liveness monitoring\n"
                "  metrics  - telemetry snapshot\n"
                "  version  - daemon and protocol identification\n"};
    }

    ControlResponse dispatchControlRequest(
        const ControlRequest &req,
        const ControlContext &ctx)
    {
        using Handler = std::function<ControlResponse(const ControlContext &)>;

        // Static dispatch table:
        // - Initialized once on first call (not per request)
        // - Lives for the lifetime of the program
        // - Scoped to this function to avoid global exposure
        // - Maps control commands to their corresponding handlers
        static const std::unordered_map<std::string, Handler> handlers = {
            {"status", handleStatus},
            {"health", handleHealth},
            {"metrics", handleMetrics},
            {"version", handleVersion},
            {"help", handleHelp}};

        auto it = handlers.find(req.command);
        if (it == handlers.end())
        {
            return ControlResponse{
                .success = false,
                .error = "unknown command"};
        }

        return it->second(ctx);
    }
} // namespace edgenetswitch::control