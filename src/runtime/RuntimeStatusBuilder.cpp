#include "RuntimeStatusBuilder.hpp"

#include "edgenetswitch/runtime/HealthMonitor.hpp"
#include "edgenetswitch/telemetry/Telemetry.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"

namespace edgenetswitch
{
    RuntimeStatusBuilder::RuntimeStatusBuilder()
        : RuntimeStatusBuilder(RateSmootherConfig{})
    {
    }

    RuntimeStatusBuilder::RuntimeStatusBuilder(const RateSmootherConfig &cfg)
        : rx_packet_rate_(cfg),
          rx_bytes_rate_(cfg)
    {
    }

    RuntimeStatus RuntimeStatusBuilder::build(
        const Telemetry &telemetry,
        const HealthMonitor &healthMonitor,
        const PacketStats &packetStats,
        RuntimeState state,
        std::uint64_t now_ms)
    {
        const PacketMetrics raw_metrics = packetStats.snapshotAt(now_ms);

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

        return RuntimeStatus{
            .metrics = telemetry.snapshot(),
            .health = healthMonitor.currentStatus(),
            .state = state,
            .snapshot_timestamp_ms = now_ms,
            .packet = final_metrics};
    }
} // namespace edgenetswitch
