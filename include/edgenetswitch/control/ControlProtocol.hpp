#pragma once

#include <string>

namespace edgenetswitch::control
{
    inline bool isValidProtocolVersion(const std::string &v)
    {
        return v == "1.2";
    }

    inline bool isWellFormedVersion(const std::string &v)
    {
        return v.size() == 3 && std::isdigit(v[0]) && v[1] == '.' && std::isdigit(v[2]);
    }

    // Request sent from CLI (or any external tool) to the daemon
    struct ControlRequest
    {
        std::string version;
        std::string command;
    };

    // Response sent back by the daemon
    struct ControlResponse
    {
        bool success{false};
        std::string payload;
        std::string error_code;
        std::string message;
    };

} // namespace edgenetswitch::control