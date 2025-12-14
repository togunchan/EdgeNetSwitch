#pragma once

#include "edgenetswitch/MessagingBus.hpp"
#include "edgenetswitch/Config.hpp"

#include <cstdint>

namespace edgenetswitch
{
    class Telemetry
    {
    public:
        Telemetry(MessagingBus &bus, const Config &cfg);

        void onTick();

    private:
        MessagingBus &bus_;
        std::uint64_t start_time_ms_;
        std::uint64_t tick_count_;
    };
} // namespace edgenetswitch