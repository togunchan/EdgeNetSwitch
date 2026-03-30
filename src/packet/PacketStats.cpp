#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"

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

    PacketMetrics PacketStats::snapshot() const
    {

        auto current_packets = rx_packets_.load(std::memory_order_relaxed);
        auto current_bytes = rx_bytes_.load(std::memory_order_relaxed);

        auto now = nowMs();

        if (prev_timestamp_ms_ == 0)
        {
            prev_timestamp_ms_ = now;
            prev_rx_packets_ = current_packets;
            prev_rx_bytes_ = current_bytes;

            return PacketMetrics{
                .rx_packets = current_packets,
                .rx_bytes = current_bytes,
                .drops_parse_error = drops_parse_error_.load(std::memory_order_relaxed),
                .drops_validation = drops_validation_.load(std::memory_order_relaxed),
                .rx_packets_per_sec = 0,
                .rx_bytes_per_sec = 0};
        }

        std::uint64_t delta_time = now - prev_timestamp_ms_;
        std::uint64_t delta_packets = current_packets - prev_rx_packets_;
        std::uint64_t delta_bytes = current_bytes - prev_rx_bytes_;

        std::uint64_t packets_per_sec = 0;
        std::uint64_t bytes_per_sec = 0;

        if (delta_time > 0)
        {
            packets_per_sec = (delta_packets * 1000) / delta_time;
            bytes_per_sec = (delta_bytes * 1000) / delta_time;
        }

        // NOTE:
        // prev_* fields are mutable because snapshot() is logically const.
        // They are used only for rate calculation and are not part of observable state.
        // snapshot() is expected to be called from a single thread.
        prev_rx_packets_ = current_packets;
        prev_rx_bytes_ = current_bytes;
        prev_timestamp_ms_ = now;

        return PacketMetrics{
            .rx_packets = current_packets,
            .rx_bytes = current_bytes,
            .drops_parse_error = drops_parse_error_.load(std::memory_order_relaxed),
            .drops_validation = drops_validation_.load(std::memory_order_relaxed),
            .rx_packets_per_sec = packets_per_sec,
            .rx_bytes_per_sec = bytes_per_sec};
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
