#include "AsyncLogger.h"

#include <Windows.h>

#include <condition_variable>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include "ApiDefs.hpp"
#include "LiveStreamLink.h"

struct AsyncLogger::Impl
{
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> queue;
    std::thread worker;
    std::ofstream file;
    std::filesystem::path logPath;
    bool running{ false };
    bool stopping{ false };
};

namespace
{
std::filesystem::path GetExecutableDirectory()
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size())
    {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length == 0)
    {
        return std::filesystem::current_path();
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::string CurrentTimeText()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) %
                        1000;

    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << millis.count();
    return stream.str();
}

std::string FormatLine(LogLevel level, const std::string& message)
{
    std::ostringstream stream;
    stream << '[' << CurrentTimeText() << "] [" << ToString(level) << "] " << message;
    return stream.str();
}
}

AsyncLogger& AsyncLogger::instance()
{
    static AsyncLogger logger;
    return logger;
}

AsyncLogger::AsyncLogger() :
    impl(std::make_unique<Impl>())
{
}

AsyncLogger::~AsyncLogger()
{
    stop();
}

void AsyncLogger::start()
{
    std::lock_guard lock(impl->mutex);
    if (impl->running)
    {
        return;
    }

    impl->logPath = GetExecutableDirectory() / "MHY_Scanner.log";
    impl->file.open(impl->logPath, std::ios::out | std::ios::app);
    impl->stopping = false;
    impl->running = true;
    impl->worker = std::thread([this]() {
        std::unique_lock lock(impl->mutex);
        while (!impl->stopping || !impl->queue.empty())
        {
            impl->cv.wait(lock, [this]() {
                return impl->stopping || !impl->queue.empty();
            });

            while (!impl->queue.empty())
            {
                std::string line = std::move(impl->queue.front());
                impl->queue.pop_front();
                lock.unlock();

                if (impl->file.is_open())
                {
                    impl->file << line << '\n';
                    impl->file.flush();
                }

                lock.lock();
            }
        }
    });

    impl->queue.push_back(FormatLine(LogLevel::Info, "异步日志启动，日志文件: " + impl->logPath.string()));
    impl->cv.notify_one();
}

void AsyncLogger::stop()
{
    std::thread worker;
    {
        std::lock_guard lock(impl->mutex);
        if (!impl->running)
        {
            return;
        }
        if (impl->stopping)
        {
            return;
        }
        impl->queue.push_back(FormatLine(LogLevel::Info, "异步日志停止"));
        impl->stopping = true;
        worker = std::move(impl->worker);
        impl->cv.notify_one();
    }

    if (worker.joinable())
    {
        worker.join();
    }

    std::lock_guard lock(impl->mutex);
    if (impl->file.is_open())
    {
        impl->file.close();
    }
    impl->running = false;
}

void AsyncLogger::log(LogLevel level, std::string message)
{
    std::string line = FormatLine(level, message);
    std::lock_guard lock(impl->mutex);
    if (!impl->running || impl->stopping)
    {
        return;
    }
    impl->queue.push_back(std::move(line));
    impl->cv.notify_one();
}

void LogInfo(std::string message)
{
    AsyncLogger::instance().log(LogLevel::Info, std::move(message));
}

void LogWarning(std::string message)
{
    AsyncLogger::instance().log(LogLevel::Warning, std::move(message));
}

void LogError(std::string message)
{
    AsyncLogger::instance().log(LogLevel::Error, std::move(message));
}

std::string ToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string ToString(GameType gameType)
{
    switch (gameType)
    {
    case GameType::Honkai3:
        return "Honkai3";
    case GameType::TearsOfThemis:
        return "TearsOfThemis";
    case GameType::Genshin:
        return "Genshin";
    case GameType::HonkaiStarRail:
        return "HonkaiStarRail";
    case GameType::ZenlessZoneZero:
        return "ZenlessZoneZero";
    case GameType::Honkai3_BiliBili:
        return "Honkai3_BiliBili";
    case GameType::UNKNOW:
        return "Unknown";
    default:
        return "GameType(" + std::to_string(static_cast<int>(gameType)) + ")";
    }
}

std::string ToString(ServerType serverType)
{
    switch (serverType)
    {
    case ServerType::Official:
        return "Official";
    case ServerType::BH3_BiliBili:
        return "BH3_BiliBili";
    case ServerType::UNKNOW:
        return "Unknown";
    default:
        return "ServerType(" + std::to_string(static_cast<int>(serverType)) + ")";
    }
}

std::string ToString(ScanRet ret)
{
    switch (ret)
    {
    case ScanRet::UNKNOW:
        return "Unknown";
    case ScanRet::SUCCESS:
        return "Success";
    case ScanRet::FAILURE_1:
        return "ScanFailed";
    case ScanRet::FAILURE_2:
        return "ConfirmFailed";
    case ScanRet::LIVESTOP:
        return "LiveStop";
    case ScanRet::STREAMERROR:
        return "StreamError";
    default:
        return "ScanRet(" + std::to_string(static_cast<int>(ret)) + ")";
    }
}

std::string ToString(LiveStreamStatus status)
{
    switch (status)
    {
    case LiveStreamStatus::Normal:
        return "Normal";
    case LiveStreamStatus::Absent:
        return "Absent";
    case LiveStreamStatus::NotLive:
        return "NotLive";
    case LiveStreamStatus::Error:
        return "Error";
    default:
        return "LiveStreamStatus(" + std::to_string(static_cast<int>(status)) + ")";
    }
}

std::string ToString(LivePlatform platform)
{
    switch (platform)
    {
    case LivePlatform::Douyin:
        return "Douyin";
    case LivePlatform::BiliBili:
        return "BiliBili";
    default:
        return "LivePlatform(" + std::to_string(static_cast<int>(platform)) + ")";
    }
}

std::string MaskSensitive(std::string_view value, std::size_t visiblePrefix, std::size_t visibleSuffix)
{
    if (value.empty())
    {
        return "";
    }
    if (value.size() <= visiblePrefix + visibleSuffix)
    {
        return std::string(value.size(), '*');
    }
    std::string result;
    result.reserve(visiblePrefix + visibleSuffix + 3);
    result.append(value.substr(0, visiblePrefix));
    result.append("***");
    result.append(value.substr(value.size() - visibleSuffix, visibleSuffix));
    return result;
}
