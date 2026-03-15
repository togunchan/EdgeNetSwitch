#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/runtime/RuntimeMetrics.hpp"

#include <cstdint>

namespace edgenetswitch
{
    class Telemetry
    {
    public:
        Telemetry(MessagingBus &bus, const Config &cfg);

        void onTick();

        RuntimeMetrics snapshot() const;

    private:
        MessagingBus &bus_;
        std::uint64_t start_time_ms_;
        std::uint64_t tick_count_;
    };
} // namespace edgenetswitch