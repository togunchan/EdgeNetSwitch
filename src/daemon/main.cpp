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
} // namespace

int main(int argc, char *argv[])
{
    if (argc > 1 && std::string(argv[1]) == "status")
    {
        Logger::init(LogLevel::Info, "");

        // Temporary local snapshot
        RuntimeStatus status{
            .metrics = RuntimeMetrics{
                .uptime_ms = 0,
                .tick_count = 0},
            .state = "UNKNOWN"};

        Logger::info("Runtime Status");
        Logger::info("--------------");
        Logger::info("State      : " + status.state);
        Logger::info("Uptime (ms): " + std::to_string(status.metrics.uptime_ms));
        Logger::info("Tick Count : " + std::to_string(status.metrics.tick_count));

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
