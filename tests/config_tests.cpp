#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/Config.hpp"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <random>
#include <stdexcept>

using namespace edgenetswitch;
namespace fs = std::filesystem;

namespace
{

    // Helper: temporary directory with auto cleanup
    struct TempDir
    {
        fs::path path;

        TempDir()
        {
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uniform_int_distribution<std::uint64_t> dist;

            const fs::path base = fs::temp_directory_path();
            std::error_code ec;

            for (int i = 0; i < 16; ++i)
            {
                auto candidate = base / ("ens_cfg_" + std::to_string(dist(gen)));
                ec.clear();
                if (fs::create_directories(candidate, ec))
                {
                    path = candidate;
                    return;
                }
            }

            throw std::runtime_error("Failed to create temporary directory for tests");
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    void writeFile(const fs::path &p, const std::string &content)
    {
        std::ofstream ofs(p);
        REQUIRE(ofs.is_open());
        ofs << content;
    }

} // namespace

TEST_CASE("ConfigLoader loads full config correctly", "[Config]")
{
    TempDir tmp;
    fs::path cfgPath = tmp.path / "edgenetswitch.json";

    writeFile(cfgPath,
              R"({
            "log": {
                "level": "debug",
                "file": "custom.log"
            },
            "daemon": {
                "tick_ms": 250
            }
        })");

    Config cfg = ConfigLoader::loadFromFile(cfgPath.string());

    REQUIRE(cfg.log.level == "debug");
    REQUIRE(cfg.log.file == "custom.log");
    REQUIRE(cfg.daemon.tick_ms == 250);
}

TEST_CASE("ConfigLoader applies defaults when fields are missing", "[Config]")
{
    TempDir tmp;
    fs::path cfgPath = tmp.path / "edgenetswitch.json";

    writeFile(cfgPath,
              R"({
            "log": {}
        })");

    Config cfg = ConfigLoader::loadFromFile(cfgPath.string());

    REQUIRE(cfg.log.level == "info");             // default
    REQUIRE(cfg.log.file == "edgenetswitch.log"); // default
    REQUIRE(cfg.daemon.tick_ms == 100);           // default
}

TEST_CASE("ConfigLoader throws when config file does not exist", "[Config]")
{
    REQUIRE_THROWS_AS(
        ConfigLoader::loadFromFile("definitely_not_existing_config.json"),
        std::runtime_error);
}

TEST_CASE("ConfigLoader throws on malformed JSON", "[Config]")
{
    TempDir tmp;
    fs::path cfgPath = tmp.path / "broken.json";

    writeFile(cfgPath,
              R"({
            "log": {
                "level": "info",
        )" // malformed JSON
    );

    REQUIRE_THROWS_AS(
        ConfigLoader::loadFromFile(cfgPath.string()),
        std::runtime_error);
}
