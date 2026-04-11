#include <atomic>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "ControlContext.hpp"
#include "ControlDispatch.hpp"
#include "runtime/SnapshotPublisher.hpp"
#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/runtime/RuntimeStatus.hpp"
#include "JsonResponse.hpp"

namespace edgenetswitch::control
{
    using CommandTable = std::unordered_map<std::string, CommandDescriptor>;
    constexpr const char *VERSION = "1.2.0";

    static const CommandTable &commandTable();

    static std::shared_ptr<const RuntimeStatus> loadSnapshot(const ControlContext &ctx)
    {
        if (!ctx.publisher)
        {
            return {};
        }

        return ctx.publisher->load();
    }

    static ControlResponse handleStatus(
        const ControlContext &ctx,
        const std::string &arg)
    {
        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        auto snap = loadSnapshot(ctx);
        if (!snap)
        {
            return makeJsonError(error::InternalError, "runtime snapshot not available");
        }

        if (arg == "json")
        {
            nlohmann::json j;
            j["state"] = stateToString(snap->state);
            j["uptime_ms"] = snap->metrics.uptime_ms;
            j["tick_count"] = snap->metrics.tick_count;
            j["snapshot_version"] = snap->snapshot_version;
            j["snapshot_timestamp_ms"] = snap->snapshot_timestamp_ms;

            return makeJsonSuccess(j);
        }

        return ControlResponse{
            .success = true,
            .payload =
                "state=" + stateToString(snap->state) + "\n" +
                "uptime_ms=" + std::to_string(snap->metrics.uptime_ms) + "\n" +
                "tick_count=" + std::to_string(snap->metrics.tick_count) + "\n" +
                "snapshot_version=" + std::to_string(snap->snapshot_version) + "\n" +
                "snapshot_timestamp_ms=" + std::to_string(snap->snapshot_timestamp_ms)};
    }

    static ControlResponse handleHealth(
        const ControlContext &ctx,
        const std::string &arg)
    {
        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        auto snap = loadSnapshot(ctx);
        if (!snap)
        {
            return makeJsonError(error::InternalError, "runtime snapshot not available");
        }

        if (arg == "json")
        {
            nlohmann::json j;
            j["alive"] = snap->health.is_alive;
            j["silence_ms"] = snap->health.silence_duration_ms;
            j["last_heartbeat_ms"] = snap->health.last_heartbeat_ms;

            return makeJsonSuccess(j);
        }

        return ControlResponse{
            .success = true,
            .payload =
                "alive=" + std::string(snap->health.is_alive ? "true" : "false") + "\n" +
                "silence_ms=" + std::to_string(snap->health.silence_duration_ms) + "\n" +
                "last_heartbeat_ms=" + std::to_string(snap->health.last_heartbeat_ms)};
    }

    static ControlResponse handleMetrics(
        const ControlContext &ctx,
        const std::string &arg)
    {
        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        auto snap = loadSnapshot(ctx);
        if (!snap)
        {
            return makeJsonError(error::InternalError, "runtime snapshot not available");
        }

        if (arg == "json")
        {
            nlohmann::json j;
            j["uptime_ms"] = snap->metrics.uptime_ms;
            j["tick_count"] = snap->metrics.tick_count;

            return makeJsonSuccess(j);
        }

        return ControlResponse{
            .success = true,
            .payload =
                "uptime_ms=" + std::to_string(snap->metrics.uptime_ms) + "\n" +
                "tick_count=" + std::to_string(snap->metrics.tick_count)};
    }

    static ControlResponse handleVersion(
        const ControlContext &,
        const std::string &arg)
    {
        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        if (arg == "json")
        {
            nlohmann::json j;
            j["version"] = VERSION;
            j["protocol"] = "1.2";
            j["build"] = "debug";

            return makeJsonSuccess(j);
        }

        return ControlResponse{
            .success = true,
            .payload =
                std::string("version=") + VERSION + "\n" +
                "protocol=1.2\n" +
                "build=debug"};
    }

