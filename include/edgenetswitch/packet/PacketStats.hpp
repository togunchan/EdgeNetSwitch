#pragma once

#include <cstdint>
#include "edgenetswitch/MessagingBus.hpp"

namespace edgenetswitch
{

    class PacketStats
    {
    public:
        explicit PacketStats(MessagingBus &bus);

        std::uint64_t rxPackets() const;
        std::uint64_t rxBytes() const;
        std::uint64_t drops() const;

    private:
        std::uint64_t rx_packets_{0};
        std::uint64_t rx_bytes_{0};
        std::uint64_t drops_{0};
    };

} // namespace edgenetswitch
