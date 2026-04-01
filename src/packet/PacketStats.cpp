#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"
#include <cmath>

namespace edgenetswitch
{
    PacketStats::PacketStats(MessagingBus &bus)
    {
        bus.subscribe(MessageType::PacketProcessed, [this](const Message &msg)
                      {
                          const Packet &p = std::get<Packet>(msg.payload);

                          rx_packets_.fetch_add(1, std::memory_order_relaxed);
                          rx_bytes_.fetch_add(p.payload_size, std::memory_order_relaxed); });

        bus.subscribe(MessageType::PacketDropped, [this](const Message &msg)
                      {
            auto reason = std::get<PacketDropReason>(msg.payload);

            switch (reason)
            {
            case PacketDropReason::ParseError:
                incrementParseError();
                break;

            case PacketDropReason::ValidationError:
                incrementValidationError();
                break;
            } });
    }

    void PacketStats::updateRates(std::uint64_t now_ms) const
    {
        const std::uint64_t current_packets = rx_packets_.load(std::memory_order_relaxed);
        const std::uint64_t current_bytes = rx_bytes_.load(std::memory_order_relaxed);

        if (current_packets < prev_rx_packets_ || current_bytes < prev_rx_bytes_)
        {
            // Counter reset scenario
            prev_rx_packets_ = current_packets;
            prev_rx_bytes_ = current_bytes;
            prev_timestamp_ms_ = now_ms;

            smoothed_packets_per_sec_ = 0.0;
            smoothed_bytes_per_sec_ = 0.0;

            last_smoothed_rx_packets_per_sec_ = 0;
            last_smoothed_rx_bytes_per_sec_ = 0;
            last_rx_packets_per_sec_raw_ = 0;
            last_rx_bytes_per_sec_raw_ = 0;

            return;
        }

        if (prev_timestamp_ms_ == 0)
        {
            prev_timestamp_ms_ = now_ms;
            prev_rx_packets_ = current_packets;
            prev_rx_bytes_ = current_bytes;
            return;
        }

        const std::uint64_t delta_time = now_ms - prev_timestamp_ms_;
        if (delta_time < 1000)
        {
            return;
        }

        const std::uint64_t delta_packets = current_packets - prev_rx_packets_;
        const std::uint64_t delta_bytes = current_bytes - prev_rx_bytes_;

        const double instant_packets_per_sec =
            (static_cast<double>(delta_packets) * 1000.0) / static_cast<double>(delta_time);
        const double instant_bytes_per_sec =
            (static_cast<double>(delta_bytes) * 1000.0) / static_cast<double>(delta_time);

        last_rx_packets_per_sec_raw_ =
            static_cast<std::uint64_t>(std::llround(instant_packets_per_sec));
        last_rx_bytes_per_sec_raw_ =
            static_cast<std::uint64_t>(std::llround(instant_bytes_per_sec));

        // NOTE:
        // EWMA smoothing factor (alpha) is currently hardcoded.
        // In production systems, this should be configurable
        constexpr double alpha = 0.2;
        smoothed_packets_per_sec_ =
            (alpha * instant_packets_per_sec) + ((1.0 - alpha) * smoothed_packets_per_sec_);
        smoothed_bytes_per_sec_ =
            (alpha * instant_bytes_per_sec) + ((1.0 - alpha) * smoothed_bytes_per_sec_);

        last_smoothed_rx_packets_per_sec_ =
            static_cast<std::uint64_t>(std::llround(smoothed_packets_per_sec_));
        last_smoothed_rx_bytes_per_sec_ =
            static_cast<std::uint64_t>(std::llround(smoothed_bytes_per_sec_));

        prev_rx_packets_ = current_packets;
        prev_rx_bytes_ = current_bytes;
        prev_timestamp_ms_ = now_ms;
    }

    PacketMetrics PacketStats::snapshot() const
    {
        return snapshotAt(nowMs());
    }

    PacketMetrics PacketStats::snapshotAt(std::uint64_t now_ms) const
    {
        updateRates(now_ms);

        const std::uint64_t current_packets = rx_packets_.load(std::memory_order_relaxed);
        const std::uint64_t current_bytes = rx_bytes_.load(std::memory_order_relaxed);
        const std::uint64_t drops_parse_error =
            drops_parse_error_.load(std::memory_order_relaxed);
        const std::uint64_t drops_validation =
            drops_validation_.load(std::memory_order_relaxed);

        return PacketMetrics{
            .rx_packets = current_packets,
            .rx_bytes = current_bytes,
            .rx_packets_per_sec = last_smoothed_rx_packets_per_sec_,
            .rx_bytes_per_sec = last_smoothed_rx_bytes_per_sec_,
            .drops_parse_error = drops_parse_error,
            .drops_validation = drops_validation,
            .rx_packets_per_sec_raw = last_rx_packets_per_sec_raw_,
            .rx_bytes_per_sec_raw = last_rx_bytes_per_sec_raw_};
    }

    std::uint64_t PacketStats::rxPackets() const
    {
        return rx_packets_.load(std::memory_order_relaxed);
    }

    std::uint64_t PacketStats::rxBytes() const
    {
        return rx_bytes_.load(std::memory_order_relaxed);
    }

    std::uint64_t PacketStats::drops() const
    {
        return drops_parse_error_.load(std::memory_order_relaxed) +
               drops_validation_.load(std::memory_order_relaxed);
    }

    void PacketStats::incrementParseError()
    {
        drops_parse_error_.fetch_add(1, std::memory_order_relaxed);
    }

    void PacketStats::incrementValidationError()
    {
        drops_validation_.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace edgenetswitch
