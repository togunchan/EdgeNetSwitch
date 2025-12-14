#include "edgenetswitch/Logger.hpp"
#include "edgenetswitch/MessagingBus.hpp"

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

    Logger::init(LogLevel::Info, "edgenetswitch.log");
    Logger::info("EdgeNetSwitch daemon starting...");

    MessagingBus bus;

    bus.subscribe(MessageType::SystemStart, [&](const Message &msg)
                  { Logger::info("SystemStart received by daemon"); });

    bus.subscribe(MessageType::SystemShutdown, [&](const Message &msg)
                  { Logger::info("SystemShutdown received by daemon"); });

    bus.publish({MessageType::SystemStart, nowMs()});

    // keep the process alive until a stop is requested.
    while (!g_stopRequested.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Logger::warn("Stop requested. Shutting down...");

    bus.publish({MessageType::SystemShutdown, nowMs()});

    Logger::info("EdgeNetSwitch daemon stopped.");
    Logger::shutdown();

    return 0;
}
