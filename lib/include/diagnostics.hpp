#ifndef WOLVRIX_DIAGNOSTICS_HPP
#define WOLVRIX_DIAGNOSTICS_HPP

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "slang/text/SourceLocation.h"

namespace wolvrix::lib::diag
{

    enum class DiagnosticKind
    {
        Todo,
        Error,
        Warning,
        Info,
        Debug
    };

    struct Diagnostic
    {
        DiagnosticKind kind = DiagnosticKind::Error;
        std::string message;
        std::string context;
        std::string passName;
        std::string originSymbol;
        std::optional<slang::SourceLocation> location;
    };

    class Diagnostics
    {
    public:
        void todo(std::string message, std::string context = {});
        void error(std::string message, std::string context = {});
        void warning(std::string message, std::string context = {});
        void info(std::string message, std::string context = {});
        void debug(std::string message, std::string context = {});

        void setOnError(std::function<void()> callback) { onError_ = std::move(callback); }
        void enableThreadLocal(bool enable) noexcept { threadLocalEnabled_ = enable; }
        void flushThreadLocal();
        const std::vector<Diagnostic> &messages() const noexcept { return messages_; }
        bool empty() const noexcept { return messages_.empty(); }
        bool hasError() const noexcept { return hasError_.load(std::memory_order_relaxed); }
        void clear();

    protected:
        void add(DiagnosticKind kind,
                 std::string message,
                 std::string context = {},
                 std::string passName = {},
                 std::string originSymbol = {},
                 std::optional<slang::SourceLocation> location = {});

    private:
        struct ThreadLocalBuffer
        {
            std::vector<Diagnostic> messages;
            bool hasError = false;
        };

        void flushThreadLocalLocked(ThreadLocalBuffer &buffer);

        static thread_local ThreadLocalBuffer threadLocal_;
        bool threadLocalEnabled_ = false;
        std::vector<Diagnostic> messages_;
        std::atomic<bool> hasError_{false};
        std::function<void()> onError_;
        mutable std::mutex mutex_;
    };

} // namespace wolvrix::lib::diag

#endif // WOLVRIX_DIAGNOSTICS_HPP
