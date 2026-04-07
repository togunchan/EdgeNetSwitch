#pragma once

#include <string>
#include <cstdint>

namespace edgenetswitch::core
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

    struct RateConfig
    {
        double alpha{0.2};
        std::uint64_t window_ms{1000};
    };

    struct Config
    {
        LogConfig log;
        DaemonConfig daemon;
        UdpConfig udp;
        RateConfig rate;
    };

    class ConfigLoader
    {
    public:
        static Config loadFromFile(const std::string &path);
    };

} // namespace edgenetswitch