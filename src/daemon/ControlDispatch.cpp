#pragma once

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
        control::ControlResponse resp{
            .success = true,
            .payload =
                "state=" + stateToString(status.state) + "\n" +
                "uptime_ms=" + std::to_string(status.metrics.uptime_ms) + "\n" +
                "tick_count=" + std::to_string(status.metrics.tick_count)};

        return resp;
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
            {"status", handleStatus}};

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