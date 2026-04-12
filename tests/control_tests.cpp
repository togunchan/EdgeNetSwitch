#include <catch2/catch_test_macros.hpp>

#include "../src/control/ControlContext.hpp"
#include "../src/control/ControlDispatch.hpp"
#include "../src/runtime/SnapshotPublisher.hpp"

#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/control/ControlProtocol.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace
{
    using edgenetswitch::RuntimeState;
    using edgenetswitch::RuntimeStatus;
    using edgenetswitch::control::ControlContext;
    using edgenetswitch::control::ControlRequest;
    using edgenetswitch::control::ControlResponse;
    using edgenetswitch::control::dispatchControlRequest;

    RuntimeStatus makeDeterministicStatus()
    {
        RuntimeStatus s{};
        s.state = RuntimeState::Running;
        s.metrics.uptime_ms = 123456;
        s.metrics.tick_count = 789;
        s.health.is_alive = true;
        s.health.silence_duration_ms = 42;
        s.health.last_heartbeat_ms = 123400;
        s.snapshot_timestamp_ms = 1700000000123ULL;
        s.packet.rx_packets = 101;
        s.packet.rx_bytes = 202;
        s.packet.drops_by_reason[edgenetswitch::PacketDropReason::ParseError] = 3;
        s.packet.drops_by_reason[edgenetswitch::PacketDropReason::ValidationError] = 4;
        s.packet.rx_packets_per_sec = 55;
        s.packet.rx_bytes_per_sec = 66;
        s.packet.rx_packets_per_sec_raw = 77;
        s.packet.rx_bytes_per_sec_raw = 88;
        return s;
    }

    edgenetswitch::core::Config makeDeterministicConfig()
    {
        edgenetswitch::core::Config cfg{};
        cfg.log.level = "warn";
        cfg.log.file = "control-test.log";
        cfg.daemon.tick_ms = 250;
        cfg.udp.enabled = true;
        cfg.udp.port = 9200;
        cfg.rate.alpha = 0.5;
        cfg.rate.window_ms = 4000;
        return cfg;
    }

    class FakeSnapshotPublisher final
    {
    public:
        explicit FakeSnapshotPublisher(bool publish_snapshot)
        {
            if (publish_snapshot)
            {
                RuntimeStatus status = makeDeterministicStatus();
                publisher_.publish(status);
            }
        }

        const edgenetswitch::daemon::SnapshotPublisher *ptr() const
        {
            return &publisher_;
        }

    private:
        edgenetswitch::daemon::SnapshotPublisher publisher_;
    };

    ControlResponse dispatch(
        const std::string &command,
        const ControlContext &ctx,
        const std::string &version = "1.2")
    {
        return dispatchControlRequest(
            ControlRequest{
                .version = version,
                .command = command,
            },
            ctx);
    }

    bool contains(const std::string &text, const std::string &token)
    {
        return text.find(token) != std::string::npos;
    }

} // namespace

TEST_CASE("Control dispatch routes commands and validates request envelope", "[control][dispatch]")
{
    FakeSnapshotPublisher publisher(true);
    const auto cfg = makeDeterministicConfig();
    const ControlContext ctx{
        .publisher = publisher.ptr(),
        .config = &cfg,
    };

    SECTION("valid command returns success")
    {
        const auto resp = dispatch("status", ctx);
        REQUIRE(resp.success);
        CHECK(resp.error_code.empty());
    }

    SECTION("invalid command returns error")
    {
        const auto resp = dispatch("definitely-not-a-command", ctx);
        REQUIRE_FALSE(resp.success);
        CHECK(resp.error_code == edgenetswitch::control::error::UnknownCommand);
    }

    SECTION("missing version returns error")
    {
        const auto resp = dispatch("status", ctx, "");
        REQUIRE_FALSE(resp.success);
        CHECK(resp.error_code == edgenetswitch::control::error::InvalidRequest);
    }

    SECTION("unsupported version returns error")
    {
        const auto resp = dispatch("status", ctx, "1.3");
        REQUIRE_FALSE(resp.success);
        CHECK(resp.error_code == edgenetswitch::control::error::UnsupportedVersion);
    }
}

