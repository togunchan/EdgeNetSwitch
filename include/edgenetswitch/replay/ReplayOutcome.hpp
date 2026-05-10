#pragma once

#include "edgenetswitch/packet/Packet.hpp"

#include <cstdint>

namespace edgenetswitch
{
    enum class ReplayOutcomeType
    {
        Processed,
        Dropped
    };

    struct ReplayOutcome
    {
        std::uint64_t sequence{0};
        ReplayOutcomeType type{ReplayOutcomeType::Processed};
        std::uint64_t lifecycle_id{0};
        PacketDropReason drop_reason{PacketDropReason::Unknown};

        bool operator==(const ReplayOutcome &) const = default;
    };
} // namespace edgenetswitch
