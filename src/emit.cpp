#include "emit.hpp"

#include <system_error>
#include <unordered_set>

namespace wolf_sv::emit
{

    Emit::Emit(EmitDiagnostics *diagnostics) : diagnostics_(diagnostics) {}

    void EmitDiagnostics::error(std::string message, std::string context)
    {
        messages_.push_back(EmitDiagnostic{EmitDiagnosticKind::Error, std::move(message), std::move(context)});
    }

    void EmitDiagnostics::warning(std::string message, std::string context)
    {
        messages_.push_back(EmitDiagnostic{EmitDiagnosticKind::Warning, std::move(message), std::move(context)});
    }

    bool EmitDiagnostics::hasError() const noexcept
    {
        for (const auto &message : messages_)
        {
            if (message.kind == EmitDiagnosticKind::Error)
            {
                return true;
            }
        }
        return false;
    }

    void Emit::reportError(std::string message, std::string context) const
    {
        if (diagnostics_ != nullptr)
        {
            diagnostics_->error(std::move(message), std::move(context));
        }
    }

    void Emit::reportWarning(std::string message, std::string context) const
    {
        if (diagnostics_ != nullptr)
        {
            diagnostics_->warning(std::move(message), std::move(context));
        }
    }

    bool Emit::validateTopGraphs(const std::vector<const grh::Graph *> &topGraphs) const
    {
        if (topGraphs.empty())
        {
            reportError("No top graphs available for emission");
            return false;
        }
        return true;
    }

    std::vector<const grh::Graph *> Emit::resolveTopGraphs(const grh::Netlist &netlist,
                                                           const EmitOptions &options) const
    {
        std::vector<const grh::Graph *> result;
        std::unordered_set<std::string> seen;

        auto tryAdd = [&](std::string_view name)
        {
            if (seen.find(std::string(name)) != seen.end())
            {
                return;
            }

            const grh::Graph *graph = netlist.findGraph(name);
            if (graph == nullptr)
            {
                reportError("Top graph not found", std::string(name));
                return;
            }

            seen.insert(std::string(graph->name()));
            result.push_back(graph);
        };

        if (!options.topOverrides.empty())
        {
            for (const auto &name : options.topOverrides)
            {
                tryAdd(name);
            }
        }
        else
        {
            for (const auto &name : netlist.topGraphs())
            {
                tryAdd(name);
            }
        }

        return result;
    }

    std::filesystem::path Emit::resolveOutputDir(const EmitOptions &options) const
    {
        if (options.outputDir && !options.outputDir->empty())
        {
            return std::filesystem::path(*options.outputDir);
        }
        return std::filesystem::current_path();
    }

    bool Emit::ensureParentDirectory(const std::filesystem::path &path) const
    {
        const std::filesystem::path parent = path.parent_path();
        if (parent.empty())
        {
            return true;
        }

        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            reportError("Failed to create output directory: " + ec.message(), parent.string());
            return false;
        }
        return true;
    }

    std::unique_ptr<std::ofstream> Emit::openOutputFile(const std::filesystem::path &path) const
    {
        if (!ensureParentDirectory(path))
        {
            return nullptr;
        }

        auto stream = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
        if (!stream->is_open())
        {
            reportError("Failed to open output file for writing", path.string());
            return nullptr;
        }
        return stream;
    }

    EmitResult Emit::emit(const grh::Netlist &netlist, const EmitOptions &options)
    {
        EmitResult result;

        std::vector<const grh::Graph *> topGraphs = resolveTopGraphs(netlist, options);
        if (!validateTopGraphs(topGraphs))
        {
            result.success = false;
            return result;
        }

        result = emitImpl(netlist, topGraphs, options);
        if (diagnostics_ && diagnostics_->hasError())
        {
            result.success = false;
        }
        return result;
    }

} // namespace wolf_sv::emit
