#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "edgenetswitch/telemetry/WindowedEwmaRateSmoother.hpp"

using namespace edgenetswitch;

namespace
{
double variance(const std::vector<std::uint64_t> &samples)
{
    if (samples.empty())
    {
        return 0.0;
    }

    double mean = 0.0;
    for (const auto value : samples)
    {
        mean += static_cast<double>(value);
    }
    mean /= static_cast<double>(samples.size());

    double squared_error_sum = 0.0;
    for (const auto value : samples)
    {
        const double diff = static_cast<double>(value) - mean;
        squared_error_sum += diff * diff;
    }

    return squared_error_sum / static_cast<double>(samples.size());
}
} // namespace

TEST_CASE("WindowedEwmaRateSmoother convergence under stable traffic", "[RateSmoother][EWMA]")
{
    GIVEN("a smoother with 1-second window and near-stable counter increments")
    {
        WindowedEwmaRateSmoother smoother(RateSmootherConfig{.alpha = 0.2, .window_ms = 1000});
        std::uint64_t counter = 0;
        std::uint64_t now_ms = 1'000'000;

        const std::array<std::uint64_t, 12> increments = {
            19, 21, 20, 22, 18, 20, 21, 19, 20, 20, 21, 20};

        WHEN("samples are observed every 1000 ms")
        {
            smoother.observe(counter, now_ms);

            std::vector<std::uint64_t> raw_samples;
            std::vector<std::uint64_t> smoothed_samples;
            raw_samples.reserve(increments.size());
            smoothed_samples.reserve(increments.size());

            for (const auto step : increments)
            {
                counter += step;
                now_ms += 1000;
                smoother.observe(counter, now_ms);
                const RateSnapshot snapshot = smoother.snapshot();

                REQUIRE(snapshot.valid);
                raw_samples.push_back(snapshot.raw_per_sec);
                smoothed_samples.push_back(snapshot.smoothed_per_sec);
            }

            THEN("EWMA converges near target and has lower variance than raw samples")
            {
                const auto [raw_min_it, raw_max_it] = std::minmax_element(raw_samples.begin(), raw_samples.end());
                REQUIRE(*raw_max_it > *raw_min_it);

                const double raw_var = variance(raw_samples);
                const double smooth_var = variance(smoothed_samples);

                REQUIRE(raw_var > 0.0);
                REQUIRE(smooth_var < raw_var);

                const std::uint64_t ewma_final = smoothed_samples.back();
                REQUIRE(ewma_final >= 18);
                REQUIRE(ewma_final <= 22);
            }
        }
    }
}

TEST_CASE("WindowedEwmaRateSmoother smooths burst traffic", "[RateSmoother][EWMA]")
{
    GIVEN("a warmed-up smoother and alternating high/low counter bursts")
    {
        WindowedEwmaRateSmoother smoother(RateSmootherConfig{.alpha = 0.2, .window_ms = 1000});
        std::uint64_t counter = 0;
        std::uint64_t now_ms = 2'000'000;

        smoother.observe(counter, now_ms);
        for (int i = 0; i < 4; ++i)
        {
            counter += 20;
            now_ms += 1000;
            smoother.observe(counter, now_ms);
            REQUIRE(smoother.snapshot().valid);
        }

        const std::array<std::uint64_t, 12> burst_steps = {
            60, 5, 60, 5, 60, 5, 60, 5, 60, 5, 60, 5};

        WHEN("burst samples are observed on each window boundary")
        {
            std::vector<std::uint64_t> raw_samples;
            std::vector<std::uint64_t> smoothed_samples;
            raw_samples.reserve(burst_steps.size());
            smoothed_samples.reserve(burst_steps.size());

            for (const auto step : burst_steps)
            {
                counter += step;
                now_ms += 1000;
                smoother.observe(counter, now_ms);
                const RateSnapshot snapshot = smoother.snapshot();

                REQUIRE(snapshot.valid);
                raw_samples.push_back(snapshot.raw_per_sec);
                smoothed_samples.push_back(snapshot.smoothed_per_sec);
            }

            THEN("raw peaks and troughs exceed EWMA peaks and troughs while EWMA stays bounded")
            {
                const auto [raw_min_it, raw_max_it] = std::minmax_element(raw_samples.begin(), raw_samples.end());
                const auto [smooth_min_it, smooth_max_it] = std::minmax_element(smoothed_samples.begin(), smoothed_samples.end());

                REQUIRE(*raw_max_it > *smooth_max_it);
                REQUIRE(*raw_min_it < *smooth_min_it);

                for (const auto smoothed : smoothed_samples)
                {
                    REQUIRE(smoothed >= *raw_min_it);
                    REQUIRE(smoothed <= *raw_max_it);
                }
            }
        }
    }
}

