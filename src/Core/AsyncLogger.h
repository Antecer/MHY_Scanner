#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

enum class GameType;
enum class ServerType : uint8_t;
enum class ScanRet;
enum class LiveStreamStatus : uint8_t;
enum class LivePlatform;

enum class LogLevel
{
    Info,
    Warning,
    Error
};

class AsyncLogger
{
public:
    static AsyncLogger& instance();

    void start();
    void stop();
    void log(LogLevel level, std::string message);

private:
    AsyncLogger();
    ~AsyncLogger();

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

void LogInfo(std::string message);
void LogWarning(std::string message);
void LogError(std::string message);

std::string ToString(LogLevel level);
std::string ToString(GameType gameType);
std::string ToString(ServerType serverType);
std::string ToString(ScanRet ret);
std::string ToString(LiveStreamStatus status);
std::string ToString(LivePlatform platform);
std::string MaskSensitive(std::string_view value, std::size_t visiblePrefix = 4, std::size_t visibleSuffix = 4);
