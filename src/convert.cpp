#include "convert.hpp"

#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"

#include <cctype>
#include <span>
#include <utility>

namespace wolf_sv_parser {

namespace {

std::string toLowerCopy(std::string_view text)
{
    std::string lowered;
    lowered.reserve(text.size());
    for (unsigned char raw : text)
    {
        lowered.push_back(static_cast<char>(std::tolower(raw)));
    }
    return lowered;
}

void reportUnsupportedPort(const slang::ast::Symbol& symbol, std::string_view description,
                           ConvertDiagnostics* diagnostics)
{
    if (!diagnostics)
    {
        return;
    }
    std::string message = "Unsupported port form: ";
    message.append(description);
    diagnostics->error(symbol, message);
}

bool hasBlackboxAttribute(const slang::ast::InstanceBodySymbol& body)
{
    auto checkAttrs = [](std::span<const slang::ast::AttributeSymbol* const> attrs) {
        for (const slang::ast::AttributeSymbol* attr : attrs)
        {
            if (!attr)
            {
                continue;
            }
            const std::string lowered = toLowerCopy(attr->name);
            if (lowered == "blackbox" || lowered == "black_box" || lowered == "syn_black_box")
            {
                return true;
            }
        }
        return false;
    };

    slang::ast::Compilation& compilation = body.getCompilation();
    if (checkAttrs(compilation.getAttributes(body.getDefinition())))
    {
        return true;
    }
    return checkAttrs(compilation.getAttributes(body));
}

bool hasBlackboxImplementation(const slang::ast::InstanceBodySymbol& body)
{
    for (const slang::ast::Symbol& member : body.members())
    {
        if (member.as_if<slang::ast::ContinuousAssignSymbol>() ||
            member.as_if<slang::ast::ProceduralBlockSymbol>() ||
            member.as_if<slang::ast::InstanceSymbol>() ||
            member.as_if<slang::ast::InstanceArraySymbol>() ||
            member.as_if<slang::ast::GenerateBlockSymbol>() ||
            member.as_if<slang::ast::GenerateBlockArraySymbol>())
        {
            return true;
        }
    }
    return false;
}

bool isBlackboxBody(const slang::ast::InstanceBodySymbol& body, ConvertDiagnostics* diagnostics)
{
    const bool explicitAttribute = hasBlackboxAttribute(body);
    const bool hasImplementation = hasBlackboxImplementation(body);
    if (explicitAttribute && hasImplementation && diagnostics)
    {
        diagnostics->error(body.getDefinition(),
                           "Module marked as blackbox but contains implementation; treating as "
                           "normal module body");
    }
    return !hasImplementation;
}

void collectPorts(const slang::ast::InstanceBodySymbol& body, ModulePlan& plan,
                  ConvertDiagnostics* diagnostics)
{
    plan.ports.reserve(body.getPortList().size());

    for (const slang::ast::Symbol* portSymbol : body.getPortList())
    {
        if (!portSymbol)
        {
            continue;
        }

        if (const auto* port = portSymbol->as_if<slang::ast::PortSymbol>())
        {
            if (port->isNullPort || port->name.empty())
            {
                reportUnsupportedPort(*port,
                                      port->isNullPort ? "null ports are not supported"
                                                       : "anonymous ports are not supported",
                                      diagnostics);
                continue;
            }

            PortInfo info;
            info.name = plan.symbols.intern(port->name);
            switch (port->direction)
            {
            case slang::ast::ArgumentDirection::In:
                info.direction = PortDirection::Input;
                break;
            case slang::ast::ArgumentDirection::Out:
                info.direction = PortDirection::Output;
                break;
            case slang::ast::ArgumentDirection::InOut:
                info.direction = PortDirection::Inout;
                break;
            case slang::ast::ArgumentDirection::Ref:
                reportUnsupportedPort(*port,
                                      std::string("direction ") +
                                          std::string(slang::ast::toString(port->direction)),
                                      diagnostics);
                continue;
            }

            if (info.direction == PortDirection::Inout)
            {
                std::string base(port->name);
                info.inout = PortInfo::InoutBinding{
                    plan.symbols.intern(base + "__in"),
                    plan.symbols.intern(base + "__out"),
                    plan.symbols.intern(base + "__oe")};
            }

            plan.ports.push_back(std::move(info));
            continue;
        }

        if (const auto* multi = portSymbol->as_if<slang::ast::MultiPortSymbol>())
        {
            reportUnsupportedPort(*multi, "multi-port aggregations", diagnostics);
            continue;
        }

        if (const auto* iface = portSymbol->as_if<slang::ast::InterfacePortSymbol>())
        {
            reportUnsupportedPort(*iface, "interface ports", diagnostics);
            continue;
        }

        reportUnsupportedPort(*portSymbol, "unhandled symbol kind", diagnostics);
    }
}

void collectSignals(const slang::ast::InstanceBodySymbol& body, ModulePlan& plan,
                    ConvertDiagnostics* diagnostics)
{
    for (const slang::ast::Symbol& member : body.members())
    {
        if (const auto* net = member.as_if<slang::ast::NetSymbol>())
        {
            if (net->name.empty())
            {
                if (diagnostics)
                {
                    diagnostics->warn(*net, "Skipping anonymous net symbol");
                }
                continue;
            }
            SignalInfo info;
            info.name = plan.symbols.intern(net->name);
            info.kind = SignalKind::Net;
            plan.signals.push_back(std::move(info));
            continue;
        }

        if (const auto* variable = member.as_if<slang::ast::VariableSymbol>())
        {
            if (variable->name.empty())
            {
                if (diagnostics)
                {
                    diagnostics->warn(*variable, "Skipping anonymous variable symbol");
                }
                continue;
            }
            SignalInfo info;
            info.name = plan.symbols.intern(variable->name);
            info.kind = SignalKind::Variable;
            plan.signals.push_back(std::move(info));
            continue;
        }
    }
}

void enqueuePlanKey(ConvertContext& context, const slang::ast::InstanceBodySymbol& body)
{
    if (!context.planQueue)
    {
        return;
    }
    PlanKey key;
    key.body = &body;
    context.planQueue->push(std::move(key));
}

void collectInstance(const slang::ast::InstanceSymbol& instance, ModulePlan& plan,
                     ConvertContext& context)
{
    const slang::ast::InstanceBodySymbol& body = instance.body;

    InstanceInfo info;
    std::string_view instanceName = instance.name;
    if (instanceName.empty())
    {
        instanceName = instance.getArrayName();
    }
    info.instanceName = plan.symbols.intern(instanceName);

    std::string_view moduleName = body.getDefinition().name;
    if (moduleName.empty())
    {
        moduleName = instance.name;
    }
    info.moduleName = plan.symbols.intern(moduleName);
    info.isBlackbox = isBlackboxBody(body, context.diagnostics);
    plan.instances.push_back(std::move(info));

    enqueuePlanKey(context, body);
}

void collectInstanceArray(const slang::ast::InstanceArraySymbol& array, ModulePlan& plan,
                          ConvertContext& context);
void collectGenerateBlock(const slang::ast::GenerateBlockSymbol& block, ModulePlan& plan,
                          ConvertContext& context);
void collectGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array, ModulePlan& plan,
                               ConvertContext& context);

void collectInstanceArray(const slang::ast::InstanceArraySymbol& array, ModulePlan& plan,
                          ConvertContext& context)
{
    for (const slang::ast::Symbol* element : array.elements)
    {
        if (!element)
        {
            continue;
        }

        if (const auto* childInstance = element->as_if<slang::ast::InstanceSymbol>())
        {
            collectInstance(*childInstance, plan, context);
            continue;
        }

        if (const auto* nestedArray = element->as_if<slang::ast::InstanceArraySymbol>())
        {
            collectInstanceArray(*nestedArray, plan, context);
            continue;
        }

        if (const auto* generateBlock = element->as_if<slang::ast::GenerateBlockSymbol>())
        {
            collectGenerateBlock(*generateBlock, plan, context);
            continue;
        }

        if (const auto* generateArray = element->as_if<slang::ast::GenerateBlockArraySymbol>())
        {
            collectGenerateBlockArray(*generateArray, plan, context);
        }
    }
}

void collectGenerateBlock(const slang::ast::GenerateBlockSymbol& block, ModulePlan& plan,
                          ConvertContext& context)
{
    if (block.isUninstantiated)
    {
        return;
    }

    for (const slang::ast::Symbol& member : block.members())
    {
        if (const auto* childInstance = member.as_if<slang::ast::InstanceSymbol>())
        {
            collectInstance(*childInstance, plan, context);
            continue;
        }

        if (const auto* instanceArray = member.as_if<slang::ast::InstanceArraySymbol>())
        {
            collectInstanceArray(*instanceArray, plan, context);
            continue;
        }

        if (const auto* nestedBlock = member.as_if<slang::ast::GenerateBlockSymbol>())
        {
            collectGenerateBlock(*nestedBlock, plan, context);
            continue;
        }

        if (const auto* nestedArray = member.as_if<slang::ast::GenerateBlockArraySymbol>())
        {
            collectGenerateBlockArray(*nestedArray, plan, context);
            continue;
        }
    }
}

void collectGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array, ModulePlan& plan,
                               ConvertContext& context)
{
    for (const slang::ast::GenerateBlockSymbol* entry : array.entries)
    {
        if (!entry)
        {
            continue;
        }
        collectGenerateBlock(*entry, plan, context);
    }
}

