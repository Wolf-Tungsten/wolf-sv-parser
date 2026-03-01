#include "transform.hpp"

#include "transform/blackbox_guard.hpp"
#include "transform/comb_loop_elim.hpp"
#include "transform/demo_stats.hpp"
#include "transform/hier_flatten.hpp"
#include "transform/latch_transparent_read.hpp"
#include "transform/multidriven_guard.hpp"
#include "transform/memory_init_check.hpp"
#include "transform/simplify.hpp"
#include "transform/xmr_resolve.hpp"

#include <chrono>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace wolvrix::lib::transform
{

    namespace
    {
        std::string formatContext(const wolvrix::lib::grh::Graph *graph, const wolvrix::lib::grh::Operation *op, const wolvrix::lib::grh::Value *value)
        {
            std::string ctx;
            if (graph)
            {
                ctx += graph->symbol();
            }
            if (op)
            {
                if (!ctx.empty())
                {
                    ctx += "::";
                }
                ctx.append(op->symbolText());
            }
            if (value)
            {
                if (!ctx.empty())
                {
                    ctx += "::";
                }
                ctx.append(value->symbolText());
            }
            return ctx;
        }

        constexpr int diagnosticLevel(PassDiagnosticKind kind)
        {
            switch (kind)
            {
            case PassDiagnosticKind::Debug:
                return 0;
            case PassDiagnosticKind::Info:
                return 1;
            case PassDiagnosticKind::Warning:
                return 2;
            case PassDiagnosticKind::Todo:
            case PassDiagnosticKind::Error:
            default:
                return 3;
            }
        }
    } // namespace

    void PassDiagnostics::error(std::string passName, std::string message, std::string context)
    {
        add(PassDiagnosticKind::Error, std::move(message), std::move(context), std::move(passName));
    }

    void PassDiagnostics::warning(std::string passName, std::string message, std::string context)
    {
        add(PassDiagnosticKind::Warning, std::move(message), std::move(context), std::move(passName));
    }

    void PassDiagnostics::info(std::string passName, std::string message, std::string context)
    {
        add(PassDiagnosticKind::Info, std::move(message), std::move(context), std::move(passName));
    }

    void PassDiagnostics::debug(std::string passName, std::string message, std::string context)
    {
        add(PassDiagnosticKind::Debug, std::move(message), std::move(context), std::move(passName));
    }

    Pass::Pass(std::string id, std::string name, std::string description)
        : id_(std::move(id)), name_(std::move(name)), description_(std::move(description))
    {
    }

    bool Pass::shouldEmit(PassDiagnosticKind kind) const noexcept
    {
        if (!context_)
        {
            return false;
        }
        return diagnosticLevel(kind) >= static_cast<int>(context_->verbosity);
    }

    bool Pass::shouldLog(LogLevel level) const noexcept
    {
        if (!context_ || !context_->logSink)
        {
            return false;
        }
        if (static_cast<int>(level) < static_cast<int>(context_->logLevel))
        {
            return false;
        }
        return true;
    }

    void Pass::log(LogLevel level, std::string message)
    {
        const std::string_view tag = name_.empty() ? std::string_view(id_) : std::string_view(name_);
        log(level, tag, std::move(message));
    }

    void Pass::log(LogLevel level, std::string_view tag, std::string message)
    {
        if (!shouldLog(level))
        {
            return;
        }
        context_->logSink(level, tag, message);
    }

    void Pass::error(std::string message, std::string context)
    {
        if (!shouldEmit(PassDiagnosticKind::Error))
        {
            return;
        }
        diags().error(name_.empty() ? id_ : name_, std::move(message), std::move(context));
    }

    void Pass::warning(std::string message, std::string context)
    {
        if (!shouldEmit(PassDiagnosticKind::Warning))
        {
            return;
        }
        diags().warning(name_.empty() ? id_ : name_, std::move(message), std::move(context));
    }

    void Pass::info(std::string message, std::string context)
    {
        if (!shouldEmit(PassDiagnosticKind::Info))
        {
            return;
        }
        diags().info(name_.empty() ? id_ : name_, std::move(message), std::move(context));
    }

    void Pass::debug(std::string message, std::string context)
    {
        if (!shouldEmit(PassDiagnosticKind::Debug))
        {
            return;
        }
        diags().debug(name_.empty() ? id_ : name_, std::move(message), std::move(context));
    }

    void Pass::error(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Error))
        {
            return;
        }
        diags().error(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, &op, nullptr));
    }

    void Pass::warning(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Warning))
        {
            return;
        }
        diags().warning(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, &op, nullptr));
    }

    void Pass::info(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Info))
        {
            return;
        }
        diags().info(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, &op, nullptr));
    }

    void Pass::debug(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Debug))
        {
            return;
        }
        diags().debug(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, &op, nullptr));
    }

    void Pass::error(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Error))
        {
            return;
        }
        diags().error(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, &value));
    }

    void Pass::warning(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Warning))
        {
            return;
        }
        diags().warning(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, &value));
    }

    void Pass::info(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Info))
        {
            return;
        }
        diags().info(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, &value));
    }

    void Pass::debug(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Debug))
        {
            return;
        }
        diags().debug(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, &value));
    }

    void Pass::error(const wolvrix::lib::grh::Graph &graph, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Error))
        {
            return;
        }
        diags().error(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, nullptr));
    }

    void Pass::warning(const wolvrix::lib::grh::Graph &graph, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Warning))
        {
            return;
        }
        diags().warning(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, nullptr));
    }

    void Pass::info(const wolvrix::lib::grh::Graph &graph, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Info))
        {
            return;
        }
        diags().info(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, nullptr));
    }

    void Pass::debug(const wolvrix::lib::grh::Graph &graph, std::string message)
    {
        if (!shouldEmit(PassDiagnosticKind::Debug))
        {
            return;
        }
        diags().debug(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, nullptr));
    }

    PassManager::PassManager(PassManagerOptions options)
        : options_(options)
    {
    }

    void PassManager::addPass(std::unique_ptr<Pass> pass, std::string instanceName)
    {
        PassEntry entry;
        if (pass)
        {
            if (!instanceName.empty())
            {
                pass->setName(std::move(instanceName));
            }
            else
            {
                pass->setName(pass->id());
            }
        }
        entry.instance = std::move(pass);
        pipeline_.push_back(std::move(entry));
    }

    void PassManager::clear()
    {
        pipeline_.clear();
    }

    PassManagerResult PassManager::run(wolvrix::lib::grh::Design &design, PassDiagnostics &diags)
    {
        PassManagerResult result;
        PassContext context{design, diags, options_.verbosity, options_.logLevel, options_.logSink, options_.keepDeclaredSymbols};
        bool encounteredFailure = false;
        auto emitLog = [&](LogLevel level, std::string_view tag, std::string_view message) {
            if (!options_.logSink)
            {
                return;
            }
            if (static_cast<int>(level) < static_cast<int>(options_.logLevel))
            {
                return;
            }
            options_.logSink(level, tag, message);
        };

        for (auto &entry : pipeline_)
        {
            if (options_.stopOnError && diags.hasError())
            {
                encounteredFailure = true;
                break;
            }

            Pass *pass = entry.instance.get();

            if (pass == nullptr)
            {
                diags.error("unknown", "Pass instance is null", "pipeline");
                encounteredFailure = true;
                if (options_.stopOnError)
                {
                    break;
                }
                continue;
            }

            pass->setContext(&context);
            auto startTime = std::chrono::steady_clock::now();
            PassResult passResult = pass->run();
            auto endTime = std::chrono::steady_clock::now();
            pass->clearContext();
            result.changed = result.changed || passResult.changed;

            auto durationMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
                    .count();
            std::string message;
            message.reserve(pass->id().size() + 32);
            message.append(pass->id());
            message.append(passResult.failed ? " failed in " : " done in ");
            message.append(std::to_string(durationMs));
            message.append("ms");
            if (passResult.changed)
            {
                message.append(" (changed)");
            }
            emitLog(LogLevel::Info, "timing", message);

            if (passResult.failed)
            {
                encounteredFailure = true;
                if (options_.stopOnError)
                {
                    break;
                }
            }
            else if (options_.stopOnError && diags.hasError())
            {
                encounteredFailure = true;
                break;
            }
        }

        result.success = !encounteredFailure && !diags.hasError();
        return result;
    }

    std::string normalizePassName(std::string_view name)
    {
        std::string normalized(name);
        for (char &ch : normalized)
        {
            if (ch == '_')
            {
                ch = '-';
            }
        }
        return normalized;
    }

    std::vector<std::string> availableTransformPasses()
    {
        return {
            "blackbox-guard",
            "comb-loop-elim",
            "latch-transparent-read",
            "multidriven-guard",
            "hier-flatten",
            "xmr-resolve",
            "memory-init-check",
            "simplify",
            "stats",
        };
    }

    std::unique_ptr<Pass> makePass(std::string_view name,
                                   std::span<const std::string_view> args,
                                   std::string &error)
    {
        const std::string normalized = normalizePassName(name);
        if (normalized == "xmr-resolve")
        {
            if (!args.empty())
            {
                error = "xmr-resolve does not accept arguments";
                return nullptr;
            }
            return std::make_unique<XmrResolvePass>();
        }
        if (normalized == "multidriven-guard")
        {
            if (!args.empty())
            {
                error = "multidriven-guard does not accept arguments";
                return nullptr;
            }
            return std::make_unique<MultiDrivenGuardPass>();
        }
        if (normalized == "blackbox-guard")
        {
            if (!args.empty())
            {
                error = "blackbox-guard does not accept arguments";
                return nullptr;
            }
            return std::make_unique<BlackboxGuardPass>();
        }
        if (normalized == "hier-flatten")
        {
            HierFlattenOptions options;
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const std::string_view arg = args[i];
                if (arg == "-preserve-modules")
                {
                    options.preserveFlattenedModules = true;
                }
                else if (arg == "-sym-protect" || arg.starts_with("-sym-protect="))
                {
                    std::string_view value;
                    if (arg == "-sym-protect")
                    {
                        if (i + 1 >= args.size())
                        {
                            error = "-sym-protect expects a value";
                            return nullptr;
                        }
                        value = args[++i];
                    }
                    else
                    {
                        value = arg.substr(std::string_view("-sym-protect=").size());
                    }
                    if (value == "all")
                    {
                        options.symProtect = HierFlattenOptions::SymProtectMode::All;
                    }
                    else if (value == "hierarchy")
                    {
                        options.symProtect = HierFlattenOptions::SymProtectMode::Hierarchy;
                    }
                    else if (value == "stateful")
                    {
                        options.symProtect = HierFlattenOptions::SymProtectMode::Stateful;
                    }
                    else if (value == "none")
                    {
                        options.symProtect = HierFlattenOptions::SymProtectMode::None;
                    }
                    else
                    {
                        error = "unknown -sym-protect mode";
                        return nullptr;
                    }
                }
                else
                {
                    error = "unknown hier-flatten option";
                    return nullptr;
                }
            }
            return std::make_unique<HierFlattenPass>(options);
        }
        if (normalized == "simplify")
        {
            SimplifyOptions options;
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const std::string_view arg = args[i];
                if (arg == "-max-iter")
                {
                    if (i + 1 >= args.size())
                    {
                        error = "-max-iter expects a value";
                        return nullptr;
                    }
                    try
                    {
                        options.maxIterations = std::stoi(std::string(args[++i]));
                    }
                    catch (const std::exception &)
                    {
                        error = "invalid -max-iter value";
                        return nullptr;
                    }
                }
                else if (arg == "-x-fold" || arg.starts_with("-x-fold="))
                {
                    std::string_view value;
                    if (arg == "-x-fold")
                    {
                        if (i + 1 >= args.size())
                        {
                            error = "-x-fold expects a value";
                            return nullptr;
                        }
                        value = args[++i];
                    }
                    else
                    {
                        value = arg.substr(std::string_view("-x-fold=").size());
                    }
                    if (value == "strict")
                    {
                        options.xFold = ConstantFoldOptions::XFoldMode::Strict;
                    }
                    else if (value == "known")
                    {
                        options.xFold = ConstantFoldOptions::XFoldMode::Known;
                    }
                    else if (value == "propagate")
                    {
                        options.xFold = ConstantFoldOptions::XFoldMode::Propagate;
                    }
                    else
                    {
                        error = "unknown -x-fold mode";
                        return nullptr;
                    }
                }
                else if (arg == "-semantics" || arg.starts_with("-semantics="))
                {
                    std::string_view value;
                    if (arg == "-semantics")
                    {
                        if (i + 1 >= args.size())
                        {
                            error = "-semantics expects a value";
                            return nullptr;
                        }
                        value = args[++i];
                    }
                    else
                    {
                        value = arg.substr(std::string_view("-semantics=").size());
                    }
                    if (value == "2state")
                    {
                        options.semantics = ConstantFoldOptions::Semantics::TwoState;
                    }
                    else if (value == "4state")
                    {
                        options.semantics = ConstantFoldOptions::Semantics::FourState;
                    }
                    else
                    {
                        error = "unknown -semantics mode";
                        return nullptr;
                    }
                }
                else
                {
                    error = "unknown simplify option";
                    return nullptr;
                }
            }
            return std::make_unique<SimplifyPass>(options);
        }
        if (normalized == "comb-loop-elim")
        {
            CombLoopElimOptions options;
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const std::string_view arg = args[i];
                if (arg == "-max-analysis-nodes" || arg == "-max-nodes" || arg.starts_with("-max-analysis-nodes=") ||
                    arg.starts_with("-max-nodes="))
                {
                    std::string_view value;
                    if (arg == "-max-analysis-nodes" || arg == "-max-nodes")
                    {
                        if (i + 1 >= args.size())
                        {
                            error = "-max-analysis-nodes expects a value";
                            return nullptr;
                        }
                        value = args[++i];
                    }
                    else if (arg.starts_with("-max-analysis-nodes="))
                    {
                        value = arg.substr(std::string_view("-max-analysis-nodes=").size());
                    }
                    else
                    {
                        value = arg.substr(std::string_view("-max-nodes=").size());
                    }
                    try
                    {
                        options.maxAnalysisNodes = static_cast<std::size_t>(std::stoull(std::string(value)));
                    }
                    catch (const std::exception &)
                    {
                        error = "invalid -max-analysis-nodes value";
                        return nullptr;
                    }
                }
                else if (arg == "-num-threads" || arg == "-threads" || arg.starts_with("-num-threads=") ||
                         arg.starts_with("-threads="))
                {
                    std::string_view value;
                    if (arg == "-num-threads" || arg == "-threads")
                    {
                        if (i + 1 >= args.size())
                        {
                            error = "-num-threads expects a value";
                            return nullptr;
                        }
                        value = args[++i];
                    }
                    else if (arg.starts_with("-num-threads="))
                    {
                        value = arg.substr(std::string_view("-num-threads=").size());
                    }
                    else
                    {
                        value = arg.substr(std::string_view("-threads=").size());
                    }
                    try
                    {
                        options.numThreads = static_cast<std::size_t>(std::stoull(std::string(value)));
                    }
                    catch (const std::exception &)
                    {
                        error = "invalid -num-threads value";
                        return nullptr;
                    }
                }
                else if (arg == "-fix-false-loops" || arg == "-no-fix-false-loops" ||
                         arg.starts_with("-fix-false-loops="))
                {
                    if (arg == "-fix-false-loops")
                    {
                        options.fixFalseLoops = true;
                    }
                    else if (arg == "-no-fix-false-loops")
                    {
                        options.fixFalseLoops = false;
                    }
                    else
                    {
                        std::string_view value = arg.substr(std::string_view("-fix-false-loops=").size());
                        if (value == "1" || value == "true" || value == "yes")
                        {
                            options.fixFalseLoops = true;
                        }
                        else if (value == "0" || value == "false" || value == "no")
                        {
                            options.fixFalseLoops = false;
                        }
                        else
                        {
                            error = "invalid -fix-false-loops value";
                            return nullptr;
                        }
                    }
                }
                else if (arg == "-max-fix-iter" || arg == "-max-fix-iterations" || arg.starts_with("-max-fix-iter=") ||
                         arg.starts_with("-max-fix-iterations="))
                {
                    std::string_view value;
                    if (arg == "-max-fix-iter" || arg == "-max-fix-iterations")
                    {
                        if (i + 1 >= args.size())
                        {
                            error = "-max-fix-iter expects a value";
                            return nullptr;
                        }
                        value = args[++i];
                    }
                    else if (arg.starts_with("-max-fix-iter="))
                    {
                        value = arg.substr(std::string_view("-max-fix-iter=").size());
                    }
                    else
                    {
                        value = arg.substr(std::string_view("-max-fix-iterations=").size());
                    }
                    try
                    {
                        options.maxFixIterations = static_cast<std::size_t>(std::stoull(std::string(value)));
                    }
                    catch (const std::exception &)
                    {
                        error = "invalid -max-fix-iter value";
                        return nullptr;
                    }
                }
                else if (arg == "-fail-on-true-loop")
                {
                    options.failOnTrueLoop = true;
                }
                else
                {
                    error = "unknown comb-loop-elim option";
                    return nullptr;
                }
            }
            return std::make_unique<CombLoopElimPass>(options);
        }
        if (normalized == "memory-init-check")
        {
            if (!args.empty())
            {
                error = "memory-init-check does not accept arguments";
                return nullptr;
            }
            return std::make_unique<MemoryInitCheckPass>();
        }
        if (normalized == "latch-transparent-read")
        {
            if (!args.empty())
            {
                error = "latch-transparent-read does not accept arguments";
                return nullptr;
            }
            return std::make_unique<LatchTransparentReadPass>();
        }
        if (normalized == "stats")
        {
            if (!args.empty())
            {
                error = "stats does not accept arguments";
                return nullptr;
            }
            return std::make_unique<StatsPass>();
        }

        error = "unknown pass: " + normalized;
        return nullptr;
    }

} // namespace wolvrix::lib::transform
