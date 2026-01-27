#include "convert.hpp"

#include "slang/ast/Symbol.h"

#include <utility>

namespace wolf_sv_parser {

void ConvertDiagnostics::todo(const slang::ast::Symbol& symbol, std::string message)
{
    add(ConvertDiagnosticKind::Todo, symbol, std::move(message));
}

void ConvertDiagnostics::error(const slang::ast::Symbol& symbol, std::string message)
{
    add(ConvertDiagnosticKind::Error, symbol, std::move(message));
}

void ConvertDiagnostics::warn(const slang::ast::Symbol& symbol, std::string message)
{
    add(ConvertDiagnosticKind::Warning, symbol, std::move(message));
}

void ConvertDiagnostics::todo(const slang::SourceLocation& location, std::string message,
                              std::string originSymbol)
{
    add(ConvertDiagnosticKind::Todo, std::move(originSymbol),
        location.valid() ? std::optional(location) : std::nullopt, std::move(message));
}

void ConvertDiagnostics::error(const slang::SourceLocation& location, std::string message,
                               std::string originSymbol)
{
    add(ConvertDiagnosticKind::Error, std::move(originSymbol),
        location.valid() ? std::optional(location) : std::nullopt, std::move(message));
}

void ConvertDiagnostics::warn(const slang::SourceLocation& location, std::string message,
                              std::string originSymbol)
{
    add(ConvertDiagnosticKind::Warning, std::move(originSymbol),
        location.valid() ? std::optional(location) : std::nullopt, std::move(message));
}

void ConvertDiagnostics::add(ConvertDiagnosticKind kind, const slang::ast::Symbol& symbol,
                             std::string message)
{
    add(kind, std::string(symbol.name),
        symbol.location.valid() ? std::optional(symbol.location) : std::nullopt,
        std::move(message));
}

void ConvertDiagnostics::add(ConvertDiagnosticKind kind, std::string originSymbol,
                             std::optional<slang::SourceLocation> location,
                             std::string message)
{
    messages_.push_back(ConvertDiagnostic{kind, std::move(message),
                                          std::move(originSymbol), location});
    if (kind == ConvertDiagnosticKind::Error)
    {
        hasError_ = true;
        if (onError_)
        {
            onError_();
        }
    }
}

void ConvertLogger::allowTag(std::string_view tag)
{
    tags_.insert(std::string(tag));
}

void ConvertLogger::clearTags()
{
    tags_.clear();
}

bool ConvertLogger::enabled(ConvertLogLevel level, std::string_view tag) const noexcept
{
    if (!enabled_ || level_ == ConvertLogLevel::Off)
    {
        return false;
    }
    if (static_cast<int>(level) < static_cast<int>(level_))
    {
        return false;
    }
    if (!tags_.empty() && tags_.find(std::string(tag)) == tags_.end())
    {
        return false;
    }
    return true;
}

void ConvertLogger::log(ConvertLogLevel level, std::string_view tag, std::string_view message)
{
    if (!enabled(level, tag) || !sink_)
    {
        return;
    }
    ConvertLogEvent event{level, std::string(tag), std::string(message)};
    sink_(event);
}

ModulePlan ModulePlanner::plan(const slang::ast::InstanceBodySymbol& body)
{
    ModulePlan plan;
    plan.body = &body;
    return plan;
}

grh::ir::Graph& GraphAssembler::build(const ModulePlan& plan)
{
    std::string symbol = plan.symbol;
    if (symbol.empty())
    {
        symbol = "convert_graph_" + std::to_string(nextAnonymousId_++);
    }
    return netlist_.createGraph(std::move(symbol));
}

ConvertDriver::ConvertDriver(ConvertOptions options)
    : options_(options)
{
    logger_.setLevel(options_.logLevel);
    if (options_.enableLogging)
    {
        logger_.enable();
    }
    if (options_.abortOnError)
    {
        diagnostics_.setOnError([]() { throw ConvertAbort(); });
    }
}

grh::ir::Netlist ConvertDriver::convert(const slang::ast::RootSymbol& root)
{
    grh::ir::Netlist netlist;

    ConvertContext context{};
    context.root = &root;
    context.options = options_;
    context.diagnostics = &diagnostics_;
    context.logger = &logger_;

    (void)context;
    return netlist;
}

} // namespace wolf_sv_parser
