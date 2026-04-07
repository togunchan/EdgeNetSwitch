#pragma once

#include <cstdint>

namespace edgenetswitch
{
    struct RateSmootherConfig
    {
        double alpha{0.2};
        std::uint64_t window_ms{1000};
    };

    struct RateSnapshot
    {
        bool valid{false};
        std::uint64_t raw_per_sec{0};
        std::uint64_t smoothed_per_sec{0};
    };

    inline RateSmootherConfig toSmootherConfig(const RateConfig &cfg)
    {
        return {
            .alpha = cfg.alpha,
            .window_ms = cfg.window_ms};
    }

} // namespace edgenetswitch