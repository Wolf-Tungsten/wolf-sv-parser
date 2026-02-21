#include "transform.hpp"

#include <chrono>

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

    PassManagerResult PassManager::run(wolvrix::lib::grh::Netlist &netlist, PassDiagnostics &diags)
    {
        PassManagerResult result;
        PassContext context{netlist, diags, options_.verbosity, options_.logLevel, options_.logSink, options_.keepDeclaredSymbols};
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

} // namespace wolvrix::lib::transform
