#include "edgenetswitch/packet/PacketValidator.hpp"
#include "edgenetswitch/core/Logger.hpp"

namespace edgenetswitch
{

    std::string toString(PacketRejectReason reason)
    {
        switch (reason)
        {
        case PacketRejectReason::None:
            return "none";
        case PacketRejectReason::InvalidFormat:
            return "invalid_format";
        case PacketRejectReason::EmptyPayload:
            return "empty_payload";
        case PacketRejectReason::MissingSource:
            return "missing_source";
        case PacketRejectReason::PayloadTooLarge:
            return "payload_too_large";
        default:
            return "unknown";
        }
    }

    static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;

    ValidationResult PacketValidator::validate(const Packet &packet)
    {
        if (!packet.valid)
            return {false, PacketRejectReason::InvalidFormat};

        if (packet.payload.empty() || std::all_of(packet.payload.begin(), packet.payload.end(), isspace))
        {
            return {false, PacketRejectReason::EmptyPayload};
        }

        if (packet.source_ip.empty())
        {
            return {false, PacketRejectReason::MissingSource};
        }

        if (packet.payload.size() > MAX_PAYLOAD_SIZE)
        {
            return {false, PacketRejectReason::PayloadTooLarge};
        }

        return {true, PacketRejectReason::None};
    }
}
