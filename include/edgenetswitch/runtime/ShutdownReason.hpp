#pragma once

namespace edgenetswitch
{
    enum class ShutdownReason
    {
        Unknown,
        ControlPlaneRequest,
        SignalInterrupt,
        SignalTerminate
    };
} // namespace edgenetswitch