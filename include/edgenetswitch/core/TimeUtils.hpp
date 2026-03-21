#pragma once

#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

inline std::uint64_t nowMs()
{
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch())
            .count());
}

inline std::string formatTimestamp(std::uint64_t timestamp_ms)
{
    using namespace std::chrono;

    system_clock::time_point tp{milliseconds(timestamp_ms)};
    std::time_t tt = system_clock::to_time_t(tp);

    std::tm tm{};
    localtime_r(&tt, &tm); // thread-safe

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    auto ms = timestamp_ms % 1000;
    oss << "." << std::setfill('0') << std::setw(3) << ms;

    return oss.str();
}