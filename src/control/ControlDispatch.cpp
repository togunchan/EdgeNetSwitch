#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "edgenetswitch/core/TimeUtils.hpp"

#include "ControlDispatch.hpp"
#include "JsonResponse.hpp"
#include "edgenetswitch/control/ControlContext.hpp"
#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/runtime/RuntimeStatus.hpp"
#include "edgenetswitch/system/fd/FdState.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"
#include "runtime/SnapshotPublisher.hpp"

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

    static ControlResponse handleStatus(const ControlContext &ctx, const std::string &arg)
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
            .payload = "state=" + stateToString(snap->state) + "\n" +
                       "uptime_ms=" + std::to_string(snap->metrics.uptime_ms) + "\n" +
                       "tick_count=" + std::to_string(snap->metrics.tick_count) + "\n" +
                       "snapshot_version=" + std::to_string(snap->snapshot_version) + "\n" +
                       "snapshot_timestamp_ms=" + std::to_string(snap->snapshot_timestamp_ms)};
    }

    static ControlResponse handleHealth(const ControlContext &ctx, const std::string &arg)
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
            .payload = "alive=" + std::string(snap->health.is_alive ? "true" : "false") + "\n" +
                       "silence_ms=" + std::to_string(snap->health.silence_duration_ms) + "\n" +
                       "last_heartbeat_ms=" + std::to_string(snap->health.last_heartbeat_ms)};
    }

    static ControlResponse handleMetrics(const ControlContext &ctx, const std::string &arg)
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

        return ControlResponse{.success = true,
                               .payload = "uptime_ms=" + std::to_string(snap->metrics.uptime_ms) +
                                          "\n" +
                                          "tick_count=" + std::to_string(snap->metrics.tick_count)};
    }

    static ControlResponse handleVersion(const ControlContext &, const std::string &arg)
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

        return ControlResponse{.success = true,
                               .payload = std::string("version=") + VERSION + "\n" +
                                          "protocol=1.2\n" + "build=debug"};
    }

    static ControlResponse handleHelp(const ControlContext &, const std::string &target)
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

    static ControlResponse handlePacketStats(const ControlContext &ctx, const std::string &arg)
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
            j["ingress_packets"] = snap->packet.ingress_packets;
            j["processed_packets"] = snap->packet.processed_packets;
            j["processing_gap"] = snap->packet.processing_gap;

            nlohmann::json drops_json = nlohmann::json::object();

            for (const auto &[reason, count] : snap->packet.drops_by_reason)
            {
                drops_json[dropReasonToString(reason)] = count;
            }

            j["drops"] = drops_json;
            j["rx_packets_per_sec"] = snap->packet.rx_packets_per_sec;
            j["rx_bytes_per_sec"] = snap->packet.rx_bytes_per_sec;
            j["rx_packets_per_sec_raw"] = snap->packet.rx_packets_per_sec_raw;
            j["rx_bytes_per_sec_raw"] = snap->packet.rx_bytes_per_sec_raw;
            j["terminal_events"] = snap->packet.terminal_events;
            j["duplicate_events"] = snap->packet.duplicate_events;
            j["pending_terminal_events"] = snap->packet.pending_terminal_events;
            j["average_processing_latency_ns"] = snap->packet.average_processing_latency_ns;
            j["max_processing_latency_ns"] = snap->packet.max_processing_latency_ns;
            j["latency_samples"] = snap->packet.latency_samples;
            j["udp_drain_completions"] = snap->packet.udp_drain_completions;

            return makeJsonSuccess(j);
        }

        std::string payload;

        payload += "rx_packets=" + std::to_string(snap->packet.rx_packets) + "\n";
        payload += "rx_bytes=" + std::to_string(snap->packet.rx_bytes) + "\n";
        payload += "ingress_packets=" + std::to_string(snap->packet.ingress_packets) + "\n";
        payload += "processed_packets=" + std::to_string(snap->packet.processed_packets) + "\n";
        payload += "processing_gap=" + std::to_string(snap->packet.processing_gap) + "\n";

        std::uint64_t total_drops = 0;

        for (const auto &[reason, count] : snap->packet.drops_by_reason)
        {
            payload += "drops_" + dropReasonToString(reason) + "=" + std::to_string(count) + "\n";
            total_drops += count;
        }

        payload += "drops_total=" + std::to_string(total_drops) + "\n";

        payload += "rx_packets_per_sec=" + std::to_string(snap->packet.rx_packets_per_sec) + "\n";
        payload += "rx_bytes_per_sec=" + std::to_string(snap->packet.rx_bytes_per_sec) + "\n";
        payload +=
            "rx_packets_per_sec_raw=" + std::to_string(snap->packet.rx_packets_per_sec_raw) + "\n";
        payload += "rx_bytes_per_sec_raw=" + std::to_string(snap->packet.rx_bytes_per_sec_raw);
        payload += "terminal_events=" + std::to_string(snap->packet.terminal_events);
        payload += "duplicate_events=" + std::to_string(snap->packet.duplicate_events);
        payload +=
            "pending_terminal_events=" + std::to_string(snap->packet.pending_terminal_events);
        payload += "average_processing_latency_ns=" +
                   std::to_string(snap->packet.average_processing_latency_ns) + "\n";
        payload +=
            "max_processing_latency_ns=" + std::to_string(snap->packet.max_processing_latency_ns) +
            "\n";
        payload +=
            "udp_drain_completions=" + std::to_string(snap->packet.udp_drain_completions) + "\n";

        return ControlResponse{.success = true, .payload = std::move(payload)};
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
            .payload = "log.level=" + cfg.log.level + "\n" + "log.file=" + cfg.log.file + "\n" +
                       "daemon.tick_ms=" + std::to_string(cfg.daemon.tick_ms) + "\n" +
                       "udp.enabled=" + std::string(cfg.udp.enabled ? "true" : "false") + "\n" +
                       "udp.port=" + std::to_string(cfg.udp.port) + "\n" +
                       "rate.alpha=" + std::to_string(cfg.rate.alpha) + "\n" +
                       "rate.window_ms=" + std::to_string(cfg.rate.window_ms)};
    }

    static void publishSyntheticPacket(MessagingBus &bus, std::uint64_t id,
                                       const std::string &payload, const char *source_mac_str,
                                       const char *destination_mac_str, std::uint32_t ingress_port)
    {
        auto source_mac = MacAddress::fromString(source_mac_str);
        auto destination_mac = MacAddress::fromString(destination_mac_str);

        if (!source_mac || !destination_mac)
        {
            return;
        }

        Packet packet{};
        packet.id = id;
        packet.lifecycle_id = id;
        packet.timestamp_ms = nowMs();
        packet.payload = payload;
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.valid = true;

        packet.source_mac = *source_mac;
        packet.destination_mac = *destination_mac;
        packet.ingress_port = ingress_port;

        Message msg{};
        msg.type = MessageType::PacketRx;
        msg.timestamp_ms = packet.timestamp_ms;
        msg.payload = packet;

        bus.publish(std::move(msg));
    }

    static ControlResponse handleSendPacket(const ControlContext &ctx, const std::string &arg)
    {
        if (!ctx.bus)
        {
            return makeJsonError(error::InternalError, "messaging bus unavailable");
        }

        if (arg == "broadcast")
        {
            publishSyntheticPacket(*ctx.bus, 999, "control-plane-broadcast", "00:11:22:33:44:55",
                                   "ff:ff:ff:ff:ff:ff", 2);

            nlohmann::json j;
            j["result"] = "packet injected";
            j["mode"] = arg;
            j["lifecycle_id"] = 999;

            return makeJsonSuccess(j);
        }
        else if (arg == "learn")
        {
            publishSyntheticPacket(*ctx.bus, 1000, "mac-learning", "AA:AA:AA:AA:AA:AA",
                                   "FF:FF:FF:FF:FF:FF", 1);

            publishSyntheticPacket(*ctx.bus, 1001, "known-unicast", "BB:BB:BB:BB:BB:BB",
                                   "AA:AA:AA:AA:AA:AA", 2);

            nlohmann::json j;
            j["result"] = "mac learning sequence injected";

            return makeJsonSuccess(j);
        }
        else if (arg == "topology-demo")
        {
            publishSyntheticPacket(*ctx.bus, 2000, "broadcast-discovery", "AA:AA:AA:AA:AA:AA",
                                   "FF:FF:FF:FF:FF:FF", 1);

            publishSyntheticPacket(*ctx.bus, 2001, "known-unicast-bb", "BB:BB:BB:BB:BB:BB",
                                   "AA:AA:AA:AA:AA:AA", 2);

            publishSyntheticPacket(*ctx.bus, 2002, "known-unicast-cc", "CC:CC:CC:CC:CC:CC",
                                   "AA:AA:AA:AA:AA:AA", 3);

            publishSyntheticPacket(*ctx.bus, 2003, "unknown-unicast", "DD:DD:DD:DD:DD:DD",
                                   "EE:EE:EE:EE:EE:EE", 4);

            publishSyntheticPacket(*ctx.bus, 2004, "known-unicast-ee", "EE:EE:EE:EE:EE:EE",
                                   "CC:CC:CC:CC:CC:CC", 5);

            nlohmann::json j;
            j["result"] = "topology demo injected";
            j["ports"] = {1, 2, 3, 4, 5};
            j["packet_count"] = 5;

            return makeJsonSuccess(j);
        }

        return makeJsonError(error::InvalidRequest, "unsupported packet mode: " + arg);
    }

    static ControlResponse handleShow(const ControlContext &ctx, const std::string &arg)
    {
        if (!ctx.forwarding_engine)
        {
            return makeJsonError(error::InternalError, "forwarding engine unavailable");
        }

        if (arg == "mac-table")
        {
            const auto entries = ctx.forwarding_engine->macTable().snapshot();

            std::string payload;

            payload += "mac_table_size=" + std::to_string(entries.size()) + "\n";

            for (const auto &entry : entries)
            {
                payload += entry.mac.toString() + " port=" + std::to_string(entry.port_id) +
                           " last_seen=" + std::to_string(entry.last_seen_tick) + "\n";
            }

            return ControlResponse{.success = true, .payload = std::move(payload)};
        }

        return makeJsonError(error::InvalidRequest, "unsupported packet mode: " + arg);
    }

    static std::string fdStateToString(FdState state)
    {
        switch (state)
        {
        case FdState::Active:
            return "active";

        case FdState::Closed:
            return "closed";

        case FdState::Released:
            return "released";

        default:
            return "invalid";
        }
    }

    static std::string fdTypeToString(FdType type)
    {
        switch (type)
        {
        case FdType::UdpSocket:
            return "udp_socket";

        case FdType::UnixSocket:
            return "unix_socket";

        default:
            return "unknown";
        }
    }

    static ControlResponse handleFdStatus(const ControlContext &ctx, const std::string &arg)
    {
        if (!ctx.fd_registry)
        {
            return makeJsonError(error::InternalError, "fd registry unavailable");
        }

        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        const auto records = ctx.fd_registry->snapshot();

        if (arg == "json")
        {
            nlohmann::json fds = nlohmann::json::array();

            for (const auto &record : records)
            {
                nlohmann::json fd;

                fd["fd"] = record.fd;
                fd["state"] = fdStateToString(record.state);
                fd["type"] = fdTypeToString(record.fd_type);

                fds.push_back(std::move(fd));
            }

            nlohmann::json j;
            j["fds"] = std::move(fds);

            return makeJsonSuccess(j);
        }

        std::string payload;

        payload += "fd_count=" + std::to_string(records.size()) + "\n";

        for (const auto &record : records)
        {
            payload += "fd=" + std::to_string(record.fd) +
                       " state=" + fdStateToString(record.state) +
                       " type=" + fdTypeToString(record.fd_type) + "\n";
        }

        return ControlResponse{.success = true, .payload = std::move(payload)};
    }

    static ControlResponse handleTransportStats(const ControlContext &ctx, const std::string &arg)
    {
        if (!ctx.transport_manager)
        {
            return makeJsonError(error::InternalError, "transport manager unavailable");
        }

        if (!arg.empty() && arg != "json")
        {
            return makeJsonError(error::InvalidRequest, "unsupported argument: " + arg);
        }

        const auto &counters = ctx.transport_manager->counters();

        if (arg == "json")
        {
            nlohmann::json j;

            j["tx_packets"] = counters.tx_packets;
            j["tx_bytes"] = counters.tx_bytes;
            j["tx_failed"] = counters.tx_failed;
            j["backend_unavailable"] = counters.backend_unavailable;
            j["port_down"] = counters.port_down;
            j["invalid_packet"] = counters.invalid_packet;

            return makeJsonSuccess(j);
        }

        std::string payload;

        payload += "tx_packets=" + std::to_string(counters.tx_packets) + "\n";
        payload += "tx_bytes=" + std::to_string(counters.tx_bytes) + "\n";
        payload += "tx_failed=" + std::to_string(counters.tx_failed) + "\n";
        payload += "backend_unavailable=" + std::to_string(counters.backend_unavailable) + "\n";
        payload += "port_down=" + std::to_string(counters.port_down) + "\n";
        payload += "invalid_packet=" + std::to_string(counters.invalid_packet);

        return ControlResponse{.success = true, .payload = std::move(payload)};
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
            {"send-packet",
             {.name = "send-packet",
              .description = "inject synthetic packet into runtime",
              .fields = {"broadcast", "learn", "topology-demo"},
              .handler = handleSendPacket}},
            {"show",
             {.name = "show",
              .description = "runtime inspection commands",
              .fields = {"mac-table"},
              .handler = handleShow}},
            {"fd-status",
             {.name = "fd-status",
              .description = "file descriptor runtime state",
              .fields = {"fd", "state", "type"},
              .handler = handleFdStatus}},
            {"transport-stats",
             {.name = "transport-stats",
              .description = "transport layer statistics",
              .fields = {"tx_packets", "tx_bytes", "tx_failed", "backend_unavailable", "port_down",
                         "invalid_packet"},
              .handler = handleTransportStats}},
        };
        return table;
    }

    static ControlResponse dispatchV12(const ControlRequest &req, const ControlContext &ctx)
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
