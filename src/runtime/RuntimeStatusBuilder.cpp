#include "RuntimeStatusBuilder.hpp"

#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/telemetry/Telemetry.hpp"

namespace edgenetswitch
{
    RuntimeStatusBuilder::RuntimeStatusBuilder() : RuntimeStatusBuilder(RateSmootherConfig{}) {}

    RuntimeStatusBuilder::RuntimeStatusBuilder(const RateSmootherConfig &cfg)
        : rx_packet_rate_(cfg), rx_bytes_rate_(cfg)
    {
    }

    RuntimeStatus RuntimeStatusBuilder::build(const Telemetry &telemetry,
                                              const HealthMonitor &healthMonitor,
                                              const PacketStats &packetStats,
                                              const PacketProcessor &packetProcessor,
                                              RuntimeState state, std::uint64_t now_ms)
    {
        PacketMetrics raw_metrics = packetStats.snapshotAt(now_ms);

        raw_metrics.queue_depth = packetProcessor.queueDepth();
        raw_metrics.queue_capacity = packetProcessor.queueCapacity();
        raw_metrics.queue_high_water_mark = packetProcessor.queueHighWaterMark();

        std::uint64_t queue_overflow_count = 0;

        const auto overflow_it = raw_metrics.drops_by_reason.find(PacketDropReason::QueueOverflow);

        if (overflow_it != raw_metrics.drops_by_reason.end())
        {
            queue_overflow_count = overflow_it->second;
        }

        const bool new_queue_overflow = queue_overflow_count > last_queue_overflow_count_;

        last_queue_overflow_count_ = queue_overflow_count;

        const bool queue_saturated =
            raw_metrics.queue_capacity > 0 && raw_metrics.queue_depth >= raw_metrics.queue_capacity;

        // we compute rate here
        rx_packet_rate_.observe(raw_metrics.rx_packets, now_ms);
        rx_bytes_rate_.observe(raw_metrics.rx_bytes, now_ms);

        const RateSnapshot packet_rate = rx_packet_rate_.snapshot();
        const RateSnapshot byte_rate = rx_bytes_rate_.snapshot();

        PacketMetrics final_metrics = raw_metrics;

        if (packet_rate.valid)
        {
            final_metrics.rx_packets_per_sec = packet_rate.smoothed_per_sec;
            final_metrics.rx_packets_per_sec_raw = packet_rate.raw_per_sec;
        }
        else
        {
            final_metrics.rx_packets_per_sec = 0;
            final_metrics.rx_packets_per_sec_raw = 0;
        }

        if (byte_rate.valid)
        {
            final_metrics.rx_bytes_per_sec = byte_rate.smoothed_per_sec;
            final_metrics.rx_bytes_per_sec_raw = byte_rate.raw_per_sec;
        }
        else
        {
            final_metrics.rx_bytes_per_sec = 0;
            final_metrics.rx_bytes_per_sec_raw = 0;
        }

        const auto health_status = healthMonitor.currentStatus();

        OperationalHealth operational_health = OperationalHealth::Healthy;

        if (state == RuntimeState::Stopping)
        {
            operational_health = OperationalHealth::ShuttingDown;
        }
        else if (new_queue_overflow || queue_saturated)
        {
            operational_health = OperationalHealth::Overloaded;
        }
        else if (!health_status.is_alive)
        {
            operational_health = OperationalHealth::Degraded;
        }

        return RuntimeStatus{.metrics = telemetry.snapshot(),
                             .health = health_status,
                             .state = state,
                             .operational_health = operational_health,
                             .snapshot_timestamp_ms = now_ms,
                             .packet = final_metrics};
    }
} // namespace edgenetswitch
