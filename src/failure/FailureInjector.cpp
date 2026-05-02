#include "edgenetswitch/failure/FailureInjector.hpp"

namespace edgenetswitch::failure
{
    FailureInjector::FailureInjector(const FailureConfig &cfg) : config_(cfg) {}

    FailureResult FailureInjector::inject(const Packet &, std::uint64_t)
    {
        if (!config_.enabled || config_.type == FailureType::None)
            return {FailureType::None, false};

        if (config_.every_n_packets == 0)
            return {FailureType::None, false};

        ++seen_packets_;

        if (seen_packets_ % config_.every_n_packets != 0)
            return {FailureType::None, false};

        const bool terminal = config_.type != FailureType::ArtificialDelay;

        FailureResult result{.type = config_.type,
                             .is_terminal = terminal};

        if (config_.type == FailureType::ArtificialDelay)
        {
            result.delay_ms = config_.delay_ms;
        }
        return result;
    }
} // namespace edgenetswitch::failure
