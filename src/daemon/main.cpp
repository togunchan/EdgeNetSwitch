#include "edgenetswitch/Logger.hpp"
#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/Config.hpp"
#include "edgenetswitch/Telemetry.hpp"
#include "edgenetswitch/HealthMonitor.hpp"
#include "edgenetswitch/RuntimeStatus.hpp"

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

    void controlSocketThreadFunc(int control_fd, const Telemetry &telemetry, const std::atomic_bool &stopRequested)
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
                if (cmd.find("status") != std::string::npos)
                {
                    auto metrics = telemetry.snapshot();
                    std::string response =
                        "uptime_ms=" + std::to_string(metrics.uptime_ms) +
                        " tick_count=" + std::to_string(metrics.tick_count) + "\n";

                    ::write(client_fd, response.c_str(), response.size());
                }
                else
                {
                    std::string response = "unknown command\n";
                    ::write(client_fd, response.c_str(), response.size());
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

        const char *cmd = "status";
        ::write(fd, cmd, std::strlen(cmd));

        char buffer[256]{};
        ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);

        if (n > 0)
            Logger::info(std::string(buffer, n));
        else
            Logger::warn("CLI: empty response");

        ::close(fd);
        return true;
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
        controlThread = std::thread(controlSocketThreadFunc, control_fd, std::cref(telemetry), std::cref(g_stopRequested));
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
