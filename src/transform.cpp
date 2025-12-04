#include "transform.hpp"

#include <iostream>

namespace wolf_sv::transform
{

    namespace
    {
        std::string formatContext(const grh::Graph *graph, const grh::Operation *op, const grh::Value *value)
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
                ctx += op->symbol();
            }
            if (value)
            {
                if (!ctx.empty())
                {
                    ctx += "::";
                }
                ctx += value->symbol();
            }
            return ctx;
        }

        void appendMessage(std::vector<PassDiagnostic> &messages, PassDiagnosticKind kind, std::string passName, std::string message, std::string context)
        {
            messages.push_back(PassDiagnostic{
                .kind = kind,
                .message = std::move(message),
                .context = std::move(context),
                .passName = std::move(passName),
            });
        }
    } // namespace

    void PassDiagnostics::error(std::string passName, std::string message, std::string context)
    {
        appendMessage(messages_, PassDiagnosticKind::Error, std::move(passName), std::move(message), std::move(context));
    }

    void PassDiagnostics::warning(std::string passName, std::string message, std::string context)
    {
        appendMessage(messages_, PassDiagnosticKind::Warning, std::move(passName), std::move(message), std::move(context));
    }

    void PassDiagnostics::info(std::string passName, std::string message, std::string context)
    {
        appendMessage(messages_, PassDiagnosticKind::Info, std::move(passName), std::move(message), std::move(context));
    }

    bool PassDiagnostics::hasError() const noexcept
    {
        for (const auto &message : messages_)
        {
            if (message.kind == PassDiagnosticKind::Error)
            {
                return true;
            }
        }
        return false;
    }

    Pass::Pass(std::string id, std::string name, std::string description)
        : id_(std::move(id)), name_(std::move(name)), description_(std::move(description))
    {
    }

    void Pass::error(std::string message, std::string context)
    {
        diags().error(name_.empty() ? id_ : name_, std::move(message), std::move(context));
    }

    void Pass::warning(std::string message, std::string context)
    {
        diags().warning(name_.empty() ? id_ : name_, std::move(message), std::move(context));
    }

    void Pass::info(std::string message, std::string context)
    {
        diags().info(name_.empty() ? id_ : name_, std::move(message), std::move(context));
    }

    void Pass::error(const grh::Graph &graph, const grh::Operation &op, std::string message)
    {
        diags().error(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, &op, nullptr));
    }

    void Pass::warning(const grh::Graph &graph, const grh::Operation &op, std::string message)
    {
        diags().warning(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, &op, nullptr));
    }

    void Pass::info(const grh::Graph &graph, const grh::Operation &op, std::string message)
    {
        diags().info(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, &op, nullptr));
    }

    void Pass::error(const grh::Graph &graph, const grh::Value &value, std::string message)
    {
        diags().error(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, &value));
    }

    void Pass::warning(const grh::Graph &graph, const grh::Value &value, std::string message)
    {
        diags().warning(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, &value));
    }

    void Pass::info(const grh::Graph &graph, const grh::Value &value, std::string message)
    {
        diags().info(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, &value));
    }

    void Pass::error(const grh::Graph &graph, std::string message)
    {
        diags().error(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, nullptr));
    }

    void Pass::warning(const grh::Graph &graph, std::string message)
    {
        diags().warning(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, nullptr));
    }

    void Pass::info(const grh::Graph &graph, std::string message)
    {
        diags().info(name_.empty() ? id_ : name_, std::move(message), formatContext(&graph, nullptr, nullptr));
    }

    PassManager::PassManager(PassPipelineOptions options)
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

    TransformResult PassManager::run(grh::Netlist &netlist, PassDiagnostics &diags)
    {
        TransformResult result;
        PassContext context{netlist, diags, options_.verbose};
        bool encounteredFailure = false;

        for (auto &entry : pipeline_)
        {
            if (options_.stopOnError && diags.hasError())
            {
                encounteredFailure = true;
                break;
            }

            context.entryName = entry.instance ? entry.instance->name() : std::string_view();
            context.currentGraph = nullptr;

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

            if (options_.verbose)
            {
                std::cerr << "[transform] [" << pass->id() << "] start\n";
            }

            pass->setContext(&context);
            PassResult passResult = pass->run();
            pass->clearContext();
            result.changed = result.changed || passResult.changed;

            if (options_.verbose)
            {
                std::cerr << "[transform] [" << pass->id() << "] " << (passResult.failed ? "failed" : "done");
                if (passResult.changed)
                {
                    std::cerr << " (changed)";
                }
                std::cerr << '\n';
            }

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

} // namespace wolf_sv::transform