TEST_CASE("WindowedEwmaRateSmoother honors minimum window", "[RateSmoother][Window]")
{
    GIVEN("a smoother with 1000 ms gate")
    {
        WindowedEwmaRateSmoother smoother(RateSmootherConfig{.alpha = 0.2, .window_ms = 1000});

        WHEN("delta time remains below the configured window")
        {
            smoother.observe(100, 1'000);
            smoother.observe(120, 1'500);
            const RateSnapshot snapshot = smoother.snapshot();

            THEN("snapshot remains invalid")
            {
                REQUIRE_FALSE(snapshot.valid);
                REQUIRE(snapshot.raw_per_sec == 0);
                REQUIRE(snapshot.smoothed_per_sec == 0);
            }
        }
    }
}

TEST_CASE("WindowedEwmaRateSmoother reset on counter drop", "[RateSmoother][Reset]")
{
    GIVEN("a smoother that has already produced a valid rate")
    {
        WindowedEwmaRateSmoother smoother(RateSmootherConfig{.alpha = 0.2, .window_ms = 1000});
        smoother.observe(100, 1'000);
        smoother.observe(140, 2'000);
        REQUIRE(smoother.snapshot().valid);

        WHEN("the observed counter drops")
        {
            smoother.observe(10, 3'000);
            const RateSnapshot snapshot = smoother.snapshot();

            THEN("the smoother resets and snapshot becomes invalid")
            {
                REQUIRE_FALSE(snapshot.valid);
                REQUIRE(snapshot.raw_per_sec == 0);
                REQUIRE(snapshot.smoothed_per_sec == 0);
            }
        }
    }
}

TEST_CASE("WindowedEwmaRateSmoother first sample stays invalid", "[RateSmoother][FirstSample]")
{
    GIVEN("a new smoother instance")
    {
        WindowedEwmaRateSmoother smoother(RateSmootherConfig{.alpha = 0.2, .window_ms = 1000});

        WHEN("only the first observation is provided")
        {
            smoother.observe(42, 1'000);
            const RateSnapshot snapshot = smoother.snapshot();

            THEN("no valid rate is produced yet")
            {
                REQUIRE_FALSE(snapshot.valid);
                REQUIRE(snapshot.raw_per_sec == 0);
                REQUIRE(snapshot.smoothed_per_sec == 0);
            }
        }
    }
}

TEST_CASE("WindowedEwmaRateSmoother computes basic known rate", "[RateSmoother][Basic]")
{
    GIVEN("a deterministic delta counter and delta time pair")
    {
        WindowedEwmaRateSmoother smoother(RateSmootherConfig{.alpha = 0.2, .window_ms = 1000});

        WHEN("the second observation spans 200 increments over 2000 ms")
        {
            smoother.observe(500, 10'000);
            smoother.observe(700, 12'000);
            const RateSnapshot snapshot = smoother.snapshot();

            THEN("raw and first EWMA value are exactly 100 per second")
            {
                REQUIRE(snapshot.valid);
                REQUIRE(snapshot.raw_per_sec == 100);
                REQUIRE(snapshot.smoothed_per_sec == 100);
            }
        }
    }
}
