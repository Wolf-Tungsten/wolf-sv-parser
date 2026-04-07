#ifndef WOLVRIX_EMIT_HPP
#define WOLVRIX_EMIT_HPP

#include "core/diagnostics.hpp"
#include "core/grh.hpp"
#include "core/transform.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wolvrix::lib::emit
{

    using EmitDiagnosticKind = wolvrix::lib::diag::DiagnosticKind;
    using EmitDiagnostic = wolvrix::lib::diag::Diagnostic;

    class EmitDiagnostics : public wolvrix::lib::diag::Diagnostics
    {
    public:
        using wolvrix::lib::diag::Diagnostics::Diagnostics;
        using wolvrix::lib::diag::Diagnostics::error;
        using wolvrix::lib::diag::Diagnostics::warning;
        using wolvrix::lib::diag::Diagnostics::info;
        using wolvrix::lib::diag::Diagnostics::debug;
    };

    struct EmitOptions
    {
        std::optional<std::string> outputDir;
        std::optional<std::string> outputFilename;
        std::vector<std::string> topOverrides;
        std::map<std::string, std::string, std::less<>> attributes;
        bool traceUnderscoreValues = false;
        bool splitModules = false;
        wolvrix::lib::transform::SessionStore *session = nullptr;
        std::optional<std::string> sessionPathPrefix;
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

        EmitResult emit(const wolvrix::lib::grh::Design &design, const EmitOptions &options = EmitOptions());

    protected:
        EmitDiagnostics *diagnostics() const noexcept { return diagnostics_; }

        std::vector<const wolvrix::lib::grh::Graph *> resolveTopGraphs(const wolvrix::lib::grh::Design &design,
                                                         const EmitOptions &options) const;
        std::filesystem::path resolveOutputDir(const EmitOptions &options) const;
        bool ensureParentDirectory(const std::filesystem::path &path) const;
        std::unique_ptr<std::ofstream> openOutputFile(const std::filesystem::path &path) const;
        std::string resolveSessionPathPrefix(const wolvrix::lib::grh::Graph &graph,
                                             const EmitOptions &options) const;
        template <typename T>
        const T *getSessionValue(const EmitOptions &options, std::string_view key) const noexcept
        {
            if (options.session == nullptr)
            {
                return nullptr;
            }
            auto it = options.session->find(std::string(key));
            if (it == options.session->end())
            {
                return nullptr;
            }
            auto *typed =
                dynamic_cast<const wolvrix::lib::transform::SessionSlotValue<T> *>(it->second.get());
            return typed ? &typed->value : nullptr;
        }

        void reportError(std::string message, std::string context = {}) const;
        void reportWarning(std::string message, std::string context = {}) const;

    protected:
        virtual EmitResult emitImpl(const wolvrix::lib::grh::Design &design,
                                    std::span<const wolvrix::lib::grh::Graph *const> topGraphs,
                                    const EmitOptions &options) = 0;

        bool validateTopGraphs(const std::vector<const wolvrix::lib::grh::Graph *> &topGraphs) const;

    private:
        EmitDiagnostics *diagnostics_ = nullptr;
    };

} // namespace wolvrix::lib::emit

#endif // WOLVRIX_EMIT_HPP
