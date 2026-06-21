#include "edgenetswitch/control/ControlProtocol.hpp"
#include "edgenetswitch/control/ControlServer.hpp"
#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"
#include "edgenetswitch/failure/FailureInjector.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/network/IngressMode.hpp"
#include "edgenetswitch/network/UdpReceiver.hpp"
#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/packet/PacketGenerator.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/runtime/RuntimeStatus.hpp"
#include "edgenetswitch/runtime/ShutdownReason.hpp"
#include "edgenetswitch/runtime/ShutdownRequest.hpp"
#include "edgenetswitch/switching/ForwardingDecision.hpp"
#include "edgenetswitch/switching/ForwardingEvent.hpp"
#include "edgenetswitch/switching/InterfaceRegistry.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/switching/SwitchPort.hpp"
#include "edgenetswitch/system/epoll/ControlReadyHandler.hpp"
#include "edgenetswitch/system/epoll/EpollEventLoop.hpp"
#include "edgenetswitch/system/epoll/EpollManager.hpp"
#include "edgenetswitch/system/epoll/UdpReadyHandler.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"
#include "edgenetswitch/system/fd/FileDescriptor.hpp"
#include "edgenetswitch/telemetry/Telemetry.hpp"
#include "runtime/RuntimeStatusBuilder.hpp"
#include "runtime/SnapshotPublisher.hpp"
#include "telemetry/FileTelemetryExporter.hpp"
#include "telemetry/InMemoryTelemetryExporter.hpp"
#include "telemetry/TelemetryExportManager.hpp"

#include <cstddef>
#include <fcntl.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <signal.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <utility>

using namespace edgenetswitch;
using namespace edgenetswitch::telemetry;

// anonymous namespace: restricts symbols to this translation unit only
namespace
{
    volatile sig_atomic_t g_receivedSignal = 0;
    edgenetswitch::daemon::SnapshotPublisher g_snapshotPublisher;

    void handleSignal(int signal)
    {
        g_receivedSignal = signal;
    }

    void installSignalHandlers()
    {
        struct sigaction action
        {
        };
        action.sa_flags = 0;
        action.sa_handler = handleSignal;
        sigemptyset(&action.sa_mask);

        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
    }

    constexpr const char *CONTROL_SOCKET_PATH = "/tmp/edgenetswitch.sock";

    FileDescriptor createControlSocket(FdRegistry *fd_registry)
    {
        const int raw_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (raw_fd < 0)
        {
            Logger::error("Failed to create control socket");
            return {};
        }

        FileDescriptor control_fd(raw_fd, fd_registry, FdType::UnixSocket);

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, CONTROL_SOCKET_PATH, sizeof(addr.sun_path) - 1);

        // Remove any existing socket file at the same path
        ::unlink(CONTROL_SOCKET_PATH);

        if (::bind(control_fd.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            Logger::error("Failed to bind the control socket");
            control_fd.reset();
            return {};
        }

        // Allow the socket to accept incoming connections, with a queue size of up to 4
        if (::listen(control_fd.get(), 4) < 0)
        {
            Logger::error("Failed to listen on control socket");
            control_fd.reset();
            return {};
        }

        const int flags = ::fcntl(control_fd.get(), F_GETFL, 0);

        if (flags < 0)
        {
            Logger::error("Failed to get control socket flags");
            control_fd.reset();
            return {};
        }

        if (::fcntl(control_fd.get(), F_SETFL, flags | O_NONBLOCK) < 0)
        {
            Logger::error("Failed to enable O_NONBLOCK on control socket");
            control_fd.reset();
            return {};
        }

        Logger::info("Control socket running in non-blocking mode");

        Logger::info(std::string("Control socket listening at ") + CONTROL_SOCKET_PATH);
        return control_fd;
    }

    void destroyControlSocket(FileDescriptor &fd)
    {
        Logger::debug("[CONTROL] Closing control socket fd=" + std::to_string(fd.get()));
        fd.reset();
        Logger::debug("[CONTROL] Control socket close completed");
        ::unlink(CONTROL_SOCKET_PATH);
        Logger::info("Control socket closed");
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

    bool executeControlCommand(const std::string &command)
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
            if (n <= 0)
                break;

            accum.append(buffer, buffer + n);
        }

        if (accum.empty())
        {
            Logger::warn("CLI: no response from daemon");
            ::close(fd);
            return false;
        }

        Logger::info(cliTitleForCommand(command));
        Logger::info("--------------");

        const nlohmann::json parsed = nlohmann::json::parse(accum, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("status") &&
            parsed["status"].is_string())
        {
            if (parsed["status"] == "ok")
            {
                Logger::info(accum);
                return true;
            }

            Logger::error(accum);
            return false;
        }

        Logger::info(accum);
        return true;
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

        const bool ok = executeControlCommand(command);