void collectInstances(const slang::ast::InstanceBodySymbol& body, ModulePlan& plan,
                      ConvertContext& context)
{
    for (const slang::ast::Symbol& member : body.members())
    {
        if (const auto* childInstance = member.as_if<slang::ast::InstanceSymbol>())
        {
            collectInstance(*childInstance, plan, context);
            continue;
        }

        if (const auto* instanceArray = member.as_if<slang::ast::InstanceArraySymbol>())
        {
            collectInstanceArray(*instanceArray, plan, context);
            continue;
        }

        if (const auto* generateBlock = member.as_if<slang::ast::GenerateBlockSymbol>())
        {
            collectGenerateBlock(*generateBlock, plan, context);
            continue;
        }

        if (const auto* generateArray = member.as_if<slang::ast::GenerateBlockArraySymbol>())
        {
            collectGenerateBlockArray(*generateArray, plan, context);
            continue;
        }
    }
}

} // namespace

PlanSymbolId PlanSymbolTable::intern(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    if (auto it = index_.find(text); it != index_.end())
    {
        return it->second;
    }
    storage_.emplace_back(text);
    const std::string_view stored = storage_.back();
    PlanSymbolId id{static_cast<PlanIndex>(storage_.size() - 1)};
    index_.emplace(stored, id);
    return id;
}

