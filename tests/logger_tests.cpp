#include <catch2/catch_all.hpp>
#include "edgenetswitch/Logger.hpp"
#include <filesystem>

TEST_CASE("Logger writes basic messages", "[logger]")
{
    const std::string path = "logger_test_output.log";
    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path);
    }

    Logger::init(LogLevel::Debug, path);

    Logger::debug("debug msg");
    Logger::info("info msg");
    Logger::warn("warn msg");
    Logger::error("error msg");

    Logger::shutdown();

    REQUIRE(std::filesystem::exists(path));

    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    REQUIRE(content.find("debug msg") != std::string::npos);
    REQUIRE(content.find("info msg") != std::string::npos);
    REQUIRE(content.find("warn msg") != std::string::npos);
    REQUIRE(content.find("error msg") != std::string::npos);
}