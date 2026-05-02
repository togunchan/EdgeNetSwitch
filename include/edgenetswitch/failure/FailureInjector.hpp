#pragma once

#include <cstdint>

namespace edgenetswitch
{
    struct Packet;
}

namespace edgenetswitch::failure
{
    enum class FailureType
    {
        None,
        MalformedPacket,
        ValidationError,
        SimulatedLoss,
        ArtificialDelay,
        ProcessingRejection
    };

    struct FailureResult
    {
        FailureType type;
        bool is_terminal;
        double delay_ms{0.0};
    };
    struct FailureConfig
    {
        double drop_rate{0.0};
        double delay_ms{0.0};
        bool enable_malformed{false};

        // 0 = never trigger
        // 5 = trigger every 5th packet
        FailureType type{FailureType::None};
        bool enabled{false};

        std::uint64_t every_n_packets{0};
    };

    class FailureInjector
    {
    public:
        explicit FailureInjector(const FailureConfig &cfg);
        FailureResult inject(const Packet &pkt, std::uint64_t now);

    private:
        FailureConfig config_;
        std::uint64_t seen_packets_{0};
    };
} // namespace edgenetswitch::failure
