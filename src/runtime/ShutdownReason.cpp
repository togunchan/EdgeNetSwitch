#include "edgenetswitch/runtime/ShutdownReason.hpp"

namespace edgenetswitch
{
    std::string_view toString(ShutdownReason reason)
    {
        switch (reason)
        {
        case ShutdownReason::ControlPlaneRequest:
            return "ControlPlaneRequest";
        case ShutdownReason::SignalInterrupt:
            return "SignalInterrupt";
        case ShutdownReason::SignalTerminate:
            return "SignalTerminate";
        case ShutdownReason::Unknown:
        default:
            return "Unknown";
        }
    }
} // namespace edgenetswitch