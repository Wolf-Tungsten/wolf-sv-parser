#include "transform.hpp"

#include <charconv>
#include <cctype>
#include <iostream>
#include <system_error>
#include <utility>

namespace wolf_sv::transform
{

    namespace
    {
        std::string toLowerCopy(std::string_view text)
        {
            std::string lowered;
            lowered.reserve(text.size());
            for (char c : text)
            {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return lowered;
        }
    } // namespace

    void PassDiagnostics::error(std::string passId, std::string message, std::string context)
    {
        messages_.push_back(PassDiagnostic{
            .kind = PassDiagnosticKind::Error,
            .message = std::move(message),
            .context = std::move(context),
            .passId = std::move(passId),
        });
    }

    void PassDiagnostics::warning(std::string passId, std::string message, std::string context)
    {
        messages_.push_back(PassDiagnostic{
            .kind = PassDiagnosticKind::Warning,
            .message = std::move(message),
            .context = std::move(context),
            .passId = std::move(passId),
        });
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

    bool PassConfig::getBool(std::string_view key, bool defaultValue) const
    {
        auto it = args.find(std::string(key));
        if (it == args.end())
        {
            return defaultValue;
        }

        const std::string lowered = toLowerCopy(it->second);
        if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
        {
            return true;
        }
        if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
        {
            return false;
        }
        return defaultValue;
    }

    int64_t PassConfig::getInt(std::string_view key, int64_t defaultValue) const
    {
        auto it = args.find(std::string(key));
        if (it == args.end())
        {
            return defaultValue;
        }

        int64_t value = defaultValue;
        const std::string &text = it->second;
        const char *begin = text.data();
        const char *end = text.data() + text.size();
        auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc())
        {
            return defaultValue;
        }
        return value;
    }

    std::string PassConfig::getString(std::string_view key, std::string defaultValue) const
    {
        auto it = args.find(std::string(key));
        if (it == args.end())
        {
            return defaultValue;
        }
        return it->second;
    }

    Pass::Pass(std::string id, std::string name, std::string description)
        : id_(std::move(id)), name_(std::move(name)), description_(std::move(description))
    {
    }

    void Pass::configure(const PassConfig &)
    {
    }

    PassManager::PassManager(PassPipelineOptions options)
        : options_(options)
    {
    }

    void PassManager::addPass(std::unique_ptr<Pass> pass, PassConfig config)
    {
        PassEntry entry;
        entry.config = std::move(config);
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
            if (!entry.config.enabled)
            {
                continue;
            }

            if (options_.stopOnError && diags.hasError())
            {
                encounteredFailure = true;
                break;
            }

            context.entryName = entry.instance ? entry.instance->id() : std::string_view();
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

            pass->configure(entry.config);

            if (options_.verbose)
            {
                std::cerr << "[transform] [" << pass->id() << "] start\n";
            }

            PassResult passResult = pass->run(context);
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
