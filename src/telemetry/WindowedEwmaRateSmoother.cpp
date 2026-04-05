#include "edgenetswitch/telemetry/WindowedEwmaRateSmoother.hpp"
#include <cmath>

namespace edgenetswitch
{
    WindowedEwmaRateSmoother::WindowedEwmaRateSmoother(const RateSmootherConfig &config) : config_(config) { reset(); }

    void WindowedEwmaRateSmoother::reset()
    {
        prev_counter_ = 0;
        prev_time_ms_ = 0;
        has_prev_ = false;

        smoothed_ = 0.0;

        last_snapshot_ = {.valid = false, .raw_per_sec = 0, .smoothed_per_sec = 0};
    }

    void WindowedEwmaRateSmoother::observe(std::uint64_t counter, std::uint64_t now_ms)
    {
        if (!has_prev_)
        {
            prev_counter_ = counter;
            prev_time_ms_ = now_ms;
            has_prev_ = true;
            return;
        }

        // in case of counter reset
        if (counter < prev_counter_)
        {
            reset();
            prev_counter_ = counter;
            prev_time_ms_ = now_ms;
            has_prev_ = true;
            return;
        }

        const std::uint64_t delta_time = now_ms - prev_time_ms_;

        if (delta_time == 0)
            return;

        if (delta_time < config_.window_ms)
            return;
        

        const std::uint64_t delta_counter = counter - prev_counter_;

        // calculating rate
        const double instant =
            (static_cast<double>(delta_counter) * 1000.0) /
            static_cast<double>(delta_time);

        const std::uint64_t raw =
            static_cast<std::uint64_t>(std::llround(instant));

        // calculating EWMA
        if(!last_snapshot_.valid){
            // first valid value
            smoothed_ = instant;
        }
        else
        {
            smoothed_ =
                (config_.alpha * instant) +
                ((1.0 - config_.alpha) * smoothed_);
        }

        const std::uint64_t smoothed =
            static_cast<std::uint64_t>(std::llround(smoothed_));

        last_snapshot_ = {
            .valid = true,
            .raw_per_sec = raw,
            .smoothed_per_sec = smoothed};

        prev_counter_ = counter;
        prev_time_ms_ = now_ms;
    }

    RateSnapshot WindowedEwmaRateSmoother::snapshot() const
    {
        return last_snapshot_;
    }

} // namespace edgenetswitch
