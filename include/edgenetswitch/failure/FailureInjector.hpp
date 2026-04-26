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
    };

    struct FailureConfig
    {
        double drop_rate{0.0};
        double delay_ms{0.0};
        bool enable_malformed{false};
    };

    class FailureInjector
    {
    public:
        explicit FailureInjector(const FailureConfig &cfg);
        FailureResult inject(const Packet &pkt, std::uint64_t now);

    private:
        FailureConfig config_;
    };
} // namespace edgenetswitch::failure
