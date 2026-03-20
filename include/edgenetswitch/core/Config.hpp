#pragma once

#include <string>
#include <cstdint>

namespace edgenetswitch
{

    struct LogConfig
    {
        std::string level;
        std::string file;
    };

    struct DaemonConfig
    {
        std::uint32_t tick_ms{100};
    };

    struct UdpConfig
    {
        bool enabled{false};
        int port{9000};
    };

    struct Config
    {
        LogConfig log;
        DaemonConfig daemon;
        UdpConfig udp;
    };

    class ConfigLoader
    {
    public:
        static Config loadFromFile(const std::string &path);
    };

} // namespace edgenetswitch