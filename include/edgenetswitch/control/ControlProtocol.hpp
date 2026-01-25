#pragma once

#include <string>

namespace edgenetswitch::control
{
    // `inline` ensures ODR-safety when defined in headers.
    // `constexpr` guarantees compile-time availability.
    // `const char*` keeps the protocol representation explicit and lightweight.
    namespace error
    {
        inline constexpr const char *InvalidRequest = "invalid_request";
        inline constexpr const char *InvalidVersionFormat = "invalid_version_format";
        inline constexpr const char *UnsupportedVersion = "unsupported_version";
        inline constexpr const char *UnknownCommand = "unknown_command";
        inline constexpr const char *InternalError = "internal_error";
    } // namespace error

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