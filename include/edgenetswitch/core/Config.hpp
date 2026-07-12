#pragma once

#include <string>
#include <cstdint>
#include <vector>

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

    

    struct UdpIngressConfig
    {
        std::uint32_t switch_port{0};
        std::uint16_t listen_port{0};
    };

    struct UdpConfig
    {
        bool enabled{false};
        std::vector<UdpIngressConfig> endpoints;
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