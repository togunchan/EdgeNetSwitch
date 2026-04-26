#include "edgenetswitch/failure/FailureInjector.hpp"

namespace edgenetswitch::failure
{
    FailureInjector::FailureInjector(const FailureConfig &cfg) : config_(cfg) {}

    FailureResult FailureInjector::inject(const Packet &, std::uint64_t)
    {
        return {FailureType::None, false};
    }
} // namespace edgenetswitch::failure
