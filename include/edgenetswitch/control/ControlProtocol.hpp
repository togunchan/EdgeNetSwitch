#pragma once

#include <string>

namespace edgenetswitch::control
{

    // Request sent from CLI (or any external tool) to the daemon
    struct ControlRequest
    {
        std::string version;
        std::string command;
    };

    // Response sent back by the daemon
    struct ControlResponse
    {
        bool success = false;
        std::string payload;
        std::string error;
    };

} // namespace edgenetswitch::control