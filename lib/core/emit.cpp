#include "core/emit.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_set>

namespace wolvrix::lib::emit
{

    Emit::Emit(EmitDiagnostics *diagnostics) : diagnostics_(diagnostics) {}

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

    bool Emit::validateTopGraphs(const std::vector<const wolvrix::lib::grh::Graph *> &topGraphs) const
    {
        if (topGraphs.empty())
        {
            reportError("No top graphs available for emission");
            return false;
        }
        return true;
    }

    std::vector<const wolvrix::lib::grh::Graph *> Emit::resolveTopGraphs(const wolvrix::lib::grh::Design &design,
                                                                         const EmitOptions &options) const
    {
        std::vector<const wolvrix::lib::grh::Graph *> result;
        std::unordered_set<std::string> seen;

        auto tryAdd = [&](std::string_view name)
        {
            if (seen.find(std::string(name)) != seen.end())
            {
                return;
            }

            const wolvrix::lib::grh::Graph *graph = design.findGraph(name);
            if (graph == nullptr)
            {
                reportError("Top graph not found", std::string(name));
                return;
            }

            seen.insert(std::string(graph->symbol()));
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
            for (const auto &name : design.topGraphs())
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

    std::string Emit::resolveSessionPathPrefix(const wolvrix::lib::grh::Graph &graph,
                                               const EmitOptions &options) const
    {
        if (options.sessionPathPrefix && !options.sessionPathPrefix->empty())
        {
            return *options.sessionPathPrefix;
        }
        return graph.symbol();
    }

    EmitResult Emit::emit(const wolvrix::lib::grh::Design &design, const EmitOptions &options)
    {
        EmitResult result;

        std::vector<const wolvrix::lib::grh::Graph *> topGraphs = resolveTopGraphs(design, options);
        if (!validateTopGraphs(topGraphs))
        {
            result.success = false;
            return result;
        }

        result = emitImpl(design, topGraphs, options);
        if (diagnostics_ && diagnostics_->hasError())
        {
            result.success = false;
        }
        return result;
    }

} // namespace wolvrix::lib::emit
