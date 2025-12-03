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
        Warning
    };

    struct PassDiagnostic
    {
        PassDiagnosticKind kind = PassDiagnosticKind::Error;
        std::string message;
        std::string context;
        std::string passId;
    };

    class PassDiagnostics
    {
    public:
        void error(std::string passId, std::string message, std::string context = {});
        void warning(std::string passId, std::string message, std::string context = {});

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

    using PassArgMap = std::unordered_map<std::string, std::string>;

    struct PassConfig
    {
        PassArgMap args;
        bool enabled = true;

        bool getBool(std::string_view key, bool defaultValue = false) const;
        int64_t getInt(std::string_view key, int64_t defaultValue = 0) const;
        std::string getString(std::string_view key, std::string defaultValue = {}) const;
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

        virtual PassResult run(PassContext &context) = 0;
        virtual void configure(const PassConfig &config);

        const std::string &id() const noexcept { return id_; }
        const std::string &name() const noexcept { return name_; }
        const std::string &description() const noexcept { return description_; }

    private:
        std::string id_;
        std::string name_;
        std::string description_;
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

        void addPass(std::unique_ptr<Pass> pass, PassConfig config = PassConfig());
        void clear();

        TransformResult run(grh::Netlist &netlist, PassDiagnostics &diags);

        PassPipelineOptions &options() noexcept { return options_; }
        const PassPipelineOptions &options() const noexcept { return options_; }

    private:
        struct PassEntry
        {
            PassConfig config;
            std::unique_ptr<Pass> instance;
        };

        std::vector<PassEntry> pipeline_;
        PassPipelineOptions options_;
    };

} // namespace wolf_sv::transform
