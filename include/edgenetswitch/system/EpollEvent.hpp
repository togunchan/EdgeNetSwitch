#pragma once

#include <cstdint>

namespace edgenetswitch
{
    struct EpollEvent
    {
        int fd{-1};
        std::uint32_t events{0};
    };
} // namespace edgenetswitch