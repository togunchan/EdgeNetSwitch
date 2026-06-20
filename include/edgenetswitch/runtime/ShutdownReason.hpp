#pragma once

#include <string_view>
namespace edgenetswitch
{
    enum class ShutdownReason
    {
        Unknown,
        ControlPlaneRequest,
        SignalInterrupt,
        SignalTerminate
    };

    std::string_view toString(ShutdownReason reason);
} // namespace edgenetswitch