PlanSymbolId PlanSymbolTable::lookup(std::string_view text) const
{
    if (text.empty())
    {
        return {};
    }
    if (auto it = index_.find(text); it != index_.end())
    {
        return it->second;
    }
    return {};
}

std::string_view PlanSymbolTable::text(PlanSymbolId id) const
{
    if (!id.valid() || id.index >= storage_.size())
    {
        return {};
    }
    return storage_[id.index];
}

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

bool PlanCache::tryClaim(const PlanKey& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end())
    {
        entries_.emplace(key, PlanEntry{PlanStatus::Planning, std::nullopt});
        return true;
    }
    if (it->second.status == PlanStatus::Planning)
    {
        return false;
    }
    if (it->second.status == PlanStatus::Done)
    {
        return false;
    }
    it->second.status = PlanStatus::Planning;
    it->second.plan.reset();
    return true;
}

PlanEntry& PlanCache::getOrCreateEntryLocked(const PlanKey& key)
{
    return entries_[key];
}

PlanEntry* PlanCache::findEntryLocked(const PlanKey& key)
{
    auto it = entries_.find(key);
    if (it == entries_.end())
    {
        return nullptr;
    }
    return &it->second;
}

const PlanEntry* PlanCache::findEntryLocked(const PlanKey& key) const
{
    auto it = entries_.find(key);
    if (it == entries_.end())
    {
        return nullptr;
    }
    return &it->second;
}

void PlanCache::storePlan(const PlanKey& key, ModulePlan plan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PlanEntry& entry = entries_[key];
    entry.status = PlanStatus::Done;
    entry.plan = std::move(plan);
}

void PlanCache::markFailed(const PlanKey& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PlanEntry& entry = entries_[key];
    entry.status = PlanStatus::Failed;
    entry.plan.reset();
}

std::optional<ModulePlan> PlanCache::findReady(const PlanKey& key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.status != PlanStatus::Done || !it->second.plan)
    {
        return std::nullopt;
    }
    return it->second.plan;
}

void PlanCache::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

static bool canWriteArtifacts(const PlanEntry& entry)
{
    if (entry.status == PlanStatus::Failed || entry.status == PlanStatus::Pending)
    {
        return false;
    }
    return true;
}

bool PlanCache::setLoweringPlan(const PlanKey& key, LoweringPlan plan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PlanEntry* entry = findEntryLocked(key);
    if (!entry || !canWriteArtifacts(*entry))
    {
        return false;
    }
    entry->artifacts.loweringPlan = std::move(plan);
    return true;
}

