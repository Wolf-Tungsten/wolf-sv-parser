#ifndef WOLF_SV_TRANSFORM_HPP
#define WOLF_SV_TRANSFORM_HPP

#include "grh.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef WOLF_SV_TRANSFORM_ENABLE_DEBUG_DIAGNOSTICS
#    ifdef NDEBUG
#        define WOLF_SV_TRANSFORM_ENABLE_DEBUG_DIAGNOSTICS 0
#    else
#        define WOLF_SV_TRANSFORM_ENABLE_DEBUG_DIAGNOSTICS 1
#    endif
#endif

#ifndef WOLF_SV_TRANSFORM_ENABLE_INFO_DIAGNOSTICS
#    ifdef NDEBUG
#        define WOLF_SV_TRANSFORM_ENABLE_INFO_DIAGNOSTICS 0
#    else
#        define WOLF_SV_TRANSFORM_ENABLE_INFO_DIAGNOSTICS 1
#    endif
#endif

namespace wolf_sv_parser::transform
{

    enum class PassVerbosity
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3
    };

    enum class PassDiagnosticKind
    {
        Debug,
        Error,
        Warning,
        Info
    };

    struct PassDiagnostic
    {
        PassDiagnosticKind kind = PassDiagnosticKind::Error;
        std::string message;
        std::string context;
        std::string passName;
    };

    class PassDiagnostics
    {
    public:
        void error(std::string passName, std::string message, std::string context = {});
        void warning(std::string passName, std::string message, std::string context = {});
        void info(std::string passName, std::string message, std::string context = {});
        void debug(std::string passName, std::string message, std::string context = {});

        const std::vector<PassDiagnostic> &messages() const noexcept { return messages_; }
        bool hasError() const noexcept;
        bool empty() const noexcept { return messages_.empty(); }
        void clear() { messages_.clear(); }

    private:
        std::vector<PassDiagnostic> messages_;
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
        grh::ir::Netlist &netlist;
        PassDiagnostics &diags;
        PassVerbosity verbosity = PassVerbosity::Info;
        std::unordered_map<std::string, std::unique_ptr<ScratchpadSlot>> scratchpad;
    };

    struct PassResult
    {
        bool changed = false;
        bool failed = false;
        std::vector<std::string> artifacts;
    };

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
        grh::ir::Netlist &netlist() { return context_->netlist; }
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
        void error(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string message);
        void warning(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string message);
        void info(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string message);
        void debug(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string message);
        void error(const grh::ir::Graph &graph, const grh::ir::Value &value, std::string message);
        void warning(const grh::ir::Graph &graph, const grh::ir::Value &value, std::string message);
        void info(const grh::ir::Graph &graph, const grh::ir::Value &value, std::string message);
        void debug(const grh::ir::Graph &graph, const grh::ir::Value &value, std::string message);
        void error(const grh::ir::Graph &graph, std::string message);
        void warning(const grh::ir::Graph &graph, std::string message);
        void info(const grh::ir::Graph &graph, std::string message);
        void debug(const grh::ir::Graph &graph, std::string message);

    private:
        friend class PassManager;

        bool shouldEmit(PassDiagnosticKind kind) const noexcept;
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
        PassVerbosity verbosity = PassVerbosity::Info;
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

        PassManagerResult run(grh::ir::Netlist &netlist, PassDiagnostics &diags);

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

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_TRANSFORM_HPP
