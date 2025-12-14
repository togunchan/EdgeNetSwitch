#include "edgenetswitch/Logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

std::unique_ptr<Logger> Logger::instance_ = nullptr;

static std::string timestamp()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    auto tt = system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&tt);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%F %T");
    return oss.str();
}

Logger::Logger(LogLevel level, const std::string &filePath) : minLevel_(level), file_(filePath, std::ios::app) {}

Logger::~Logger() {}

void Logger::init(LogLevel level, const std::string &filePath)
{
    instance_ = std::unique_ptr<Logger>(new Logger(level, filePath));
}

void Logger::shutdown()
{
    instance_.reset();
}

LogLevel Logger::parseLevel(const std::string &levelStr)
{
    std::string s = levelStr;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });

    if (s == "debug")
        return LogLevel::Debug;
    if (s == "warn" || s == "warning")
        return LogLevel::Warning;
    if (s == "error")
        return LogLevel::Error;
    if (s == "info")
        return LogLevel::Info;

    return LogLevel::Info;
}

void Logger::debug(const std::string &msg)
{
    if (instance_)
        instance_->log(LogLevel::Debug, msg);
}

void Logger::info(const std::string &msg)
{
    if (instance_)
        instance_->log(LogLevel::Info, msg);
}

void Logger::warn(const std::string &msg)
{
    if (instance_)
        instance_->log(LogLevel::Warning, msg);
}

void Logger::error(const std::string &msg)
{
    if (instance_)
        instance_->log(LogLevel::Error, msg);
}

void Logger::log(LogLevel level, const std::string &msg)
{
    if (level < minLevel_)
        return;

    std::lock_guard<std::mutex> lock(mutex_);

    std::string levelStr;
    switch (level)
    {
    case LogLevel::Debug:
        levelStr = "DEBUG";
        break;
    case LogLevel::Info:
        levelStr = "INFO";
        break;
    case LogLevel::Warning:
        levelStr = "WARN";
        break;
    case LogLevel::Error:
        levelStr = "ERROR";
        break;
    }

    std::string line = "[" + timestamp() + "][" + levelStr + "] " + msg;

    std::cout << line << std::endl;
    if (file_.is_open())
    {
        file_ << line << std::endl;
    }
}