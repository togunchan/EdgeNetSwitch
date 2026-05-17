#pragma once
#include "edgenetswitch/switching/MacAddress.hpp"
#include <cstdint>
#include <string>

namespace edgenetswitch
{
    enum class PacketDropReason
    {
        ParseError,
        ValidationError,
        QueueOverflow,
        SimulatedLoss,
        RateLimited,
        ProcessingError,
        InternalError,
        Unknown
    };

    struct PacketDropped
    {
        PacketDropReason reason;
        std::uint64_t timestamp_ms;
        std::uint64_t packet_id{0};
        std::uint64_t lifecycle_id{0};
    };

    struct Packet
    {
        std::uint64_t id{0};
        std::uint64_t lifecycle_id{0};
        std::string payload;
        std::uint64_t timestamp_ms{0};
        std::uint32_t wire_size{0};    // raw packet size coming from UDP
        std::uint32_t payload_size{0}; // parsed packet size
        bool valid{false};
        std::string source_ip{};
        std::uint16_t source_port{0};
        std::optional<MacAddress> source_mac;
        std::optional<MacAddress> destination_mac;
    };
} // namespace edgenetswitch
