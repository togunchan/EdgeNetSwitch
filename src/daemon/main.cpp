#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/telemetry/Telemetry.hpp"
#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/runtime/RuntimeStatus.hpp"
#include "edgenetswitch/control/ControlProtocol.hpp"
#include "edgenetswitch/control/ControlWire.hpp"
#include "runtime/RuntimeStatusBuilder.hpp"
#include "control/ControlContext.hpp"
#include "control/ControlDispatch.hpp"
#include "telemetry/TelemetryExportManager.hpp"
#include "telemetry/InMemoryTelemetryExporter.hpp"
#include "telemetry/FileTelemetryExporter.hpp"
#include "runtime/SnapshotPublisher.hpp"
#include "edgenetswitch/packet/PacketGenerator.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/network/UdpReceiver.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <memory>

using namespace edgenetswitch;
using namespace edgenetswitch::telemetry;

// anonymous namespace: restricts symbols to this translation unit only
namespace
{
    std::atomic_bool g_stopRequested{false};
    edgenetswitch::daemon::SnapshotPublisher g_snapshotPublisher;

    void handleSignal(int)
    {
        g_stopRequested.store(true, std::memory_order_relaxed);
    }

    void installSignalHandlers()
    {
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
    }

    constexpr const char *CONTROL_SOCKET_PATH = "/tmp/edgenetswitch.sock";

    int createControlSocket()
    {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            Logger::error("Failed to create control socket");
            return -1;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, CONTROL_SOCKET_PATH, sizeof(addr.sun_path) - 1);

        // Remove any existing socket file at the same path
        ::unlink(CONTROL_SOCKET_PATH);

        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            Logger::error("Failed to bind the control socket");
            ::close(fd);
            return -1;
        }

        // Allow the socket to accept incoming connections, with a queue size of up to 4
        if (::listen(fd, 4) < 0)
        {
            Logger::error("Failed to listen on control socket");
            ::close(fd);
            return -1;
        }

        Logger::info(std::string("Control socket listening at ") + CONTROL_SOCKET_PATH);
        return fd;
    }

    void destroyControlSocket(int fd)
    {
        if (fd >= 0)
        {
            ::close(fd);
            ::unlink(CONTROL_SOCKET_PATH);
            Logger::info("Control socket closed");
        }
    }

    void controlSocketThreadFunc(int control_fd,
                                 const Telemetry &telemetry,
                                 const RuntimeState &runtimeState,
                                 const HealthMonitor &healthMonitor,
                                 const std::atomic_bool &stopRequested)
    {
        while (!stopRequested.load(std::memory_order_relaxed))
        {
            int client_fd = ::accept(control_fd, nullptr, nullptr);
            if (client_fd < 0)
            {
                // If accept() fails with EINTR (interrupted by signal), just retry.
                if (errno == EINTR)
                {
                    continue;
                }
                // For other errors, log the failure and continue the loop.
                Logger::error("Control socket accept failed");
                continue;
            }

            char buffer[128]{};
            ssize_t n = ::read(client_fd, buffer, sizeof(buffer) - 1);
            if (n > 0)
            {
                std::string cmd(buffer, n);

                auto sep = cmd.find('|');
                if (sep == std::string::npos)
                {
                    control::ControlResponse resp{
                        .success = false,
                        .error_code = control::error::InvalidRequest,
                        .message = "malformed_request"};
                    auto wire = control::encodeResponse(resp);
                    ::write(client_fd, wire.c_str(), wire.size());
                    ::close(client_fd);
                    continue;
                }

                control::ControlRequest req{
                    .version = cmd.substr(0, sep),
                    .command = cmd.substr(sep + 1)};

                // trim newline
                req.command.erase(
                    req.command.find_last_not_of(" \n\r\t") + 1);

                control::ControlContext ctx{
                    .publisher = &g_snapshotPublisher,
                };

                control::ControlResponse resp = control::dispatchControlRequest(req, ctx);
                std::string wire = control::encodeResponse(resp);
                ::write(client_fd, wire.c_str(), wire.size());
            }
            ::close(client_fd);
        }
    }

    std::string cliTitleForCommand(const std::string &command)
    {
        if (command == "status")
            return "Runtime Status";
        if (command == "health")
            return "Health Status";
        if (command == "metrics")
            return "Metrics";
        if (command == "version")
            return "Version";
        if (command == "help")
            return "Help";
        return command.empty() ? "Command" : command;
    }

    bool runControlCLI(const std::string &command)
    {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            Logger::error("CLI: failed to create socket");
            return false;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, CONTROL_SOCKET_PATH, sizeof(addr.sun_path) - 1);

        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            Logger::error("CLI: connect() failed (is daemon running?)");
            ::close(fd);
            return false;
        }

        control::ControlRequest req{.version = "1.2", .command = command};
        std::string wire = req.version + "|" + req.command;
        ::write(fd, wire.c_str(), wire.size());

        std::string accum;
        char buffer[256]{};

        while (true)
        {
            ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
            if (n < 0)
                break;

            accum.append(buffer, buffer + n);

            if (accum.find("END\n") != std::string::npos)
                break;
        }

        if (accum.empty())
        {
            Logger::warn("CLI: no response from daemon");
            ::close(fd);
            return false;
        }

        auto firstNL = accum.find('\n');
        std::string header = (firstNL == std::string::npos) ? accum : accum.substr(0, firstNL);

        Logger::info(cliTitleForCommand(command));
        Logger::info("--------------");

        auto endPos = accum.find("END\n");
        std::string body = (firstNL == std::string::npos) ? "" : accum.substr(firstNL + 1, endPos - (firstNL + 1));

        if (header == "OK")
        {
            Logger::info(body);
            return true;
        }
        else
        {
            Logger::error(body);
            return false;
        }
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        Logger::init(LogLevel::Info, "");

        std::string command = argv[1];
        if (command == "help" && argc > 2)
        {
            command += ":";
            command += argv[2];
        }

        if (!runControlCLI(command))
        {
            Logger::error("Failed to retrieve command (is daemon running?)");
        }

        Logger::shutdown();
        return 0;
    }
    installSignalHandlers();

    Config cfg = ConfigLoader::loadFromFile("config/edgenetswitch.json");

    Logger::init(Logger::parseLevel(cfg.log.level), cfg.log.file);
    Logger::info("EdgeNetSwitch daemon starting...");

    MessagingBus bus;
    RuntimeState runtimeState = RuntimeState::Booting;
    Telemetry telemetry(bus, cfg);
    HealthMonitor healthMonitor(bus, 500);
    PacketGenerator packetGenerator(bus);
    PacketProcessor packetProcessor(bus);
    PacketStats packetStats(bus);
    TelemetryExportManager exportManager;
    int control_fd = createControlSocket();
    std::thread controlThread;
    std::unique_ptr<UdpReceiver> udpReceiver;

    if (cfg.udp.enabled)
    {
        udpReceiver = std::make_unique<UdpReceiver>(bus, cfg.udp.port);
        udpReceiver->start();
    }

    exportManager.addExporter(std::make_unique<StdoutTelemetryExporter>());
    exportManager.addExporter(std::make_unique<InMemoryTelemetryExporter>());
    exportManager.addExporter(std::make_unique<FileTelemetryExporter>("telemetry.log"));

    exportManager.start();

    {
        auto status = buildRuntimeStatus(
            telemetry,
            healthMonitor,
            packetStats,
            runtimeState,
            nowMs());

        g_snapshotPublisher.publish(status);
    }

    if (control_fd >= 0)
    {
        controlThread = std::thread(controlSocketThreadFunc,
                                    control_fd,
                                    std::cref(telemetry),
                                    std::cref(runtimeState),
                                    std::cref(healthMonitor),
                                    std::cref(g_stopRequested));
    }
    else
    {
        Logger::warn("Control socket not available; continuing without IPC");
    }

