#pragma once

#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wolf_sv::emit
{

    enum class EmitDiagnosticKind
    {
        Error,
        Warning
    };

    struct EmitDiagnostic
    {
        EmitDiagnosticKind kind = EmitDiagnosticKind::Error;
        std::string message;
        std::string context;
    };

    class EmitDiagnostics
    {
    public:
        void error(std::string message, std::string context = {});
        void warning(std::string message, std::string context = {});

        const std::vector<EmitDiagnostic> &messages() const noexcept { return messages_; }
        bool hasError() const noexcept;
        bool empty() const noexcept { return messages_.empty(); }
        void clear() { messages_.clear(); }

    private:
        std::vector<EmitDiagnostic> messages_;
    };

    struct EmitOptions
    {
        std::optional<std::string> outputDir;
        bool prettyPrint = true;
        std::vector<std::string> topOverrides;
        std::map<std::string, std::string, std::less<>> attributes;
    };

    struct EmitResult
    {
        bool success = true;
        std::vector<std::string> artifacts;
    };

    class Emit
    {
    public:
        explicit Emit(EmitDiagnostics *diagnostics = nullptr);
        virtual ~Emit() = default;

        EmitResult emit(const grh::Netlist &netlist, const EmitOptions &options = EmitOptions());

    protected:
        EmitDiagnostics *diagnostics() const noexcept { return diagnostics_; }

        std::vector<const grh::Graph *> resolveTopGraphs(const grh::Netlist &netlist,
                                                         const EmitOptions &options) const;
        std::filesystem::path resolveOutputDir(const EmitOptions &options) const;
        bool ensureParentDirectory(const std::filesystem::path &path) const;
        std::unique_ptr<std::ofstream> openOutputFile(const std::filesystem::path &path) const;

        void reportError(std::string message, std::string context = {}) const;
        void reportWarning(std::string message, std::string context = {}) const;

    private:
        virtual EmitResult emitImpl(const grh::Netlist &netlist,
                                    std::span<const grh::Graph *const> topGraphs,
                                    const EmitOptions &options) = 0;

        bool validateTopGraphs(const std::vector<const grh::Graph *> &topGraphs) const;

        EmitDiagnostics *diagnostics_ = nullptr;
    };

    class EmitJSON : public Emit
    {
    public:
        using Emit::Emit;

    private:
        EmitResult emitImpl(const grh::Netlist &netlist,
                            std::span<const grh::Graph *const> topGraphs,
                            const EmitOptions &options) override;
    };

} // namespace wolf_sv::emit