        Logger::shutdown();
        return ok ? 0 : 1;
    }

    ShutdownRequest shutdownRequest;
    installSignalHandlers();

    core::Config cfg = core::ConfigLoader::loadFromFile("config/edgenetswitch.json");

    Logger::init(Logger::parseLevel(cfg.log.level), cfg.log.file);
    Logger::info("EdgeNetSwitch daemon starting...");

    FdRegistry fd_registry;
    {
        MessagingBus bus;
        RuntimeState runtimeState = RuntimeState::Booting;
        Telemetry telemetry(bus, cfg);
        HealthMonitor healthMonitor(bus, 500);
        PacketGenerator packetGenerator(bus);
        failure::FailureInjector failureInjector(failure::FailureConfig{});
        InterfaceRegistry interfaces;

        SwitchPort port1(1, "eth1");
        port1.setState(PortState::Up);
        interfaces.addPort(std::move((port1)));

        SwitchPort port2(2, "eth2");
        port2.setState(PortState::Up);
        interfaces.addPort(std::move((port2)));

        SwitchPort port3(3, "eth3");
        port3.setState(PortState::Up);
        interfaces.addPort(std::move((port3)));

        SwitchPort port4(4, "eth4");
        port4.setState(PortState::Up);
        interfaces.addPort(std::move((port4)));

        SwitchPort port5(5, "eth5");
        port5.setState(PortState::Up);
        interfaces.addPort(std::move(port5));

        MacTable macTable(1024);
        SwitchForwardingEngine forwardingEngine(macTable, interfaces);
        PacketProcessor packetProcessor(bus, forwardingEngine, failureInjector);
        PacketStats packetStats(bus);
        EpollManager epollManager(&fd_registry);
        EpollEventLoop epollLoop(epollManager, &fd_registry);
        TelemetryExportManager exportManager;
        FileDescriptor control_fd = createControlSocket(&fd_registry);
        std::thread epollThread;
        std::unique_ptr<UdpReceiver> udpReceiver;
        RuntimeStatusBuilder statusBuilder(toSmootherConfig(cfg.rate));
        std::unique_ptr<UdpReadyHandler> udpHandler;
        std::unique_ptr<control::ControlServer> controlServer;
        std::unique_ptr<ControlReadyHandler> controlHandler;

        if (cfg.udp.enabled)
        {
            udpReceiver = std::make_unique<UdpReceiver>(bus, cfg.udp.port, &fd_registry,
                                                        IngressMode::NonBlocking);
            udpReceiver->initializeSocket();

            udpHandler = std::make_unique<UdpReadyHandler>(*udpReceiver);

            Logger::debug("UDP fd = " + std::to_string(udpReceiver->fd()));

            epollManager.add(udpReceiver->fd(), EPOLLIN);
            epollLoop.registerHandler(udpReceiver->fd(), udpHandler.get());
        }

        exportManager.addExporter(std::make_unique<StdoutTelemetryExporter>());
        exportManager.addExporter(std::make_unique<InMemoryTelemetryExporter>());
        exportManager.addExporter(std::make_unique<FileTelemetryExporter>("telemetry.log"));

        exportManager.start();

        {
            auto status =
                statusBuilder.build(telemetry, healthMonitor, packetStats, runtimeState, nowMs());

            g_snapshotPublisher.publish(status);
        }

        if (control_fd.valid())
        {
            controlServer = std::make_unique<control::ControlServer>(
                control_fd, g_snapshotPublisher, cfg, bus, forwardingEngine, fd_registry);

            controlHandler = std::make_unique<ControlReadyHandler>(*controlServer);

            epollManager.add(controlServer->fd(), EPOLLIN);

            epollLoop.registerHandler(controlServer->fd(), controlHandler.get());
        }
        if (!control_fd.valid())
        {
            Logger::error("Fatal: control socket initialization failed");
            return EXIT_FAILURE;
        }

        epollThread = std::thread(
            [&epollLoop]()
            {
                Logger::info("[EPOLL] Event loop thread started");
                epollLoop.run();
                Logger::debug("[EPOLL] Event loop stopped");
                Logger::debug("[EPOLL] Event loop thread exiting");
            });

#ifdef EDGENETSWITCH_DEBUG_READER
        std::thread debugReaderThread(
            [&shutdownRequest]
            {
                while (!shutdownRequest.isRequested())
                {
                    auto snap = g_snapshotPublisher.load();
                    if (snap)
                    {
                        Logger::debug(
                            "debug_reader: version=" + std::to_string(snap->snapshot_version) +
                            " tick=" + std::to_string(snap->metrics.tick_count));
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            });
#endif

        bus.subscribe(MessageType::SystemStart,
                      [&](const Message &msg) { Logger::info("SystemStart received by daemon"); });

        bus.subscribe(MessageType::SystemShutdown, [&](const Message &msg)
                      { Logger::info("SystemShutdown received by daemon"); });

        bus.subscribe(MessageType::Telemetry,
                      [&](const Message &msg)
                      {
                          healthMonitor.onHeartbeat();

                          const auto *data = std::get_if<TelemetryData>(&msg.payload);
                          if (data)
                          {
                              // Logger::debug("Telemetry: uptime_ms=" +
                              // std::to_string(data->uptime_ms)
                              // + " tick_count=" + std::to_string(data->tick_count));

                              RuntimeMetrics metrics{.uptime_ms = data->uptime_ms,
                                                     .tick_count = data->tick_count};

                              exportManager.enqueue(std::move(metrics));
                          }
                      });

        bus.subscribe(MessageType::HealthStatus,
                      [&](const Message &msg)
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
                          }
                      });

        bus.subscribe(MessageType::PacketRx,
                      [](const Message &msg)
                      {
                          const Packet &p = std::get<Packet>(msg.payload);
                          Logger::info("Packet received: "
                                       "id=" +
                                       std::to_string(p.id) + " payload=" + p.payload +
                                       " timestamp=" + formatTimestamp(p.timestamp_ms) +
                                       " source_ip=" + p.source_ip +
                                       " source_port=" + std::to_string(p.source_port));
                      });

        bus.subscribe(MessageType::ForwardingDecisionMade,
                      [](const Message &msg)
                      {
                          const auto *event = std::get_if<ForwardingEvent>(&msg.payload);

                          if (!event)
                              return;

                          std::string action = "Drop";

                          if (event->action == ForwardingAction::Flood)
                              action = "Flood";
                          else if (event->action == ForwardingAction::ForwardToPorts)
                              action = "ForwardToPorts";

                          std::string ports;
                          for (std::size_t i = 0; i < event->egress_ports.size(); ++i)
                          {
                              if (i != 0)
                                  ports += ",";

                              ports += std::to_string(event->egress_ports[i]);
                          }

                          Logger::info("ForwardingDecisionMade: lifecycle_id=" +
                                       std::to_string(event->lifecycle_id) + " action=" + action +
                                       " egress_ports=[" + ports + "]");
                      });

        bus.subscribe(MessageType::PacketProcessed,
                      [](const Message &msg)
                      {
                          const Packet &p = std::get<Packet>(msg.payload);

                          Logger::info(
                              "PacketProcessed: lifecycle_id=" + std::to_string(p.lifecycle_id) +
                              " packet_id=" + std::to_string(p.id));
                      });

        bus.publish({MessageType::SystemStart, nowMs()});
        runtimeState = RuntimeState::Running;

        // keep the process alive until a stop is requested.
        while (!shutdownRequest.isRequested())
        {
            if (g_receivedSignal == SIGINT)
            {
                shutdownRequest.request(ShutdownReason::SignalInterrupt);
                break;
            }

            if (g_receivedSignal == SIGTERM)
            {
                shutdownRequest.request(ShutdownReason::SignalTerminate);
                break;
            }

            telemetry.onTick();
            healthMonitor.onTick();

            auto status =
                statusBuilder.build(telemetry, healthMonitor, packetStats, runtimeState, nowMs());

            g_snapshotPublisher.publish(status);
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.daemon.tick_ms));
        }

