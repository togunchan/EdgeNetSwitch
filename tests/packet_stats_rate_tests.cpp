#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>
#include <algorithm>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"

using namespace edgenetswitch;

namespace
{
    constexpr std::uint64_t BASE_TIME_MS = 1'000'000;
    constexpr std::uint64_t RATE_WINDOW_MS = 1'000;
    constexpr std::uint32_t DEFAULT_PAYLOAD_SIZE = 64;

    class PacketTrafficSimulator
    {
    public:
        explicit PacketTrafficSimulator(MessagingBus &bus) : bus_(bus) {}

        void emitProcessed(std::uint64_t packet_count, std::uint32_t payload_size = DEFAULT_PAYLOAD_SIZE)
        {
            for (std::uint64_t i = 0; i < packet_count; ++i)
            {
                Packet packet{};
                packet.id = next_packet_id_++;
                packet.payload_size = payload_size;
                packet.valid = true;

                Message msg{};
                msg.type = MessageType::PacketProcessed;
                msg.timestamp_ms = packet.id;
                msg.payload = packet;

                bus_.publish(msg);
            }
        }

    private:
        MessagingBus &bus_;
        std::uint64_t next_packet_id_{1};
    };

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

TEST_CASE("PacketStats EWMA converges under stable traffic", "[PacketStats][Rates][EWMA]")
{
    MessagingBus bus;
    PacketStats stats(bus);
    PacketTrafficSimulator traffic(bus);

    std::uint64_t now_ms = BASE_TIME_MS;
    (void)stats.snapshotAt(now_ms);

    for (int i = 0; i < 8; ++i)
    {
        traffic.emitProcessed(20);
        now_ms += RATE_WINDOW_MS;
        (void)stats.snapshotAt(now_ms);
    }

    const std::array<std::uint64_t, 12> per_second_packets = {
        19, 21, 20, 22, 18, 20, 21, 19, 20, 20, 21, 19};

    std::vector<std::uint64_t> raw_rates;
    std::vector<std::uint64_t> ewma_rates;
    raw_rates.reserve(per_second_packets.size());
    ewma_rates.reserve(per_second_packets.size());

    for (const auto packet_count : per_second_packets)
    {
        traffic.emitProcessed(packet_count);
        now_ms += RATE_WINDOW_MS;
        const PacketMetrics metrics = stats.snapshotAt(now_ms);

        raw_rates.push_back(metrics.rx_packets_per_sec_raw);
        ewma_rates.push_back(metrics.rx_packets_per_sec);
    }

    REQUIRE(raw_rates.size() == ewma_rates.size());

    const double raw_variance = variance(raw_rates);
    const double ewma_variance = variance(ewma_rates);

    REQUIRE(raw_variance > 0.0);
    REQUIRE(ewma_variance < raw_variance);

    const std::uint64_t target_rate = 20;
    const std::uint64_t tolerance = 2; // +-10%
    const std::uint64_t converged_ewma = ewma_rates.back();

    REQUIRE(converged_ewma >= (target_rate - tolerance));
    REQUIRE(converged_ewma <= (target_rate + tolerance));
}

TEST_CASE("PacketStats EWMA smooths burst traffic oscillation", "[PacketStats][Rates][EWMA]")
{
    MessagingBus bus;
    PacketStats stats(bus);
    PacketTrafficSimulator traffic(bus);

    std::uint64_t now_ms = BASE_TIME_MS;
    (void)stats.snapshotAt(now_ms);

    for (int i = 0; i < 5; ++i)
    {
        traffic.emitProcessed(20);
        now_ms += RATE_WINDOW_MS;
        (void)stats.snapshotAt(now_ms);
    }

    const std::array<std::uint64_t, 16> burst_pattern = {
        40, 4, 40, 4, 40, 4, 40, 4,
        40, 4, 40, 4, 40, 4, 40, 4};

    std::vector<std::uint64_t> raw_rates;
    std::vector<std::uint64_t> ewma_rates;
    raw_rates.reserve(burst_pattern.size());
    ewma_rates.reserve(burst_pattern.size());

    for (const auto packet_count : burst_pattern)
    {
        traffic.emitProcessed(packet_count);
        now_ms += RATE_WINDOW_MS;
        const PacketMetrics metrics = stats.snapshotAt(now_ms);

        raw_rates.push_back(metrics.rx_packets_per_sec_raw);
        ewma_rates.push_back(metrics.rx_packets_per_sec);
    }

    const auto [raw_min_it, raw_max_it] = std::minmax_element(raw_rates.begin(), raw_rates.end());
    const auto [ewma_min_it, ewma_max_it] = std::minmax_element(ewma_rates.begin(), ewma_rates.end());

    REQUIRE(*raw_max_it > *ewma_max_it);
    REQUIRE(*raw_min_it < *ewma_min_it);

    for (const auto ewma_rate : ewma_rates)
    {
        REQUIRE(ewma_rate >= *raw_min_it);
        REQUIRE(ewma_rate <= *raw_max_it);
    }
}

TEST_CASE("PacketStats restart reset yields safe zero rates", "[PacketStats][Rates][EWMA][Reset]")
{
    MessagingBus bus;
    PacketStats stats(bus);
    PacketTrafficSimulator traffic(bus);

    std::uint64_t now_ms = BASE_TIME_MS;
    (void)stats.snapshotAt(now_ms);

    traffic.emitProcessed(30);
    now_ms += RATE_WINDOW_MS;
    const PacketMetrics before_reset = stats.snapshotAt(now_ms);

    REQUIRE(before_reset.rx_packets_per_sec_raw > 0);
    REQUIRE(before_reset.rx_packets_per_sec > 0);

    MessagingBus restarted_bus;
    PacketStats restarted_stats(restarted_bus);

    now_ms += RATE_WINDOW_MS;
    const PacketMetrics after_reset_immediate = restarted_stats.snapshotAt(now_ms);

    REQUIRE(after_reset_immediate.rx_packets == 0);
    REQUIRE(after_reset_immediate.rx_packets_per_sec_raw == 0);
    REQUIRE(after_reset_immediate.rx_packets_per_sec == 0);

    now_ms += RATE_WINDOW_MS;
    const PacketMetrics after_reset_next = restarted_stats.snapshotAt(now_ms);

    REQUIRE(after_reset_next.rx_packets_per_sec_raw == 0);
    REQUIRE(after_reset_next.rx_packets_per_sec == 0);
}
