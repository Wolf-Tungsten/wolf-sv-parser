#ifndef WOLF_SV_EMIT_HPP
#define WOLF_SV_EMIT_HPP

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

namespace grh::emit
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

    enum class JsonPrintMode
    {
        Compact,
        PrettyCompact,
        Pretty
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
        std::optional<std::string> outputFilename;
        JsonPrintMode jsonMode = JsonPrintMode::PrettyCompact;
        std::vector<std::string> topOverrides;
        std::map<std::string, std::string, std::less<>> attributes;
        bool traceUnderscoreValues = false;
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

        EmitResult emit(const grh::ir::Netlist &netlist, const EmitOptions &options = EmitOptions());

    protected:
        EmitDiagnostics *diagnostics() const noexcept { return diagnostics_; }

        std::vector<const grh::ir::Graph *> resolveTopGraphs(const grh::ir::Netlist &netlist,
                                                         const EmitOptions &options) const;
        std::filesystem::path resolveOutputDir(const EmitOptions &options) const;
        bool ensureParentDirectory(const std::filesystem::path &path) const;
        std::unique_ptr<std::ofstream> openOutputFile(const std::filesystem::path &path) const;

        void reportError(std::string message, std::string context = {}) const;
        void reportWarning(std::string message, std::string context = {}) const;

    protected:
        virtual EmitResult emitImpl(const grh::ir::Netlist &netlist,
                                    std::span<const grh::ir::Graph *const> topGraphs,
                                    const EmitOptions &options) = 0;

        bool validateTopGraphs(const std::vector<const grh::ir::Graph *> &topGraphs) const;

    private:
        EmitDiagnostics *diagnostics_ = nullptr;
    };

    class EmitJSON : public Emit
    {
    public:
        using Emit::Emit;

        std::optional<std::string> emitToString(const grh::ir::Netlist &netlist, const EmitOptions &options = EmitOptions());

    private:
        EmitResult emitImpl(const grh::ir::Netlist &netlist,
                            std::span<const grh::ir::Graph *const> topGraphs,
                            const EmitOptions &options) override;
    };

    class EmitSystemVerilog : public Emit
    {
    public:
        using Emit::Emit;

    private:
        EmitResult emitImpl(const grh::ir::Netlist &netlist,
                            std::span<const grh::ir::Graph *const> topGraphs,
                            const EmitOptions &options) override;
    };

} // namespace grh::emit

#endif // WOLF_SV_EMIT_HPP