#ifdef EDGENETSWITCH_DEBUG_READER
        if (debugReaderThread.joinable())
        {
            debugReaderThread.join();
        }
#endif

        Logger::info("[SHUTDOWN] Stopping epoll event loop");
        epollLoop.stop();
        Logger::info("[SHUTDOWN] Epoll event loop stop requested");

        if (epollThread.joinable())
        {
            Logger::info("[SHUTDOWN] Waiting for epoll thread");
            epollThread.join();
            Logger::info("[SHUTDOWN] Epoll thread stopped");
        }

        destroyControlSocket(control_fd);

        if (udpReceiver)
        {
            Logger::info("[SHUTDOWN] Stopping UDP receiver");
            udpReceiver->stop();
            Logger::info("[SHUTDOWN] UDP receiver stopped");
        }

        Logger::info("[SHUTDOWN] Stopping telemetry export manager");
        exportManager.stop();
        Logger::info("[SHUTDOWN] Telemetry export manager stopped");

        runtimeState = RuntimeState::Stopping;
        Logger::warn("Stop requested. Shutting down...");
        Logger::info("[SHUTDOWN] Reason: " + std::string(toString(shutdownRequest.reason())));
        const auto status =
            statusBuilder.build(telemetry, healthMonitor, packetStats, runtimeState, nowMs());
        Logger::info("RuntimeStatus: state=" + stateToString(status.state) +
                     " uptime_ms=" + std::to_string(status.metrics.uptime_ms) +
                     " tick_count=" + std::to_string(status.metrics.tick_count));
        bus.publish({MessageType::SystemShutdown, nowMs()});
    }

    const auto remaining_fds = fd_registry.snapshot();

    bool leak_detected = false;

    for (const auto &record : remaining_fds)
    {
        if (record.state == FdState::Active)
        {
            leak_detected = true;

            Logger::error("[FD][LEAK] active descriptor detected during shutdown: fd=" +
                          std::to_string(record.fd));
        }
    }

    if (!leak_detected)
    {
        Logger::info("[FD] all descriptors released cleanly");
    }

    Logger::info("EdgeNetSwitch daemon stopped.");
    Logger::shutdown();

    return 0;
}