TEST_CASE("JSON mode returns current command payloads", "[control][json]")
{
    FakeSnapshotPublisher publisher(true);
    const auto cfg = makeDeterministicConfig();
    const ControlContext ctx{
        .publisher = publisher.ptr(),
        .config = &cfg,
    };

    SECTION("status json")
    {
        const auto resp = dispatch("status:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);
        REQUIRE(j["status"] == "ok");
        CHECK(j["data"].contains("state"));
        CHECK(j["data"].contains("uptime_ms"));
        CHECK(j["data"].contains("tick_count"));
        CHECK(j["data"].contains("snapshot_version"));
        CHECK(j["data"].contains("snapshot_timestamp_ms"));
    }

    SECTION("health json")
    {
        const auto resp = dispatch("health:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);
        REQUIRE(j["status"] == "ok");
        CHECK(j["data"].contains("alive"));
        CHECK(j["data"].contains("silence_ms"));
        CHECK(j["data"].contains("last_heartbeat_ms"));
    }

    SECTION("metrics json")
    {
        const auto resp = dispatch("metrics:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);
        REQUIRE(j["status"] == "ok");
        CHECK(j["data"].contains("uptime_ms"));
        CHECK(j["data"].contains("tick_count"));
    }

    SECTION("version json")
    {
        const auto resp = dispatch("version:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);
        REQUIRE(j["status"] == "ok");
        CHECK(j["data"].contains("version"));
        CHECK(j["data"].contains("protocol"));
        CHECK(j["data"].contains("build"));
    }

    SECTION("help json")
    {
        const auto resp = dispatch("help:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);
        REQUIRE(j["status"] == "ok");
        REQUIRE(j["data"].contains("commands"));
        CHECK(j["data"]["commands"].contains("status"));
        CHECK(j["data"]["commands"].contains("health"));
        CHECK(j["data"]["commands"].contains("metrics"));
        CHECK(j["data"]["commands"].contains("version"));
        CHECK(j["data"]["commands"].contains("help"));
        CHECK(j["data"]["commands"].contains("packet-stats"));
        CHECK(j["data"]["commands"].contains("show-config"));
    }

    SECTION("packet-stats json")
    {
        const auto resp = dispatch("packet-stats:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);
        REQUIRE(j["status"] == "ok");
        CHECK(j["data"].contains("rx_packets"));
        CHECK(j["data"].contains("rx_bytes"));
        REQUIRE(j["data"].contains("drops"));
        REQUIRE(j["data"]["drops"].contains("parse_error"));
        REQUIRE(j["data"]["drops"].contains("validation_error"));
        CHECK(j["data"].contains("rx_packets_per_sec"));
        CHECK(j["data"].contains("rx_bytes_per_sec"));
        CHECK(j["data"].contains("rx_packets_per_sec_raw"));
        CHECK(j["data"].contains("rx_bytes_per_sec_raw"));
    }

    SECTION("show-config json")
    {
        const auto resp = dispatch("show-config:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);
        REQUIRE(j["status"] == "ok");
        CHECK(j["data"].contains("log"));
        CHECK(j["data"].contains("daemon"));
        CHECK(j["data"].contains("udp"));
        CHECK(j["data"].contains("rate"));
    }
}

TEST_CASE("Text mode exposes key=value output", "[control][text]")
{
    FakeSnapshotPublisher publisher(true);
    const auto cfg = makeDeterministicConfig();
    const ControlContext ctx{
        .publisher = publisher.ptr(),
        .config = &cfg,
    };

    struct TextCase
    {
        std::string command;
        std::vector<std::string> expected_tokens;
    };

    const std::vector<TextCase> cases = {
        {"status", {"state=", "uptime_ms=", "tick_count="}},
        {"health", {"alive=", "silence_ms=", "last_heartbeat_ms="}},
        {"metrics", {"uptime_ms=", "tick_count="}},
        {"version", {"version=", "protocol=", "build="}},
        {"help:version", {"command=", "description="}},
        {"packet-stats", {"rx_packets=", "rx_bytes=", "drops_total="}},
        {"show-config", {"log.level=", "daemon.tick_ms=", "udp.port=", "rate.window_ms="}},
    };

    for (const auto &tc : cases)
    {
        INFO("command=" << tc.command);
        const auto resp = dispatch(tc.command, ctx);
        REQUIRE(resp.success);
        CHECK(contains(resp.payload, "="));
        for (const auto &token : tc.expected_tokens)
        {
            CHECK(contains(resp.payload, token));
        }
    }
}

TEST_CASE("Snapshot error paths return unsuccessful response and error code", "[control][errors]")
{
    const auto cfg = makeDeterministicConfig();

    SECTION("null publisher")
    {
        const ControlContext ctx{
            .publisher = nullptr,
            .config = &cfg,
        };

        const auto resp = dispatch("status", ctx);
        REQUIRE_FALSE(resp.success);
        CHECK_FALSE(resp.error_code.empty());
        CHECK(resp.error_code == edgenetswitch::control::error::InternalError);
    }

    SECTION("missing snapshot")
    {
        FakeSnapshotPublisher publisher(false);
        const ControlContext ctx{
            .publisher = publisher.ptr(),
            .config = &cfg,
        };

        const auto resp = dispatch("status", ctx);
        REQUIRE_FALSE(resp.success);
        CHECK_FALSE(resp.error_code.empty());
        CHECK(resp.error_code == edgenetswitch::control::error::InternalError);
    }
}

TEST_CASE("show-config exposes configured fields in text and json modes", "[control][show-config]")
{
    const auto cfg = makeDeterministicConfig();
    const ControlContext ctx{
        .publisher = nullptr,
        .config = &cfg,
    };

    SECTION("text output contains config field keys")
    {
        const auto resp = dispatch("show-config", ctx);
        REQUIRE(resp.success);
        CHECK(contains(resp.payload, "log.level="));
        CHECK(contains(resp.payload, "log.file="));
        CHECK(contains(resp.payload, "daemon.tick_ms="));
        CHECK(contains(resp.payload, "udp.enabled="));
        CHECK(contains(resp.payload, "udp.port="));
        CHECK(contains(resp.payload, "rate.alpha="));
        CHECK(contains(resp.payload, "rate.window_ms="));
    }

    SECTION("json output contains config fields")
    {
        const auto resp = dispatch("show-config:json", ctx);
        REQUIRE(resp.success);
        const auto j = nlohmann::json::parse(resp.payload);

        REQUIRE(j["status"] == "ok");
        CHECK(j["data"]["log"].contains("level"));
        CHECK(j["data"]["log"].contains("file"));
        CHECK(j["data"]["daemon"].contains("tick_ms"));
        CHECK(j["data"]["udp"].contains("enabled"));
        CHECK(j["data"]["udp"].contains("port"));
        CHECK(j["data"]["rate"].contains("alpha"));
        CHECK(j["data"]["rate"].contains("window_ms"));
    }
}
