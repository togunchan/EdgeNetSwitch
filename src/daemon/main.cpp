#include "edgenetswitch/Logger.hpp"
#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/Config.hpp"
#include "edgenetswitch/Telemetry.hpp"
#include "edgenetswitch/HealthMonitor.hpp"

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
} // namespace

int main()
{
    installSignalHandlers();

    Config cfg = ConfigLoader::loadFromFile("config/edgenetswitch.json");

    Logger::init(Logger::parseLevel(cfg.log.level), cfg.log.file);
    Logger::info("EdgeNetSwitch daemon starting...");

    MessagingBus bus;
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

    // keep the process alive until a stop is requested.
    while (!g_stopRequested.load(std::memory_order_relaxed))
    {
        telemetry.onTick();
        health.onTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.daemon.tick_ms));
    }

    Logger::warn("Stop requested. Shutting down...");

    bus.publish({MessageType::SystemShutdown, nowMs()});

    Logger::info("EdgeNetSwitch daemon stopped.");
    Logger::shutdown();

    return 0;
}
