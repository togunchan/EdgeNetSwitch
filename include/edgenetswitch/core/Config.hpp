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

    struct Config
    {
        LogConfig log;
        DaemonConfig daemon;
    };

    class ConfigLoader
    {
    public:
        static Config loadFromFile(const std::string &path);
    };

} // namespace edgenetswitch