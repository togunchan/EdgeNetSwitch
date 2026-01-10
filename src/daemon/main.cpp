#include "edgenetswitch/Logger.hpp"
#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/Config.hpp"
#include "edgenetswitch/Telemetry.hpp"
#include "edgenetswitch/HealthMonitor.hpp"
#include "edgenetswitch/RuntimeStatus.hpp"
#include "edgenetswitch/control/ControlProtocol.hpp"
#include "edgenetswitch/control/ControlWire.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

using namespace edgenetswitch;

// anonymous namespace: restricts symbols to this translation unit only
namespace
{
    std::atomic_bool g_stopRequested{false};

    void handleSignal(int)
    {
        g_stopRequested.store(true, std::memory_order_relaxed);
    }

    void installSignalHandlers()
    {
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
    }

    std::uint64_t nowMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    enum class RuntimeState
    {
        Booting,
        Running,
        Stopping
    };

    static std::string stateToString(RuntimeState s)
    {
        switch (s)
        {
        case RuntimeState::Booting:
            return "BOOTING";
            break;
        case RuntimeState::Running:
            return "RUNNING";
            break;
        case RuntimeState::Stopping:
            return "STOPPING";
            break;

        default:
            return "UNKNOWN";
        }
    }

    static RuntimeStatus buildRuntimeStatus(const Telemetry &telemetry, RuntimeState state)
    {
        return RuntimeStatus{.metrics = telemetry.snapshot(), .state = stateToString(state)};
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
                    Logger::error("Malformed control request: missing '|' separator:" + cmd);
                    ::close(client_fd);
                    continue;
                }

                control::ControlRequest req{
                    .version = cmd.substr(0, sep),
                    .command = cmd.substr(sep + 1)};

                // trim newline
                req.command.erase(
                    req.command.find_last_not_of(" \n\r\t") + 1);

                if (req.command == "status")
                {
                    auto status = buildRuntimeStatus(telemetry, runtimeState);
                    control::ControlResponse resp{
                        .success = true,
                        .payload =
                            "state=" + status.state + "\n" +
                            "uptime_ms=" + std::to_string(status.metrics.uptime_ms) + "\n" +
                            "tick_count=" + std::to_string(status.metrics.tick_count)};

                    const std::string wire = control::encodeResponse(resp);
                    ::write(client_fd, wire.c_str(), wire.size());
                }
                else
                {
                    control::ControlResponse resp{
                        .success = false,
                        .error = "unknown command"};

                    const std::string wire = control::encodeResponse(resp);
                    ::write(client_fd, wire.c_str(), wire.size());
                }
            }
            ::close(client_fd);
        }
    }

    bool runStatusCLI()
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

        control::ControlRequest req{.version = "1.2", .command = "status"};
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

        Logger::info("Runtime Status");
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
    if (argc > 1 && std::string(argv[1]) == "status")
    {
        Logger::init(LogLevel::Info, "");

        if (!runStatusCLI())
        {
            Logger::error("Failed to retrieve runtime status (is daemon running?)");
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
    HealthMonitor health(bus, 500);

    int control_fd = createControlSocket();
    std::thread controlThread;

    if (control_fd >= 0)
    {
        controlThread = std::thread(controlSocketThreadFunc,
                                    control_fd,
                                    std::cref(telemetry),
                                    std::cref(runtimeState),
                                    std::cref(g_stopRequested));
    }
    else
    {
        Logger::warn("Control socket not available; continuing without IPC");
    }

    bus.subscribe(MessageType::SystemStart, [&](const Message &msg)
                  { Logger::info("SystemStart received by daemon"); });

    bus.subscribe(MessageType::SystemShutdown, [&](const Message &msg)
                  { Logger::info("SystemShutdown received by daemon"); });

    bus.subscribe(MessageType::Telemetry, [&](const Message &msg)
                  {
                    health.onHeartbeat();

                    const auto* data = std::get_if<TelemetryData>(&msg.payload);
                    if (data) {
                        Logger::debug("Telemetry: uptime_ms=" + std::to_string(data->uptime_ms) + " tick_count=" + std::to_string(data->tick_count)); 
                    } });

    bus.subscribe(MessageType::HealthStatus, [&](const Message &msg)
                  {
                        const auto* hs = std::get_if<HealthStatus>(&msg.payload);
                        if (!hs) return;

                        if (!hs->is_alive) {
                            Logger::warn("HealthStatus: NOT ALIVE (timeout exceeded)");
                        } else {
                            Logger::debug("HealthStatus: alive"); 
                        } });

    bus.publish({MessageType::SystemStart, nowMs()});
    runtimeState = RuntimeState::Running;

    // keep the process alive until a stop is requested.
    while (!g_stopRequested.load(std::memory_order_relaxed))
    {
        telemetry.onTick();
        health.onTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.daemon.tick_ms));
    }

    if (controlThread.joinable())
    {
        controlThread.join();
    }

    destroyControlSocket(control_fd);

    runtimeState = RuntimeState::Stopping;
    Logger::warn("Stop requested. Shutting down...");

    const auto status = buildRuntimeStatus(telemetry, runtimeState);
    Logger::info(
        "RuntimeStatus: state=" + status.state +
        " uptime_ms=" + std::to_string(status.metrics.uptime_ms) +
        " tick_count=" + std::to_string(status.metrics.tick_count));
    bus.publish({MessageType::SystemShutdown, nowMs()});

    Logger::info("EdgeNetSwitch daemon stopped.");
    Logger::shutdown();

    return 0;
}