bool PlanCache::setWriteBackPlan(const PlanKey& key, WriteBackPlan plan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PlanEntry* entry = findEntryLocked(key);
    if (!entry || !canWriteArtifacts(*entry))
    {
        return false;
    }
    entry->artifacts.writeBackPlan = std::move(plan);
    return true;
}

std::optional<LoweringPlan> PlanCache::getLoweringPlan(const PlanKey& key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || !it->second.artifacts.loweringPlan)
    {
        return std::nullopt;
    }
    return it->second.artifacts.loweringPlan;
}

std::optional<WriteBackPlan> PlanCache::getWriteBackPlan(const PlanKey& key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || !it->second.artifacts.writeBackPlan)
    {
        return std::nullopt;
    }
    return it->second.artifacts.writeBackPlan;
}

bool PlanCache::withLoweringPlan(const PlanKey& key,
                                 const std::function<void(const LoweringPlan&)>& fn) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const PlanEntry* entry = findEntryLocked(key);
    if (!entry || !entry->artifacts.loweringPlan || !fn)
    {
        return false;
    }
    fn(*entry->artifacts.loweringPlan);
    return true;
}

bool PlanCache::withWriteBackPlan(const PlanKey& key,
                                  const std::function<void(const WriteBackPlan&)>& fn) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const PlanEntry* entry = findEntryLocked(key);
    if (!entry || !entry->artifacts.writeBackPlan || !fn)
    {
        return false;
    }
    fn(*entry->artifacts.writeBackPlan);
    return true;
}

bool PlanCache::withLoweringPlanMut(const PlanKey& key,
                                    const std::function<void(LoweringPlan&)>& fn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PlanEntry* entry = findEntryLocked(key);
    if (!entry || !entry->artifacts.loweringPlan || !fn || !canWriteArtifacts(*entry))
    {
        return false;
    }
    fn(*entry->artifacts.loweringPlan);
    return true;
}

bool PlanCache::withWriteBackPlanMut(const PlanKey& key,
                                     const std::function<void(WriteBackPlan&)>& fn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    PlanEntry* entry = findEntryLocked(key);
    if (!entry || !entry->artifacts.writeBackPlan || !fn || !canWriteArtifacts(*entry))
    {
        return false;
    }
    fn(*entry->artifacts.writeBackPlan);
    return true;
}

void PlanTaskQueue::push(PlanKey key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_)
    {
        return;
    }
    queue_.push_back(std::move(key));
}

bool PlanTaskQueue::tryPop(PlanKey& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
    {
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void PlanTaskQueue::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
}

bool PlanTaskQueue::closed() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

std::size_t PlanTaskQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void PlanTaskQueue::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    closed_ = false;
}

ModulePlan ModulePlanner::plan(const slang::ast::InstanceBodySymbol& body)
{
    ModulePlan plan;
    plan.body = &body;
    std::string_view moduleName = body.name;
    if (moduleName.empty())
    {
        moduleName = body.getDefinition().name;
    }
    plan.symbol = plan.symbols.intern(moduleName);
    collectPorts(body, plan, context_.diagnostics);
    collectSignals(body, plan, context_.diagnostics);
    collectInstances(body, plan, context_);
    return plan;
}

grh::ir::Graph& GraphAssembler::build(const ModulePlan& plan)
{
    std::string symbol;
    if (plan.symbol.valid())
    {
        symbol = std::string(plan.symbols.text(plan.symbol));
    }
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

    planCache_.clear();
    planQueue_.reset();

    ConvertContext context{};
    context.compilation = &root.getCompilation();
    context.root = &root;
    context.options = options_;
    context.diagnostics = &diagnostics_;
    context.logger = &logger_;
    context.planCache = &planCache_;
    context.planQueue = &planQueue_;

    ModulePlanner planner(context);
    for (const slang::ast::InstanceSymbol* topInstance : root.topInstances)
    {
        if (!topInstance)
        {
            continue;
        }
        enqueuePlanKey(context, topInstance->body);
    }

    PlanKey key;
    while (planQueue_.tryPop(key))
    {
        if (!key.body)
        {
            continue;
        }
        if (!planCache_.tryClaim(key))
        {
            continue;
        }
        ModulePlan plan = planner.plan(*key.body);
        planCache_.storePlan(key, std::move(plan));
    }
    return netlist;
}

} // namespace wolf_sv_parser