#ifdef EDGENETSWITCH_DEBUG_READER
    std::thread debugReaderThread([]
                                  {
        while (!g_stopRequested.load())
        {
        auto snap = g_snapshotPublisher.load();
        if (snap)
        {
            Logger::debug(
                "debug_reader: version=" + std::to_string(snap->snapshot_version) +
                " tick=" + std::to_string(snap->metrics.tick_count)
            );
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } });
#endif

    bus.subscribe(MessageType::SystemStart, [&](const Message &msg)
                  { Logger::info("SystemStart received by daemon"); });

    bus.subscribe(MessageType::SystemShutdown, [&](const Message &msg)
                  { Logger::info("SystemShutdown received by daemon"); });

    bus.subscribe(MessageType::Telemetry, [&](const Message &msg)
                  {
        healthMonitor.onHeartbeat();

        const auto *data = std::get_if<TelemetryData>(&msg.payload);
        if (data)
        {
            // Logger::debug("Telemetry: uptime_ms=" + std::to_string(data->uptime_ms) + " tick_count=" + std::to_string(data->tick_count));

            RuntimeMetrics metrics{
                .uptime_ms = data->uptime_ms,
                .tick_count = data->tick_count};

            exportManager.enqueue(std::move(metrics));
        } });

    bus.subscribe(MessageType::HealthStatus, [&](const Message &msg)
                  {    
        const auto *hs = std::get_if<HealthStatus>(&msg.payload);
        if (!hs)
            return;

        if (!hs->is_alive)
        {
            Logger::warn("HealthStatus: NOT ALIVE (timeout exceeded)");
        }
        else
        {
            Logger::debug("HealthStatus: alive");
        } });

    bus.subscribe(MessageType::PacketRx, [](const Message &msg)
                  {
    const Packet &p = std::get<Packet>(msg.payload);
    Logger::info(
    "Packet received: "
    "id=" + std::to_string(p.id) +
    " payload=" + p.payload +
    " timestamp=" + formatTimestamp(p.timestamp_ms) +
    " source_ip=" + p.source_ip +
    " source_port=" + std::to_string(p.source_port)
); });

    bus.publish({MessageType::SystemStart, nowMs()});
    runtimeState = RuntimeState::Running;

    // keep the process alive until a stop is requested.
    while (!g_stopRequested.load(std::memory_order_relaxed))
    {
        telemetry.onTick();
        healthMonitor.onTick();

        auto status =
            buildRuntimeStatus(telemetry, healthMonitor, packetStats, runtimeState, nowMs());

        g_snapshotPublisher.publish(status);
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.daemon.tick_ms));
    }

#ifdef EDGENETSWITCH_DEBUG_READER
    if (debugReaderThread.joinable())
    {
        debugReaderThread.join();
    }
#endif
    if (controlThread.joinable())
    {
        controlThread.join();
    }

    destroyControlSocket(control_fd);

    if (udpReceiver)
    {
        udpReceiver->stop();
    }

    exportManager.stop();

    runtimeState = RuntimeState::Stopping;
    Logger::warn("Stop requested. Shutting down...");
    const auto status = buildRuntimeStatus(telemetry, healthMonitor, packetStats, runtimeState, nowMs());
    Logger::info(
        "RuntimeStatus: state=" + stateToString(status.state) +
        " uptime_ms=" + std::to_string(status.metrics.uptime_ms) +
        " tick_count=" + std::to_string(status.metrics.tick_count));
    bus.publish({MessageType::SystemShutdown, nowMs()});

    Logger::info("EdgeNetSwitch daemon stopped.");
    Logger::shutdown();

    return 0;
}
