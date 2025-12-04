#pragma once

#include "grh.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wolf_sv::transform
{

    enum class PassDiagnosticKind
    {
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

        const std::vector<PassDiagnostic> &messages() const noexcept { return messages_; }
        bool hasError() const noexcept;
        bool empty() const noexcept { return messages_.empty(); }
        void clear() { messages_.clear(); }

    private:
        std::vector<PassDiagnostic> messages_;
    };

    using TransformDiagnostics = PassDiagnostics;

    struct PassContext
    {
        grh::Netlist &netlist;
        PassDiagnostics &diags;
        bool verbose = false;
        grh::Graph *currentGraph = nullptr;
        std::string_view entryName;
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
        grh::Netlist &netlist() { return context_->netlist; }
        PassDiagnostics &diags() { return context_->diags; }
        bool verbose() const noexcept { return context_ ? context_->verbose : false; }
        grh::Graph *currentGraph() const noexcept { return context_ ? context_->currentGraph : nullptr; }
        std::string_view entryName() const noexcept { return context_ ? context_->entryName : std::string_view(); }
        void error(std::string message, std::string context = {});
        void warning(std::string message, std::string context = {});
        void info(std::string message, std::string context = {});
        void error(const grh::Graph &graph, const grh::Operation &op, std::string message);
        void warning(const grh::Graph &graph, const grh::Operation &op, std::string message);
        void info(const grh::Graph &graph, const grh::Operation &op, std::string message);
        void error(const grh::Graph &graph, const grh::Value &value, std::string message);
        void warning(const grh::Graph &graph, const grh::Value &value, std::string message);
        void info(const grh::Graph &graph, const grh::Value &value, std::string message);
        void error(const grh::Graph &graph, std::string message);
        void warning(const grh::Graph &graph, std::string message);
        void info(const grh::Graph &graph, std::string message);

    private:
        friend class PassManager;

        void setContext(PassContext *ctx) { context_ = ctx; }
        void clearContext() { context_ = nullptr; }

        std::string id_;
        std::string name_;
        std::string description_;
        PassContext *context_ = nullptr;
    };

    struct PassPipelineOptions
    {
        bool stopOnError = true;
        bool verbose = false;
    };

    struct TransformResult
    {
        bool success = true;
        bool changed = false;
    };

    class PassManager
    {
    public:
        explicit PassManager(PassPipelineOptions options = PassPipelineOptions());

        void addPass(std::unique_ptr<Pass> pass, std::string instanceName = {});
        void clear();

        TransformResult run(grh::Netlist &netlist, PassDiagnostics &diags);

        PassPipelineOptions &options() noexcept { return options_; }
        const PassPipelineOptions &options() const noexcept { return options_; }

    private:
        struct PassEntry
        {
            std::unique_ptr<Pass> instance;
        };

        std::vector<PassEntry> pipeline_;
        PassPipelineOptions options_;
    };

} // namespace wolf_sv::transform
