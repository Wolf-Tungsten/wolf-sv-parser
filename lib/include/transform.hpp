#ifndef WOLVRIX_TRANSFORM_HPP
#define WOLVRIX_TRANSFORM_HPP

#include "diagnostics.hpp"
#include "logging.hpp"
#include "grh.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <type_traits>
#include <utility>
#include <vector>

namespace wolvrix::lib::transform
{

    enum class PassVerbosity
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3
    };

    using PassDiagnosticKind = wolvrix::lib::diag::DiagnosticKind;
    using PassDiagnostic = wolvrix::lib::diag::Diagnostic;

    class PassDiagnostics : public wolvrix::lib::diag::Diagnostics
    {
    public:
        using wolvrix::lib::diag::Diagnostics::Diagnostics;
        using wolvrix::lib::diag::Diagnostics::todo;

        void error(std::string passName, std::string message, std::string context = {});
        void warning(std::string passName, std::string message, std::string context = {});
        void info(std::string passName, std::string message, std::string context = {});
        void debug(std::string passName, std::string message, std::string context = {});
    };

    using TransformDiagnostics = PassDiagnostics;

    struct ScratchpadSlot
    {
        virtual ~ScratchpadSlot() = default;
    };

    template <typename T>
    struct ScratchpadSlotValue : ScratchpadSlot
    {
        template <typename U>
        explicit ScratchpadSlotValue(U &&v) : value(std::forward<U>(v)) {}

        T value;
    };

    struct PassContext
    {
        wolvrix::lib::grh::Netlist &netlist;
        PassDiagnostics &diags;
        PassVerbosity verbosity = PassVerbosity::Info;
        LogLevel logLevel = LogLevel::Warn;
        std::function<void(LogLevel, std::string_view, std::string_view)> logSink;
        bool keepDeclaredSymbols = true;
        std::unordered_map<std::string, std::unique_ptr<ScratchpadSlot>> scratchpad;
    };

    struct PassResult
    {
        bool changed = false;
        bool failed = false;
        std::vector<std::string> artifacts;
    };

    inline wolvrix::lib::grh::SrcLoc makeTransformSrcLoc(std::string_view passId,
                                               std::string_view note = {})
    {
        wolvrix::lib::grh::SrcLoc loc{};
        loc.origin = "transform";
        loc.pass = std::string(passId);
        loc.note = std::string(note);
        return loc;
    }

    class Pass
    {
    public:
        Pass(std::string id, std::string name, std::string description = {});
        virtual ~Pass() = default;

        virtual PassResult run() = 0;

        const std::string &id() const noexcept { return id_; }
        const std::string &name() const noexcept { return name_; }
        const std::string &description() const noexcept { return description_; }
        void setName(std::string name) { name_ = std::move(name); }

    protected:
        void log(LogLevel level, std::string message);
        void log(LogLevel level, std::string_view tag, std::string message);
        void logInfo(std::string message) { log(LogLevel::Info, std::move(message)); }
        void logWarn(std::string message) { log(LogLevel::Warn, std::move(message)); }
        void logError(std::string message) { log(LogLevel::Error, std::move(message)); }
        void logDebug(std::string message) { log(LogLevel::Debug, std::move(message)); }
        wolvrix::lib::grh::Netlist &netlist() { return context_->netlist; }
        PassDiagnostics &diags() { return context_->diags; }
        PassVerbosity verbosity() const noexcept { return context_ ? context_->verbosity : PassVerbosity::Error; }
        bool hasScratchpad(std::string_view key) const noexcept
        {
            if (!context_)
            {
                return false;
            }
            return context_->scratchpad.find(std::string(key)) != context_->scratchpad.end();
        }
        ScratchpadSlot *getScratchpadSlot(std::string_view key) noexcept
        {
            if (!context_)
            {
                return nullptr;
            }
            auto it = context_->scratchpad.find(std::string(key));
            return it == context_->scratchpad.end() ? nullptr : it->second.get();
        }
        const ScratchpadSlot *getScratchpadSlot(std::string_view key) const noexcept
        {
            if (!context_)
            {
                return nullptr;
            }
            auto it = context_->scratchpad.find(std::string(key));
            return it == context_->scratchpad.end() ? nullptr : it->second.get();
        }
        template <typename T>
        T *getScratchpad(std::string_view key) noexcept
        {
            if (auto *slot = getScratchpadSlot(key))
            {
                if (auto *typed = dynamic_cast<ScratchpadSlotValue<T> *>(slot))
                {
                    return &typed->value;
                }
            }
            return nullptr;
        }
        template <typename T>
        const T *getScratchpad(std::string_view key) const noexcept
        {
            if (const auto *slot = getScratchpadSlot(key))
            {
                if (const auto *typed = dynamic_cast<const ScratchpadSlotValue<T> *>(slot))
                {
                    return &typed->value;
                }
            }
            return nullptr;
        }
        template <typename T>
        void setScratchpad(std::string key, T &&value)
        {
            if (!context_)
            {
                return;
            }
            context_->scratchpad.insert_or_assign(
                std::move(key),
                std::make_unique<ScratchpadSlotValue<std::decay_t<T>>>(std::forward<T>(value)));
        }
        void eraseScratchpad(std::string_view key)
        {
            if (!context_)
            {
                return;
            }
            context_->scratchpad.erase(std::string(key));
        }
        void debug(std::string message, std::string context = {});
        void error(std::string message, std::string context = {});
        void warning(std::string message, std::string context = {});
        void info(std::string message, std::string context = {});
        void error(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message);
        void warning(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message);
        void info(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message);
        void debug(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message);
        void error(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message);
        void warning(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message);
        void info(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message);
        void debug(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message);
        void error(const wolvrix::lib::grh::Graph &graph, std::string message);
        void warning(const wolvrix::lib::grh::Graph &graph, std::string message);
        void info(const wolvrix::lib::grh::Graph &graph, std::string message);
        void debug(const wolvrix::lib::grh::Graph &graph, std::string message);
        bool keepDeclaredSymbols() const noexcept { return context_ ? context_->keepDeclaredSymbols : true; }

    private:
        friend class PassManager;

        bool shouldEmit(PassDiagnosticKind kind) const noexcept;
        bool shouldLog(LogLevel level) const noexcept;
        void setContext(PassContext *ctx) { context_ = ctx; }
        void clearContext() { context_ = nullptr; }

        std::string id_;
        std::string name_;
        std::string description_;
        PassContext *context_ = nullptr;
    };

    struct PassManagerOptions
    {
        bool stopOnError = true;
        bool emitTiming = false;
        PassVerbosity verbosity = PassVerbosity::Info;
        LogLevel logLevel = LogLevel::Warn;
        std::function<void(LogLevel, std::string_view, std::string_view)> logSink;
        bool keepDeclaredSymbols = true;
    };

    struct PassManagerResult
    {
        bool success = true;
        bool changed = false;
    };

    class PassManager
    {
    public:
        explicit PassManager(PassManagerOptions options = PassManagerOptions());

        void addPass(std::unique_ptr<Pass> pass, std::string instanceName = {});
        void clear();

        PassManagerResult run(wolvrix::lib::grh::Netlist &netlist, PassDiagnostics &diags);

        PassManagerOptions &options() noexcept { return options_; }
        const PassManagerOptions &options() const noexcept { return options_; }

    private:
        struct PassEntry
        {
            std::unique_ptr<Pass> instance;
        };

        std::vector<PassEntry> pipeline_;
        PassManagerOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_HPP
