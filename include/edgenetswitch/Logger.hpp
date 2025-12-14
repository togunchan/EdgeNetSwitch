#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

class Logger
{
public:
    static void init(LogLevel level, const std::string &filePath);
    static void shutdown();
    ~Logger();

    static void debug(const std::string &msg);
    static void info(const std::string &msg);
    static void warn(const std::string &msg);
    static void error(const std::string &msg);

private:
    Logger(LogLevel level, const std::string &filePath);

    void log(LogLevel level, const std::string &msg);

    LogLevel minLevel_;
    std::ofstream file_;
    std::mutex mutex_;

    static std::unique_ptr<Logger> instance_;
};