    static ControlResponse handleHelp(
        const ControlContext &,
        const std::string &target)
    {
        const auto &handlers = commandTable();

        if (target == "json")
        {
            nlohmann::json j;

            for (const auto &[name, desc] : handlers)
            {
                nlohmann::json cmd;
                cmd["description"] = desc.description;
                cmd["fields"] = desc.fields;

                j["commands"][name] = cmd;
            }

            return makeJsonSuccess(j);
        }

        // help <command>
        if (!target.empty())
        {
            auto it = handlers.find(target);
            if (it == handlers.end())
            {
                return makeJsonError(error::UnknownCommand, "unknown command: " + target);
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

    static std::string dropReasonToString(PacketDropReason reason)
    {
        switch (reason)
        {
        case PacketDropReason::ParseError:
            return "parse_error";
        case PacketDropReason::ValidationError:
            return "validation_error";
        case PacketDropReason::QueueOverflow:
            return "queue_overflow";
        case PacketDropReason::RateLimited:
            return "rate_limited";
        default:
            return "unknown";
        }
    }

    static ControlResponse handlePacketStats(
        const ControlContext &ctx,
        const std::string &arg)
    {
        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        auto snap = loadSnapshot(ctx);
        if (!snap)
        {
            return makeJsonError(error::InternalError, "runtime snapshot not available");
        }

        if (arg == "json")
        {
            nlohmann::json j;
            j["rx_packets"] = snap->packet.rx_packets;
            j["rx_bytes"] = snap->packet.rx_bytes;

            nlohmann::json drops_json;

            for (const auto &[reason, count] : snap->packet.drops_by_reason)
            {
                drops_json[dropReasonToString(reason)] = count;
            }

            j["drops"] = drops_json;
            j["rx_packets_per_sec"] = snap->packet.rx_packets_per_sec;
            j["rx_bytes_per_sec"] = snap->packet.rx_bytes_per_sec;
            j["rx_packets_per_sec_raw"] = snap->packet.rx_packets_per_sec_raw;
            j["rx_bytes_per_sec_raw"] = snap->packet.rx_bytes_per_sec_raw;

            return makeJsonSuccess(j);
        }

        std::string payload;

        payload += "rx_packets=" + std::to_string(snap->packet.rx_packets) + "\n";
        payload += "rx_bytes=" + std::to_string(snap->packet.rx_bytes) + "\n";

        std::uint64_t total_drops = 0;

        for (const auto &[reason, count] : snap->packet.drops_by_reason)
        {
            payload += "drops_" + dropReasonToString(reason) + "=" + std::to_string(count) + "\n";
            total_drops += count;
        }

        payload += "drops_total=" + std::to_string(total_drops) + "\n";

        payload += "rx_packets_per_sec=" + std::to_string(snap->packet.rx_packets_per_sec) + "\n";
        payload += "rx_bytes_per_sec=" + std::to_string(snap->packet.rx_bytes_per_sec) + "\n";
        payload += "rx_packets_per_sec_raw=" + std::to_string(snap->packet.rx_packets_per_sec_raw) + "\n";
        payload += "rx_bytes_per_sec_raw=" + std::to_string(snap->packet.rx_bytes_per_sec_raw);

        return ControlResponse{
            .success = true,
            .payload = std::move(payload)};
    }

    static ControlResponse handleConfig(const ControlContext &ctx, const std::string &arg)
    {
        if (!ctx.config)
            return makeJsonError(error::InternalError, "config is not available");

        const auto &cfg = *ctx.config;

        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        if (arg == "json")
        {
            nlohmann::json j;
            j["log"]["level"] = cfg.log.level;
            j["log"]["file"] = cfg.log.file;
            j["daemon"]["tick_ms"] = cfg.daemon.tick_ms;
            j["udp"]["enabled"] = cfg.udp.enabled;
            j["udp"]["port"] = cfg.udp.port;
            j["rate"]["alpha"] = cfg.rate.alpha;
            j["rate"]["window_ms"] = cfg.rate.window_ms;

            return makeJsonSuccess(j);
        }

        return ControlResponse{
            .success = true,
            .payload =
                "log.level=" + cfg.log.level + "\n" +
                "log.file=" + cfg.log.file + "\n" +
                "daemon.tick_ms=" + std::to_string(cfg.daemon.tick_ms) + "\n" +
                "udp.enabled=" + std::string(cfg.udp.enabled ? "true" : "false") + "\n" +
                "udp.port=" + std::to_string(cfg.udp.port) + "\n" +
                "rate.alpha=" + std::to_string(cfg.rate.alpha) + "\n" +
                "rate.window_ms=" + std::to_string(cfg.rate.window_ms)};
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
              .handler = handleHelp}},

            {"packet-stats",
             {.name = "packet-stats",
              .description = "packet pipeline statistics",
              .fields = {"rx_packets", "rx_bytes", "drops"},
              .handler = handlePacketStats}},

            {"show-config",
             {.name = "show-config",
              .description = "current runtime configuration",
              .fields = {"log", "daemon", "udp", "rate"},
              .handler = handleConfig}},

        };
        return table;
    }

    static ControlResponse dispatchV12(
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
            return makeJsonError(error::UnknownCommand, "unknown command: " + command);
        }
        return it->second.handler(ctx, arg);
    }

    ControlResponse dispatchControlRequest(const ControlRequest &req, const ControlContext &ctx)
    {
        if (req.version.empty() || req.command.empty())
        {
            return makeJsonError(error::InvalidRequest, "missing version or command");
        }

        if (!isWellFormedVersion(req.version))
        {
            return makeJsonError(error::InvalidVersionFormat,
                                 "invalid protocol version format: " + req.version);
        }

        if (!isValidProtocolVersion(req.version))
        {
            return makeJsonError(error::UnsupportedVersion,
                                 "unsupported protocol version: " + req.version);
        }

        if (req.version == "1.2")
        {
            return dispatchV12(req, ctx);
        }

        return makeJsonError(error::UnsupportedVersion,
                             "unsupported protocol version: " + req.version);
    }
} // namespace edgenetswitch::control
