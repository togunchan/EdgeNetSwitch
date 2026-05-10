#include "edgenetswitch/failure/FailureInjector.hpp"
#include "edgenetswitch/packet/Packet.hpp"

namespace edgenetswitch::failure
{
    namespace
    {
        FailureResult makeFailureResult(FailureType type, double delay_ms)
        {
            if (type == FailureType::None)
            {
                return {FailureType::None, false};
            }

            FailureResult result{
                .type = type,
                .is_terminal = type != FailureType::ArtificialDelay,
            };

            if (type == FailureType::ArtificialDelay)
            {
                result.delay_ms = delay_ms;
            }

            return result;
        }
    } // namespace

    FailureInjector::FailureInjector(const FailureConfig &cfg) : config_(cfg) {}

    FailureResult FailureInjector::inject(const Packet &pkt, std::uint64_t)
    {
        if (!config_.enabled)
            return {FailureType::None, false};

        for (const auto &rule : config_.lifecycle_rules)
        {
            if (rule.lifecycle_id == pkt.lifecycle_id)
            {
                return makeFailureResult(rule.type, config_.delay_ms);
            }
        }

        if (config_.every_n_packets == 0)
            return {FailureType::None, false};

        if (config_.type == FailureType::None)
            return {FailureType::None, false};

        ++seen_packets_;

        if (seen_packets_ % config_.every_n_packets != 0)
            return {FailureType::None, false};

        return makeFailureResult(config_.type, config_.delay_ms);
    }
} // namespace edgenetswitch::failure
