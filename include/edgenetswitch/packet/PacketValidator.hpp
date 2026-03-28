#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include <string>

namespace edgenetswitch
{
    enum class PacketRejectReason
    {
        None,
        InvalidFormat,
        EmptyPayload,
        MissingSource,
        PayloadTooLarge
    };

    struct ValidationResult
    {
        bool accepted{false};
        PacketRejectReason reason{PacketRejectReason::None};
    };

    std::string toString(PacketRejectReason reason);

    class PacketValidator
    {
    public:
        static ValidationResult validate(const Packet &packet);
    };

} // namespace edgenetswitch
