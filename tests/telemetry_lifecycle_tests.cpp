#include "lifecycle_test_support.hpp"

#include "edgenetswitch/telemetry/InMemoryTelemetryExporter.hpp"
#include "edgenetswitch/telemetry/TelemetryExportManager.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace edgenetswitch;
using lifecycle_test::requireCleanExit;

namespace
{
    struct ExportedSamples
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::uint64_t> ticks;
    };

    class ThrowingExporter final : public TelemetryExporter
    {
    public:
        void exportSample(const RuntimeMetrics &) override
        {
            ++call_count;
            throw std::runtime_error("export failure");
        }

        int call_count = 0;
    };

    class SignallingExporter final : public TelemetryExporter
    {
    public:
        explicit SignallingExporter(ExportedSamples &samples,
                                    lifecycle_test::Gate *first_sample_gate = nullptr)
            : samples_(samples), first_sample_gate_(first_sample_gate)
        {
        }

        void exportSample(const RuntimeMetrics &sample) override
        {
            {
                std::lock_guard<std::mutex> lock(samples_.mutex);
                samples_.ticks.push_back(sample.tick_count);
            }
            samples_.cv.notify_all();
            if (first_sample_gate_ && sample.tick_count == 101)
                first_sample_gate_->arriveAndWait();
        }

    private:
        ExportedSamples &samples_;
        lifecycle_test::Gate *first_sample_gate_;
    };
} // namespace

TEST_CASE("TelemetryExportManager destruction of started manager requires safe worker cleanup",
          "[TelemetryExportManager][RuntimeLifecycle]")
{
    requireCleanExit(
        []
        {
            ExportedSamples samples;
            {
                telemetry::TelemetryExportManager manager;
                manager.addExporter(std::make_unique<SignallingExporter>(samples));
                manager.start();
                manager.enqueue(RuntimeMetrics{.tick_count = 101});
                bool started;
                {
                    std::unique_lock<std::mutex> lock(samples.mutex);
                    started = samples.cv.wait_for(lock, std::chrono::seconds(3),
                                                  [&] { return !samples.ticks.empty(); });
                }
                if (!started)
                {
                    manager.stop();
                    std::fprintf(stderr, "worker startup acknowledgement timed out\n");
                    return 2;
                }
                std::fprintf(stderr, "destroying started telemetry manager without stop\n");
                // Safe invariant: destruction joins the worker. Intentionally red:
                // the current default destructor destroys a joinable std::thread.
            }
            return 0;
        });
}

TEST_CASE("TelemetryExportManager enqueue after completed stop must not create stranded work",
          "[TelemetryExportManager][RuntimeLifecycle]")
{
    requireCleanExit(
        []
        {
            telemetry::TelemetryExportManager manager;
            auto exporter = std::make_unique<telemetry::InMemoryTelemetryExporter>();
            auto *recorded = exporter.get();
            manager.addExporter(std::move(exporter));
            manager.start();
            manager.stop();
            manager.enqueue(RuntimeMetrics{.uptime_ms = 9001, .tick_count = 7001});
            const auto after_enqueue = manager.queueSize();
            manager.stop();
            const auto after_second_stop = manager.queueSize();
            const auto delivered = recorded->snapshot();
            std::fprintf(stderr, "post-stop: queue=%zu; second stop: queue=%zu; delivered=%zu\n",
                         after_enqueue, after_second_stop, delivered.size());
            // Safe invariant: a completed stop cannot leave new, unserviceable work.
            // Intentionally red: enqueue retains tick 7001, and stop is now a no-op.
            return after_enqueue == 0 && after_second_stop == 0 ? 0 : 1;
        });
}

TEST_CASE("TelemetryExportManager retained samples drain before completed stop",
          "[TelemetryExportManager][RuntimeLifecycle]")
{
    requireCleanExit(
        []
        {
            ExportedSamples samples;
            lifecycle_test::Gate gate;
            telemetry::TelemetryExportManager manager;
            lifecycle_test::ReleaseGateOnExit release{gate};
            manager.addExporter(std::make_unique<SignallingExporter>(samples, &gate));
            manager.start();
            manager.enqueue(RuntimeMetrics{.tick_count = 101});
            const bool arrived = gate.waitForArrival();
            manager.enqueue(RuntimeMetrics{.tick_count = 102});
            manager.enqueue(RuntimeMetrics{.tick_count = 103});
            const auto retained = manager.queueSize();
            // Two samples are retained behind the in-flight first export. After release,
            // stop must join with all three delivered, regardless of which thread runs first.
            gate.release();
            manager.stop();
            std::lock_guard<std::mutex> lock(samples.mutex);
            const bool drained = manager.queueSize() == 0 &&
                                 samples.ticks == std::vector<std::uint64_t>{101, 102, 103};
            std::fprintf(stderr, "drain: gate=%d; retained=%zu; exported=%zu; queue=%zu\n", arrived,
                         retained, samples.ticks.size(), manager.queueSize());
            return arrived && retained == 2 && drained ? 0 : 1;
        });
}

TEST_CASE("TelemetryExportManager idle worker stop must complete",
          "[TelemetryExportManager][RuntimeLifecycle]")
{
    requireCleanExit(
        []
        {
            telemetry::TelemetryExportManager manager;
            manager.start();
            manager.stop();
            // Public API exposes neither the predicate check nor entry into wait.
            // This bounds empty-queue stop/join only; it cannot force lost wakeup.
            return manager.queueSize() == 0 ? 0 : 1;
        });
}

TEST_CASE("TelemetryExportManager worker processes later samples after an exporter throws",
          "[TelemetryExportManager][RuntimeLifecycle]")
{
    requireCleanExit(
        []
        {
            ExportedSamples samples;
            lifecycle_test::Gate gate;
            telemetry::TelemetryExportManager manager;
            lifecycle_test::ReleaseGateOnExit release{gate};
            auto throwing = std::make_unique<ThrowingExporter>();
            auto *throwing_ptr = throwing.get();
            manager.addExporter(std::move(throwing));
            manager.addExporter(std::make_unique<SignallingExporter>(samples, &gate));

            manager.start();
            manager.enqueue(RuntimeMetrics{.tick_count = 101});
            // The healthy exporter is registered after the throwing exporter. Its gate
            // acknowledges delivery of sample 1 before sample 2 can be admitted.
            const bool first_received = gate.waitForArrival();
            bool second_received = false;
            if (first_received)
            {
                manager.enqueue(RuntimeMetrics{.tick_count = 102});
                gate.release();
                std::unique_lock<std::mutex> lock(samples.mutex);
                // The predicate acknowledges actual delivery; the timeout only guards failure.
                second_received = samples.cv.wait_for(lock, std::chrono::seconds(3),
                                                      [&] { return samples.ticks.size() == 2; });
            }

            gate.release();
            manager.stop();
            // stop() has joined the worker, so exporter state is safe to inspect here.
            const bool drained = manager.queueSize() == 0 && manager.droppedCount() == 0 &&
                                 samples.ticks == std::vector<std::uint64_t>{101, 102};
            std::fprintf(
                stderr,
                "async exceptions: first=%d; second=%d; throws=%d; exported=%zu; queue=%zu\n",
                first_received, second_received, throwing_ptr->call_count, samples.ticks.size(),
                manager.queueSize());
            return first_received && second_received && throwing_ptr->call_count == 2 && drained
                       ? 0
                       : 1;
        });
}
