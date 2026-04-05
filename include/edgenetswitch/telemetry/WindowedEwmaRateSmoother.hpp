#pragma once

#include "edgenetswitch/telemetry/RateTypes.hpp"
#include <cstdint>

namespace edgenetswitch
{
    class WindowedEwmaRateSmoother
    {
        public:
        explicit WindowedEwmaRateSmoother(const RateSmootherConfig &config);

        void reset();

        void observe(std::uint64_t counter, std::uint64_t now_ms);

        RateSnapshot snapshot() const;

    private:
        RateSmootherConfig config_;

        std::uint64_t prev_counter_{0};
        std::uint64_t prev_time_ms_{0};
        bool has_prev_{false};

        double smoothed_{0.0};

        RateSnapshot last_snapshot_;
    };
} // namespace edgenetswitch
