#ifndef WOLF_SV_LOGGING_HPP
#define WOLF_SV_LOGGING_HPP

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace wolf_sv_parser {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5
};

struct LogEvent {
    LogLevel level;
    std::string tag;
    std::string message;
};

class Logger {
public:
    using Sink = std::function<void(const LogEvent&)>;

    void setLevel(LogLevel level) noexcept { level_ = level; }
    void enable() noexcept { enabled_ = true; }
    void disable() noexcept { enabled_ = false; }
    void setSink(Sink sink) { sink_ = std::move(sink); }

    void allowTag(std::string_view tag)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tags_.insert(std::string(tag));
    }

    void clearTags()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tags_.clear();
    }

    bool enabled(LogLevel level, std::string_view tag) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_ || level_ == LogLevel::Off)
        {
            return false;
        }
        if (static_cast<int>(level) < static_cast<int>(level_))
        {
            return false;
        }
        if (!tags_.empty() && tags_.find(std::string(tag)) == tags_.end())
        {
            return false;
        }
        return true;
    }

    void log(LogLevel level, std::string_view tag, std::string_view message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_ || level_ == LogLevel::Off)
        {
            return;
        }
        if (level < level_)
        {
            return;
        }
        if (!tags_.empty() && tags_.find(std::string(tag)) == tags_.end())
        {
            return;
        }
        if (!sink_)
        {
            return;
        }
        LogEvent event{level, std::string(tag), std::string(message)};
        sink_(event);
    }

private:
    bool enabled_ = false;
    LogLevel level_ = LogLevel::Warn;
    std::unordered_set<std::string> tags_{};
    Sink sink_{};
    mutable std::mutex mutex_{};
};

} // namespace wolf_sv_parser

#endif // WOLF_SV_LOGGING_HPP
