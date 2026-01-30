#include "convert.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Statement.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/numeric/SVInt.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/text/SourceManager.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <variant>

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

std::string normalizeSystemTaskName(std::string_view name)
{
    if (!name.empty() && name.front() == '$')
    {
        name.remove_prefix(1);
    }
    return toLowerCopy(name);
}

std::string sanitizeParamToken(std::string_view text, bool allowLeadingDigit = false)
{
    std::string result;
    result.reserve(text.size());
    bool lastUnderscore = false;

    for (unsigned char raw : text)
    {
        char ch = static_cast<char>(raw);
        if (std::isalnum(raw) || ch == '_' || ch == '$')
        {
            result.push_back(ch);
            lastUnderscore = false;
            continue;
        }

        if (lastUnderscore)
        {
            continue;
        }

        result.push_back('_');
        lastUnderscore = true;
    }

    if (!result.empty() && result.back() == '_')
    {
        result.pop_back();
    }

    if (!allowLeadingDigit && !result.empty() &&
        std::isdigit(static_cast<unsigned char>(result.front())))
    {
        result.insert(result.begin(), '_');
    }

    return result;
}

std::string parameterValueToString(const slang::ConstantValue& value)
{
    if (value.bad())
    {
        return "bad";
    }

    std::string sanitized = sanitizeParamToken(value.toString(), true);
    if (sanitized.empty())
    {
        sanitized = "value";
    }
    return sanitized;
}

std::string typeParameterToString(const slang::ast::TypeParameterSymbol& param)
{
    return sanitizeParamToken(param.getTypeAlias().toString());
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

struct ParameterSnapshot {
    std::string signature;
    std::vector<InstanceParameter> parameters;
};

ParameterSnapshot snapshotParameters(const slang::ast::InstanceBodySymbol& body, ModulePlan* plan)
{
    ParameterSnapshot snapshot;
    for (const slang::ast::ParameterSymbolBase* paramBase : body.getParameters())
    {
        if (!paramBase || paramBase->isLocalParam())
        {
            continue;
        }

        const std::string_view name = paramBase->symbol.name;
        if (name.empty())
        {
            continue;
        }

        std::string value;
        if (const auto* valueParam = paramBase->symbol.as_if<slang::ast::ParameterSymbol>())
        {
            value = parameterValueToString(valueParam->getValue());
        }
        else if (const auto* typeParam =
                     paramBase->symbol.as_if<slang::ast::TypeParameterSymbol>())
        {
            value = typeParameterToString(*typeParam);
        }
        else
        {
            value = "unsupported_param";
        }

        if (value.empty())
        {
            continue;
        }

        if (!snapshot.signature.empty())
        {
            snapshot.signature.push_back(';');
        }
        snapshot.signature.append(name);
        snapshot.signature.push_back('=');
        snapshot.signature.append(value);

        if (plan)
        {
            InstanceParameter param;
            param.symbol = plan->symbolTable.intern(name);
            param.value = value;
            snapshot.parameters.push_back(std::move(param));
        }
    }

    return snapshot;
}

struct TypeResolution {
    int32_t width = 1;
    bool isSigned = false;
    int64_t memoryRows = 0;
    std::vector<int32_t> packedDims;
    std::vector<UnpackedDimInfo> unpackedDims;
    bool hasUnpacked = false;
};

int32_t clampWidth(uint64_t width, const slang::ast::Symbol& origin, ConvertDiagnostics* diagnostics,
                   std::string_view label)
{
    if (width == 0)
    {
        if (diagnostics)
        {
            std::string message(label);
            message.append(" has indeterminate width; treating as 1-bit placeholder");
            diagnostics->error(origin, std::move(message));
        }
        return 1;
    }

    constexpr uint64_t maxValue = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
    if (width > maxValue)
    {
        if (diagnostics)
        {
            std::string message(label);
            message.append(" width exceeds GRH limit; clamping to int32_t::max");
            diagnostics->error(origin, std::move(message));
        }
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(width);
}

int32_t clampDim(uint64_t width, const slang::ast::Symbol& origin, ConvertDiagnostics* diagnostics,
                 std::string_view label)
{
    if (width == 0)
    {
        if (diagnostics)
        {
            std::string message(label);
            message.append(" must have positive extent; treating as 1");
            diagnostics->error(origin, std::move(message));
        }
        return 1;
    }

    constexpr uint64_t maxValue = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
    if (width > maxValue)
    {
        if (diagnostics)
        {
            std::string message(label);
            message.append(" exceeds GRH limit; clamping to int32_t::max");
            diagnostics->error(origin, std::move(message));
        }
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(width);
}

uint64_t computeFixedWidth(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                           ConvertDiagnostics* diagnostics)
{
    const uint64_t bitstreamWidth = type.getBitstreamWidth();
    if (bitstreamWidth > 0)
    {
        return bitstreamWidth;
    }

    if (type.hasFixedRange())
    {
        const uint64_t selectable = type.getSelectableWidth();
        if (selectable > 0)
        {
            return selectable;
        }
    }

    const slang::ast::Type& canonical = type.getCanonicalType();
    auto accumulateStruct = [&](const slang::ast::Scope& scope, bool isUnion) -> uint64_t {
        uint64_t total = 0;
        uint64_t maxWidth = 0;
        for (const auto& field : scope.membersOfType<slang::ast::FieldSymbol>())
        {
            const uint64_t fieldWidth = computeFixedWidth(field.getType(), field, diagnostics);
            if (fieldWidth == 0)
            {
                continue;
            }
            total += fieldWidth;
            if (fieldWidth > maxWidth)
            {
                maxWidth = fieldWidth;
            }
        }
        return isUnion ? maxWidth : total;
    };

    switch (canonical.kind)
    {
    case slang::ast::SymbolKind::PackedArrayType: {
        const auto& packed = canonical.as<slang::ast::PackedArrayType>();
        const uint64_t elementWidth = computeFixedWidth(packed.elementType, origin, diagnostics);
        if (elementWidth == 0)
        {
            return 0;
        }
        const uint64_t elements = packed.range.fullWidth();
        return elementWidth * elements;
    }
    case slang::ast::SymbolKind::FixedSizeUnpackedArrayType: {
        const auto& unpacked = canonical.as<slang::ast::FixedSizeUnpackedArrayType>();
        const uint64_t elementWidth = computeFixedWidth(unpacked.elementType, origin, diagnostics);
        if (elementWidth == 0)
        {
            return 0;
        }
        const uint64_t elements = unpacked.range.fullWidth();
        return elementWidth * elements;
    }
    case slang::ast::SymbolKind::PackedStructType:
        return accumulateStruct(canonical.as<slang::ast::Scope>(), false);
    case slang::ast::SymbolKind::UnpackedStructType:
        return accumulateStruct(canonical.as<slang::ast::Scope>(), false);
    case slang::ast::SymbolKind::PackedUnionType:
        return accumulateStruct(canonical.as<slang::ast::Scope>(), true);
    case slang::ast::SymbolKind::UnpackedUnionType:
        return accumulateStruct(canonical.as<slang::ast::Scope>(), true);
    case slang::ast::SymbolKind::TypeAlias: {
        const auto& alias = canonical.as<slang::ast::TypeAliasType>();
        return computeFixedWidth(alias.targetType.getType(), origin, diagnostics);
    }
    default:
        break;
    }
    return 0;
}

void collectPackedDims(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                       ConvertDiagnostics* diagnostics, std::vector<int32_t>& out)
{
    const slang::ast::Type* current = &type;
    while (current)
    {
        const slang::ast::Type& canonical = current->getCanonicalType();
        if (canonical.kind == slang::ast::SymbolKind::TypeAlias)
        {
            current = &canonical.as<slang::ast::TypeAliasType>().targetType.getType();
            continue;
        }
        if (canonical.kind == slang::ast::SymbolKind::PackedArrayType)
        {
            const auto& packed = canonical.as<slang::ast::PackedArrayType>();
            const uint64_t extent = packed.range.fullWidth();
            out.push_back(clampDim(extent, origin, diagnostics, "Packed array dimension"));
            current = &packed.elementType;
            continue;
        }
        break;
    }
}

bool containsUnpackedDims(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                          ConvertDiagnostics* diagnostics)
{
    const slang::ast::Type* current = &type;
    while (current)
    {
        const slang::ast::Type& canonical = current->getCanonicalType();
        switch (canonical.kind)
        {
        case slang::ast::SymbolKind::TypeAlias:
            current = &canonical.as<slang::ast::TypeAliasType>().targetType.getType();
            continue;
        case slang::ast::SymbolKind::PackedArrayType:
            current = &canonical.as<slang::ast::PackedArrayType>().elementType;
            continue;
        case slang::ast::SymbolKind::FixedSizeUnpackedArrayType:
            return true;
        case slang::ast::SymbolKind::DynamicArrayType:
        case slang::ast::SymbolKind::AssociativeArrayType:
        case slang::ast::SymbolKind::QueueType:
            if (diagnostics)
            {
                diagnostics->error(origin,
                                   "Unsupported unpacked array kind on port declaration");
            }
            return true;
        default:
            break;
        }
        break;
    }
    return false;
}

TypeResolution analyzeSignalType(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                                 ConvertDiagnostics* diagnostics)
{
    TypeResolution info{};
    const slang::ast::Type* current = &type;
    int64_t rows = 1;

    while (current)
    {
        const slang::ast::Type& canonical = current->getCanonicalType();
        if (canonical.kind == slang::ast::SymbolKind::TypeAlias)
        {
            current = &canonical.as<slang::ast::TypeAliasType>().targetType.getType();
            continue;
        }
        if (canonical.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType)
        {
            info.hasUnpacked = true;
            const auto& unpacked = canonical.as<slang::ast::FixedSizeUnpackedArrayType>();
            const slang::ConstantRange range = unpacked.range;
            uint64_t extent = unpacked.range.fullWidth();
            if (extent == 0)
            {
                if (diagnostics)
                {
                    diagnostics->error(origin,
                                       "Unpacked array dimension must have positive extent");
                }
                extent = 1;
            }
            UnpackedDimInfo dim;
            dim.extent = clampDim(extent, origin, diagnostics, "Unpacked array dimension");
            dim.left = range.left;
            dim.right = range.right;
            info.unpackedDims.push_back(dim);

            constexpr uint64_t maxRows =
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
            uint64_t total = static_cast<uint64_t>(rows);
            if (extent != 0 && total > maxRows / extent)
            {
                if (diagnostics)
                {
                    diagnostics->error(origin,
                                       "Memory row count exceeds GRH limit; clamping to "
                                       "int64_t::max");
                }
                rows = std::numeric_limits<int64_t>::max();
            }
            else
            {
                total *= extent;
                rows = static_cast<int64_t>(total);
            }

            current = &unpacked.elementType;
            continue;
        }

        if (canonical.kind == slang::ast::SymbolKind::DynamicArrayType ||
            canonical.kind == slang::ast::SymbolKind::AssociativeArrayType ||
            canonical.kind == slang::ast::SymbolKind::QueueType)
        {
            if (diagnostics)
            {
                diagnostics->error(origin,
                                   "Unsupported unpacked array kind on signal declaration");
            }
            break;
        }
        break;
    }

    if (!current)
    {
        return info;
    }

    collectPackedDims(*current, origin, diagnostics, info.packedDims);
    const uint64_t width = computeFixedWidth(*current, origin, diagnostics);
    info.width = clampWidth(width, origin, diagnostics, "Signal");
    info.isSigned = current->isSigned();
    if (info.hasUnpacked)
    {
        info.memoryRows = rows > 0 ? rows : 1;
    }
    return info;
}

TypeResolution analyzePortType(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                               ConvertDiagnostics* diagnostics)
{
    TypeResolution info{};
    if (containsUnpackedDims(type, origin, diagnostics))
    {
        if (diagnostics)
        {
            diagnostics->warn(origin,
                              "Unpacked array port flattened; array dimensions are ignored");
        }
    }
    const uint64_t width = computeFixedWidth(type, origin, diagnostics);
    info.width = clampWidth(width, origin, diagnostics, "Port");
    info.isSigned = type.isSigned();
    return info;
}

const slang::ast::TimingControl* findTimingControl(const slang::ast::Statement& stmt)
{
    if (const auto* timed = stmt.as_if<slang::ast::TimedStatement>())
    {
        return &timed->timing;
    }

    if (const auto* block = stmt.as_if<slang::ast::BlockStatement>())
    {
        return findTimingControl(block->body);
    }

    if (const auto* list = stmt.as_if<slang::ast::StatementList>())
    {
        for (const slang::ast::Statement* child : list->list)
        {
            if (!child)
            {
                continue;
            }
            if (const auto* timing = findTimingControl(*child))
            {
                return timing;
            }
        }
    }
    return nullptr;
}

bool isLevelSensitiveEventList(const slang::ast::TimingControl& timing)
{
    using slang::ast::TimingControlKind;
    switch (timing.kind)
    {
    case TimingControlKind::SignalEvent: {
        const auto& signal = timing.as<slang::ast::SignalEventControl>();
        return signal.edge == slang::ast::EdgeKind::None;
    }
    case TimingControlKind::EventList: {
        const auto& list = timing.as<slang::ast::EventListControl>();
        bool hasSignal = false;
        for (const slang::ast::TimingControl* ctrl : list.events)
        {
            if (!ctrl)
            {
                continue;
            }
            if (!isLevelSensitiveEventList(*ctrl))
            {
                return false;
            }
            hasSignal = true;
        }
        return hasSignal;
    }
    case TimingControlKind::RepeatedEvent:
        return isLevelSensitiveEventList(
            timing.as<slang::ast::RepeatedEventControl>().event);
    default:
        return false;
    }
}

bool containsEdgeSensitiveEvent(const slang::ast::TimingControl& timing)
{
    using slang::ast::TimingControlKind;
    switch (timing.kind)
    {
    case TimingControlKind::SignalEvent: {
        const auto& signal = timing.as<slang::ast::SignalEventControl>();
        return signal.edge != slang::ast::EdgeKind::None;
    }
    case TimingControlKind::EventList: {
        const auto& list = timing.as<slang::ast::EventListControl>();
        for (const slang::ast::TimingControl* ctrl : list.events)
        {
            if (!ctrl)
            {
                continue;
            }
            if (containsEdgeSensitiveEvent(*ctrl))
            {
                return true;
            }
        }
        return false;
    }
    case TimingControlKind::RepeatedEvent:
        return containsEdgeSensitiveEvent(
            timing.as<slang::ast::RepeatedEventControl>().event);
    default:
        return false;
    }
}

ControlDomain classifyProceduralBlock(const slang::ast::ProceduralBlockSymbol& block)
{
    using slang::ast::ProceduralBlockKind;
    switch (block.procedureKind)
    {
    case ProceduralBlockKind::AlwaysComb:
        return ControlDomain::Combinational;
    case ProceduralBlockKind::AlwaysLatch:
        return ControlDomain::Latch;
    case ProceduralBlockKind::AlwaysFF:
    case ProceduralBlockKind::Initial:
    case ProceduralBlockKind::Final:
        return ControlDomain::Sequential;
    case ProceduralBlockKind::Always: {
        const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
        if (!timing || timing->kind == slang::ast::TimingControlKind::ImplicitEvent)
        {
            return ControlDomain::Combinational;
        }
        if (containsEdgeSensitiveEvent(*timing))
        {
            return ControlDomain::Sequential;
        }
        if (isLevelSensitiveEventList(*timing))
        {
            return ControlDomain::Combinational;
        }
        return ControlDomain::Unknown;
    }
    default:
        break;
    }
    return ControlDomain::Unknown;
}

uint64_t encodeRWKey(SignalId id, ControlDomain domain, bool isWrite)
{
    return (static_cast<uint64_t>(id) << 3) |
           (static_cast<uint64_t>(domain) << 1) |
           (isWrite ? 1ULL : 0ULL);
}

uint64_t encodeMemKey(SignalId id, bool isRead, bool isWrite, bool isMasked, bool isSync,
                      bool hasReset)
{
    uint64_t key = static_cast<uint64_t>(id) << 5;
    key |= (isRead ? 1ULL : 0ULL) << 0;
    key |= (isWrite ? 1ULL : 0ULL) << 1;
    key |= (isMasked ? 1ULL : 0ULL) << 2;
    key |= (isSync ? 1ULL : 0ULL) << 3;
    key |= (hasReset ? 1ULL : 0ULL) << 4;
    return key;
}

struct RWAnalyzerState {
    ModulePlan& plan;
    ConvertDiagnostics* diagnostics;
    std::vector<SignalId> signalBySymbol;
    std::unordered_map<uint64_t, RWOpId> rwKeys;
    std::unordered_map<uint64_t, MemoryPortId> memKeys;
    uint32_t nextSite = 0;

    explicit RWAnalyzerState(ModulePlan& plan, ConvertDiagnostics* diagnostics)
        : plan(plan), diagnostics(diagnostics)
    {
        signalBySymbol.assign(plan.symbolTable.size(), kInvalidPlanIndex);
        for (SignalId i = 0; i < static_cast<SignalId>(plan.signals.size()); ++i)
        {
            const PlanSymbolId id = plan.signals[i].symbol;
            if (id.valid() && id.index < signalBySymbol.size())
            {
                signalBySymbol[id.index] = i;
            }
        }
    }

    SignalId resolveSignal(const slang::ast::ValueSymbol& symbol) const
    {
        if (symbol.name.empty())
        {
            return kInvalidPlanIndex;
        }
        const PlanSymbolId id = plan.symbolTable.lookup(symbol.name);
        if (!id.valid() || id.index >= signalBySymbol.size())
        {
            return kInvalidPlanIndex;
        }
        return signalBySymbol[id.index];
    }

    void recordRead(const slang::ast::ValueSymbol& symbol, ControlDomain domain,
                    slang::SourceLocation location)
    {
        recordAccess(resolveSignal(symbol), domain, false, location);
    }

    void recordWrite(const slang::ast::ValueSymbol& symbol, ControlDomain domain,
                     slang::SourceLocation location)
    {
        recordAccess(resolveSignal(symbol), domain, true, location);
    }

private:
    void recordAccess(SignalId id, ControlDomain domain, bool isWrite,
                      slang::SourceLocation location)
    {
        if (id == kInvalidPlanIndex)
        {
            return;
        }
        const uint64_t key = encodeRWKey(id, domain, isWrite);
        AccessSite site{location, nextSite++};
        auto [rwIt, inserted] = rwKeys.emplace(key, static_cast<RWOpId>(plan.rwOps.size()));
        if (inserted)
        {
            plan.rwOps.push_back(RWOp{id, domain, isWrite});
        }
        RWOp& op = plan.rwOps[rwIt->second];
        op.sites.push_back(site);

        if (plan.signals[id].memoryRows > 0)
        {
            if (isWrite)
            {
                recordMemoryPort(id, false, true, domain, site);
            }
            else
            {
                recordMemoryPort(id, true, false, domain, site);
            }
        }
    }

    void recordMemoryPort(SignalId id, bool isRead, bool isWrite, ControlDomain domain,
                          const AccessSite& site)
    {
        const bool isSync = domain == ControlDomain::Sequential;
        const bool isMasked = false;
        const bool hasReset = false;
        const uint64_t key = encodeMemKey(id, isRead, isWrite, isMasked, isSync, hasReset);
        auto [memIt, inserted] = memKeys.emplace(
            key, static_cast<MemoryPortId>(plan.memPorts.size()));
        if (inserted)
        {
            MemoryPortInfo info;
            info.memory = id;
            info.isRead = isRead;
            info.isWrite = isWrite;
            info.isMasked = isMasked;
            info.isSync = isSync;
            info.hasReset = hasReset;
            plan.memPorts.push_back(std::move(info));
        }
        MemoryPortInfo& info = plan.memPorts[memIt->second];
        info.sites.push_back(site);
    }
};

class RWVisitor : public slang::ast::ASTVisitor<RWVisitor, true, true> {
public:
    RWVisitor(RWAnalyzerState& state, ControlDomain domain)
        : state_(state), domain_(domain) {}

    void handle(const slang::ast::AssignmentExpression& expr)
    {
        const bool saved = inLValue_;
        inLValue_ = true;
        expr.left().visit(*this);
        inLValue_ = false;
        expr.right().visit(*this);
        inLValue_ = saved;
    }

    void handle(const slang::ast::UnaryExpression& expr)
    {
        if (!slang::ast::OpInfo::isLValue(expr.op))
        {
            visitDefault(expr);
            return;
        }
        const bool saved = inLValue_;
        inLValue_ = true;
        expr.operand().visit(*this);
        inLValue_ = false;
        expr.operand().visit(*this);
        inLValue_ = saved;
    }

    void handle(const slang::ast::ElementSelectExpression& expr)
    {
        if (!inLValue_)
        {
            visitDefault(expr);
            return;
        }
        const bool saved = inLValue_;
        expr.value().visit(*this);
        inLValue_ = false;
        expr.selector().visit(*this);
        inLValue_ = saved;
    }

    void handle(const slang::ast::RangeSelectExpression& expr)
    {
        if (!inLValue_)
        {
            visitDefault(expr);
            return;
        }
        const bool saved = inLValue_;
        expr.value().visit(*this);
        inLValue_ = false;
        expr.left().visit(*this);
        expr.right().visit(*this);
        inLValue_ = saved;
    }

    void handle(const slang::ast::NamedValueExpression& expr)
    {
        recordSymbol(expr.symbol, expr.sourceRange.start());
    }

    void handle(const slang::ast::HierarchicalValueExpression& expr)
    {
        recordSymbol(expr.symbol, expr.sourceRange.start());
    }

private:
    void recordSymbol(const slang::ast::ValueSymbol& symbol, slang::SourceLocation location)
    {
        if (inLValue_)
        {
            state_.recordWrite(symbol, domain_, location);
        }
        else
        {
            state_.recordRead(symbol, domain_, location);
        }
    }

    RWAnalyzerState& state_;
    ControlDomain domain_;
    bool inLValue_ = false;
};

void analyzeGenerateBlock(const slang::ast::GenerateBlockSymbol& block, RWAnalyzerState& state);
void analyzeGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                               RWAnalyzerState& state);

void analyzeProceduralBlock(const slang::ast::ProceduralBlockSymbol& block, RWAnalyzerState& state)
{
    const ControlDomain domain = classifyProceduralBlock(block);
    RWVisitor visitor(state, domain);
    block.getBody().visit(visitor);
}

void analyzeContinuousAssign(const slang::ast::ContinuousAssignSymbol& assign,
                             RWAnalyzerState& state)
{
    RWVisitor visitor(state, ControlDomain::Combinational);
    assign.getAssignment().visit(visitor);
}

void analyzeMemberSymbol(const slang::ast::Symbol& member, RWAnalyzerState& state)
{
    if (const auto* continuous = member.as_if<slang::ast::ContinuousAssignSymbol>())
    {
        analyzeContinuousAssign(*continuous, state);
        return;
    }
    if (const auto* block = member.as_if<slang::ast::ProceduralBlockSymbol>())
    {
        analyzeProceduralBlock(*block, state);
        return;
    }
    if (const auto* generateBlock = member.as_if<slang::ast::GenerateBlockSymbol>())
    {
        analyzeGenerateBlock(*generateBlock, state);
        return;
    }
    if (const auto* generateArray = member.as_if<slang::ast::GenerateBlockArraySymbol>())
    {
        analyzeGenerateBlockArray(*generateArray, state);
        return;
    }
}

void analyzeGenerateBlock(const slang::ast::GenerateBlockSymbol& block, RWAnalyzerState& state)
{
    if (block.isUninstantiated)
    {
        return;
    }
    for (const slang::ast::Symbol& member : block.members())
    {
        analyzeMemberSymbol(member, state);
    }
}

void analyzeGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                               RWAnalyzerState& state)
{
    for (const slang::ast::GenerateBlockSymbol* entry : array.entries)
    {
        if (!entry)
        {
            continue;
        }
        analyzeGenerateBlock(*entry, state);
    }
}

std::optional<grh::ir::OperationKind> mapUnaryOp(slang::ast::UnaryOperator op)
{
    switch (op)
    {
    case slang::ast::UnaryOperator::BitwiseNot:
        return grh::ir::OperationKind::kNot;
    case slang::ast::UnaryOperator::LogicalNot:
        return grh::ir::OperationKind::kLogicNot;
    case slang::ast::UnaryOperator::BitwiseAnd:
        return grh::ir::OperationKind::kReduceAnd;
    case slang::ast::UnaryOperator::BitwiseOr:
        return grh::ir::OperationKind::kReduceOr;
    case slang::ast::UnaryOperator::BitwiseXor:
        return grh::ir::OperationKind::kReduceXor;
    case slang::ast::UnaryOperator::BitwiseNand:
        return grh::ir::OperationKind::kReduceNand;
    case slang::ast::UnaryOperator::BitwiseNor:
        return grh::ir::OperationKind::kReduceNor;
    case slang::ast::UnaryOperator::BitwiseXnor:
        return grh::ir::OperationKind::kReduceXnor;
    default:
        break;
    }
    return std::nullopt;
}

std::optional<grh::ir::OperationKind> mapBinaryOp(slang::ast::BinaryOperator op)
{
    switch (op)
    {
    case slang::ast::BinaryOperator::Add:
        return grh::ir::OperationKind::kAdd;
    case slang::ast::BinaryOperator::Subtract:
        return grh::ir::OperationKind::kSub;
    case slang::ast::BinaryOperator::Multiply:
        return grh::ir::OperationKind::kMul;
    case slang::ast::BinaryOperator::Divide:
        return grh::ir::OperationKind::kDiv;
    case slang::ast::BinaryOperator::Mod:
        return grh::ir::OperationKind::kMod;
    case slang::ast::BinaryOperator::BinaryAnd:
        return grh::ir::OperationKind::kAnd;
    case slang::ast::BinaryOperator::BinaryOr:
        return grh::ir::OperationKind::kOr;
    case slang::ast::BinaryOperator::BinaryXor:
        return grh::ir::OperationKind::kXor;
    case slang::ast::BinaryOperator::BinaryXnor:
        return grh::ir::OperationKind::kXnor;
    case slang::ast::BinaryOperator::Equality:
        return grh::ir::OperationKind::kEq;
    case slang::ast::BinaryOperator::CaseEquality:
        return grh::ir::OperationKind::kCaseEq;
    case slang::ast::BinaryOperator::WildcardEquality:
        return grh::ir::OperationKind::kWildcardEq;
    case slang::ast::BinaryOperator::Inequality:
        return grh::ir::OperationKind::kNe;
    case slang::ast::BinaryOperator::CaseInequality:
        return grh::ir::OperationKind::kCaseNe;
    case slang::ast::BinaryOperator::WildcardInequality:
        return grh::ir::OperationKind::kWildcardNe;
    case slang::ast::BinaryOperator::GreaterThanEqual:
        return grh::ir::OperationKind::kGe;
    case slang::ast::BinaryOperator::GreaterThan:
        return grh::ir::OperationKind::kGt;
    case slang::ast::BinaryOperator::LessThanEqual:
        return grh::ir::OperationKind::kLe;
    case slang::ast::BinaryOperator::LessThan:
        return grh::ir::OperationKind::kLt;
    case slang::ast::BinaryOperator::LogicalAnd:
        return grh::ir::OperationKind::kLogicAnd;
    case slang::ast::BinaryOperator::LogicalOr:
        return grh::ir::OperationKind::kLogicOr;
    case slang::ast::BinaryOperator::LogicalShiftLeft:
    case slang::ast::BinaryOperator::ArithmeticShiftLeft:
        return grh::ir::OperationKind::kShl;
    case slang::ast::BinaryOperator::LogicalShiftRight:
        return grh::ir::OperationKind::kLShr;
    case slang::ast::BinaryOperator::ArithmeticShiftRight:
        return grh::ir::OperationKind::kAShr;
    default:
        break;
    }
    return std::nullopt;
}

struct ExprLowererState {
    ModulePlan& plan;
    ConvertDiagnostics* diagnostics;
    LoweringPlan lowering;
    std::unordered_map<const slang::ast::Expression*, ExprNodeId> lowered;
    uint32_t nextTemp = 0;

    ExprLowererState(ModulePlan& plan, ConvertDiagnostics* diagnostics)
        : plan(plan), diagnostics(diagnostics) {}

    void lowerRoot(const slang::ast::Expression& expr)
    {
        ExprNodeId id = lowerExpression(expr);
        if (id == kInvalidPlanIndex)
        {
            return;
        }
        LoweredRoot root;
        root.value = id;
        root.location = expr.sourceRange.start();
        lowering.roots.push_back(std::move(root));
    }

    void lowerAssignment(const slang::ast::AssignmentExpression& expr)
    {
        if (expr.op)
        {
            const auto opKind = mapBinaryOp(*expr.op);
            if (!opKind)
            {
                reportUnsupported(expr, "Unsupported compound assignment operator");
                return;
            }
            ExprNodeId lhs = lowerExpression(expr.left());
            ExprNodeId rhs = lowerExpression(expr.right());
            if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
            {
                return;
            }
            ExprNode value;
            value.kind = ExprNodeKind::Operation;
            value.op = *opKind;
            value.operands = {lhs, rhs};
            value.location = expr.sourceRange.start();
            value.tempSymbol = makeTempSymbol();
            ExprNodeId id = addNode(expr, std::move(value));
            LoweredRoot root;
            root.value = id;
            root.location = expr.sourceRange.start();
            lowering.roots.push_back(std::move(root));
            return;
        }
        lowerRoot(expr.right());
    }

    ExprNodeId lowerExpression(const slang::ast::Expression& expr)
    {
        if (auto it = lowered.find(&expr); it != lowered.end())
        {
            return it->second;
        }

        auto paramLiteral = [](const slang::ast::ParameterSymbol& param)
            -> std::optional<std::string> {
            slang::ConstantValue value = param.getValue();
            if (value.bad())
            {
                return std::nullopt;
            }
            if (!value.isInteger())
            {
                value = value.convertToInt();
            }
            if (!value.isInteger())
            {
                return std::nullopt;
            }
            const slang::SVInt& literal = value.integer();
            if (literal.hasUnknown())
            {
                return std::nullopt;
            }
            return literal.toString();
        };

        ExprNode node;
        node.location = expr.sourceRange.start();

        if (const slang::ConstantValue* constant = expr.getConstant())
        {
            if (constant->isInteger())
            {
                const slang::SVInt& literal = constant->integer();
                if (!literal.hasUnknown())
                {
                    node.kind = ExprNodeKind::Constant;
                    node.literal = literal.toString();
                    return addNode(expr, std::move(node));
                }
            }
        }

        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            if (const auto* param =
                    named->symbol.as_if<slang::ast::ParameterSymbol>())
            {
                if (auto literal = paramLiteral(*param))
                {
                    node.kind = ExprNodeKind::Constant;
                    node.literal = *literal;
                    return addNode(expr, std::move(node));
                }
            }
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(named->symbol.name);
            if (!node.symbol.valid() &&
                (named->symbol.kind == slang::ast::SymbolKind::Parameter ||
                 named->symbol.kind == slang::ast::SymbolKind::TypeParameter))
            {
                node.symbol = plan.symbolTable.intern(named->symbol.name);
            }
            if (!node.symbol.valid())
            {
                reportUnsupported(expr, "Unknown symbol in expression");
            }
            return addNode(expr, std::move(node));
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            if (const auto* param =
                    hier->symbol.as_if<slang::ast::ParameterSymbol>())
            {
                if (auto literal = paramLiteral(*param))
                {
                    node.kind = ExprNodeKind::Constant;
                    node.literal = *literal;
                    return addNode(expr, std::move(node));
                }
            }
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(hier->symbol.name);
            if (!node.symbol.valid() &&
                (hier->symbol.kind == slang::ast::SymbolKind::Parameter ||
                 hier->symbol.kind == slang::ast::SymbolKind::TypeParameter))
            {
                node.symbol = plan.symbolTable.intern(hier->symbol.name);
            }
            if (!node.symbol.valid())
            {
                reportUnsupported(expr, "Unknown hierarchical symbol in expression");
            }
            return addNode(expr, std::move(node));
        }
        if (const auto* literal = expr.as_if<slang::ast::IntegerLiteral>())
        {
            node.kind = ExprNodeKind::Constant;
            node.literal = literal->getValue().toString();
            return addNode(expr, std::move(node));
        }
        if (const auto* literal = expr.as_if<slang::ast::UnbasedUnsizedIntegerLiteral>())
        {
            node.kind = ExprNodeKind::Constant;
            node.literal = literal->getValue().toString();
            return addNode(expr, std::move(node));
        }
        if (const auto* literal = expr.as_if<slang::ast::StringLiteral>())
        {
            node.kind = ExprNodeKind::Constant;
            node.literal.assign(literal->getValue());
            return addNode(expr, std::move(node));
        }
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            return lowerExpression(conversion->operand());
        }
        if (const auto* unary = expr.as_if<slang::ast::UnaryExpression>())
        {
            const auto opKind = mapUnaryOp(unary->op);
            if (!opKind)
            {
                reportUnsupported(expr, "Unsupported unary operator");
                return kInvalidPlanIndex;
            }
            ExprNodeId operand = lowerExpression(unary->operand());
            if (operand == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = *opKind;
            node.operands = {operand};
            node.tempSymbol = makeTempSymbol();
            return addNode(expr, std::move(node));
        }
        if (const auto* binary = expr.as_if<slang::ast::BinaryExpression>())
        {
            const auto opKind = mapBinaryOp(binary->op);
            if (!opKind)
            {
                reportUnsupported(expr, "Unsupported binary operator");
                return kInvalidPlanIndex;
            }
            ExprNodeId lhs = lowerExpression(binary->left());
            ExprNodeId rhs = lowerExpression(binary->right());
            if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = *opKind;
            node.operands = {lhs, rhs};
            node.tempSymbol = makeTempSymbol();
            return addNode(expr, std::move(node));
        }
        if (const auto* cond = expr.as_if<slang::ast::ConditionalExpression>())
        {
            if (cond->conditions.empty())
            {
                reportUnsupported(expr, "Conditional expression missing condition");
                return kInvalidPlanIndex;
            }
            if (cond->conditions.size() > 1)
            {
                reportUnsupported(expr, "Conditional expression with patterns unsupported");
            }
            const slang::ast::Expression& condExpr = *cond->conditions.front().expr;
            ExprNodeId condId = lowerExpression(condExpr);
            ExprNodeId lhs = lowerExpression(cond->left());
            ExprNodeId rhs = lowerExpression(cond->right());
            if (condId == kInvalidPlanIndex || lhs == kInvalidPlanIndex ||
                rhs == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kMux;
            node.operands = {condId, lhs, rhs};
            node.tempSymbol = makeTempSymbol();
            return addNode(expr, std::move(node));
        }
        if (const auto* concat = expr.as_if<slang::ast::ConcatenationExpression>())
        {
            std::vector<ExprNodeId> operands;
            operands.reserve(concat->operands().size());
            for (const slang::ast::Expression* operand : concat->operands())
            {
                if (!operand)
                {
                    continue;
                }
                ExprNodeId id = lowerExpression(*operand);
                if (id == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                operands.push_back(id);
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kConcat;
            node.operands = std::move(operands);
            node.tempSymbol = makeTempSymbol();
            return addNode(expr, std::move(node));
        }
        if (const auto* repl = expr.as_if<slang::ast::ReplicationExpression>())
        {
            ExprNodeId count = lowerExpression(repl->count());
            ExprNodeId concat = lowerExpression(repl->concat());
            if (count == kInvalidPlanIndex || concat == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kReplicate;
            node.operands = {count, concat};
            node.tempSymbol = makeTempSymbol();
            return addNode(expr, std::move(node));
        }
        if (const auto* select = expr.as_if<slang::ast::ElementSelectExpression>())
        {
            ExprNodeId value = lowerExpression(select->value());
            ExprNodeId index = lowerExpression(select->selector());
            if (value == kInvalidPlanIndex || index == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kSliceDynamic;
            node.operands = {value, index};
            node.tempSymbol = makeTempSymbol();
            return addNode(expr, std::move(node));
        }
        if (const auto* range = expr.as_if<slang::ast::RangeSelectExpression>())
        {
            ExprNodeId value = lowerExpression(range->value());
            ExprNodeId left = lowerExpression(range->left());
            ExprNodeId right = lowerExpression(range->right());
            if (value == kInvalidPlanIndex || left == kInvalidPlanIndex ||
                right == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kSliceDynamic;
            node.operands = {value, left, right};
            node.tempSymbol = makeTempSymbol();
            return addNode(expr, std::move(node));
        }

        reportUnsupported(expr, "Unsupported expression kind");
        return kInvalidPlanIndex;
    }

private:
    void reportUnsupported(const slang::ast::Expression& expr, std::string_view message)
    {
        if (!diagnostics)
        {
            return;
        }
        diagnostics->todo(expr.sourceRange.start(), std::string(message));
    }

    PlanSymbolId makeTempSymbol()
    {
        const std::string name = "_expr_tmp_" + std::to_string(nextTemp++);
        PlanSymbolId id = plan.symbolTable.intern(name);
        lowering.tempSymbols.push_back(id);
        return id;
    }

    ExprNodeId addNode(const slang::ast::Expression& expr, ExprNode node)
    {
        if (node.widthHint == 0)
        {
            uint64_t width = expr.type->getBitstreamWidth();
            if (width == 0)
            {
                if (auto effective = expr.getEffectiveWidth())
                {
                    width = *effective;
                }
            }
            if (width > 0)
            {
                const uint64_t maxValue =
                    static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
                node.widthHint = width > maxValue
                                     ? std::numeric_limits<int32_t>::max()
                                     : static_cast<int32_t>(width);
            }
        }
        const ExprNodeId id = static_cast<ExprNodeId>(lowering.values.size());
        lowering.values.push_back(std::move(node));
        lowered.emplace(&expr, id);
        return id;
    }
};

class ExprLowererVisitor : public slang::ast::ASTVisitor<ExprLowererVisitor, true, true> {
public:
    explicit ExprLowererVisitor(ExprLowererState& state) : state_(state) {}

    void handle(const slang::ast::AssignmentExpression& expr)
    {
        state_.lowerAssignment(expr);
    }

private:
    ExprLowererState& state_;
};

void lowerGenerateBlock(const slang::ast::GenerateBlockSymbol& block, ExprLowererState& state);
void lowerGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                             ExprLowererState& state);

void lowerProceduralBlock(const slang::ast::ProceduralBlockSymbol& block, ExprLowererState& state)
{
    ExprLowererVisitor visitor(state);
    block.getBody().visit(visitor);
}

void lowerContinuousAssign(const slang::ast::ContinuousAssignSymbol& assign,
                           ExprLowererState& state)
{
    ExprLowererVisitor visitor(state);
    assign.getAssignment().visit(visitor);
}

void lowerMemberSymbol(const slang::ast::Symbol& member, ExprLowererState& state)
{
    if (const auto* continuous = member.as_if<slang::ast::ContinuousAssignSymbol>())
    {
        lowerContinuousAssign(*continuous, state);
        return;
    }
    if (const auto* block = member.as_if<slang::ast::ProceduralBlockSymbol>())
    {
        lowerProceduralBlock(*block, state);
        return;
    }
    if (const auto* generateBlock = member.as_if<slang::ast::GenerateBlockSymbol>())
    {
        lowerGenerateBlock(*generateBlock, state);
        return;
    }
    if (const auto* generateArray = member.as_if<slang::ast::GenerateBlockArraySymbol>())
    {
        lowerGenerateBlockArray(*generateArray, state);
        return;
    }
}

void lowerGenerateBlock(const slang::ast::GenerateBlockSymbol& block, ExprLowererState& state)
{
    if (block.isUninstantiated)
    {
        return;
    }
    for (const slang::ast::Symbol& member : block.members())
    {
        lowerMemberSymbol(member, state);
    }
}

void lowerGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                             ExprLowererState& state)
{
    for (const slang::ast::GenerateBlockSymbol* entry : array.entries)
    {
        if (!entry)
        {
            continue;
        }
        lowerGenerateBlock(*entry, state);
    }
}

struct StmtLowererState {
    class AssignmentExprVisitor;

    enum class LoopControlResult { None, Break, Continue, Unsupported };

    struct ForeachDimState {
        const slang::ast::ValueSymbol* loopVar = nullptr;
        int32_t start = 0;
        int32_t stop = 0;
        int32_t step = 1;
    };

    ModulePlan& plan;
    ConvertDiagnostics* diagnostics;
    LoweringPlan& lowering;
    std::unordered_map<const slang::ast::Expression*, ExprNodeId> lowered;
    std::unordered_map<const slang::ast::AssignmentExpression*, ExprNodeId> assignmentRoots;
    uint32_t maxLoopIterations = 0;
    uint32_t nextTemp = 0;
    uint32_t nextDpiResult = 0;
    std::size_t nextRoot = 0;
    ControlDomain domain = ControlDomain::Unknown;
    std::vector<ExprNodeId> guardStack;
    std::vector<ExprNodeId> flowStack;
    struct LoopFlowContext {
        ExprNodeId loopAlive = kInvalidPlanIndex;
    };
    struct EventContext {
        bool hasTiming = false;
        bool edgeSensitive = false;
        std::vector<EventEdge> edges;
        std::vector<ExprNodeId> operands;
    };
    struct LValueTarget {
        PlanSymbolId target;
        std::vector<WriteSlice> slices;
        uint64_t width = 0;
        slang::SourceLocation location{};
    };
    struct LValueCompositeInfo {
        bool isComposite = false;
        bool reverseOrder = false;
    };
    std::vector<LoopFlowContext> loopFlowStack;
    std::optional<std::string> loopControlFailure;
    EventContext eventContext;

    StmtLowererState(ModulePlan& plan, ConvertDiagnostics* diagnostics, LoweringPlan& lowering,
                     uint32_t maxLoopIterations)
        : plan(plan),
          diagnostics(diagnostics),
          lowering(lowering),
          maxLoopIterations(maxLoopIterations)
    {
        nextTemp = static_cast<uint32_t>(lowering.tempSymbols.size());
    }

    void scanExpression(const slang::ast::Expression& expr)
    {
        AssignmentExprVisitor visitor(*this);
        expr.visit(visitor);
    }

    void visitStatement(const slang::ast::Statement& stmt)
    {
        if (const auto* list = stmt.as_if<slang::ast::StatementList>())
        {
            for (const slang::ast::Statement* child : list->list)
            {
                if (!child)
                {
                    continue;
                }
                visitStatement(*child);
            }
            return;
        }
        if (const auto* block = stmt.as_if<slang::ast::BlockStatement>())
        {
            visitStatement(block->body);
            return;
        }
        if (const auto* timed = stmt.as_if<slang::ast::TimedStatement>())
        {
            warnTimedStatement(*timed);
            visitStatement(timed->stmt);
            return;
        }
        if (const auto* wait = stmt.as_if<slang::ast::WaitStatement>())
        {
            warnWaitStatement(*wait);
            visitStatement(wait->stmt);
            return;
        }
        if (const auto* waitFork = stmt.as_if<slang::ast::WaitForkStatement>())
        {
            warnWaitForkStatement(*waitFork);
            return;
        }
        if (const auto* waitOrder = stmt.as_if<slang::ast::WaitOrderStatement>())
        {
            warnWaitOrderStatement(*waitOrder);
            if (waitOrder->ifTrue)
            {
                visitStatement(*waitOrder->ifTrue);
            }
            if (waitOrder->ifFalse)
            {
                visitStatement(*waitOrder->ifFalse);
            }
            return;
        }
        if (const auto* eventTrigger = stmt.as_if<slang::ast::EventTriggerStatement>())
        {
            warnEventTriggerStatement(*eventTrigger);
            return;
        }
        if (const auto* disableFork = stmt.as_if<slang::ast::DisableForkStatement>())
        {
            warnDisableForkStatement(*disableFork);
            return;
        }
        if (const auto* conditional = stmt.as_if<slang::ast::ConditionalStatement>())
        {
            visitConditional(*conditional);
            return;
        }
        if (const auto* caseStmt = stmt.as_if<slang::ast::CaseStatement>())
        {
            visitCase(*caseStmt);
            return;
        }
        if (const auto* patternCase = stmt.as_if<slang::ast::PatternCaseStatement>())
        {
            reportError(stmt, "Pattern case lowering is unsupported");
            return;
        }
        if (stmt.as_if<slang::ast::BreakStatement>())
        {
            if (handleLoopBreak(stmt))
            {
                return;
            }
            reportUnsupported(stmt, "Break statement lowering is unsupported");
            return;
        }
        if (stmt.as_if<slang::ast::ContinueStatement>())
        {
            if (handleLoopContinue(stmt))
            {
                return;
            }
            reportUnsupported(stmt, "Continue statement lowering is unsupported");
            return;
        }
        if (const auto* exprStmt = stmt.as_if<slang::ast::ExpressionStatement>())
        {
            if (handleExpressionStatement(exprStmt->expr))
            {
                return;
            }
            scanExpression(exprStmt->expr);
            return;
        }
        if (const auto* procAssign = stmt.as_if<slang::ast::ProceduralAssignStatement>())
        {
            scanExpression(procAssign->assignment);
            return;
        }
        if (const auto* forLoop = stmt.as_if<slang::ast::ForLoopStatement>())
        {
            scanForLoopControl(*forLoop);
            const bool hasLoopControl = containsLoopControl(forLoop->body);
            if (hasLoopControl)
            {
                clearLoopControlFailure();
            }
            if (!tryUnrollFor(*forLoop))
            {
                if (hasLoopControl)
                {
                    reportLoopControlError(stmt,
                                           "For-loop with break/continue requires static unrolling");
                    return;
                }
                reportUnsupported(stmt, "For-loop lowering uses unconditional guards");
                visitStatement(forLoop->body);
            }
            return;
        }
        if (const auto* repeatLoop = stmt.as_if<slang::ast::RepeatLoopStatement>())
        {
            scanExpression(repeatLoop->count);
            const bool hasLoopControl = containsLoopControl(repeatLoop->body);
            if (hasLoopControl)
            {
                clearLoopControlFailure();
            }
            if (!tryUnrollRepeat(*repeatLoop))
            {
                if (hasLoopControl)
                {
                    reportLoopControlError(stmt,
                                           "Repeat-loop with break/continue requires static unrolling");
                    return;
                }
                reportUnsupported(stmt, "Repeat-loop lowering uses unconditional guards");
                visitStatement(repeatLoop->body);
            }
            return;
        }
        if (const auto* whileLoop = stmt.as_if<slang::ast::WhileLoopStatement>())
        {
            scanExpression(whileLoop->cond);
            const bool hasLoopControl = containsLoopControl(whileLoop->body);
            clearLoopControlFailure();
            if (!tryUnrollWhile(*whileLoop))
            {
                if (hasLoopControl)
                {
                    reportLoopControlError(stmt,
                                           "While-loop with break/continue requires static unrolling");
                    return;
                }
                reportLoopFailure(stmt, "While-loop lowering failed");
            }
            return;
        }
        if (const auto* doWhileLoop = stmt.as_if<slang::ast::DoWhileLoopStatement>())
        {
            scanExpression(doWhileLoop->cond);
            const bool hasLoopControl = containsLoopControl(doWhileLoop->body);
            clearLoopControlFailure();
            if (!tryUnrollDoWhile(*doWhileLoop))
            {
                if (hasLoopControl)
                {
                    reportLoopControlError(
                        stmt, "Do-while loop with break/continue requires static unrolling");
                    return;
                }
                reportLoopFailure(stmt, "Do-while loop lowering failed");
            }
            return;
        }
        if (const auto* foreverLoop = stmt.as_if<slang::ast::ForeverLoopStatement>())
        {
            const bool hasLoopControl = containsLoopControl(foreverLoop->body);
            clearLoopControlFailure();
            if (!tryUnrollForever(*foreverLoop))
            {
                if (hasLoopControl)
                {
                    reportLoopControlError(
                        stmt, "Forever-loop with break/continue requires static unrolling");
                    return;
                }
                reportLoopFailure(stmt, "Forever-loop lowering failed");
            }
            return;
        }
        if (const auto* foreachLoop = stmt.as_if<slang::ast::ForeachLoopStatement>())
        {
            scanExpression(foreachLoop->arrayRef);
            const bool hasLoopControl = containsLoopControl(foreachLoop->body);
            if (hasLoopControl)
            {
                clearLoopControlFailure();
            }
            if (!tryUnrollForeach(*foreachLoop))
            {
                if (hasLoopControl)
                {
                    reportLoopControlError(stmt,
                                           "Foreach-loop with break/continue requires static unrolling");
                    return;
                }
                reportUnsupported(stmt, "Foreach-loop lowering uses unconditional guards");
                visitStatement(foreachLoop->body);
            }
            return;
        }
        if (const auto* invalid = stmt.as_if<slang::ast::InvalidStatement>())
        {
            if (invalid->child)
            {
                visitStatement(*invalid->child);
            }
            return;
        }
        if (stmt.kind == slang::ast::StatementKind::Empty)
        {
            return;
        }
        reportUnsupported(stmt, "Unsupported statement kind");
    }

    void visitConditional(const slang::ast::ConditionalStatement& stmt)
    {
        if (stmt.conditions.empty())
        {
            reportUnsupported(stmt, "Conditional statement missing condition");
            return;
        }
        bool hasPattern = false;
        for (const auto& cond : stmt.conditions)
        {
            if (cond.pattern)
            {
                hasPattern = true;
            }
        }
        if (hasPattern)
        {
            reportError(stmt, "Patterned condition lowering is unsupported");
            return;
        }
        for (const auto& cond : stmt.conditions)
        {
            scanExpression(*cond.expr);
        }

        ExprNodeId combinedCond = kInvalidPlanIndex;
        for (const auto& cond : stmt.conditions)
        {
            ExprNodeId condId = lowerExpression(*cond.expr);
            if (condId == kInvalidPlanIndex)
            {
                reportUnsupported(stmt, "Failed to lower conditional guard");
                visitStatement(stmt.ifTrue);
                if (stmt.ifFalse)
                {
                    visitStatement(*stmt.ifFalse);
                }
                return;
            }
            if (combinedCond == kInvalidPlanIndex)
            {
                combinedCond = condId;
            }
            else
            {
                combinedCond =
                    makeLogicAnd(combinedCond, condId, stmt.sourceRange.start());
            }
        }

        ExprNodeId baseGuard = currentPathGuard();
        ExprNodeId trueGuard = combineGuard(baseGuard, combinedCond, stmt.sourceRange.start());
        pushGuard(trueGuard);
        visitStatement(stmt.ifTrue);
        popGuard();

        if (stmt.ifFalse)
        {
            ExprNodeId notCond = makeLogicNot(combinedCond, stmt.sourceRange.start());
            ExprNodeId falseGuard = combineGuard(baseGuard, notCond, stmt.sourceRange.start());
            pushGuard(falseGuard);
            visitStatement(*stmt.ifFalse);
            popGuard();
        }
    }

    void visitCase(const slang::ast::CaseStatement& stmt)
    {
        scanExpression(stmt.expr);
        ExprNodeId control = lowerExpression(stmt.expr);
        if (control == kInvalidPlanIndex)
        {
            reportUnsupported(stmt, "Case control expression lowering failed");
            for (const auto& item : stmt.items)
            {
                if (item.stmt)
                {
                    visitStatement(*item.stmt);
                }
            }
            if (stmt.defaultCase)
            {
                visitStatement(*stmt.defaultCase);
            }
            return;
        }

        ExprNodeId baseGuard = currentPathGuard();
        ExprNodeId priorMatch = kInvalidPlanIndex;

        bool warnedCaseEq = false;
        for (const auto& item : stmt.items)
        {
            bool usedCaseEq = false;
            ExprNodeId itemMatch =
                buildCaseItemMatch(control, stmt.expr, stmt.condition, item.expressions,
                                   stmt.sourceRange.start(), usedCaseEq);
            if (usedCaseEq && !warnedCaseEq && diagnostics)
            {
                std::string originSymbol = describeFileLocation(stmt.sourceRange.start());
                diagnostics->warn(stmt.sourceRange.start(),
                                  "Case match lowered with case-equality; may be unsynthesizable",
                                  std::move(originSymbol));
                warnedCaseEq = true;
            }
            if (itemMatch == kInvalidPlanIndex)
            {
                reportUnsupported(stmt, "Failed to lower case item match");
                if (item.stmt)
                {
                    visitStatement(*item.stmt);
                }
                continue;
            }

            ExprNodeId itemGuard = combineGuard(baseGuard, itemMatch, stmt.sourceRange.start());
            if (priorMatch != kInvalidPlanIndex)
            {
                ExprNodeId notPrior = makeLogicNot(priorMatch, stmt.sourceRange.start());
                itemGuard = makeLogicAnd(itemGuard, notPrior, stmt.sourceRange.start());
            }

            pushGuard(itemGuard);
            if (item.stmt)
            {
                visitStatement(*item.stmt);
            }
            popGuard();

            if (priorMatch == kInvalidPlanIndex)
            {
                priorMatch = itemMatch;
            }
            else
            {
                priorMatch =
                    makeLogicOr(priorMatch, itemMatch, stmt.sourceRange.start());
            }
        }

        if (stmt.defaultCase)
        {
            ExprNodeId defaultGuard = baseGuard;
            if (priorMatch != kInvalidPlanIndex)
            {
                ExprNodeId notPrior = makeLogicNot(priorMatch, stmt.sourceRange.start());
                defaultGuard = combineGuard(baseGuard, notPrior, stmt.sourceRange.start());
            }
            pushGuard(defaultGuard);
            visitStatement(*stmt.defaultCase);
            popGuard();
        }
    }

    void handleAssignment(const slang::ast::AssignmentExpression& expr)
    {
        ExprNodeId value = resolveAssignmentRoot(expr);
        std::vector<LValueTarget> targets;
        LValueCompositeInfo composite;
        if (!resolveLValueTargets(expr.left(), targets, composite))
        {
            return;
        }
        if (targets.empty())
        {
            reportUnsupported(expr, "Unsupported LHS in assignment");
            return;
        }
        if (value == kInvalidPlanIndex)
        {
            return;
        }

        ExprNodeId guard = currentGuard(expr.sourceRange.start());
        if (targets.size() == 1 && !composite.isComposite)
        {
            WriteIntent intent;
            intent.target = targets.front().target;
            intent.slices = std::move(targets.front().slices);
            intent.value = value;
            intent.guard = guard;
            intent.domain = domain;
            intent.isNonBlocking = expr.isNonBlocking();
            intent.location = expr.sourceRange.start();
            recordWriteIntent(std::move(intent));
            return;
        }

        uint64_t totalWidth = 0;
        for (const auto& target : targets)
        {
            if (target.width == 0)
            {
                reportUnsupported(expr, "Unsupported LHS width in assignment");
                return;
            }
            totalWidth += target.width;
        }
        if (totalWidth == 0)
        {
            reportUnsupported(expr, "Unsupported LHS width in assignment");
            return;
        }

        auto emitSliceWrite = [&](LValueTarget& target, uint64_t high, uint64_t low) {
            ExprNodeId sliceValue = makeRhsSlice(value, high, low, expr.sourceRange.start());
            if (sliceValue == kInvalidPlanIndex)
            {
                return;
            }
            WriteIntent intent;
            intent.target = target.target;
            intent.slices = std::move(target.slices);
            intent.value = sliceValue;
            intent.guard = guard;
            intent.domain = domain;
            intent.isNonBlocking = expr.isNonBlocking();
            intent.location = expr.sourceRange.start();
            recordWriteIntent(std::move(intent));
        };

        if (composite.reverseOrder)
        {
            uint64_t offset = 0;
            for (auto& target : targets)
            {
                const uint64_t low = offset;
                const uint64_t high = offset + target.width - 1;
                offset += target.width;
                emitSliceWrite(target, high, low);
            }
            return;
        }

        uint64_t remaining = totalWidth;
        for (auto& target : targets)
        {
            if (target.width > remaining)
            {
                reportUnsupported(expr, "LHS width exceeds RHS span");
                return;
            }
            remaining -= target.width;
            const uint64_t low = remaining;
            const uint64_t high = remaining + target.width - 1;
            emitSliceWrite(target, high, low);
        }
    }

    class AssignmentExprVisitor
        : public slang::ast::ASTVisitor<AssignmentExprVisitor, true, true> {
    public:
        explicit AssignmentExprVisitor(StmtLowererState& state) : state_(state) {}

        void handle(const slang::ast::AssignmentExpression& expr)
        {
            state_.handleAssignment(expr);
        }

    private:
        StmtLowererState& state_;
    };

    EventContext buildEventContext(const slang::ast::ProceduralBlockSymbol& block)
    {
        EventContext context;
        const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
        if (!timing)
        {
            return context;
        }
        context.hasTiming = true;
        std::vector<EventEdge> edges;
        std::vector<ExprNodeId> operands;
        if (collectEdgeSensitiveEvents(*timing, edges, operands))
        {
            context.edgeSensitive = true;
            context.edges = std::move(edges);
            context.operands = std::move(operands);
        }
        return context;
    }

private:
    bool handleExpressionStatement(const slang::ast::Expression& expr)
    {
        const auto* call = expr.as_if<slang::ast::CallExpression>();
        if (!call)
        {
            return false;
        }
        if (call->isSystemCall())
        {
            if (handleSystemTaskCall(*call))
            {
                return true;
            }
            reportUnsupported(expr, "Unsupported system task call");
            return true;
        }

        if (auto* subroutine = std::get_if<const slang::ast::SubroutineSymbol*>(&call->subroutine))
        {
            if (*subroutine && (*subroutine)->flags.has(slang::ast::MethodFlags::DPIImport))
            {
                return handleDpiCall(*call, **subroutine);
            }
        }

        reportUnsupported(expr, "Unsupported subroutine call");
        return true;
    }

    bool handleSystemTaskCall(const slang::ast::CallExpression& call)
    {
        const std::string name = normalizeSystemTaskName(call.getSubroutineName());
        if (name == "display" || name == "write" || name == "strobe")
        {
            return emitDisplayCall(call, name);
        }
        if (name == "info" || name == "warning" || name == "error" || name == "fatal")
        {
            return emitAssertCall(call, name);
        }
        return false;
    }

    bool emitDisplayCall(const slang::ast::CallExpression& call, std::string_view displayKind)
    {
        if (!ensureEdgeSensitive(call.sourceRange.start(), "display"))
        {
            return true;
        }
        DisplayStmt display;
        display.displayKind = std::string(displayKind);

        const auto args = call.arguments();
        std::size_t index = 0;
        if (!args.empty())
        {
            auto literal = extractStringLiteral(*args.front());
            if (!literal)
            {
                reportUnsupported(call, "Display format string must be a literal");
                return true;
            }
            display.formatString = std::move(*literal);
            index = 1;
        }

        for (; index < args.size(); ++index)
        {
            if (!args[index])
            {
                continue;
            }
            ExprNodeId argId = lowerExpression(*args[index]);
            if (argId == kInvalidPlanIndex)
            {
                reportUnsupported(call, "Failed to lower display argument");
                return true;
            }
            display.args.push_back(argId);
        }

        LoweredStmt stmt;
        stmt.kind = LoweredStmtKind::Display;
        stmt.op = grh::ir::OperationKind::kDisplay;
        stmt.updateCond = ensureGuardExpr(currentGuard(call.sourceRange.start()),
                                          call.sourceRange.start());
        stmt.eventEdges = eventContext.edges;
        stmt.eventOperands = eventContext.operands;
        stmt.location = call.sourceRange.start();
        stmt.display = std::move(display);
        lowering.loweredStmts.push_back(std::move(stmt));
        return true;
    }

    bool emitAssertCall(const slang::ast::CallExpression& call, std::string_view severity)
    {
        if (!ensureEdgeSensitive(call.sourceRange.start(), "assert"))
        {
            return true;
        }
        AssertStmt assertion;
        assertion.severity = std::string(severity);

        const auto args = call.arguments();
        if (!args.empty() && args.front())
        {
            std::optional<std::string> literal = extractStringLiteral(*args.front());
            if (!literal && args.size() > 1 && args[1] && isIntegerLiteralExpr(*args.front()))
            {
                literal = extractStringLiteral(*args[1]);
            }
            if (literal)
            {
                assertion.message = std::move(*literal);
            }
            else
            {
                reportUnsupported(call, "Assert message must be a literal");
                return true;
            }
        }

        assertion.condition = addConstantLiteral("1'b0", call.sourceRange.start());

        LoweredStmt stmt;
        stmt.kind = LoweredStmtKind::Assert;
        stmt.op = grh::ir::OperationKind::kAssert;
        stmt.updateCond = ensureGuardExpr(currentGuard(call.sourceRange.start()),
                                          call.sourceRange.start());
        stmt.eventEdges = eventContext.edges;
        stmt.eventOperands = eventContext.operands;
        stmt.location = call.sourceRange.start();
        stmt.assertion = std::move(assertion);
        lowering.loweredStmts.push_back(std::move(stmt));
        return true;
    }

    bool handleDpiCall(const slang::ast::CallExpression& call,
                       const slang::ast::SubroutineSymbol& subroutine)
    {
        if (!ensureEdgeSensitive(call.sourceRange.start(), "dpi"))
        {
            return true;
        }
        if (subroutine.subroutineKind != slang::ast::SubroutineKind::Function)
        {
            reportUnsupported(call, "DPI call supports only functions");
            return true;
        }

        const auto args = call.arguments();
        const auto formals = subroutine.getArguments();
        if (args.size() != formals.size())
        {
            reportUnsupported(call, "DPI call argument count mismatch");
            return true;
        }

        DpiCallStmt dpi;
        dpi.targetImportSymbol = std::string(subroutine.name);

        for (std::size_t i = 0; i < formals.size(); ++i)
        {
            const auto* formal = formals[i];
            const auto* actual = args[i];
            if (!formal || !actual)
            {
                reportUnsupported(call, "DPI call missing argument");
                return true;
            }

            switch (formal->direction)
            {
            case slang::ast::ArgumentDirection::In: {
                const slang::ast::Expression& actualExpr = unwrapDpiArgument(*actual, false);
                ExprNodeId argId = lowerExpression(actualExpr);
                if (argId == kInvalidPlanIndex)
                {
                    reportUnsupported(call, "Failed to lower DPI input argument");
                    return true;
                }
                dpi.inArgNames.emplace_back(formal->name);
                dpi.inArgs.push_back(argId);
                break;
            }
            case slang::ast::ArgumentDirection::Out: {
                const slang::ast::Expression& actualExpr = unwrapDpiArgument(*actual, true);
                PlanSymbolId symbol = resolveSimpleSymbol(actualExpr);
                if (!symbol.valid())
                {
                    std::string message("Unsupported DPI output argument kind: ");
                    message.append(std::string(slang::ast::toString(actualExpr.kind)));
                    reportUnsupported(call, std::move(message));
                    return true;
                }
                dpi.outArgNames.emplace_back(formal->name);
                dpi.results.push_back(symbol);
                break;
            }
            default:
                reportUnsupported(call, "DPI call supports only input/output arguments");
                return true;
            }
        }

        const bool hasReturn = subroutine.getReturnType().isVoid() == false;
        dpi.hasReturn = hasReturn;
        if (hasReturn)
        {
            PlanSymbolId retSymbol = makeDpiResultSymbol();
            dpi.results.insert(dpi.results.begin(), retSymbol);
        }

        if (!recordDpiImport(subroutine, call.sourceRange.start()))
        {
            return true;
        }

        LoweredStmt stmt;
        stmt.kind = LoweredStmtKind::DpiCall;
        stmt.op = grh::ir::OperationKind::kDpicCall;
        stmt.updateCond = ensureGuardExpr(currentGuard(call.sourceRange.start()),
                                          call.sourceRange.start());
        stmt.eventEdges = eventContext.edges;
        stmt.eventOperands = eventContext.operands;
        stmt.location = call.sourceRange.start();
        stmt.dpiCall = std::move(dpi);
        lowering.loweredStmts.push_back(std::move(stmt));
        return true;
    }

    bool recordDpiImport(const slang::ast::SubroutineSymbol& subroutine,
                         slang::SourceLocation location)
    {
        auto info = buildDpiImportInfo(subroutine, location);
        if (!info)
        {
            return false;
        }
        for (const auto& existing : lowering.dpiImports)
        {
            if (existing.symbol != info->symbol)
            {
                continue;
            }
            if (!dpiImportSignatureMatches(existing, *info))
            {
                if (diagnostics)
                {
                    diagnostics->error(
                        location,
                        std::string("Conflicting DPI import signature for ") + existing.symbol);
                }
                return false;
            }
            return true;
        }
        lowering.dpiImports.push_back(std::move(*info));
        return true;
    }

    std::optional<DpiImportInfo> buildDpiImportInfo(
        const slang::ast::SubroutineSymbol& subroutine,
        slang::SourceLocation location)
    {
        DpiImportInfo info;
        info.symbol = std::string(subroutine.name);
        const auto formals = subroutine.getArguments();
        info.argsDirection.reserve(formals.size());
        info.argsWidth.reserve(formals.size());
        info.argsName.reserve(formals.size());
        info.argsSigned.reserve(formals.size());

        for (const auto* formal : formals)
        {
            if (!formal)
            {
                if (diagnostics)
                {
                    diagnostics->error(location, "DPI import missing formal argument");
                }
                return std::nullopt;
            }
            std::string direction;
            switch (formal->direction)
            {
            case slang::ast::ArgumentDirection::In:
                direction = "input";
                break;
            case slang::ast::ArgumentDirection::Out:
                direction = "output";
                break;
            default:
                if (diagnostics)
                {
                    diagnostics->error(*formal,
                                       "Unsupported DPI argument direction");
                }
                return std::nullopt;
            }
            const uint64_t widthRaw =
                computeFixedWidth(formal->getType(), *formal, diagnostics);
            const int32_t width =
                clampWidth(widthRaw, *formal, diagnostics, "DPI argument");
            info.argsDirection.push_back(std::move(direction));
            info.argsWidth.push_back(static_cast<int64_t>(width));
            info.argsName.push_back(std::string(formal->name));
            info.argsSigned.push_back(formal->getType().isSigned());
        }

        const slang::ast::Type& returnType = subroutine.getReturnType();
        if (!returnType.isVoid())
        {
            const uint64_t widthRaw =
                computeFixedWidth(returnType, subroutine, diagnostics);
            const int32_t width =
                clampWidth(widthRaw, subroutine, diagnostics, "DPI return");
            info.hasReturn = true;
            info.returnWidth = static_cast<int64_t>(width);
            info.returnSigned = returnType.isSigned();
        }
        return info;
    }

    static bool dpiImportSignatureMatches(const DpiImportInfo& lhs, const DpiImportInfo& rhs)
    {
        return lhs.symbol == rhs.symbol &&
               lhs.argsDirection == rhs.argsDirection &&
               lhs.argsWidth == rhs.argsWidth &&
               lhs.argsName == rhs.argsName &&
               lhs.argsSigned == rhs.argsSigned &&
               lhs.hasReturn == rhs.hasReturn &&
               lhs.returnWidth == rhs.returnWidth &&
               lhs.returnSigned == rhs.returnSigned;
    }

    void recordWriteIntent(WriteIntent intent)
    {
        lowering.writes.push_back(std::move(intent));
        LoweredStmt stmt;
        stmt.kind = LoweredStmtKind::Write;
        stmt.op = grh::ir::OperationKind::kAssign;
        stmt.location = lowering.writes.back().location;
        stmt.write = lowering.writes.back();
        stmt.updateCond = ensureGuardExpr(stmt.write.guard, stmt.location);
        stmt.eventEdges = eventContext.edges;
        stmt.eventOperands = eventContext.operands;
        lowering.loweredStmts.push_back(std::move(stmt));
    }

    const slang::ast::Expression& unwrapDpiArgument(const slang::ast::Expression& expr,
                                                    bool output) const
    {
        if (const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>())
        {
            return output ? assignment->left() : assignment->right();
        }
        return expr;
    }

    PlanSymbolId resolveSimpleSymbol(const slang::ast::Expression& expr)
    {
        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            return plan.symbolTable.lookup(named->symbol.name);
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            return plan.symbolTable.lookup(hier->symbol.name);
        }
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            return resolveSimpleSymbol(conversion->operand());
        }
        return {};
    }

    std::optional<std::string> extractStringLiteral(const slang::ast::Expression& expr)
    {
        if (const auto* literal = expr.as_if<slang::ast::StringLiteral>())
        {
            return std::string(literal->getValue());
        }
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            if (conversion->isImplicit())
            {
                return extractStringLiteral(conversion->operand());
            }
        }
        return std::nullopt;
    }

    bool isIntegerLiteralExpr(const slang::ast::Expression& expr)
    {
        if (expr.as_if<slang::ast::IntegerLiteral>() ||
            expr.as_if<slang::ast::UnbasedUnsizedIntegerLiteral>())
        {
            return true;
        }
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            if (conversion->isImplicit())
            {
                return isIntegerLiteralExpr(conversion->operand());
            }
        }
        return false;
    }

    bool ensureEdgeSensitive(slang::SourceLocation location, std::string_view label)
    {
        if (eventContext.edgeSensitive && !eventContext.operands.empty())
        {
            return true;
        }
        if (diagnostics)
        {
            std::string message("Ignoring ");
            message.append(label);
            message.append(" call without edge-sensitive timing control");
            diagnostics->warn(location, std::move(message));
        }
        return false;
    }

    bool collectEdgeSensitiveEvents(const slang::ast::TimingControl& timing,
                                    std::vector<EventEdge>& edges,
                                    std::vector<ExprNodeId>& operands)
    {
        std::vector<EventEdge> tempEdges;
        std::vector<ExprNodeId> tempOperands;
        if (!appendEdgeSensitiveEvents(timing, tempEdges, tempOperands))
        {
            return false;
        }
        if (tempEdges.empty())
        {
            return false;
        }
        edges = std::move(tempEdges);
        operands = std::move(tempOperands);
        return true;
    }

    bool appendEdgeSensitiveEvents(const slang::ast::TimingControl& timing,
                                   std::vector<EventEdge>& edges,
                                   std::vector<ExprNodeId>& operands)
    {
        using slang::ast::EdgeKind;
        using slang::ast::TimingControlKind;
        switch (timing.kind)
        {
        case TimingControlKind::SignalEvent: {
            const auto& signal = timing.as<slang::ast::SignalEventControl>();
            if (signal.edge == EdgeKind::None || signal.edge == EdgeKind::BothEdges)
            {
                return false;
            }
            if (signal.iffCondition)
            {
                if (diagnostics)
                {
                    diagnostics->warn(signal.sourceRange.start(),
                                      "Ignoring event control with iff condition");
                }
                return false;
            }
            ExprNodeId operand = lowerExpression(signal.expr);
            if (operand == kInvalidPlanIndex)
            {
                return false;
            }
            EventEdge edge = signal.edge == EdgeKind::PosEdge ? EventEdge::Posedge
                                                              : EventEdge::Negedge;
            edges.push_back(edge);
            operands.push_back(operand);
            return true;
        }
        case TimingControlKind::EventList: {
            const auto& list = timing.as<slang::ast::EventListControl>();
            for (const slang::ast::TimingControl* child : list.events)
            {
                if (!child)
                {
                    continue;
                }
                if (!appendEdgeSensitiveEvents(*child, edges, operands))
                {
                    return false;
                }
            }
            return true;
        }
        case TimingControlKind::RepeatedEvent:
        case TimingControlKind::ImplicitEvent:
        case TimingControlKind::Delay:
        case TimingControlKind::Delay3:
        case TimingControlKind::OneStepDelay:
        case TimingControlKind::CycleDelay:
        case TimingControlKind::BlockEventList:
        case TimingControlKind::Invalid:
        default:
            return false;
        }
    }

    ExprNodeId currentPathGuard() const
    {
        if (guardStack.empty())
        {
            return kInvalidPlanIndex;
        }
        return guardStack.back();
    }

    ExprNodeId currentFlowGuard() const
    {
        if (flowStack.empty())
        {
            return kInvalidPlanIndex;
        }
        return flowStack.back();
    }

    ExprNodeId currentGuard(slang::SourceLocation location)
    {
        return combineGuard(currentPathGuard(), currentFlowGuard(), location);
    }

    void pushGuard(ExprNodeId guard)
    {
        guardStack.push_back(guard);
    }

    void popGuard()
    {
        if (!guardStack.empty())
        {
            guardStack.pop_back();
        }
    }

    void pushFlowGuard(ExprNodeId guard)
    {
        flowStack.push_back(guard);
    }

    void popFlowGuard()
    {
        if (!flowStack.empty())
        {
            flowStack.pop_back();
        }
    }

    void updateFlowGuard(ExprNodeId guard)
    {
        if (flowStack.empty())
        {
            return;
        }
        flowStack.back() = guard;
    }

    bool inDynamicLoop() const
    {
        return !loopFlowStack.empty();
    }

    ExprNodeId currentLoopAlive() const
    {
        if (loopFlowStack.empty())
        {
            return kInvalidPlanIndex;
        }
        return loopFlowStack.back().loopAlive;
    }

    void updateLoopAlive(ExprNodeId guard)
    {
        if (loopFlowStack.empty())
        {
            return;
        }
        loopFlowStack.back().loopAlive = guard;
    }

    void pushLoopContext(slang::SourceLocation location)
    {
        LoopFlowContext context;
        context.loopAlive = addConstantLiteral("1'b1", location);
        loopFlowStack.push_back(std::move(context));
    }

    void popLoopContext()
    {
        if (!loopFlowStack.empty())
        {
            loopFlowStack.pop_back();
        }
    }

    bool handleLoopBreak(const slang::ast::Statement& stmt)
    {
        if (!inDynamicLoop())
        {
            return false;
        }
        const auto location = stmt.sourceRange.start();
        ExprNodeId trigger = ensureGuardExpr(currentGuard(location), location);
        ExprNodeId notTrigger = makeLogicNot(trigger, location);
        updateFlowGuard(combineGuard(currentFlowGuard(), notTrigger, location));
        updateLoopAlive(combineGuard(currentLoopAlive(), notTrigger, location));
        return true;
    }

    bool handleLoopContinue(const slang::ast::Statement& stmt)
    {
        if (!inDynamicLoop())
        {
            return false;
        }
        const auto location = stmt.sourceRange.start();
        ExprNodeId trigger = ensureGuardExpr(currentGuard(location), location);
        ExprNodeId notTrigger = makeLogicNot(trigger, location);
        updateFlowGuard(combineGuard(currentFlowGuard(), notTrigger, location));
        return true;
    }

    ExprNodeId takeNextRoot(slang::SourceLocation location)
    {
        if (nextRoot >= lowering.roots.size())
        {
            if (diagnostics)
            {
                diagnostics->todo(location, "Missing lowered root for assignment");
            }
            ++nextRoot;
            return kInvalidPlanIndex;
        }
        ExprNodeId value = lowering.roots[nextRoot].value;
        ++nextRoot;
        return value;
    }

    ExprNodeId combineGuard(ExprNodeId base, ExprNodeId extra,
                            slang::SourceLocation location)
    {
        if (base == kInvalidPlanIndex)
        {
            return extra;
        }
        if (extra == kInvalidPlanIndex)
        {
            return base;
        }
        return makeOperation(grh::ir::OperationKind::kLogicAnd, {base, extra}, location);
    }

    ExprNodeId ensureGuardExpr(ExprNodeId guard, slang::SourceLocation location)
    {
        if (guard != kInvalidPlanIndex)
        {
            return guard;
        }
        return addConstantLiteral("1'b1", location);
    }

    ExprNodeId makeLogicAnd(ExprNodeId lhs, ExprNodeId rhs, slang::SourceLocation location)
    {
        if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        return makeOperation(grh::ir::OperationKind::kLogicAnd, {lhs, rhs}, location);
    }

    ExprNodeId makeLogicOr(ExprNodeId lhs, ExprNodeId rhs, slang::SourceLocation location)
    {
        if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        return makeOperation(grh::ir::OperationKind::kLogicOr, {lhs, rhs}, location);
    }

    ExprNodeId makeLogicNot(ExprNodeId operand, slang::SourceLocation location)
    {
        if (operand == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        return makeOperation(grh::ir::OperationKind::kLogicNot, {operand}, location);
    }

    ExprNodeId makeEq(ExprNodeId lhs, ExprNodeId rhs, slang::SourceLocation location)
    {
        if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        return makeOperation(grh::ir::OperationKind::kEq, {lhs, rhs}, location);
    }

    ExprNodeId makeCaseEq(ExprNodeId lhs, ExprNodeId rhs, slang::SourceLocation location)
    {
        if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        return makeOperation(grh::ir::OperationKind::kCaseEq, {lhs, rhs}, location);
    }

    void warnIgnoredStatement(const slang::ast::Statement& stmt, std::string_view label)
    {
        if (!diagnostics)
        {
            return;
        }
        std::string message("Ignoring ");
        message.append(label);
        message.append("; lowering statement without timing semantics");
        diagnostics->warn(stmt.sourceRange.start(), std::move(message));
    }

    std::string describeTiming(const slang::ast::TimingControl& timing) const
    {
        using slang::ast::TimingControlKind;
        switch (timing.kind)
        {
        case TimingControlKind::Delay:
            return "delay";
        case TimingControlKind::Delay3:
            return "delay3";
        case TimingControlKind::OneStepDelay:
            return "one-step delay";
        case TimingControlKind::CycleDelay:
            return "cycle delay";
        case TimingControlKind::SignalEvent:
            return "event";
        case TimingControlKind::EventList:
            return "event list";
        case TimingControlKind::ImplicitEvent:
            return "implicit event";
        case TimingControlKind::RepeatedEvent:
            return "repeated event";
        case TimingControlKind::BlockEventList:
            return "block event list";
        case TimingControlKind::Invalid:
        default:
            break;
        }
        return "timing control";
    }

    void warnTimedStatement(const slang::ast::TimedStatement& stmt)
    {
        if (!diagnostics)
        {
            return;
        }
        std::string message("Ignoring timing control (");
        message.append(describeTiming(stmt.timing));
        message.append("); lowering statement without timing semantics");
        diagnostics->warn(stmt.sourceRange.start(), std::move(message));
    }

    void warnWaitStatement(const slang::ast::WaitStatement& stmt)
    {
        warnIgnoredStatement(stmt, "wait statement");
    }

    void warnWaitForkStatement(const slang::ast::WaitForkStatement& stmt)
    {
        warnIgnoredStatement(stmt, "wait fork");
    }

    void warnWaitOrderStatement(const slang::ast::WaitOrderStatement& stmt)
    {
        warnIgnoredStatement(stmt, "wait order");
    }

    void warnEventTriggerStatement(const slang::ast::EventTriggerStatement& stmt)
    {
        warnIgnoredStatement(stmt, "event trigger");
    }

    void warnDisableForkStatement(const slang::ast::DisableForkStatement& stmt)
    {
        warnIgnoredStatement(stmt, "disable fork");
    }

    void scanForLoopControl(const slang::ast::ForLoopStatement& stmt)
    {
        AssignmentExprVisitor visitor(*this);
        if (!stmt.loopVars.empty())
        {
            for (const slang::ast::VariableSymbol* var : stmt.loopVars)
            {
                if (!var)
                {
                    continue;
                }
                if (const slang::ast::Expression* initExpr = var->getInitializer())
                {
                    initExpr->visit(visitor);
                }
            }
        }
        else
        {
            for (const slang::ast::Expression* initExpr : stmt.initializers)
            {
                if (!initExpr)
                {
                    continue;
                }
                initExpr->visit(visitor);
            }
        }
        if (stmt.stopExpr)
        {
            stmt.stopExpr->visit(visitor);
        }
        for (const slang::ast::Expression* step : stmt.steps)
        {
            if (!step)
            {
                continue;
            }
            step->visit(visitor);
        }
    }

    bool tryUnrollRepeat(const slang::ast::RepeatLoopStatement& stmt)
    {
        if (maxLoopIterations == 0)
        {
            setLoopControlFailure("maxLoopIterations is 0");
            return false;
        }

        slang::ast::EvalContext countCtx(*plan.body);
        std::optional<int64_t> count = evalConstantInt(stmt.count, countCtx);
        if (!count || *count < 0)
        {
            setLoopControlFailure("repeat count is not statically evaluable");
            return false;
        }
        const uint64_t maxIterations = static_cast<uint64_t>(maxLoopIterations);
        if (static_cast<uint64_t>(*count) > maxIterations)
        {
            setLoopControlFailure("repeat count exceeds maxLoopIterations");
            return false;
        }

        const bool hasLoopControl = containsLoopControl(stmt.body);
        if (!hasLoopControl)
        {
            for (int64_t i = 0; i < *count; ++i)
            {
                visitStatement(stmt.body);
            }
            return true;
        }

        slang::ast::EvalContext dryCtx(*plan.body);
        LoopControlResult dryRun = runRepeatWithControl(stmt, *count, dryCtx, false);
        if (dryRun != LoopControlResult::Unsupported)
        {
            slang::ast::EvalContext emitCtx(*plan.body);
            LoopControlResult result = runRepeatWithControl(stmt, *count, emitCtx, true);
            if (result != LoopControlResult::Unsupported)
            {
                return true;
            }
        }

        clearLoopControlFailure();
        return unrollRepeatDynamic(stmt, *count);
    }

    bool tryUnrollWhile(const slang::ast::WhileLoopStatement& stmt)
    {
        if (maxLoopIterations == 0)
        {
            setLoopControlFailure("maxLoopIterations is 0");
            return false;
        }

        slang::ast::EvalContext dryCtx(*plan.body);
        LoopControlResult dryRun = runWhileWithControl(stmt, dryCtx, false);
        if (dryRun != LoopControlResult::Unsupported)
        {
            slang::ast::EvalContext emitCtx(*plan.body);
            LoopControlResult result = runWhileWithControl(stmt, emitCtx, true);
            if (result != LoopControlResult::Unsupported)
            {
                return true;
            }
        }

        return false;
    }

    bool tryUnrollDoWhile(const slang::ast::DoWhileLoopStatement& stmt)
    {
        if (maxLoopIterations == 0)
        {
            setLoopControlFailure("maxLoopIterations is 0");
            return false;
        }

        slang::ast::EvalContext dryCtx(*plan.body);
        LoopControlResult dryRun = runDoWhileWithControl(stmt, dryCtx, false);
        if (dryRun != LoopControlResult::Unsupported)
        {
            slang::ast::EvalContext emitCtx(*plan.body);
            LoopControlResult result = runDoWhileWithControl(stmt, emitCtx, true);
            if (result != LoopControlResult::Unsupported)
            {
                return true;
            }
        }

        return false;
    }

    bool tryUnrollForever(const slang::ast::ForeverLoopStatement& stmt)
    {
        if (maxLoopIterations == 0)
        {
            setLoopControlFailure("maxLoopIterations is 0");
            return false;
        }

        slang::ast::EvalContext dryCtx(*plan.body);
        LoopControlResult dryRun = runForeverWithControl(stmt, dryCtx, false);
        if (dryRun != LoopControlResult::Unsupported)
        {
            slang::ast::EvalContext emitCtx(*plan.body);
            LoopControlResult result = runForeverWithControl(stmt, emitCtx, true);
            if (result != LoopControlResult::Unsupported)
            {
                return true;
            }
        }

        return false;
    }

    bool tryUnrollFor(const slang::ast::ForLoopStatement& stmt)
    {
        if (!stmt.stopExpr)
        {
            setLoopControlFailure("for-loop missing stop condition");
            return false;
        }

        if (maxLoopIterations == 0)
        {
            setLoopControlFailure("maxLoopIterations is 0");
            return false;
        }

        const bool hasLoopControl = containsLoopControl(stmt.body);
        if (!hasLoopControl)
        {
            slang::ast::EvalContext ctx(*plan.body);
            if (!prepareForLoopState(stmt, ctx))
            {
                return false;
            }

            uint32_t iterations = 0;
            while (iterations < maxLoopIterations)
            {
                bool cond = false;
                if (!evalForLoopCondition(stmt, ctx, cond))
                {
                    return false;
                }
                if (!cond)
                {
                    return true;
                }

                visitStatement(stmt.body);

                if (!executeForLoopSteps(stmt, ctx))
                {
                    return false;
                }
                ++iterations;
            }
            return false;
        }

        slang::ast::EvalContext dryCtx(*plan.body);
        if (!prepareForLoopState(stmt, dryCtx))
        {
            setLoopControlFailure("for-loop init is not statically evaluable");
            return false;
        }
        LoopControlResult dryRun = runForWithControl(stmt, dryCtx, false);
        if (dryRun != LoopControlResult::Unsupported)
        {
            slang::ast::EvalContext emitCtx(*plan.body);
            if (!prepareForLoopState(stmt, emitCtx))
            {
                setLoopControlFailure("for-loop init is not statically evaluable");
                return false;
            }
            LoopControlResult result = runForWithControl(stmt, emitCtx, true);
            if (result != LoopControlResult::Unsupported)
            {
                return true;
            }
        }

        clearLoopControlFailure();
        return unrollForDynamic(stmt);
    }

    bool tryUnrollForeach(const slang::ast::ForeachLoopStatement& stmt)
    {
        if (stmt.loopDims.empty())
        {
            setLoopControlFailure("foreach has no loop dimensions");
            return false;
        }

        if (maxLoopIterations == 0)
        {
            return false;
        }

        const uint64_t maxIterations = static_cast<uint64_t>(maxLoopIterations);
        uint64_t total = 1;
        for (const auto& dim : stmt.loopDims)
        {
            if (!dim.range)
            {
                setLoopControlFailure("foreach dimension range is not static");
                return false;
            }
            const uint64_t width = dim.range->fullWidth();
            if (width == 0)
            {
                setLoopControlFailure("foreach dimension has zero width");
                return false;
            }
            if (total > maxIterations / width)
            {
                setLoopControlFailure("foreach iterations exceed maxLoopIterations");
                return false;
            }
            total *= width;
        }

        if (total > maxIterations)
        {
            setLoopControlFailure("foreach iterations exceed maxLoopIterations");
            return false;
        }

        const bool hasLoopControl = containsLoopControl(stmt.body);
        if (!hasLoopControl)
        {
            for (uint64_t i = 0; i < total; ++i)
            {
                visitStatement(stmt.body);
            }
            return true;
        }

        if (tryUnrollForeachWithControl(stmt))
        {
            return true;
        }

        clearLoopControlFailure();
        return unrollForeachDynamic(stmt, total);
    }

    bool containsLoopControl(const slang::ast::Statement& stmt) const
    {
        if (stmt.as_if<slang::ast::BreakStatement>() ||
            stmt.as_if<slang::ast::ContinueStatement>())
        {
            return true;
        }
        if (const auto* list = stmt.as_if<slang::ast::StatementList>())
        {
            for (const slang::ast::Statement* child : list->list)
            {
                if (!child)
                {
                    continue;
                }
                if (containsLoopControl(*child))
                {
                    return true;
                }
            }
            return false;
        }
        if (const auto* block = stmt.as_if<slang::ast::BlockStatement>())
        {
            return containsLoopControl(block->body);
        }
        if (const auto* timed = stmt.as_if<slang::ast::TimedStatement>())
        {
            return containsLoopControl(timed->stmt);
        }
        if (const auto* conditional = stmt.as_if<slang::ast::ConditionalStatement>())
        {
            if (containsLoopControl(conditional->ifTrue))
            {
                return true;
            }
            if (conditional->ifFalse && containsLoopControl(*conditional->ifFalse))
            {
                return true;
            }
            return false;
        }
        if (const auto* caseStmt = stmt.as_if<slang::ast::CaseStatement>())
        {
            for (const auto& item : caseStmt->items)
            {
                if (item.stmt && containsLoopControl(*item.stmt))
                {
                    return true;
                }
            }
            if (caseStmt->defaultCase && containsLoopControl(*caseStmt->defaultCase))
            {
                return true;
            }
            return false;
        }
        if (stmt.as_if<slang::ast::ForLoopStatement>() ||
            stmt.as_if<slang::ast::RepeatLoopStatement>() ||
            stmt.as_if<slang::ast::ForeachLoopStatement>() ||
            stmt.as_if<slang::ast::WhileLoopStatement>() ||
            stmt.as_if<slang::ast::DoWhileLoopStatement>() ||
            stmt.as_if<slang::ast::ForeverLoopStatement>())
        {
            return false;
        }
        if (const auto* invalid = stmt.as_if<slang::ast::InvalidStatement>())
        {
            if (invalid->child)
            {
                return containsLoopControl(*invalid->child);
            }
            return false;
        }
        return false;
    }

    LoopControlResult visitStatementWithControl(const slang::ast::Statement& stmt,
                                                slang::ast::EvalContext& ctx,
                                                bool emit)
    {
        if (!containsLoopControl(stmt))
        {
            if (emit)
            {
                visitStatement(stmt);
            }
            return LoopControlResult::None;
        }

        if (const auto* list = stmt.as_if<slang::ast::StatementList>())
        {
            for (const slang::ast::Statement* child : list->list)
            {
                if (!child)
                {
                    continue;
                }
                LoopControlResult result = visitStatementWithControl(*child, ctx, emit);
                if (result != LoopControlResult::None)
                {
                    return result;
                }
            }
            return LoopControlResult::None;
        }
        if (const auto* block = stmt.as_if<slang::ast::BlockStatement>())
        {
            return visitStatementWithControl(block->body, ctx, emit);
        }
        if (const auto* timed = stmt.as_if<slang::ast::TimedStatement>())
        {
            return visitStatementWithControl(timed->stmt, ctx, emit);
        }
        if (const auto* conditional = stmt.as_if<slang::ast::ConditionalStatement>())
        {
            return visitConditionalWithControl(*conditional, ctx, emit);
        }
        if (stmt.as_if<slang::ast::CaseStatement>())
        {
            setLoopControlFailure("case statement with break/continue is not statically evaluable");
            return LoopControlResult::Unsupported;
        }
        if (stmt.as_if<slang::ast::PatternCaseStatement>())
        {
            reportError(stmt, "Pattern case lowering is unsupported");
            return LoopControlResult::Unsupported;
        }
        if (stmt.as_if<slang::ast::BreakStatement>())
        {
            return LoopControlResult::Break;
        }
        if (stmt.as_if<slang::ast::ContinueStatement>())
        {
            return LoopControlResult::Continue;
        }
        if (const auto* exprStmt = stmt.as_if<slang::ast::ExpressionStatement>())
        {
            if (emit)
            {
                scanExpression(exprStmt->expr);
            }
            return LoopControlResult::None;
        }
        if (const auto* procAssign = stmt.as_if<slang::ast::ProceduralAssignStatement>())
        {
            if (emit)
            {
                scanExpression(procAssign->assignment);
            }
            return LoopControlResult::None;
        }
        if (const auto* invalid = stmt.as_if<slang::ast::InvalidStatement>())
        {
            if (invalid->child)
            {
                return visitStatementWithControl(*invalid->child, ctx, emit);
            }
            return LoopControlResult::None;
        }
        if (stmt.kind == slang::ast::StatementKind::Empty)
        {
            return LoopControlResult::None;
        }

        setLoopControlFailure("unsupported statement in loop with break/continue");
        return LoopControlResult::Unsupported;
    }

    LoopControlResult visitConditionalWithControl(const slang::ast::ConditionalStatement& stmt,
                                                  slang::ast::EvalContext& ctx,
                                                  bool emit)
    {
        if (stmt.conditions.empty())
        {
            reportUnsupported(stmt, "Conditional statement missing condition");
            return LoopControlResult::Unsupported;
        }
        for (const auto& cond : stmt.conditions)
        {
            if (cond.pattern)
            {
                reportError(stmt, "Patterned condition lowering is unsupported");
                return LoopControlResult::Unsupported;
            }
        }

        bool combined = true;
        for (const auto& cond : stmt.conditions)
        {
            if (emit)
            {
                scanExpression(*cond.expr);
            }
            std::optional<bool> value = evalConstantBool(*cond.expr, ctx);
            if (!value)
            {
                setLoopControlFailure("if-condition for break/continue is not statically evaluable");
                return LoopControlResult::Unsupported;
            }
            combined = combined && *value;
        }

        if (combined)
        {
            return visitStatementWithControl(stmt.ifTrue, ctx, emit);
        }
        if (stmt.ifFalse)
        {
            return visitStatementWithControl(*stmt.ifFalse, ctx, emit);
        }
        return LoopControlResult::None;
    }

    LoopControlResult runRepeatWithControl(const slang::ast::RepeatLoopStatement& stmt,
                                           int64_t count,
                                           slang::ast::EvalContext& ctx,
                                           bool emit)
    {
        for (int64_t i = 0; i < count; ++i)
        {
            LoopControlResult result = visitStatementWithControl(stmt.body, ctx, emit);
            if (result == LoopControlResult::Unsupported)
            {
                return result;
            }
            if (result == LoopControlResult::Break)
            {
                return result;
            }
        }
        return LoopControlResult::None;
    }

    LoopControlResult runForWithControl(const slang::ast::ForLoopStatement& stmt,
                                        slang::ast::EvalContext& ctx,
                                        bool emit)
    {
        uint32_t iterations = 0;
        while (iterations < maxLoopIterations)
        {
            bool cond = false;
            if (!evalForLoopCondition(stmt, ctx, cond))
            {
                setLoopControlFailure("for-loop condition is not statically evaluable");
                return LoopControlResult::Unsupported;
            }
            if (!cond)
            {
                return LoopControlResult::None;
            }

            LoopControlResult result = visitStatementWithControl(stmt.body, ctx, emit);
            if (result == LoopControlResult::Unsupported)
            {
                return result;
            }
            if (result == LoopControlResult::Break)
            {
                return result;
            }
            if (result == LoopControlResult::Continue)
            {
                if (!executeForLoopSteps(stmt, ctx))
                {
                    setLoopControlFailure("for-loop step is not statically evaluable");
                    return LoopControlResult::Unsupported;
                }
                ++iterations;
                continue;
            }

            if (!executeForLoopSteps(stmt, ctx))
            {
                setLoopControlFailure("for-loop step is not statically evaluable");
                return LoopControlResult::Unsupported;
            }
            ++iterations;
        }

        setLoopControlFailure("for-loop exceeds maxLoopIterations");
        return LoopControlResult::Unsupported;
    }

    LoopControlResult runWhileWithControl(const slang::ast::WhileLoopStatement& stmt,
                                          slang::ast::EvalContext& ctx,
                                          bool emit)
    {
        uint32_t iterations = 0;
        while (iterations < maxLoopIterations)
        {
            std::optional<bool> cond = evalConstantBool(stmt.cond, ctx);
            if (!cond)
            {
                setLoopControlFailure("while-loop condition is not statically evaluable");
                return LoopControlResult::Unsupported;
            }
            if (!*cond)
            {
                return LoopControlResult::None;
            }

            LoopControlResult result = visitStatementWithControl(stmt.body, ctx, emit);
            if (result == LoopControlResult::Unsupported)
            {
                return result;
            }
            if (result == LoopControlResult::Break)
            {
                return result;
            }
            ++iterations;
        }

        setLoopControlFailure("while-loop exceeds maxLoopIterations");
        return LoopControlResult::Unsupported;
    }

    LoopControlResult runDoWhileWithControl(const slang::ast::DoWhileLoopStatement& stmt,
                                            slang::ast::EvalContext& ctx,
                                            bool emit)
    {
        uint32_t iterations = 0;
        while (iterations < maxLoopIterations)
        {
            LoopControlResult result = visitStatementWithControl(stmt.body, ctx, emit);
            if (result == LoopControlResult::Unsupported)
            {
                return result;
            }
            if (result == LoopControlResult::Break)
            {
                return result;
            }
            ++iterations;

            std::optional<bool> cond = evalConstantBool(stmt.cond, ctx);
            if (!cond)
            {
                setLoopControlFailure("do-while condition is not statically evaluable");
                return LoopControlResult::Unsupported;
            }
            if (!*cond)
            {
                return LoopControlResult::None;
            }
        }

        setLoopControlFailure("do-while exceeds maxLoopIterations");
        return LoopControlResult::Unsupported;
    }

    LoopControlResult runForeverWithControl(const slang::ast::ForeverLoopStatement& stmt,
                                            slang::ast::EvalContext& ctx,
                                            bool emit)
    {
        uint32_t iterations = 0;
        while (iterations < maxLoopIterations)
        {
            LoopControlResult result = visitStatementWithControl(stmt.body, ctx, emit);
            if (result == LoopControlResult::Unsupported)
            {
                return result;
            }
            if (result == LoopControlResult::Break)
            {
                return result;
            }
            ++iterations;
        }

        setLoopControlFailure("forever-loop exceeds maxLoopIterations");
        return LoopControlResult::Unsupported;
    }

    bool unrollRepeatDynamic(const slang::ast::RepeatLoopStatement& stmt, int64_t count)
    {
        pushLoopContext(stmt.sourceRange.start());
        for (int64_t i = 0; i < count; ++i)
        {
            ExprNodeId iterGuard = currentLoopAlive();
            pushFlowGuard(iterGuard);
            visitStatement(stmt.body);
            popFlowGuard();
        }
        popLoopContext();
        return true;
    }

    bool unrollForDynamic(const slang::ast::ForLoopStatement& stmt)
    {
        slang::ast::EvalContext ctx(*plan.body);
        if (!prepareForLoopState(stmt, ctx))
        {
            setLoopControlFailure("for-loop init is not statically evaluable");
            return false;
        }

        pushLoopContext(stmt.sourceRange.start());
        uint32_t iterations = 0;
        while (iterations < maxLoopIterations)
        {
            bool cond = false;
            if (!evalForLoopCondition(stmt, ctx, cond))
            {
                setLoopControlFailure("for-loop condition is not statically evaluable");
                popLoopContext();
                return false;
            }
            if (!cond)
            {
                popLoopContext();
                return true;
            }

            ExprNodeId iterGuard = currentLoopAlive();
            pushFlowGuard(iterGuard);
            visitStatement(stmt.body);
            popFlowGuard();

            if (!executeForLoopSteps(stmt, ctx))
            {
                setLoopControlFailure("for-loop step is not statically evaluable");
                popLoopContext();
                return false;
            }
            ++iterations;
        }

        setLoopControlFailure("for-loop exceeds maxLoopIterations");
        popLoopContext();
        return false;
    }

    bool unrollForeachDynamic(const slang::ast::ForeachLoopStatement& stmt, uint64_t total)
    {
        pushLoopContext(stmt.sourceRange.start());
        for (uint64_t i = 0; i < total; ++i)
        {
            ExprNodeId iterGuard = currentLoopAlive();
            pushFlowGuard(iterGuard);
            visitStatement(stmt.body);
            popFlowGuard();
        }
        popLoopContext();
        return true;
    }

    bool tryUnrollForeachWithControl(const slang::ast::ForeachLoopStatement& stmt)
    {
        std::vector<ForeachDimState> dims;
        dims.reserve(stmt.loopDims.size());
        for (const auto& dim : stmt.loopDims)
        {
            if (!dim.range || !dim.loopVar)
            {
                setLoopControlFailure("foreach dimension is not statically evaluable");
                return false;
            }
            const slang::ast::Type& type = dim.loopVar->getType();
            if (!type.isIntegral())
            {
                setLoopControlFailure("foreach loop variable is not integral");
                return false;
            }
            const int32_t lo = std::min(dim.range->left, dim.range->right);
            const int32_t hi = std::max(dim.range->left, dim.range->right);
            ForeachDimState state;
            state.loopVar = dim.loopVar;
            state.start = lo;
            state.stop = hi;
            state.step = 1;
            dims.push_back(state);
        }

        if (dims.empty())
        {
            return false;
        }

        slang::ast::EvalContext dryCtx(*plan.body);
        std::size_t dryIterations = 0;
        LoopControlResult dryRun =
            unrollForeachRecursive(stmt, dims, 0, dryIterations, dryCtx, false);
        if (dryRun == LoopControlResult::Unsupported)
        {
            return false;
        }

        slang::ast::EvalContext emitCtx(*plan.body);
        std::size_t emitIterations = 0;
        LoopControlResult result =
            unrollForeachRecursive(stmt, dims, 0, emitIterations, emitCtx, true);
        return result == LoopControlResult::None || result == LoopControlResult::Break;
    }

    LoopControlResult unrollForeachRecursive(const slang::ast::ForeachLoopStatement& stmt,
                                             std::span<const ForeachDimState> dims,
                                             std::size_t depth,
                                             std::size_t& iterations,
                                             slang::ast::EvalContext& ctx,
                                             bool emit)
    {
        if (depth >= dims.size())
        {
            if (iterations++ >= maxLoopIterations)
            {
                setLoopControlFailure("foreach iterations exceed maxLoopIterations");
                return LoopControlResult::Unsupported;
            }
            LoopControlResult result = visitStatementWithControl(stmt.body, ctx, emit);
            if (result == LoopControlResult::Continue)
            {
                return LoopControlResult::None;
            }
            return result;
        }

        const ForeachDimState& dim = dims[depth];
        for (int32_t index = dim.start;; index += dim.step)
        {
            if (!setLoopLocal(*dim.loopVar, index, ctx))
            {
                return LoopControlResult::Unsupported;
            }

            LoopControlResult result =
                unrollForeachRecursive(stmt, dims, depth + 1, iterations, ctx, emit);
            if (result == LoopControlResult::Break)
            {
                return LoopControlResult::Break;
            }
            if (result == LoopControlResult::Unsupported)
            {
                return LoopControlResult::Unsupported;
            }

            if (index == dim.stop)
            {
                break;
            }
        }

        return LoopControlResult::None;
    }

    bool setLoopLocal(const slang::ast::ValueSymbol& symbol, int64_t value,
                      slang::ast::EvalContext& ctx) const
    {
        const slang::ast::Type& type = symbol.getType();
        if (!type.isIntegral())
        {
            return false;
        }
        const int64_t rawWidth = static_cast<int64_t>(type.getBitstreamWidth());
        const slang::bitwidth_t width =
            static_cast<slang::bitwidth_t>(rawWidth > 0 ? rawWidth : 1);
        slang::SVInt literal(value);
        slang::SVInt resized = literal.resize(width);
        resized.setSigned(type.isSigned());
        slang::ConstantValue stored(resized);
        if (slang::ConstantValue* slot = ctx.findLocal(&symbol))
        {
            *slot = std::move(stored);
            return true;
        }
        return ctx.createLocal(&symbol, std::move(stored)) != nullptr;
    }

    std::optional<bool> evalConstantBool(const slang::ast::Expression& expr,
                                         slang::ast::EvalContext& ctx) const
    {
        slang::ConstantValue value = expr.eval(ctx);
        if (!value || value.hasUnknown())
        {
            return std::nullopt;
        }
        if (value.isTrue())
        {
            return true;
        }
        if (value.isFalse())
        {
            return false;
        }
        if (value.isInteger())
        {
            if (auto raw = value.integer().as<int64_t>())
            {
                return *raw != 0;
            }
        }
        return std::nullopt;
    }

    std::optional<int64_t> evalConstantInt(const slang::ast::Expression& expr,
                                           slang::ast::EvalContext& ctx) const
    {
        ctx.reset();
        slang::ConstantValue value = expr.eval(ctx);
        if (!value || !value.isInteger() || value.hasUnknown())
        {
            return std::nullopt;
        }
        return value.integer().as<int64_t>();
    }

    std::optional<slang::SVInt> evalConstantValue(const slang::ast::Expression& expr,
                                                  slang::ast::EvalContext& ctx) const
    {
        ctx.reset();
        slang::ConstantValue value = expr.eval(ctx);
        if (!value || !value.isInteger())
        {
            return std::nullopt;
        }
        return value.integer();
    }

    const slang::ast::ValueSymbol*
    resolveAssignedValueSymbol(const slang::ast::Expression& expr) const
    {
        const slang::ast::Expression* current = &expr;
        while (current)
        {
            if (const auto* assign = current->as_if<slang::ast::AssignmentExpression>())
            {
                current = &assign->left();
                continue;
            }
            if (const auto* named = current->as_if<slang::ast::NamedValueExpression>())
            {
                return &named->symbol;
            }
            if (const auto* hier = current->as_if<slang::ast::HierarchicalValueExpression>())
            {
                return &hier->symbol;
            }
            if (const auto* select = current->as_if<slang::ast::ElementSelectExpression>())
            {
                current = &select->value();
                continue;
            }
            if (const auto* range = current->as_if<slang::ast::RangeSelectExpression>())
            {
                current = &range->value();
                continue;
            }
            if (const auto* conversion = current->as_if<slang::ast::ConversionExpression>())
            {
                if (!conversion->isImplicit())
                {
                    break;
                }
                current = &conversion->operand();
                continue;
            }
            break;
        }
        return nullptr;
    }

    bool prepareForLoopState(const slang::ast::ForLoopStatement& stmt,
                             slang::ast::EvalContext& ctx) const
    {
        auto addLoopVar = [&](const slang::ast::ValueSymbol& symbol,
                              const slang::ast::Expression& initExpr) -> bool {
            const slang::ast::Type& type = symbol.getType();
            if (!type.isIntegral())
            {
                return false;
            }

            slang::ast::EvalContext initCtx(*plan.body);
            std::optional<int64_t> value = evalConstantInt(initExpr, initCtx);
            if (!value)
            {
                return false;
            }

            const int64_t rawWidth = static_cast<int64_t>(type.getBitstreamWidth());
            const slang::bitwidth_t width =
                static_cast<slang::bitwidth_t>(rawWidth > 0 ? rawWidth : 1);
            slang::SVInt initValue(*value);
            slang::SVInt sized = initValue.resize(width);
            sized.setSigned(type.isSigned());
            slang::ConstantValue initConst(sized);
            return ctx.createLocal(&symbol, std::move(initConst)) != nullptr;
        };

        if (!stmt.loopVars.empty())
        {
            for (const slang::ast::VariableSymbol* var : stmt.loopVars)
            {
                if (!var)
                {
                    continue;
                }
                const slang::ast::Expression* initExpr = var->getInitializer();
                if (!initExpr)
                {
                    return false;
                }
                if (!addLoopVar(*var, *initExpr))
                {
                    return false;
                }
            }
            return true;
        }

        if (stmt.initializers.empty())
        {
            return false;
        }
        for (const slang::ast::Expression* initExpr : stmt.initializers)
        {
            const auto* assign = initExpr ? initExpr->as_if<slang::ast::AssignmentExpression>()
                                          : nullptr;
            if (!assign)
            {
                return false;
            }
            const slang::ast::ValueSymbol* symbol = resolveAssignedValueSymbol(assign->left());
            if (!symbol)
            {
                return false;
            }
            if (!addLoopVar(*symbol, assign->right()))
            {
                return false;
            }
        }
        return true;
    }

    bool evalForLoopCondition(const slang::ast::ForLoopStatement& stmt,
                              slang::ast::EvalContext& ctx, bool& result) const
    {
        if (!stmt.stopExpr)
        {
            return false;
        }

        slang::ConstantValue cond = stmt.stopExpr->eval(ctx);
        if (!cond || cond.hasUnknown())
        {
            return false;
        }
        if (cond.isTrue())
        {
            result = true;
            return true;
        }
        if (cond.isFalse())
        {
            result = false;
            return true;
        }
        if (cond.isInteger())
        {
            auto value = cond.integer().as<int64_t>();
            if (!value)
            {
                return false;
            }
            result = *value != 0;
            return true;
        }
        return false;
    }

    bool executeForLoopSteps(const slang::ast::ForLoopStatement& stmt,
                             slang::ast::EvalContext& ctx) const
    {
        for (const slang::ast::Expression* step : stmt.steps)
        {
            if (!step)
            {
                continue;
            }
            if (const auto* assign = step->as_if<slang::ast::AssignmentExpression>())
            {
                if (const slang::ast::ValueSymbol* symbol =
                        resolveAssignedValueSymbol(assign->left()))
                {
                    slang::ConstantValue value = assign->right().eval(ctx);
                    if (!value || !value.isInteger() || value.hasUnknown())
                    {
                        return false;
                    }
                    const slang::ast::Type& type = symbol->getType();
                    const int64_t rawWidth = static_cast<int64_t>(type.getBitstreamWidth());
                    const slang::bitwidth_t width =
                        static_cast<slang::bitwidth_t>(rawWidth > 0 ? rawWidth : 1);
                    slang::SVInt nextValue = value.integer().resize(width);
                    nextValue.setSigned(type.isSigned());
                    slang::ConstantValue nextConst(nextValue);
                    if (slang::ConstantValue* storage = ctx.findLocal(symbol))
                    {
                        *storage = std::move(nextConst);
                    }
                    else if (!ctx.createLocal(symbol, std::move(nextConst)))
                    {
                        return false;
                    }
                    continue;
                }
            }

            slang::ConstantValue value = step->eval(ctx);
            if (!value)
            {
                return false;
            }
        }
        return true;
    }

    bool isTwoStateExpr(const slang::ast::Expression& expr) const
    {
        const slang::ast::Expression* current = &expr;
        while (current)
        {
            if (const auto* conversion = current->as_if<slang::ast::ConversionExpression>())
            {
                if (conversion->isImplicit())
                {
                    current = &conversion->operand();
                    continue;
                }
            }
            if (const auto* named = current->as_if<slang::ast::NamedValueExpression>())
            {
                return !named->symbol.getType().isFourState();
            }
            if (const auto* hier = current->as_if<slang::ast::HierarchicalValueExpression>())
            {
                return !hier->symbol.getType().isFourState();
            }
            break;
        }
        return !expr.type->isFourState();
    }

    std::string describeFileLocation(slang::SourceLocation location) const
    {
        if (!location.valid() || !plan.body)
        {
            return {};
        }
        const auto* sourceManager = plan.body->getCompilation().getSourceManager();
        if (!sourceManager)
        {
            return {};
        }
        const auto loc = sourceManager->getFullyOriginalLoc(location);
        if (!loc.valid() || !sourceManager->isFileLoc(loc))
        {
            return {};
        }
        const auto fileName = sourceManager->getFileName(loc);
        const auto line = sourceManager->getLineNumber(loc);
        const auto column = sourceManager->getColumnNumber(loc);
        std::string text(fileName);
        text.push_back(':');
        text.append(std::to_string(line));
        text.push_back(':');
        text.append(std::to_string(column));
        return text;
    }

    ExprNodeId makeOperation(grh::ir::OperationKind op, std::vector<ExprNodeId> operands,
                             slang::SourceLocation location)
    {
        ExprNode node;
        node.kind = ExprNodeKind::Operation;
        node.op = op;
        node.operands = std::move(operands);
        node.location = location;
        node.tempSymbol = makeTempSymbol();
        return addNode(nullptr, std::move(node));
    }

    PlanSymbolId makeDpiResultSymbol()
    {
        const std::string name = "_dpi_ret_" + std::to_string(nextDpiResult++);
        return plan.symbolTable.intern(name);
    }

    PlanSymbolId makeTempSymbol()
    {
        const std::string name = "_expr_tmp_" + std::to_string(nextTemp++);
        PlanSymbolId id = plan.symbolTable.intern(name);
        lowering.tempSymbols.push_back(id);
        return id;
    }

    ExprNodeId addNode(const slang::ast::Expression* expr, ExprNode node)
    {
        const ExprNodeId id = static_cast<ExprNodeId>(lowering.values.size());
        lowering.values.push_back(std::move(node));
        if (expr)
        {
            lowered.emplace(expr, id);
        }
        return id;
    }

    ExprNodeId addConstantLiteral(std::string literal, slang::SourceLocation location)
    {
        ExprNode node;
        node.kind = ExprNodeKind::Constant;
        node.literal = std::move(literal);
        node.location = location;
        return addNode(nullptr, std::move(node));
    }

    struct CaseMaskInfo {
        ExprNodeId mask = kInvalidPlanIndex;
    };

    CaseMaskInfo buildCaseWildcardMask(const slang::ast::Expression& controlExpr,
                                       const slang::ast::Expression& itemExpr,
                                       slang::ast::CaseStatementCondition condition,
                                       slang::SourceLocation location)
    {
        CaseMaskInfo info{};
        slang::ast::EvalContext ctx(*plan.body);
        std::optional<slang::SVInt> itemValue = evalConstantValue(itemExpr, ctx);
        if (!itemValue)
        {
            return info;
        }

        uint64_t controlWidthRaw = controlExpr.type->getBitstreamWidth();
        slang::bitwidth_t controlWidth =
            static_cast<slang::bitwidth_t>(controlWidthRaw > 0 ? controlWidthRaw : 0);
        slang::bitwidth_t itemWidth = itemValue->getBitWidth();
        slang::bitwidth_t width = controlWidth > 0 ? std::max(controlWidth, itemWidth) : itemWidth;
        if (width == 0)
        {
            return info;
        }

        const bool controlSigned = controlExpr.type->isSigned();
        const bool bothSigned = controlSigned && itemValue->isSigned();
        slang::SVInt aligned = *itemValue;
        aligned.setSigned(bothSigned);
        if (width > aligned.getBitWidth())
        {
            aligned = aligned.extend(width, bothSigned);
        }

        std::vector<slang::logic_t> digits;
        digits.reserve(width);
        for (int32_t i = static_cast<int32_t>(width); i-- > 0;)
        {
            const slang::logic_t bit = aligned[i];
            bool wildcard = false;
            if (condition == slang::ast::CaseStatementCondition::WildcardXOrZ)
            {
                wildcard = bit.isUnknown();
            }
            else
            {
                wildcard = exactlyEqual(bit, slang::logic_t::z);
            }
            digits.push_back(wildcard ? slang::logic_t(0) : slang::logic_t(1));
        }

        slang::SVInt mask = slang::SVInt::fromDigits(width, slang::LiteralBase::Binary,
                                                     false, false, digits);
        std::string literal = mask.toString(slang::LiteralBase::Binary, true);
        info.mask = addConstantLiteral(std::move(literal), location);
        return info;
    }

    ExprNodeId buildInsideValueRangeMatch(ExprNodeId control,
                                          const slang::ast::ValueRangeExpression& range,
                                          slang::SourceLocation location)
    {
        const bool leftUnbounded =
            range.left().kind == slang::ast::ExpressionKind::UnboundedLiteral;
        const bool rightUnbounded =
            range.right().kind == slang::ast::ExpressionKind::UnboundedLiteral;

        ExprNodeId left = kInvalidPlanIndex;
        ExprNodeId right = kInvalidPlanIndex;
        if (!leftUnbounded)
        {
            left = lowerExpression(range.left());
            if (left == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
        }
        if (!rightUnbounded)
        {
            right = lowerExpression(range.right());
            if (right == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
        }

        if (range.rangeKind == slang::ast::ValueRangeKind::Simple)
        {
            ExprNodeId lowerBound = kInvalidPlanIndex;
            ExprNodeId upperBound = kInvalidPlanIndex;
            if (!leftUnbounded)
            {
                lowerBound =
                    makeOperation(grh::ir::OperationKind::kGe, {control, left}, location);
            }
            if (!rightUnbounded)
            {
                upperBound =
                    makeOperation(grh::ir::OperationKind::kLe, {control, right}, location);
            }
            if (lowerBound == kInvalidPlanIndex && upperBound == kInvalidPlanIndex)
            {
                return addConstantLiteral("1'b1", location);
            }
            if (lowerBound == kInvalidPlanIndex)
            {
                return upperBound;
            }
            if (upperBound == kInvalidPlanIndex)
            {
                return lowerBound;
            }
            return makeLogicAnd(lowerBound, upperBound, location);
        }

        if (leftUnbounded || rightUnbounded)
        {
            reportUnsupported(range, "Unbounded inside tolerance range unsupported");
            return kInvalidPlanIndex;
        }

        ExprNodeId tolerance = right;
        if (range.rangeKind == slang::ast::ValueRangeKind::RelativeTolerance)
        {
            const bool useFloating =
                (range.left().type && range.left().type->isFloating()) ||
                (range.right().type && range.right().type->isFloating());
            ExprNodeId scale =
                addConstantLiteral(useFloating ? "100.0" : "100", location);
            ExprNodeId mul =
                makeOperation(grh::ir::OperationKind::kMul, {left, right}, location);
            tolerance = makeOperation(grh::ir::OperationKind::kDiv, {mul, scale}, location);
        }
        else if (range.rangeKind != slang::ast::ValueRangeKind::AbsoluteTolerance)
        {
            reportUnsupported(range, "Unsupported inside tolerance range kind");
            return kInvalidPlanIndex;
        }

        ExprNodeId lowerExpr =
            makeOperation(grh::ir::OperationKind::kSub, {left, tolerance}, location);
        ExprNodeId upperExpr =
            makeOperation(grh::ir::OperationKind::kAdd, {left, tolerance}, location);
        ExprNodeId lowerBound =
            makeOperation(grh::ir::OperationKind::kGe, {control, lowerExpr}, location);
        ExprNodeId upperBound =
            makeOperation(grh::ir::OperationKind::kLe, {control, upperExpr}, location);
        return makeLogicAnd(lowerBound, upperBound, location);
    }

    ExprNodeId buildInsideItemMatch(ExprNodeId control,
                                    const slang::ast::Expression& controlExpr,
                                    const slang::ast::Expression& item,
                                    slang::SourceLocation location)
    {
        if (const auto* range = item.as_if<slang::ast::ValueRangeExpression>())
        {
            return buildInsideValueRangeMatch(control, *range, location);
        }
        if (item.kind == slang::ast::ExpressionKind::UnboundedLiteral)
        {
            reportUnsupported(item, "Unbounded literal inside match unsupported");
            return kInvalidPlanIndex;
        }

        ExprNodeId itemId = lowerExpression(item);
        if (itemId == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }

        const bool integral = controlExpr.type && controlExpr.type->isIntegral() &&
                              item.type && item.type->isIntegral();
        const grh::ir::OperationKind op =
            integral ? grh::ir::OperationKind::kWildcardEq
                     : grh::ir::OperationKind::kEq;
        return makeOperation(op, {control, itemId}, location);
    }

    ExprNodeId buildCaseItemMatch(ExprNodeId control,
                                  const slang::ast::Expression& controlExpr,
                                  slang::ast::CaseStatementCondition condition,
                                  std::span<const slang::ast::Expression* const> items,
                                  slang::SourceLocation location,
                                  bool& usedCaseEq)
    {
        if (control == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        if (condition == slang::ast::CaseStatementCondition::Inside)
        {
            ExprNodeId combined = kInvalidPlanIndex;
            for (const slang::ast::Expression* expr : items)
            {
                if (!expr)
                {
                    continue;
                }
                scanExpression(*expr);
                ExprNodeId term =
                    buildInsideItemMatch(control, controlExpr, *expr, location);
                if (term == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                if (combined == kInvalidPlanIndex)
                {
                    combined = term;
                }
                else
                {
                    combined = makeLogicOr(combined, term, location);
                }
            }
            return combined;
        }
        const bool controlTwoState = isTwoStateExpr(controlExpr);
        ExprNodeId combined = kInvalidPlanIndex;
        for (const slang::ast::Expression* expr : items)
        {
            if (!expr)
            {
                continue;
            }
            scanExpression(*expr);
            ExprNodeId itemId = lowerExpression(*expr);
            if (itemId == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }

            ExprNodeId term = kInvalidPlanIndex;
            if (condition == slang::ast::CaseStatementCondition::WildcardXOrZ ||
                condition == slang::ast::CaseStatementCondition::WildcardJustZ)
            {
                CaseMaskInfo maskInfo =
                    buildCaseWildcardMask(controlExpr, *expr, condition, location);
                if (maskInfo.mask == kInvalidPlanIndex)
                {
                    reportUnsupported(*expr,
                                      "Wildcard case item not constant; fallback to case equality");
                    term = makeCaseEq(control, itemId, location);
                    usedCaseEq = true;
                }
                else
                {
                    ExprNodeId maskedControl =
                        makeOperation(grh::ir::OperationKind::kAnd, {control, maskInfo.mask},
                                      location);
                    ExprNodeId maskedItem =
                        makeOperation(grh::ir::OperationKind::kAnd, {itemId, maskInfo.mask},
                                      location);
                    term = makeEq(maskedControl, maskedItem, location);
                }
            }
            else
            {
                bool preferSynth = false;
                if (controlTwoState)
                {
                    slang::ast::EvalContext ctx(*plan.body);
                    if (auto value = evalConstantValue(*expr, ctx))
                    {
                        preferSynth = !value->hasUnknown();
                    }
                }
                if (preferSynth)
                {
                    term = makeEq(control, itemId, location);
                }
                else
                {
                    term = makeCaseEq(control, itemId, location);
                    usedCaseEq = true;
                }
            }

            if (combined == kInvalidPlanIndex)
            {
                combined = term;
            }
            else
            {
                combined = makeLogicOr(combined, term, location);
            }
        }
        return combined;
    }

    ExprNodeId resolveAssignmentRoot(const slang::ast::AssignmentExpression& expr)
    {
        auto it = assignmentRoots.find(&expr);
        if (it != assignmentRoots.end())
        {
            return it->second;
        }
        ExprNodeId value = takeNextRoot(expr.sourceRange.start());
        assignmentRoots.emplace(&expr, value);
        return value;
    }

    ExprNodeId lowerExpression(const slang::ast::Expression& expr)
    {
        if (auto it = lowered.find(&expr); it != lowered.end())
        {
            return it->second;
        }

        auto paramLiteral = [](const slang::ast::ParameterSymbol& param)
            -> std::optional<std::string> {
            slang::ConstantValue value = param.getValue();
            if (value.bad())
            {
                return std::nullopt;
            }
            if (!value.isInteger())
            {
                value = value.convertToInt();
            }
            if (!value.isInteger())
            {
                return std::nullopt;
            }
            const slang::SVInt& literal = value.integer();
            if (literal.hasUnknown())
            {
                return std::nullopt;
            }
            return literal.toString();
        };

        ExprNode node;
        node.location = expr.sourceRange.start();

        if (const slang::ConstantValue* constant = expr.getConstant())
        {
            if (constant->isInteger())
            {
                const slang::SVInt& literal = constant->integer();
                if (!literal.hasUnknown())
                {
                    node.kind = ExprNodeKind::Constant;
                    node.literal = literal.toString();
                    return addNode(&expr, std::move(node));
                }
            }
        }

        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            if (const auto* param =
                    named->symbol.as_if<slang::ast::ParameterSymbol>())
            {
                if (auto literal = paramLiteral(*param))
                {
                    node.kind = ExprNodeKind::Constant;
                    node.literal = *literal;
                    return addNode(&expr, std::move(node));
                }
            }
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(named->symbol.name);
            if (!node.symbol.valid() &&
                (named->symbol.kind == slang::ast::SymbolKind::Parameter ||
                 named->symbol.kind == slang::ast::SymbolKind::TypeParameter))
            {
                node.symbol = plan.symbolTable.intern(named->symbol.name);
            }
            if (!node.symbol.valid())
            {
                reportUnsupported(expr, "Unknown symbol in expression");
            }
            return addNode(&expr, std::move(node));
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            if (const auto* param =
                    hier->symbol.as_if<slang::ast::ParameterSymbol>())
            {
                if (auto literal = paramLiteral(*param))
                {
                    node.kind = ExprNodeKind::Constant;
                    node.literal = *literal;
                    return addNode(&expr, std::move(node));
                }
            }
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(hier->symbol.name);
            if (!node.symbol.valid() &&
                (hier->symbol.kind == slang::ast::SymbolKind::Parameter ||
                 hier->symbol.kind == slang::ast::SymbolKind::TypeParameter))
            {
                node.symbol = plan.symbolTable.intern(hier->symbol.name);
            }
            if (!node.symbol.valid())
            {
                reportUnsupported(expr, "Unknown hierarchical symbol in expression");
            }
            return addNode(&expr, std::move(node));
        }
        if (const auto* literal = expr.as_if<slang::ast::IntegerLiteral>())
        {
            node.kind = ExprNodeKind::Constant;
            node.literal = literal->getValue().toString();
            return addNode(&expr, std::move(node));
        }
        if (const auto* literal = expr.as_if<slang::ast::UnbasedUnsizedIntegerLiteral>())
        {
            node.kind = ExprNodeKind::Constant;
            node.literal = literal->getValue().toString();
            return addNode(&expr, std::move(node));
        }
        if (const auto* literal = expr.as_if<slang::ast::StringLiteral>())
        {
            node.kind = ExprNodeKind::Constant;
            node.literal.assign(literal->getValue());
            return addNode(&expr, std::move(node));
        }
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            return lowerExpression(conversion->operand());
        }
        if (const auto* unary = expr.as_if<slang::ast::UnaryExpression>())
        {
            const auto opKind = mapUnaryOp(unary->op);
            if (!opKind)
            {
                reportUnsupported(expr, "Unsupported unary operator");
                return kInvalidPlanIndex;
            }
            ExprNodeId operand = lowerExpression(unary->operand());
            if (operand == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = *opKind;
            node.operands = {operand};
            node.tempSymbol = makeTempSymbol();
            return addNode(&expr, std::move(node));
        }
        if (const auto* binary = expr.as_if<slang::ast::BinaryExpression>())
        {
            const auto opKind = mapBinaryOp(binary->op);
            if (!opKind)
            {
                reportUnsupported(expr, "Unsupported binary operator");
                return kInvalidPlanIndex;
            }
            ExprNodeId lhs = lowerExpression(binary->left());
            ExprNodeId rhs = lowerExpression(binary->right());
            if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = *opKind;
            node.operands = {lhs, rhs};
            node.tempSymbol = makeTempSymbol();
            return addNode(&expr, std::move(node));
        }
        if (const auto* cond = expr.as_if<slang::ast::ConditionalExpression>())
        {
            if (cond->conditions.empty())
            {
                reportUnsupported(expr, "Conditional expression missing condition");
                return kInvalidPlanIndex;
            }
            if (cond->conditions.size() > 1)
            {
                reportUnsupported(expr, "Conditional expression with patterns unsupported");
            }
            const slang::ast::Expression& condExpr = *cond->conditions.front().expr;
            ExprNodeId condId = lowerExpression(condExpr);
            ExprNodeId lhs = lowerExpression(cond->left());
            ExprNodeId rhs = lowerExpression(cond->right());
            if (condId == kInvalidPlanIndex || lhs == kInvalidPlanIndex ||
                rhs == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kMux;
            node.operands = {condId, lhs, rhs};
            node.tempSymbol = makeTempSymbol();
            return addNode(&expr, std::move(node));
        }
        if (const auto* concat = expr.as_if<slang::ast::ConcatenationExpression>())
        {
            std::vector<ExprNodeId> operands;
            operands.reserve(concat->operands().size());
            for (const slang::ast::Expression* operand : concat->operands())
            {
                if (!operand)
                {
                    continue;
                }
                ExprNodeId id = lowerExpression(*operand);
                if (id == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                operands.push_back(id);
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kConcat;
            node.operands = std::move(operands);
            node.tempSymbol = makeTempSymbol();
            return addNode(&expr, std::move(node));
        }
        if (const auto* repl = expr.as_if<slang::ast::ReplicationExpression>())
        {
            ExprNodeId count = lowerExpression(repl->count());
            ExprNodeId concat = lowerExpression(repl->concat());
            if (count == kInvalidPlanIndex || concat == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kReplicate;
            node.operands = {count, concat};
            node.tempSymbol = makeTempSymbol();
            return addNode(&expr, std::move(node));
        }
        if (const auto* select = expr.as_if<slang::ast::ElementSelectExpression>())
        {
            ExprNodeId value = lowerExpression(select->value());
            ExprNodeId index = lowerExpression(select->selector());
            if (value == kInvalidPlanIndex || index == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kSliceDynamic;
            node.operands = {value, index};
            node.tempSymbol = makeTempSymbol();
            return addNode(&expr, std::move(node));
        }
        if (const auto* range = expr.as_if<slang::ast::RangeSelectExpression>())
        {
            ExprNodeId value = lowerExpression(range->value());
            ExprNodeId left = lowerExpression(range->left());
            ExprNodeId right = lowerExpression(range->right());
            if (value == kInvalidPlanIndex || left == kInvalidPlanIndex ||
                right == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
            node.kind = ExprNodeKind::Operation;
            node.op = grh::ir::OperationKind::kSliceDynamic;
            node.operands = {value, left, right};
            node.tempSymbol = makeTempSymbol();
            return addNode(&expr, std::move(node));
        }

        reportUnsupported(expr, "Unsupported expression kind");
        return kInvalidPlanIndex;
    }

    WriteRangeKind mapRangeKind(slang::ast::RangeSelectionKind kind) const
    {
        switch (kind)
        {
        case slang::ast::RangeSelectionKind::Simple:
            return WriteRangeKind::Simple;
        case slang::ast::RangeSelectionKind::IndexedUp:
            return WriteRangeKind::IndexedUp;
        case slang::ast::RangeSelectionKind::IndexedDown:
            return WriteRangeKind::IndexedDown;
        default:
            break;
        }
        return WriteRangeKind::Simple;
    }

    uint64_t computeExprWidth(const slang::ast::Expression& expr)
    {
        if (!expr.type)
        {
            return 0;
        }
        return computeFixedWidth(*expr.type, *plan.body, diagnostics);
    }

    ExprNodeId makeRhsSlice(ExprNodeId value, uint64_t high, uint64_t low,
                            slang::SourceLocation location)
    {
        if (value == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        if (high == low)
        {
            ExprNodeId index = addConstantLiteral(std::to_string(low), location);
            return makeOperation(grh::ir::OperationKind::kSliceDynamic, {value, index}, location);
        }
        ExprNodeId left = addConstantLiteral(std::to_string(high), location);
        ExprNodeId right = addConstantLiteral(std::to_string(low), location);
        return makeOperation(grh::ir::OperationKind::kSliceDynamic, {value, left, right}, location);
    }

    bool resolveLValueTargets(const slang::ast::Expression& expr,
                              std::vector<LValueTarget>& targets,
                              LValueCompositeInfo& composite)
    {
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            if (conversion->isImplicit())
            {
                return resolveLValueTargets(conversion->operand(), targets, composite);
            }
            reportUnsupported(expr, "Unsupported explicit conversion in LHS");
            return false;
        }
        if (const auto* concat = expr.as_if<slang::ast::ConcatenationExpression>())
        {
            composite.isComposite = true;
            for (const slang::ast::Expression* operand : concat->operands())
            {
                if (!operand)
                {
                    continue;
                }
                if (!resolveLValueTargets(*operand, targets, composite))
                {
                    return false;
                }
            }
            return true;
        }
        if (const auto* stream = expr.as_if<slang::ast::StreamingConcatenationExpression>())
        {
            composite.isComposite = true;
            if (stream->getSliceSize() != 0)
            {
                reportUnsupported(expr, "Right-to-left streaming LHS is unsupported");
                return false;
            }
            for (const auto& element : stream->streams())
            {
                if (element.withExpr)
                {
                    reportUnsupported(*element.withExpr,
                                      "Streaming LHS with with-clause is unsupported");
                    return false;
                }
                if (!resolveLValueTargets(*element.operand, targets, composite))
                {
                    return false;
                }
            }
            return true;
        }

        LValueTarget target;
        target.target = resolveLValueSymbol(expr, target.slices);
        if (!target.target.valid())
        {
            return false;
        }
        target.width = computeExprWidth(expr);
        if (composite.isComposite && target.width == 0)
        {
            reportUnsupported(expr, "Unsupported LHS width in assignment");
            return false;
        }
        target.location = expr.sourceRange.start();
        targets.push_back(std::move(target));
        return true;
    }

    PlanSymbolId resolveLValueSymbol(const slang::ast::Expression& expr,
                                     std::vector<WriteSlice>& slices)
    {
        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            return plan.symbolTable.lookup(named->symbol.name);
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            return plan.symbolTable.lookup(hier->symbol.name);
        }
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            if (conversion->isImplicit())
            {
                return resolveLValueSymbol(conversion->operand(), slices);
            }
            return {};
        }
        if (const auto* member = expr.as_if<slang::ast::MemberAccessExpression>())
        {
            PlanSymbolId base = resolveLValueSymbol(member->value(), slices);
            if (!base.valid())
            {
                return {};
            }
            if (member->member.name.empty())
            {
                return {};
            }
            WriteSlice slice;
            slice.kind = WriteSliceKind::MemberSelect;
            slice.member = plan.symbolTable.intern(member->member.name);
            slice.location = member->sourceRange.start();
            slices.push_back(std::move(slice));
            return base;
        }
        if (const auto* select = expr.as_if<slang::ast::ElementSelectExpression>())
        {
            PlanSymbolId base = resolveLValueSymbol(select->value(), slices);
            if (!base.valid())
            {
                return {};
            }
            ExprNodeId index = lowerExpression(select->selector());
            if (index == kInvalidPlanIndex)
            {
                return {};
            }
            WriteSlice slice;
            slice.kind = WriteSliceKind::BitSelect;
            slice.index = index;
            slice.location = select->sourceRange.start();
            slices.push_back(std::move(slice));
            return base;
        }
        if (const auto* range = expr.as_if<slang::ast::RangeSelectExpression>())
        {
            PlanSymbolId base = resolveLValueSymbol(range->value(), slices);
            if (!base.valid())
            {
                return {};
            }
            ExprNodeId left = lowerExpression(range->left());
            ExprNodeId right = lowerExpression(range->right());
            if (left == kInvalidPlanIndex || right == kInvalidPlanIndex)
            {
                return {};
            }
            WriteSlice slice;
            slice.kind = WriteSliceKind::RangeSelect;
            slice.rangeKind = mapRangeKind(range->getSelectionKind());
            slice.left = left;
            slice.right = right;
            slice.location = range->sourceRange.start();
            slices.push_back(std::move(slice));
            return base;
        }
        return {};
    }

    void reportUnsupported(const slang::ast::Expression& expr, std::string_view message)
    {
        if (diagnostics)
        {
            diagnostics->todo(expr.sourceRange.start(), std::string(message));
        }
    }

    void reportUnsupported(const slang::ast::Statement& stmt, std::string_view message)
    {
        if (diagnostics)
        {
            diagnostics->todo(stmt.sourceRange.start(), std::string(message));
        }
    }

    void reportError(const slang::ast::Statement& stmt, std::string_view message)
    {
        if (diagnostics)
        {
            diagnostics->error(stmt.sourceRange.start(), std::string(message));
        }
    }

    void reportError(const slang::ast::Expression& expr, std::string_view message)
    {
        if (diagnostics)
        {
            diagnostics->error(expr.sourceRange.start(), std::string(message));
        }
    }

    void reportLoopControlError(const slang::ast::Statement& stmt, std::string_view header)
    {
        std::string message(header);
        if (loopControlFailure)
        {
            message.append(": ");
            message.append(*loopControlFailure);
        }
        reportError(stmt, message);
    }

    void reportLoopFailure(const slang::ast::Statement& stmt, std::string_view header)
    {
        std::string message(header);
        if (loopControlFailure)
        {
            message.append(": ");
            message.append(*loopControlFailure);
        }
        reportError(stmt, message);
    }

    void clearLoopControlFailure()
    {
        loopControlFailure.reset();
    }

    void setLoopControlFailure(std::string message)
    {
        if (!loopControlFailure)
        {
            loopControlFailure = std::move(message);
        }
    }

};

void lowerStmtGenerateBlock(const slang::ast::GenerateBlockSymbol& block,
                            StmtLowererState& state);
void lowerStmtGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                                 StmtLowererState& state);

void lowerStmtProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                              StmtLowererState& state)
{
    const ControlDomain saved = state.domain;
    const StmtLowererState::EventContext savedEvent = state.eventContext;
    state.domain = classifyProceduralBlock(block);
    state.eventContext = state.buildEventContext(block);
    state.visitStatement(block.getBody());
    state.domain = saved;
    state.eventContext = savedEvent;
}

void lowerStmtContinuousAssign(const slang::ast::ContinuousAssignSymbol& assign,
                               StmtLowererState& state)
{
    const ControlDomain saved = state.domain;
    state.domain = ControlDomain::Combinational;
    state.scanExpression(assign.getAssignment());
    state.domain = saved;
}

void lowerStmtMemberSymbol(const slang::ast::Symbol& member, StmtLowererState& state)
{
    if (const auto* continuous = member.as_if<slang::ast::ContinuousAssignSymbol>())
    {
        lowerStmtContinuousAssign(*continuous, state);
        return;
    }
    if (const auto* block = member.as_if<slang::ast::ProceduralBlockSymbol>())
    {
        lowerStmtProceduralBlock(*block, state);
        return;
    }
    if (const auto* generateBlock = member.as_if<slang::ast::GenerateBlockSymbol>())
    {
        lowerStmtGenerateBlock(*generateBlock, state);
        return;
    }
    if (const auto* generateArray = member.as_if<slang::ast::GenerateBlockArraySymbol>())
    {
        lowerStmtGenerateBlockArray(*generateArray, state);
        return;
    }
}

void lowerStmtGenerateBlock(const slang::ast::GenerateBlockSymbol& block, StmtLowererState& state)
{
    if (block.isUninstantiated)
    {
        return;
    }
    for (const slang::ast::Symbol& member : block.members())
    {
        lowerStmtMemberSymbol(member, state);
    }
}

void lowerStmtGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                                 StmtLowererState& state)
{
    for (const slang::ast::GenerateBlockSymbol* entry : array.entries)
    {
        if (!entry)
        {
            continue;
        }
        lowerStmtGenerateBlock(*entry, state);
    }
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
            info.symbol = plan.symbolTable.intern(port->name);
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
                info.inoutSymbol = PortInfo::InoutBinding{
                    plan.symbolTable.intern(base + "__in"),
                    plan.symbolTable.intern(base + "__out"),
                    plan.symbolTable.intern(base + "__oe")};
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

void collectParameters(const slang::ast::InstanceBodySymbol& body, ModulePlan& plan)
{
    for (const slang::ast::ParameterSymbolBase* paramBase : body.getParameters())
    {
        if (!paramBase || paramBase->symbol.name.empty())
        {
            continue;
        }
        plan.symbolTable.intern(paramBase->symbol.name);
    }

    for (const slang::ast::Symbol& member : body.members())
    {
        if (const auto* param = member.as_if<slang::ast::ParameterSymbol>())
        {
            if (!param->name.empty())
            {
                plan.symbolTable.intern(param->name);
            }
            continue;
        }
        if (const auto* typeParam = member.as_if<slang::ast::TypeParameterSymbol>())
        {
            if (!typeParam->name.empty())
            {
                plan.symbolTable.intern(typeParam->name);
            }
        }
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
            info.symbol = plan.symbolTable.intern(net->name);
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
            if (variable->getType().isEvent())
            {
                if (diagnostics)
                {
                    diagnostics->warn(*variable, "Skipping event variable symbol");
                }
                continue;
            }
            SignalInfo info;
            info.symbol = plan.symbolTable.intern(variable->name);
            info.kind = SignalKind::Variable;
            plan.signals.push_back(std::move(info));
            continue;
        }
    }
}

void enqueuePlanKey(ConvertContext& context, const slang::ast::InstanceBodySymbol& body,
                    std::string paramSignature = {})
{
    if (!context.planQueue)
    {
        return;
    }
    PlanKey key;
    key.body = &body;
    key.paramSignature = std::move(paramSignature);
    context.planQueue->push(std::move(key));
}

void collectInstance(const slang::ast::InstanceSymbol& instance, ModulePlan& plan,
                     ConvertContext& context)
{
    const slang::ast::InstanceBodySymbol& body = instance.body;

    InstanceInfo info;
    info.instance = &instance;
    info.isBlackbox = isBlackboxBody(body, context.diagnostics);
    std::string_view instanceName = instance.name;
    if (instanceName.empty())
    {
        instanceName = instance.getArrayName();
    }
    info.instanceSymbol = plan.symbolTable.intern(instanceName);

    std::string_view moduleName = body.getDefinition().name;
    if (moduleName.empty())
    {
        moduleName = instance.name;
    }
    info.moduleSymbol = plan.symbolTable.intern(moduleName);
    ParameterSnapshot params = snapshotParameters(body, info.isBlackbox ? &plan : nullptr);
    if (info.isBlackbox)
    {
        info.parameters = std::move(params.parameters);
    }
    info.paramSignature = params.signature;
    plan.instances.push_back(std::move(info));

    enqueuePlanKey(context, body, std::move(params.signature));
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
    plan.moduleSymbol = plan.symbolTable.intern(moduleName);
    collectParameters(body, plan);
    collectPorts(body, plan, context_.diagnostics);
    collectSignals(body, plan, context_.diagnostics);
    collectInstances(body, plan, context_);
    return plan;
}

void TypeResolverPass::resolve(ModulePlan& plan)
{
    if (!plan.body)
    {
        return;
    }

    std::vector<PortId> portBySymbol(plan.symbolTable.size(), kInvalidPlanIndex);
    for (PortId i = 0; i < static_cast<PortId>(plan.ports.size()); ++i)
    {
        const PlanSymbolId id = plan.ports[i].symbol;
        if (id.valid() && id.index < portBySymbol.size())
        {
            portBySymbol[id.index] = i;
        }
    }

    std::vector<SignalId> signalBySymbol(plan.symbolTable.size(), kInvalidPlanIndex);
    for (SignalId i = 0; i < static_cast<SignalId>(plan.signals.size()); ++i)
    {
        const PlanSymbolId id = plan.signals[i].symbol;
        if (id.valid() && id.index < signalBySymbol.size())
        {
            signalBySymbol[id.index] = i;
        }
    }

    for (const slang::ast::Symbol* portSymbol : plan.body->getPortList())
    {
        if (!portSymbol)
        {
            continue;
        }
        const auto* port = portSymbol->as_if<slang::ast::PortSymbol>();
        if (!port || port->name.empty())
        {
            continue;
        }
        const PlanSymbolId id = plan.symbolTable.lookup(port->name);
        if (!id.valid() || id.index >= portBySymbol.size())
        {
            continue;
        }
        const PortId index = portBySymbol[id.index];
        if (index == kInvalidPlanIndex)
        {
            continue;
        }
        TypeResolution info = analyzePortType(port->getType(), *port, context_.diagnostics);
        PortInfo& portInfo = plan.ports[index];
        portInfo.width = info.width;
        portInfo.isSigned = info.isSigned;
    }

    for (const slang::ast::Symbol& member : plan.body->members())
    {
        const auto* net = member.as_if<slang::ast::NetSymbol>();
        const auto* variable = member.as_if<slang::ast::VariableSymbol>();
        const slang::ast::ValueSymbol* valueSymbol =
            net ? static_cast<const slang::ast::ValueSymbol*>(net)
                : static_cast<const slang::ast::ValueSymbol*>(variable);
        if (!valueSymbol || valueSymbol->name.empty())
        {
            continue;
        }

        const PlanSymbolId id = plan.symbolTable.lookup(valueSymbol->name);
        if (!id.valid() || id.index >= signalBySymbol.size())
        {
            continue;
        }
        const SignalId index = signalBySymbol[id.index];
        if (index == kInvalidPlanIndex)
        {
            continue;
        }
        TypeResolution info =
            analyzeSignalType(valueSymbol->getType(), *valueSymbol, context_.diagnostics);
        SignalInfo& signal = plan.signals[index];
        signal.width = info.width;
        signal.isSigned = info.isSigned;
        signal.memoryRows = info.memoryRows;
        signal.packedDims = std::move(info.packedDims);
        signal.unpackedDims = std::move(info.unpackedDims);
    }
}

void RWAnalyzerPass::analyze(ModulePlan& plan)
{
    if (!plan.body)
    {
        return;
    }

    plan.rwOps.clear();
    plan.memPorts.clear();

    RWAnalyzerState state(plan, context_.diagnostics);
    for (const slang::ast::Symbol& member : plan.body->members())
    {
        analyzeMemberSymbol(member, state);
    }
}

LoweringPlan ExprLowererPass::lower(ModulePlan& plan)
{
    if (!plan.body)
    {
        return {};
    }

    ExprLowererState state(plan, context_.diagnostics);
    for (const slang::ast::Symbol& member : plan.body->members())
    {
        lowerMemberSymbol(member, state);
    }
    return std::move(state.lowering);
}

void StmtLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering)
{
    if (!plan.body)
    {
        return;
    }

    lowering.writes.clear();
    lowering.loweredStmts.clear();
    lowering.dpiImports.clear();

    StmtLowererState state(plan, context_.diagnostics, lowering,
                           context_.options.maxLoopIterations);
    for (const slang::ast::Symbol& member : plan.body->members())
    {
        lowerStmtMemberSymbol(member, state);
    }
}

namespace {

class WriteBackBuilder {
public:
    WriteBackBuilder(ModulePlan& plan, LoweringPlan& lowering)
        : plan_(plan), lowering_(lowering)
    {
        nextTemp_ = static_cast<uint32_t>(lowering_.tempSymbols.size());
    }

    ExprNodeId ensureGuardExpr(ExprNodeId guard, slang::SourceLocation location)
    {
        if (guard != kInvalidPlanIndex)
        {
            return guard;
        }
        if (constOne_.has_value())
        {
            return *constOne_;
        }
        ExprNodeId id = addConstantLiteral("1'b1", location);
        constOne_ = id;
        return id;
    }

    ExprNodeId makeLogicOr(ExprNodeId lhs, ExprNodeId rhs, slang::SourceLocation location)
    {
        if (lhs == kInvalidPlanIndex)
        {
            return rhs;
        }
        if (rhs == kInvalidPlanIndex)
        {
            return lhs;
        }
        return makeOperation(grh::ir::OperationKind::kLogicOr, {lhs, rhs}, location);
    }

    ExprNodeId makeMux(ExprNodeId cond, ExprNodeId lhs, ExprNodeId rhs,
                       slang::SourceLocation location)
    {
        if (cond == kInvalidPlanIndex || lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        return makeOperation(grh::ir::OperationKind::kMux, {cond, lhs, rhs}, location);
    }

    ExprNodeId addSymbol(PlanSymbolId symbol, slang::SourceLocation location)
    {
        if (!symbol.valid())
        {
            return kInvalidPlanIndex;
        }
        ExprNode node;
        node.kind = ExprNodeKind::Symbol;
        node.symbol = symbol;
        node.location = location;
        return addNode(std::move(node));
    }

    ExprNodeId addConstantLiteral(std::string literal, slang::SourceLocation location)
    {
        ExprNode node;
        node.kind = ExprNodeKind::Constant;
        node.literal = std::move(literal);
        node.location = location;
        return addNode(std::move(node));
    }

private:
    PlanSymbolId makeTempSymbol()
    {
        const std::string name = "_expr_tmp_" + std::to_string(nextTemp_++);
        PlanSymbolId id = plan_.symbolTable.intern(name);
        lowering_.tempSymbols.push_back(id);
        return id;
    }

    ExprNodeId addNode(ExprNode node)
    {
        const ExprNodeId id = static_cast<ExprNodeId>(lowering_.values.size());
        lowering_.values.push_back(std::move(node));
        return id;
    }

    ExprNodeId makeOperation(grh::ir::OperationKind op, std::vector<ExprNodeId> operands,
                             slang::SourceLocation location)
    {
        for (ExprNodeId operand : operands)
        {
            if (operand == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
        }
        ExprNode node;
        node.kind = ExprNodeKind::Operation;
        node.op = op;
        node.operands = std::move(operands);
        node.location = location;
        node.tempSymbol = makeTempSymbol();
        return addNode(std::move(node));
    }

    ModulePlan& plan_;
    LoweringPlan& lowering_;
    uint32_t nextTemp_ = 0;
    std::optional<ExprNodeId> constOne_;
};

struct WriteBackGroup {
    PlanSymbolId target;
    ControlDomain domain = ControlDomain::Unknown;
    std::vector<EventEdge> eventEdges;
    std::vector<ExprNodeId> eventOperands;
    std::vector<const LoweredStmt*> writes;
};

bool matchWriteGroup(const WriteBackGroup& group, PlanSymbolId target, ControlDomain domain,
                     const std::vector<EventEdge>& edges,
                     const std::vector<ExprNodeId>& operands)
{
    return group.target.index == target.index && group.domain == domain &&
           group.eventEdges == edges && group.eventOperands == operands;
}

} // namespace

WriteBackPlan WriteBackPass::lower(ModulePlan& plan, LoweringPlan& lowering)
{
    WriteBackPlan result;
    if (!plan.body)
    {
        return result;
    }

    std::vector<SignalId> signalBySymbol(plan.symbolTable.size(), kInvalidPlanIndex);
    for (SignalId i = 0; i < static_cast<SignalId>(plan.signals.size()); ++i)
    {
        const PlanSymbolId id = plan.signals[i].symbol;
        if (id.valid() && id.index < signalBySymbol.size())
        {
            signalBySymbol[id.index] = i;
        }
    }

    std::vector<WriteBackGroup> groups;
    groups.reserve(lowering.loweredStmts.size());

    for (const auto& stmt : lowering.loweredStmts)
    {
        if (stmt.kind != LoweredStmtKind::Write)
        {
            continue;
        }
        const WriteIntent& write = stmt.write;
        if (!write.target.valid())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location, "Write-back target missing symbol");
            }
            continue;
        }
        SignalId signalId = kInvalidPlanIndex;
        if (write.target.index < signalBySymbol.size())
        {
            signalId = signalBySymbol[write.target.index];
        }
        if (signalId != kInvalidPlanIndex && plan.signals[signalId].memoryRows > 0)
        {
            continue;
        }
        if (!write.slices.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location,
                                           "Write-back merge with slices is unsupported");
            }
            continue;
        }
        if (write.value == kInvalidPlanIndex)
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location, "Write-back missing RHS expression");
            }
            continue;
        }
        if (stmt.eventEdges.size() != stmt.eventOperands.size())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(write.location,
                                           "Skipping write with mismatched event binding");
            }
            continue;
        }
        if (write.domain == ControlDomain::Sequential &&
            (stmt.eventEdges.empty() || stmt.eventOperands.empty()))
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(
                    write.location,
                    "Skipping sequential write without edge-sensitive timing control");
            }
            continue;
        }

        bool matched = false;
        for (auto& group : groups)
        {
            if (matchWriteGroup(group, write.target, write.domain,
                                stmt.eventEdges, stmt.eventOperands))
            {
                group.writes.push_back(&stmt);
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            WriteBackGroup group;
            group.target = write.target;
            group.domain = write.domain;
            group.eventEdges = stmt.eventEdges;
            group.eventOperands = stmt.eventOperands;
            group.writes.push_back(&stmt);
            groups.push_back(std::move(group));
        }
    }

    WriteBackBuilder builder(plan, lowering);
    result.entries.reserve(groups.size());

    for (const auto& group : groups)
    {
        if (group.writes.empty())
        {
            continue;
        }

        WriteBackPlan::Entry entry;
        entry.target = group.target;
        if (entry.target.valid() && entry.target.index < signalBySymbol.size())
        {
            entry.signal = signalBySymbol[entry.target.index];
        }
        entry.domain = group.domain;
        entry.eventEdges = group.eventEdges;
        entry.eventOperands = group.eventOperands;
        entry.location = group.writes.front()->location;

        ExprNodeId updateCond = kInvalidPlanIndex;
        ExprNodeId baseValue = builder.addSymbol(group.target, entry.location);
        ExprNodeId nextValue = baseValue;

        for (const LoweredStmt* stmt : group.writes)
        {
            const WriteIntent& write = stmt->write;
            ExprNodeId guard = builder.ensureGuardExpr(write.guard, write.location);
            updateCond = builder.makeLogicOr(updateCond, guard, write.location);
            nextValue = builder.makeMux(guard, write.value, nextValue, write.location);
        }

        if (updateCond == kInvalidPlanIndex || nextValue == kInvalidPlanIndex)
        {
            continue;
        }

        entry.updateCond = updateCond;
        entry.nextValue = nextValue;
        result.entries.push_back(std::move(entry));
    }

    return result;
}

namespace {

class MemoryPortBuilder {
public:
    MemoryPortBuilder(ModulePlan& plan, LoweringPlan& lowering)
        : plan_(plan), lowering_(lowering)
    {
        nextTemp_ = static_cast<uint32_t>(lowering_.tempSymbols.size());
    }

    ExprNodeId ensureGuardExpr(ExprNodeId guard, slang::SourceLocation location)
    {
        if (guard != kInvalidPlanIndex)
        {
            return guard;
        }
        if (constOne_.has_value())
        {
            return *constOne_;
        }
        ExprNodeId id = addConstantLiteral("1'b1", location);
        constOne_ = id;
        return id;
    }

    ExprNodeId addConstantLiteral(std::string literal, slang::SourceLocation location)
    {
        ExprNode node;
        node.kind = ExprNodeKind::Constant;
        node.literal = std::move(literal);
        node.location = location;
        return addNode(std::move(node));
    }

    ExprNodeId makeOperation(grh::ir::OperationKind op, std::vector<ExprNodeId> operands,
                             slang::SourceLocation location)
    {
        for (ExprNodeId operand : operands)
        {
            if (operand == kInvalidPlanIndex)
            {
                return kInvalidPlanIndex;
            }
        }
        ExprNode node;
        node.kind = ExprNodeKind::Operation;
        node.op = op;
        node.operands = std::move(operands);
        node.location = location;
        node.tempSymbol = makeTempSymbol();
        return addNode(std::move(node));
    }

private:
    PlanSymbolId makeTempSymbol()
    {
        const std::string name = "_expr_tmp_" + std::to_string(nextTemp_++);
        PlanSymbolId id = plan_.symbolTable.intern(name);
        lowering_.tempSymbols.push_back(id);
        return id;
    }

    ExprNodeId addNode(ExprNode node)
    {
        const ExprNodeId id = static_cast<ExprNodeId>(lowering_.values.size());
        lowering_.values.push_back(std::move(node));
        return id;
    }

    ModulePlan& plan_;
    LoweringPlan& lowering_;
    uint32_t nextTemp_ = 0;
    std::optional<ExprNodeId> constOne_;
};

std::optional<int64_t> evalConstInt(const ModulePlan& plan, const LoweringPlan& lowering,
                                    ExprNodeId id)
{
    std::unordered_set<ExprNodeId> visited;
    std::unordered_set<ExprNodeId> visitedSv;
    auto evalParamValue = [](const slang::ast::ParameterSymbol& param)
        -> std::optional<slang::SVInt> {
        slang::ConstantValue value = param.getValue();
        if (value.bad())
        {
            return std::nullopt;
        }
        if (!value.isInteger())
        {
            value = value.convertToInt();
        }
        if (!value.isInteger())
        {
            return std::nullopt;
        }
        const slang::SVInt& literal = value.integer();
        if (literal.hasUnknown())
        {
            return std::nullopt;
        }
        return literal;
    };
    auto lookupParamValue = [&](std::string_view name) -> std::optional<slang::SVInt> {
        if (!plan.body)
        {
            return std::nullopt;
        }
        for (const slang::ast::ParameterSymbolBase* paramBase :
             plan.body->getParameters())
        {
            if (!paramBase || paramBase->symbol.name != name)
            {
                continue;
            }
            const auto* valueParam =
                paramBase->symbol.as_if<slang::ast::ParameterSymbol>();
            if (!valueParam)
            {
                return std::nullopt;
            }
            return evalParamValue(*valueParam);
        }
        if (const slang::ast::Symbol* symbol = plan.body->find(name))
        {
            if (const auto* param = symbol->as_if<slang::ast::ParameterSymbol>())
            {
                return evalParamValue(*param);
            }
        }
        const slang::ast::RootSymbol& root = plan.body->getCompilation().getRoot();
        const slang::ast::ParameterSymbol* matched = nullptr;
        auto checkPackage = [&](const slang::ast::PackageSymbol& package) -> bool {
            const slang::ast::Symbol* symbol = package.findForImport(name);
            if (!symbol)
            {
                return true;
            }
            const auto* param = symbol->as_if<slang::ast::ParameterSymbol>();
            if (!param)
            {
                return false;
            }
            if (matched && matched != param)
            {
                return false;
            }
            matched = param;
            return true;
        };
        for (const auto& package : root.membersOfType<slang::ast::PackageSymbol>())
        {
            if (!checkPackage(package))
            {
                return std::nullopt;
            }
        }
        for (const auto& unit : root.membersOfType<slang::ast::CompilationUnitSymbol>())
        {
            for (const auto& package : unit.membersOfType<slang::ast::PackageSymbol>())
            {
                if (!checkPackage(package))
                {
                    return std::nullopt;
                }
            }
        }
        if (matched)
        {
            return evalParamValue(*matched);
        }
        return std::nullopt;
    };
    auto evalConstSVInt = [&](auto&& self, ExprNodeId nodeId)
        -> std::optional<slang::SVInt> {
        if (nodeId == kInvalidPlanIndex || nodeId >= lowering.values.size())
        {
            return std::nullopt;
        }
        if (!visitedSv.insert(nodeId).second)
        {
            return std::nullopt;
        }
        const ExprNode& node = lowering.values[nodeId];
        if (node.kind == ExprNodeKind::Constant)
        {
            slang::SVInt literal = slang::SVInt::fromString(node.literal);
            if (literal.hasUnknown())
            {
                return std::nullopt;
            }
            if (node.widthHint > 0 &&
                static_cast<uint64_t>(node.widthHint) != literal.getBitWidth())
            {
                literal = literal.resize(static_cast<slang::bitwidth_t>(node.widthHint));
            }
            return literal;
        }
        if (node.kind == ExprNodeKind::Symbol)
        {
            if (!node.symbol.valid())
            {
                return std::nullopt;
            }
            auto value = lookupParamValue(plan.symbolTable.text(node.symbol));
            if (value && node.widthHint > 0 &&
                static_cast<uint64_t>(node.widthHint) != value->getBitWidth())
            {
                *value = value->resize(static_cast<slang::bitwidth_t>(node.widthHint));
            }
            return value;
        }
        if (node.kind != ExprNodeKind::Operation)
        {
            return std::nullopt;
        }
        if (node.op == grh::ir::OperationKind::kMux && node.operands.size() == 3)
        {
            auto cond = self(self, node.operands[0]);
            if (!cond)
            {
                return std::nullopt;
            }
            slang::logic_t condBit = cond->reductionOr();
            if (condBit.isUnknown())
            {
                return std::nullopt;
            }
            ExprNodeId branch = static_cast<bool>(condBit) ? node.operands[1] : node.operands[2];
            return self(self, branch);
        }
        if (node.op == grh::ir::OperationKind::kConcat)
        {
            std::vector<slang::SVInt> operands;
            operands.reserve(node.operands.size());
            for (ExprNodeId operand : node.operands)
            {
                auto value = self(self, operand);
                if (!value)
                {
                    return std::nullopt;
                }
                operands.push_back(*value);
            }
            if (operands.empty())
            {
                return std::nullopt;
            }
            slang::SVInt result = slang::SVInt::concat(operands);
            if (node.widthHint > 0 &&
                static_cast<uint64_t>(node.widthHint) != result.getBitWidth())
            {
                result = result.resize(static_cast<slang::bitwidth_t>(node.widthHint));
            }
            return result;
        }
        if (node.op == grh::ir::OperationKind::kReplicate &&
            node.operands.size() >= 2)
        {
            auto countValue = self(self, node.operands[0]);
            auto dataValue = self(self, node.operands[1]);
            if (!countValue || !dataValue)
            {
                return std::nullopt;
            }
            auto countInt = countValue->template as<int64_t>();
            if (!countInt || *countInt < 0)
            {
                return std::nullopt;
            }
            slang::SVInt result = dataValue->replicate(*countValue);
            if (node.widthHint > 0 &&
                static_cast<uint64_t>(node.widthHint) != result.getBitWidth())
            {
                result = result.resize(static_cast<slang::bitwidth_t>(node.widthHint));
            }
            return result;
        }
        if (node.operands.size() == 1)
        {
            auto operand = self(self, node.operands.front());
            if (!operand)
            {
                return std::nullopt;
            }
            switch (node.op)
            {
            case grh::ir::OperationKind::kNot:
                return ~(*operand);
            case grh::ir::OperationKind::kLogicNot:
            {
                slang::logic_t bit = operand->reductionOr();
                if (bit.isUnknown())
                {
                    return std::nullopt;
                }
                return slang::SVInt(1, static_cast<uint64_t>(!static_cast<bool>(bit)),
                                    false);
            }
            default:
                break;
            }
            return std::nullopt;
        }
        if (node.operands.size() != 2)
        {
            return std::nullopt;
        }
        auto lhs = self(self, node.operands[0]);
        auto rhs = self(self, node.operands[1]);
        if (!lhs || !rhs)
        {
            return std::nullopt;
        }
        auto applyWidthHint = [&](slang::SVInt value) -> slang::SVInt {
            if (node.widthHint > 0 &&
                static_cast<uint64_t>(node.widthHint) != value.getBitWidth())
            {
                return value.resize(static_cast<slang::bitwidth_t>(node.widthHint));
            }
            return value;
        };
        switch (node.op)
        {
        case grh::ir::OperationKind::kAdd:
            return applyWidthHint(*lhs + *rhs);
        case grh::ir::OperationKind::kSub:
            return applyWidthHint(*lhs - *rhs);
        case grh::ir::OperationKind::kMul:
            return applyWidthHint(*lhs * *rhs);
        case grh::ir::OperationKind::kDiv:
            return applyWidthHint(*lhs / *rhs);
        case grh::ir::OperationKind::kMod:
            return applyWidthHint(*lhs % *rhs);
        case grh::ir::OperationKind::kAnd:
            return applyWidthHint(*lhs & *rhs);
        case grh::ir::OperationKind::kOr:
            return applyWidthHint(*lhs | *rhs);
        case grh::ir::OperationKind::kXor:
            return applyWidthHint(*lhs ^ *rhs);
        case grh::ir::OperationKind::kXnor:
            return applyWidthHint(lhs->xnor(*rhs));
        case grh::ir::OperationKind::kShl:
            return applyWidthHint(lhs->shl(*rhs));
        case grh::ir::OperationKind::kLShr:
            return applyWidthHint(lhs->lshr(*rhs));
        case grh::ir::OperationKind::kAShr:
            return applyWidthHint(lhs->ashr(*rhs));
        case grh::ir::OperationKind::kLogicAnd:
        {
            slang::logic_t bit = lhs->reductionOr() && rhs->reductionOr();
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        case grh::ir::OperationKind::kLogicOr:
        {
            slang::logic_t bit = lhs->reductionOr() || rhs->reductionOr();
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        case grh::ir::OperationKind::kEq:
        case grh::ir::OperationKind::kCaseEq:
        {
            slang::logic_t bit = *lhs == *rhs;
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        case grh::ir::OperationKind::kNe:
        case grh::ir::OperationKind::kCaseNe:
        {
            slang::logic_t bit = *lhs != *rhs;
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        case grh::ir::OperationKind::kLt:
        {
            slang::logic_t bit = *lhs < *rhs;
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        case grh::ir::OperationKind::kLe:
        {
            slang::logic_t bit = *lhs <= *rhs;
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        case grh::ir::OperationKind::kGt:
        {
            slang::logic_t bit = *lhs > *rhs;
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        case grh::ir::OperationKind::kGe:
        {
            slang::logic_t bit = *lhs >= *rhs;
            if (bit.isUnknown())
            {
                return std::nullopt;
            }
            return applyWidthHint(
                slang::SVInt(1, static_cast<uint64_t>(static_cast<bool>(bit)), false));
        }
        default:
            break;
        }
        return std::nullopt;
    };
    auto evalNode = [&](auto&& self, ExprNodeId nodeId) -> std::optional<int64_t> {
        if (nodeId == kInvalidPlanIndex || nodeId >= lowering.values.size())
        {
            return std::nullopt;
        }
        if (!visited.insert(nodeId).second)
        {
            return std::nullopt;
        }
        const ExprNode& node = lowering.values[nodeId];
        if (node.kind == ExprNodeKind::Constant)
        {
            slang::SVInt literal = slang::SVInt::fromString(node.literal);
            if (literal.hasUnknown())
            {
                return std::nullopt;
            }
            return literal.as<int64_t>();
        }
        if (node.kind == ExprNodeKind::Symbol)
        {
            if (!node.symbol.valid() || !plan.body)
            {
                return std::nullopt;
            }
            const std::string_view name = plan.symbolTable.text(node.symbol);
            auto value = lookupParamValue(name);
            if (value)
            {
                return value->as<int64_t>();
            }
            return std::nullopt;
        }
        if (node.kind != ExprNodeKind::Operation)
        {
            return std::nullopt;
        }
        if (node.op == grh::ir::OperationKind::kMux && node.operands.size() == 3)
        {
            auto cond = self(self, node.operands[0]);
            if (!cond)
            {
                return std::nullopt;
            }
            ExprNodeId branch = *cond != 0 ? node.operands[1] : node.operands[2];
            return self(self, branch);
        }
        if (node.op == grh::ir::OperationKind::kConcat ||
            node.op == grh::ir::OperationKind::kReplicate)
        {
            auto value = evalConstSVInt(evalConstSVInt, nodeId);
            if (!value || value->hasUnknown())
            {
                return std::nullopt;
            }
            return value->as<int64_t>();
        }
        if (node.operands.empty())
        {
            return std::nullopt;
        }
        if (node.operands.size() == 1)
        {
            auto operand = self(self, node.operands.front());
            if (!operand)
            {
                return std::nullopt;
            }
            switch (node.op)
            {
            case grh::ir::OperationKind::kNot:
                return ~(*operand);
            case grh::ir::OperationKind::kLogicNot:
                return *operand == 0 ? 1 : 0;
            default:
                return std::nullopt;
            }
        }
        if (node.operands.size() != 2)
        {
            return std::nullopt;
        }
        auto lhs = self(self, node.operands[0]);
        if (!lhs)
        {
            return std::nullopt;
        }
        auto rhs = self(self, node.operands[1]);
        if (!rhs)
        {
            return std::nullopt;
        }
        switch (node.op)
        {
        case grh::ir::OperationKind::kAdd:
            return *lhs + *rhs;
        case grh::ir::OperationKind::kSub:
            return *lhs - *rhs;
        case grh::ir::OperationKind::kMul:
            return *lhs * *rhs;
        case grh::ir::OperationKind::kDiv:
            return *rhs == 0 ? std::nullopt : std::optional<int64_t>(*lhs / *rhs);
        case grh::ir::OperationKind::kMod:
            return *rhs == 0 ? std::nullopt : std::optional<int64_t>(*lhs % *rhs);
        case grh::ir::OperationKind::kAnd:
            return *lhs & *rhs;
        case grh::ir::OperationKind::kOr:
            return *lhs | *rhs;
        case grh::ir::OperationKind::kXor:
            return *lhs ^ *rhs;
        case grh::ir::OperationKind::kXnor:
            return ~(*lhs ^ *rhs);
        case grh::ir::OperationKind::kLogicAnd:
            return (*lhs != 0 && *rhs != 0) ? 1 : 0;
        case grh::ir::OperationKind::kLogicOr:
            return (*lhs != 0 || *rhs != 0) ? 1 : 0;
        case grh::ir::OperationKind::kEq:
        case grh::ir::OperationKind::kCaseEq:
            return *lhs == *rhs ? 1 : 0;
        case grh::ir::OperationKind::kNe:
        case grh::ir::OperationKind::kCaseNe:
            return *lhs != *rhs ? 1 : 0;
        case grh::ir::OperationKind::kLt:
            return *lhs < *rhs ? 1 : 0;
        case grh::ir::OperationKind::kLe:
            return *lhs <= *rhs ? 1 : 0;
        case grh::ir::OperationKind::kGt:
            return *lhs > *rhs ? 1 : 0;
        case grh::ir::OperationKind::kGe:
            return *lhs >= *rhs ? 1 : 0;
        case grh::ir::OperationKind::kShl:
        {
            if (*rhs < 0 || *rhs >= 63)
            {
                return std::nullopt;
            }
            return *lhs << *rhs;
        }
        case grh::ir::OperationKind::kLShr:
        {
            if (*rhs < 0 || *rhs >= 63)
            {
                return std::nullopt;
            }
            return static_cast<int64_t>(static_cast<uint64_t>(*lhs) >> *rhs);
        }
        case grh::ir::OperationKind::kAShr:
        {
            if (*rhs < 0 || *rhs >= 63)
            {
                return std::nullopt;
            }
            return *lhs >> *rhs;
        }
        default:
            break;
        }
        return std::nullopt;
    };
    return evalNode(evalNode, id);
}

std::optional<std::string> makeMaskLiteral(int64_t width, int64_t lo, int64_t hi)
{
    if (width <= 0 || lo < 0 || hi < lo || hi >= width)
    {
        return std::nullopt;
    }
    std::string bits(static_cast<std::size_t>(width), '0');
    for (int64_t i = lo; i <= hi; ++i)
    {
        const std::size_t index = static_cast<std::size_t>(width - 1 - i);
        bits[index] = '1';
    }
    std::string literal;
    literal.reserve(static_cast<std::size_t>(width) + 8);
    literal.append(std::to_string(width));
    literal.append("'b");
    literal.append(bits);
    return literal;
}

struct MemoryReadUse {
    PlanSymbolId memory;
    SignalId signal = kInvalidPlanIndex;
    ExprNodeId data = kInvalidPlanIndex;
    ControlDomain domain = ControlDomain::Unknown;
    ExprNodeId updateCond = kInvalidPlanIndex;
    std::vector<ExprNodeId> addressIndices;
    std::vector<EventEdge> eventEdges;
    std::vector<ExprNodeId> eventOperands;
    slang::SourceLocation location{};
};

} // namespace

void MemoryPortLowererPass::lower(ModulePlan& plan, LoweringPlan& lowering)
{
    if (!plan.body)
    {
        return;
    }

    lowering.memoryReads.clear();
    lowering.memoryWrites.clear();

    std::vector<SignalId> signalBySymbol(plan.symbolTable.size(), kInvalidPlanIndex);
    for (SignalId i = 0; i < static_cast<SignalId>(plan.signals.size()); ++i)
    {
        const PlanSymbolId id = plan.signals[i].symbol;
        if (id.valid() && id.index < signalBySymbol.size())
        {
            signalBySymbol[id.index] = i;
        }
    }

    auto resolveMemorySignal = [&](PlanSymbolId symbol) -> SignalId {
        if (!symbol.valid() || symbol.index >= signalBySymbol.size())
        {
            return kInvalidPlanIndex;
        }
        SignalId id = signalBySymbol[symbol.index];
        if (id == kInvalidPlanIndex)
        {
            return kInvalidPlanIndex;
        }
        if (plan.signals[id].memoryRows <= 0)
        {
            return kInvalidPlanIndex;
        }
        return id;
    };

    auto getMemoryReadCandidate = [&](ExprNodeId id, MemoryReadUse& out) -> bool {
        if (id == kInvalidPlanIndex || id >= lowering.values.size())
        {
            return false;
        }
        ExprNodeId current = id;
        std::vector<ExprNodeId> indices;
        while (current != kInvalidPlanIndex && current < lowering.values.size())
        {
            const ExprNode& node = lowering.values[current];
            if (node.kind != ExprNodeKind::Operation ||
                node.op != grh::ir::OperationKind::kSliceDynamic ||
                node.operands.size() < 2)
            {
                break;
            }
            if (node.operands.size() == 2)
            {
                indices.push_back(node.operands[1]);
            }
            current = node.operands[0];
        }
        if (current == kInvalidPlanIndex || current >= lowering.values.size())
        {
            return false;
        }
        const ExprNode& baseNode = lowering.values[current];
        if (baseNode.kind != ExprNodeKind::Symbol)
        {
            return false;
        }
        SignalId signal = resolveMemorySignal(baseNode.symbol);
        if (signal == kInvalidPlanIndex)
        {
            return false;
        }
        if (!indices.empty())
        {
            std::reverse(indices.begin(), indices.end());
        }
        out.memory = baseNode.symbol;
        out.signal = signal;
        out.data = id;
        out.addressIndices = std::move(indices);
        out.location = baseNode.location.valid() ? baseNode.location : lowering.values[id].location;
        return true;
    };

    std::vector<MemoryReadUse> readUses;
    auto recordReadUse = [&](const MemoryReadUse& candidate) {
        for (const auto& existing : readUses)
        {
            if (existing.memory.index == candidate.memory.index &&
                existing.domain == candidate.domain &&
                existing.updateCond == candidate.updateCond &&
                existing.addressIndices == candidate.addressIndices &&
                existing.eventEdges == candidate.eventEdges &&
                existing.eventOperands == candidate.eventOperands)
            {
                return;
            }
        }
        readUses.push_back(candidate);
    };

    auto visitExpr = [&](auto&& self, ExprNodeId id, ControlDomain domain,
                         const std::vector<EventEdge>& edges,
                         const std::vector<ExprNodeId>& operands,
                         ExprNodeId updateCond,
                         slang::SourceLocation location,
                         std::unordered_set<ExprNodeId>& visited) -> void {
        if (id == kInvalidPlanIndex || id >= lowering.values.size())
        {
            return;
        }
        if (!visited.insert(id).second)
        {
            return;
        }
        MemoryReadUse candidate;
        if (getMemoryReadCandidate(id, candidate))
        {
            candidate.domain = domain;
            candidate.updateCond = updateCond;
            candidate.eventEdges = edges;
            candidate.eventOperands = operands;
            candidate.location = location;
            recordReadUse(candidate);
        }
        const ExprNode& node = lowering.values[id];
        if (node.kind == ExprNodeKind::Operation)
        {
            for (ExprNodeId operand : node.operands)
            {
                self(self, operand, domain, edges, operands, updateCond, location, visited);
            }
        }
    };

    MemoryPortBuilder builder(plan, lowering);

    for (const auto& stmt : lowering.loweredStmts)
    {
        if (stmt.kind != LoweredStmtKind::Write)
        {
            continue;
        }
        std::unordered_set<ExprNodeId> visited;
        const ControlDomain domain = stmt.write.domain;
        ExprNodeId updateCond = kInvalidPlanIndex;
        if (domain == ControlDomain::Sequential)
        {
            updateCond = builder.ensureGuardExpr(stmt.write.guard, stmt.location);
        }
        if (stmt.write.value != kInvalidPlanIndex)
        {
            visitExpr(visitExpr, stmt.write.value, domain, stmt.eventEdges,
                      stmt.eventOperands, updateCond, stmt.location, visited);
        }
        if (stmt.write.guard != kInvalidPlanIndex)
        {
            visitExpr(visitExpr, stmt.write.guard, domain, stmt.eventEdges,
                      stmt.eventOperands, updateCond, stmt.location, visited);
        }
    }

    auto buildLinearAddress = [&](std::span<const ExprNodeId> indices,
                                  std::span<const UnpackedDimInfo> dims,
                                  slang::SourceLocation location) -> ExprNodeId {
        if (indices.size() < dims.size() || dims.empty())
        {
            return kInvalidPlanIndex;
        }
        ExprNodeId address = kInvalidPlanIndex;
        int64_t stride = 1;
        std::vector<int64_t> strides(dims.size(), 1);
        for (std::size_t i = dims.size(); i-- > 0;)
        {
            strides[i] = stride;
            const int32_t extent = dims[i].extent;
            if (extent > 0 && stride <= std::numeric_limits<int64_t>::max() / extent)
            {
                stride *= extent;
            }
        }
        for (std::size_t i = 0; i < dims.size(); ++i)
        {
            ExprNodeId term = indices[i];
            const UnpackedDimInfo& dim = dims[i];
            if (dim.left < dim.right)
            {
                if (dim.left != 0)
                {
                    ExprNodeId offset =
                        builder.addConstantLiteral(std::to_string(dim.left), location);
                    term = builder.makeOperation(grh::ir::OperationKind::kSub,
                                                 {term, offset}, location);
                }
            }
            else
            {
                ExprNodeId offset =
                    builder.addConstantLiteral(std::to_string(dim.left), location);
                term = builder.makeOperation(grh::ir::OperationKind::kSub,
                                             {offset, term}, location);
            }
            if (strides[i] != 1)
            {
                ExprNodeId strideId =
                    builder.addConstantLiteral(std::to_string(strides[i]), location);
                term = builder.makeOperation(grh::ir::OperationKind::kMul,
                                             {term, strideId}, location);
            }
            if (address == kInvalidPlanIndex)
            {
                address = term;
            }
            else
            {
                address = builder.makeOperation(grh::ir::OperationKind::kAdd,
                                                {address, term}, location);
            }
        }
        return address;
    };

    for (const auto& use : readUses)
    {
        if (use.data == kInvalidPlanIndex)
        {
            continue;
        }
        if (use.domain == ControlDomain::Sequential && use.eventEdges.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(use.location,
                                           "Skipping synchronous memory read without edge-sensitive timing control");
            }
            continue;
        }
        const auto& dims = plan.signals[use.signal].unpackedDims;
        ExprNodeId address = kInvalidPlanIndex;
        if (dims.empty())
        {
            if (!use.addressIndices.empty())
            {
                address = use.addressIndices.front();
            }
        }
        else
        {
            address = buildLinearAddress(use.addressIndices, dims, use.location);
        }
        if (address == kInvalidPlanIndex)
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(use.location,
                                           "Memory read missing address indices");
            }
            continue;
        }

        MemoryReadPort entry;
        entry.memory = use.memory;
        entry.signal = use.signal;
        entry.address = address;
        entry.data = use.data;
        entry.isSync = use.domain == ControlDomain::Sequential;
        entry.updateCond = use.updateCond;
        entry.eventEdges = use.eventEdges;
        entry.eventOperands = use.eventOperands;
        entry.location = use.location;
        lowering.memoryReads.push_back(std::move(entry));
    }

    for (const auto& stmt : lowering.loweredStmts)
    {
        if (stmt.kind != LoweredStmtKind::Write)
        {
            continue;
        }
        const WriteIntent& write = stmt.write;
        SignalId signal = resolveMemorySignal(write.target);
        if (signal == kInvalidPlanIndex)
        {
            continue;
        }
        const auto& dims = plan.signals[signal].unpackedDims;
        if (write.slices.size() < (dims.empty() ? 1 : dims.size()))
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location,
                                           "Memory write missing address slices");
            }
            continue;
        }
        const int64_t memWidth = plan.signals[signal].width > 0 ? plan.signals[signal].width : 0;
        if (memWidth <= 0)
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location, "Memory write missing valid width");
            }
            continue;
        }

        std::vector<ExprNodeId> addressIndices;
        const std::size_t addressCount = dims.empty() ? 1 : dims.size();
        bool addressOk = true;
        for (std::size_t i = 0; i < addressCount; ++i)
        {
            const WriteSlice& addrSlice = write.slices[i];
            if (addrSlice.kind != WriteSliceKind::BitSelect ||
                addrSlice.index == kInvalidPlanIndex)
            {
                addressOk = false;
                break;
            }
            addressIndices.push_back(addrSlice.index);
        }
        if (!addressOk)
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location,
                                           "Unsupported memory address slice kind");
            }
            continue;
        }
        ExprNodeId address = kInvalidPlanIndex;
        if (dims.empty())
        {
            address = addressIndices.front();
        }
        else
        {
            address = buildLinearAddress(addressIndices, dims, write.location);
        }
        if (address == kInvalidPlanIndex)
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location,
                                           "Memory write address linearization failed");
            }
            continue;
        }
        ExprNodeId data = write.value;
        ExprNodeId mask = kInvalidPlanIndex;
        bool isMasked = false;

        const std::size_t packedStart = addressCount;
        if (write.slices.size() == packedStart)
        {
            auto literal = makeMaskLiteral(memWidth, 0, memWidth - 1);
            if (!literal)
            {
                continue;
            }
            mask = builder.addConstantLiteral(*literal, write.location);
        }
        else if (write.slices.size() == packedStart + 1)
        {
            const WriteSlice& dataSlice = write.slices[packedStart];
            if (dataSlice.kind == WriteSliceKind::BitSelect)
            {
                isMasked = true;
                const ExprNodeId bitIndex = dataSlice.index;
                if (bitIndex == kInvalidPlanIndex)
                {
                    continue;
                }
                const std::string oneLiteral = std::to_string(memWidth) + "'b1";
                ExprNodeId one = builder.addConstantLiteral(oneLiteral, write.location);
                mask = builder.makeOperation(grh::ir::OperationKind::kShl,
                                             {one, bitIndex}, write.location);
                if (bitIndex == kInvalidPlanIndex)
                {
                    continue;
                }
                data = builder.makeOperation(grh::ir::OperationKind::kShl,
                                             {write.value, bitIndex}, write.location);
            }
            else if (dataSlice.kind == WriteSliceKind::RangeSelect)
            {
                isMasked = true;
                auto leftConst = evalConstInt(plan, lowering, dataSlice.left);
                auto rightConst = evalConstInt(plan, lowering, dataSlice.right);
                if (dataSlice.rangeKind == WriteRangeKind::Simple)
                {
                    if (!leftConst || !rightConst)
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                write.location,
                                "Memory range mask bounds must be constant");
                        }
                        continue;
                    }
                    int64_t lo = std::min(*leftConst, *rightConst);
                    int64_t hi = std::max(*leftConst, *rightConst);
                    if (lo < 0 || hi < lo || hi >= memWidth)
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                write.location,
                                "Memory range mask exceeds memory width");
                        }
                        continue;
                    }
                    auto literal = makeMaskLiteral(memWidth, lo, hi);
                    if (!literal)
                    {
                        continue;
                    }
                    mask = builder.addConstantLiteral(*literal, write.location);
                    if (lo != 0)
                    {
                        ExprNodeId shift =
                            builder.addConstantLiteral(std::to_string(lo), write.location);
                        data = builder.makeOperation(grh::ir::OperationKind::kShl,
                                                     {write.value, shift}, write.location);
                    }
                    else
                    {
                        data = write.value;
                    }
                }
                else if (dataSlice.rangeKind == WriteRangeKind::IndexedUp ||
                         dataSlice.rangeKind == WriteRangeKind::IndexedDown)
                {
                    ExprNodeId base = dataSlice.left;
                    ExprNodeId width = dataSlice.right;
                    if (base == kInvalidPlanIndex || width == kInvalidPlanIndex)
                    {
                        continue;
                    }
                    auto widthConst = evalConstInt(plan, lowering, width);
                    if (!widthConst)
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                write.location,
                                "Indexed part-select width must be constant");
                        }
                        continue;
                    }
                    if (*widthConst <= 0)
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                write.location,
                                "Indexed part-select width must be positive");
                        }
                        continue;
                    }
                    if (*widthConst > memWidth)
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                write.location,
                                "Indexed part-select exceeds memory width");
                        }
                        continue;
                    }
                    auto baseConst = evalConstInt(plan, lowering, base);
                    if (baseConst)
                    {
                        int64_t lo = 0;
                        int64_t hi = 0;
                        if (dataSlice.rangeKind == WriteRangeKind::IndexedUp)
                        {
                            lo = *baseConst;
                            hi = *baseConst + *widthConst - 1;
                        }
                        else
                        {
                            lo = *baseConst - *widthConst + 1;
                            hi = *baseConst;
                        }
                        if (lo < 0 || hi < lo || hi >= memWidth)
                        {
                            if (context_.diagnostics)
                            {
                                context_.diagnostics->warn(
                                    write.location,
                                    "Indexed part-select exceeds memory width");
                            }
                            continue;
                        }
                    }
                    else
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                write.location,
                                "Indexed part-select base is dynamic; bounds check skipped");
                        }
                    }
                    ExprNodeId widthLiteral =
                        builder.addConstantLiteral(std::to_string(*widthConst), write.location);
                    ExprNodeId one = builder.addConstantLiteral("1", write.location);
                    ExprNodeId shifted = builder.makeOperation(grh::ir::OperationKind::kShl,
                                                               {one, widthLiteral}, write.location);
                    ExprNodeId ones =
                        builder.makeOperation(grh::ir::OperationKind::kSub,
                                              {shifted, one}, write.location);
                    ExprNodeId shift = base;
                    if (dataSlice.rangeKind == WriteRangeKind::IndexedDown)
                    {
                        ExprNodeId baseMinus =
                            builder.makeOperation(grh::ir::OperationKind::kSub,
                                                  {base, widthLiteral}, write.location);
                        shift = builder.makeOperation(grh::ir::OperationKind::kAdd,
                                                      {baseMinus, one}, write.location);
                    }
                    mask = builder.makeOperation(grh::ir::OperationKind::kShl,
                                                 {ones, shift}, write.location);
                    data = builder.makeOperation(grh::ir::OperationKind::kShl,
                                                 {write.value, shift}, write.location);
                }
                else
                {
                    if (context_.diagnostics)
                    {
                        context_.diagnostics->todo(write.location,
                                                   "Dynamic memory range mask is unsupported");
                    }
                    continue;
                }
            }
            else
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->todo(write.location,
                                               "Unsupported memory write slice kind");
                }
                continue;
            }
        }
        else
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->todo(write.location,
                                           "Multi-slice memory write is unsupported");
            }
            continue;
        }

        if (mask == kInvalidPlanIndex || data == kInvalidPlanIndex)
        {
            continue;
        }

        if (write.domain == ControlDomain::Sequential && stmt.eventEdges.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(
                    write.location,
                    "Skipping memory write without edge-sensitive timing control");
            }
            continue;
        }

        MemoryWritePort entry;
        entry.memory = write.target;
        entry.signal = signal;
        entry.address = address;
        entry.data = data;
        entry.mask = mask;
        entry.updateCond = builder.ensureGuardExpr(write.guard, write.location);
        entry.isMasked = isMasked;
        entry.eventEdges = stmt.eventEdges;
        entry.eventOperands = stmt.eventOperands;
        entry.location = write.location;
        lowering.memoryWrites.push_back(std::move(entry));
    }
}

class GraphAssemblyState {
public:
    GraphAssemblyState(ConvertContext& context, GraphAssembler& assembler, grh::ir::Graph& graph,
                       const ModulePlan& plan, LoweringPlan& lowering,
                       const WriteBackPlan& writeBack)
        : context_(context), assembler_(assembler), graph_(graph), plan_(plan),
          lowering_(lowering), writeBack_(writeBack), connectionLowerer_(*this)
    {
        symbolIds_.assign(plan_.symbolTable.size(), grh::ir::SymbolId::invalid());
        valueBySymbol_.assign(plan_.symbolTable.size(), grh::ir::ValueId::invalid());
        valueByExpr_.assign(lowering_.values.size(), grh::ir::ValueId::invalid());
        memoryOpBySymbol_.assign(plan_.symbolTable.size(), grh::ir::OperationId::invalid());
        memorySymbolName_.assign(plan_.symbolTable.size(), std::string());
        memoryReadIndexByExpr_.assign(lowering_.values.size(), kInvalidMemoryReadIndex);
        for (std::size_t i = 0; i < lowering_.memoryReads.size(); ++i)
        {
            ExprNodeId data = lowering_.memoryReads[i].data;
            if (data == kInvalidPlanIndex || data >= memoryReadIndexByExpr_.size())
            {
                continue;
            }
            if (memoryReadIndexByExpr_[data] == kInvalidMemoryReadIndex)
            {
                memoryReadIndexByExpr_[data] = static_cast<int32_t>(i);
            }
        }
    }

    void build()
    {
        createPortValues();
        createSignalValues();
        createMemoryOps();
        emitMemoryPorts();
        emitSideEffects();
        emitInstances();
        emitWriteBack();
    }

private:
    static int32_t normalizeWidth(int32_t width) { return width > 0 ? width : 1; }
    static constexpr int32_t kInvalidMemoryReadIndex = -1;

    grh::ir::SymbolId symbolForPlan(PlanSymbolId id)
    {
        if (!id.valid() || id.index >= symbolIds_.size())
        {
            return grh::ir::SymbolId::invalid();
        }
        if (symbolIds_[id.index].valid())
        {
            return symbolIds_[id.index];
        }
        const std::string text(plan_.symbolTable.text(id));
        if (text.empty())
        {
            return grh::ir::SymbolId::invalid();
        }
        symbolIds_[id.index] = graph_.internSymbol(text);
        return symbolIds_[id.index];
    }

    grh::ir::ValueId valueForSymbol(PlanSymbolId id)
    {
        if (!id.valid() || id.index >= valueBySymbol_.size())
        {
            return grh::ir::ValueId::invalid();
        }
        return valueBySymbol_[id.index];
    }

    grh::ir::ValueId createValue(PlanSymbolId id, int32_t width, bool isSigned)
    {
        if (!id.valid() || id.index >= valueBySymbol_.size())
        {
            return grh::ir::ValueId::invalid();
        }
        if (valueBySymbol_[id.index].valid())
        {
            return valueBySymbol_[id.index];
        }
        grh::ir::SymbolId symbol = symbolForPlan(id);
        if (!symbol.valid())
        {
            return grh::ir::ValueId::invalid();
        }
        const int32_t normalized = normalizeWidth(width);
        grh::ir::ValueId value = graph_.createValue(symbol, normalized, isSigned);
        valueBySymbol_[id.index] = value;
        return value;
    }

    void createPortValues()
    {
        for (const auto& port : plan_.ports)
        {
            if (!port.symbol.valid())
            {
                continue;
            }
            const int32_t width = normalizeWidth(port.width);
            if (port.direction == PortDirection::Input)
            {
                grh::ir::ValueId value = createValue(port.symbol, width, port.isSigned);
                if (value.valid())
                {
                    graph_.bindInputPort(symbolForPlan(port.symbol), value);
                }
                continue;
            }
            if (port.direction == PortDirection::Output)
            {
                grh::ir::ValueId value = createValue(port.symbol, width, port.isSigned);
                if (value.valid())
                {
                    graph_.bindOutputPort(symbolForPlan(port.symbol), value);
                }
                continue;
            }
            if (port.direction == PortDirection::Inout && port.inoutSymbol)
            {
                const auto& binding = *port.inoutSymbol;
                grh::ir::ValueId inValue = createValue(binding.inSymbol, width, port.isSigned);
                grh::ir::ValueId outValue = createValue(binding.outSymbol, width, port.isSigned);
                grh::ir::ValueId oeValue = createValue(binding.oeSymbol, width, false);
                if (inValue.valid() && outValue.valid() && oeValue.valid())
                {
                    graph_.bindInoutPort(symbolForPlan(port.symbol), inValue, outValue,
                                         oeValue);
                }
                continue;
            }
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(
                    slang::SourceLocation{},
                    "Skipping unsupported port direction in graph assembly");
            }
        }
    }

    void createSignalValues()
    {
        for (const auto& signal : plan_.signals)
        {
            if (!signal.symbol.valid())
            {
                continue;
            }
            if (signal.memoryRows > 0 || signal.kind == SignalKind::Memory)
            {
                continue;
            }
            const int32_t width = normalizeWidth(signal.width);
            createValue(signal.symbol, width, signal.isSigned);
        }
    }

    const SignalInfo* memorySignal(SignalId signal) const
    {
        if (signal == kInvalidPlanIndex ||
            signal >= static_cast<SignalId>(plan_.signals.size()))
        {
            return nullptr;
        }
        return &plan_.signals[signal];
    }

    bool ensureMemoryOp(PlanSymbolId memory, SignalId signal, slang::SourceLocation location)
    {
        if (!memory.valid() || memory.index >= memoryOpBySymbol_.size())
        {
            return false;
        }
        if (memoryOpBySymbol_[memory.index].valid())
        {
            return true;
        }
        const SignalInfo* info = memorySignal(signal);
        if (!info)
        {
            return false;
        }
        if (info->memoryRows <= 0)
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(location,
                                           "Skipping memory op without valid row count");
            }
            return false;
        }

        std::string name(plan_.symbolTable.text(memory));
        if (name.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(location,
                                           "Skipping memory op without symbol name");
            }
            return false;
        }
        std::string finalName = name;
        grh::ir::SymbolId sym = graph_.internSymbol(finalName);
        if (graph_.findValue(sym).valid() || graph_.findOperation(sym).valid())
        {
            std::string base = name + "__mem";
            std::size_t suffix = 0;
            finalName = base;
            while (graph_.symbols().contains(finalName))
            {
                finalName = base + "_" + std::to_string(++suffix);
            }
            sym = graph_.internSymbol(finalName);
        }

        const int64_t width = normalizeWidth(info->width);
        if (info->width <= 0 && context_.diagnostics)
        {
            context_.diagnostics->warn(location, "Memory width missing, defaulting to 1");
        }

        grh::ir::OperationId op =
            graph_.createOperation(grh::ir::OperationKind::kMemory, sym);
        graph_.setAttr(op, "width", width);
        graph_.setAttr(op, "row", static_cast<int64_t>(info->memoryRows));
        graph_.setAttr(op, "isSigned", info->isSigned);
        memoryOpBySymbol_[memory.index] = op;
        memorySymbolName_[memory.index] = finalName;
        return true;
    }

    std::string_view memorySymbolText(PlanSymbolId memory) const
    {
        if (!memory.valid() || memory.index >= memorySymbolName_.size())
        {
            return {};
        }
        return memorySymbolName_[memory.index];
    }

    void createMemoryOps()
    {
        for (SignalId i = 0; i < static_cast<SignalId>(plan_.signals.size()); ++i)
        {
            const auto& signal = plan_.signals[i];
            if (!signal.symbol.valid())
            {
                continue;
            }
            if (signal.memoryRows <= 0 && signal.kind != SignalKind::Memory)
            {
                continue;
            }
            ensureMemoryOp(signal.symbol, i, slang::SourceLocation{});
        }
    }

    void emitMemoryPorts()
    {
        for (const auto& entry : lowering_.memoryReads)
        {
            if (entry.data == kInvalidPlanIndex)
            {
                continue;
            }
            emitMemoryRead(entry.data);
        }
        emitMemoryWrites();
    }

    grh::ir::ValueId emitMemoryRead(ExprNodeId id)
    {
        if (id == kInvalidPlanIndex || id >= memoryReadIndexByExpr_.size())
        {
            return grh::ir::ValueId::invalid();
        }
        if (valueByExpr_[id].valid())
        {
            return valueByExpr_[id];
        }
        const int32_t readIndex = memoryReadIndexByExpr_[id];
        if (readIndex == kInvalidMemoryReadIndex)
        {
            return grh::ir::ValueId::invalid();
        }
        if (readIndex < 0 ||
            static_cast<std::size_t>(readIndex) >= lowering_.memoryReads.size())
        {
            return grh::ir::ValueId::invalid();
        }
        const MemoryReadPort& entry = lowering_.memoryReads[readIndex];
        if (entry.isSync && entry.eventEdges.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(
                    entry.location,
                    "Skipping synchronous memory read without edge-sensitive timing control");
            }
            return grh::ir::ValueId::invalid();
        }
        if (!ensureMemoryOp(entry.memory, entry.signal, entry.location))
        {
            return grh::ir::ValueId::invalid();
        }
        const SignalInfo* info = memorySignal(entry.signal);
        if (!info)
        {
            return grh::ir::ValueId::invalid();
        }
        std::string_view memSymbol = memorySymbolText(entry.memory);
        if (memSymbol.empty())
        {
            return grh::ir::ValueId::invalid();
        }

        const int32_t width = normalizeWidth(info->width);
        grh::ir::SymbolId dataSym = grh::ir::SymbolId::invalid();
        if (id < lowering_.values.size())
        {
            const ExprNode& node = lowering_.values[id];
            if (node.tempSymbol.valid())
            {
                dataSym = symbolForPlan(node.tempSymbol);
            }
        }
        if (!dataSym.valid())
        {
            dataSym = graph_.internSymbol("__mem_data_" + std::to_string(nextMemValueId_++));
        }
        grh::ir::ValueId dataValue = graph_.createValue(dataSym, width, info->isSigned);

        grh::ir::ValueId addressValue = emitExpr(entry.address);
        if (!addressValue.valid())
        {
            return grh::ir::ValueId::invalid();
        }

        grh::ir::SymbolId readSym = makeOpSymbol(entry.memory, "mem_read");
        grh::ir::OperationId readOp =
            graph_.createOperation(grh::ir::OperationKind::kMemoryReadPort, readSym);
        graph_.addOperand(readOp, addressValue);
        graph_.setAttr(readOp, "memSymbol", std::string(memSymbol));

        if (!entry.isSync)
        {
            graph_.addResult(readOp, dataValue);
            valueByExpr_[id] = dataValue;
            return dataValue;
        }

        grh::ir::SymbolId readValueSym =
            graph_.internSymbol("__mem_rd_" + std::to_string(nextMemReadValueId_++));
        grh::ir::ValueId readValue =
            graph_.createValue(readValueSym, width, info->isSigned);
        graph_.addResult(readOp, readValue);

        grh::ir::ValueId updateCond = emitExpr(entry.updateCond);
        if (!updateCond.valid())
        {
            return grh::ir::ValueId::invalid();
        }
        grh::ir::SymbolId regSym = makeOpSymbol(entry.memory, "mem_read_reg");
        grh::ir::OperationId regOp =
            graph_.createOperation(grh::ir::OperationKind::kRegister, regSym);
        graph_.addOperand(regOp, updateCond);
        graph_.addOperand(regOp, readValue);
        for (ExprNodeId evtId : entry.eventOperands)
        {
            grh::ir::ValueId evt = emitExpr(evtId);
            if (!evt.valid())
            {
                continue;
            }
            graph_.addOperand(regOp, evt);
        }
        std::vector<std::string> edges;
        edges.reserve(entry.eventEdges.size());
        for (EventEdge edge : entry.eventEdges)
        {
            edges.push_back(edgeText(edge));
        }
        graph_.setAttr(regOp, "eventEdge", std::move(edges));
        graph_.addResult(regOp, dataValue);
        valueByExpr_[id] = dataValue;
        return dataValue;
    }

    void emitMemoryWrites()
    {
        for (const auto& entry : lowering_.memoryWrites)
        {
            if (!entry.memory.valid())
            {
                continue;
            }
            if (entry.eventEdges.empty())
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->warn(
                        entry.location,
                        "Skipping memory write without edge-sensitive timing control");
                }
                continue;
            }
            if (!ensureMemoryOp(entry.memory, entry.signal, entry.location))
            {
                continue;
            }
            std::string_view memSymbol = memorySymbolText(entry.memory);
            if (memSymbol.empty())
            {
                continue;
            }
            grh::ir::ValueId updateCond = emitExpr(entry.updateCond);
            grh::ir::ValueId address = emitExpr(entry.address);
            grh::ir::ValueId data = emitExpr(entry.data);
            grh::ir::ValueId mask = emitExpr(entry.mask);
            if (!updateCond.valid() || !address.valid() || !data.valid() || !mask.valid())
            {
                continue;
            }

            grh::ir::SymbolId opSym = makeOpSymbol(entry.memory, "mem_write");
            grh::ir::OperationId op =
                graph_.createOperation(grh::ir::OperationKind::kMemoryWritePort, opSym);
            graph_.addOperand(op, updateCond);
            graph_.addOperand(op, address);
            graph_.addOperand(op, data);
            graph_.addOperand(op, mask);
            for (ExprNodeId evtId : entry.eventOperands)
            {
                grh::ir::ValueId evt = emitExpr(evtId);
                if (!evt.valid())
                {
                    continue;
                }
                graph_.addOperand(op, evt);
            }
            std::vector<std::string> edges;
            edges.reserve(entry.eventEdges.size());
            for (EventEdge edge : entry.eventEdges)
            {
                edges.push_back(edgeText(edge));
            }
            graph_.setAttr(op, "eventEdge", std::move(edges));
            graph_.setAttr(op, "memSymbol", std::string(memSymbol));
        }
    }

    void emitSideEffects()
    {
        emitDpiImports();
        for (const auto& stmt : lowering_.loweredStmts)
        {
            switch (stmt.kind)
            {
            case LoweredStmtKind::Display:
                emitDisplay(stmt);
                break;
            case LoweredStmtKind::Assert:
                emitAssert(stmt);
                break;
            case LoweredStmtKind::DpiCall:
                emitDpiCall(stmt);
                break;
            default:
                break;
            }
        }
    }

    const DpiImportInfo* findDpiImport(std::string_view symbol) const
    {
        for (const auto& info : lowering_.dpiImports)
        {
            if (info.symbol == symbol)
            {
                return &info;
            }
        }
        return nullptr;
    }

    void emitDpiImports()
    {
        for (const auto& info : lowering_.dpiImports)
        {
            if (info.symbol.empty())
            {
                continue;
            }
            grh::ir::SymbolId sym = graph_.internSymbol(info.symbol);
            grh::ir::OperationId existing = graph_.findOperation(sym);
            if (existing.valid())
            {
                if (graph_.getOperation(existing).kind() != grh::ir::OperationKind::kDpicImport &&
                    context_.diagnostics)
                {
                    context_.diagnostics->warn(
                        slang::SourceLocation{},
                        std::string("Skipping DPI import with conflicting symbol ") +
                            info.symbol);
                }
                continue;
            }
            if (graph_.findValue(sym).valid())
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->warn(
                        slang::SourceLocation{},
                        std::string("Skipping DPI import with conflicting value ") +
                            info.symbol);
                }
                continue;
            }

            grh::ir::OperationId op =
                graph_.createOperation(grh::ir::OperationKind::kDpicImport, sym);
            graph_.setAttr(op, "argsDirection", info.argsDirection);
            graph_.setAttr(op, "argsWidth", info.argsWidth);
            graph_.setAttr(op, "argsName", info.argsName);
            graph_.setAttr(op, "argsSigned", info.argsSigned);
            graph_.setAttr(op, "hasReturn", info.hasReturn);
            if (info.hasReturn)
            {
                graph_.setAttr(op, "returnWidth", info.returnWidth);
                graph_.setAttr(op, "returnSigned", info.returnSigned);
            }
        }
    }

    void emitDisplay(const LoweredStmt& stmt)
    {
        if (stmt.eventEdges.size() != stmt.eventOperands.size())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(stmt.location,
                                           "Skipping display with mismatched event binding");
            }
            return;
        }
        if (stmt.eventEdges.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(stmt.location,
                                           "Skipping display without edge-sensitive timing");
            }
            return;
        }

        grh::ir::ValueId updateCond = emitExpr(stmt.updateCond);
        if (!updateCond.valid())
        {
            return;
        }

        grh::ir::OperationId op =
            graph_.createOperation(grh::ir::OperationKind::kDisplay,
                                   grh::ir::SymbolId::invalid());
        graph_.addOperand(op, updateCond);

        for (ExprNodeId argId : stmt.display.args)
        {
            grh::ir::ValueId arg = emitExpr(argId);
            if (!arg.valid())
            {
                return;
            }
            graph_.addOperand(op, arg);
        }
        for (ExprNodeId evtId : stmt.eventOperands)
        {
            grh::ir::ValueId evt = emitExpr(evtId);
            if (!evt.valid())
            {
                return;
            }
            graph_.addOperand(op, evt);
        }

        graph_.setAttr(op, "formatString", stmt.display.formatString);
        graph_.setAttr(op, "displayKind", stmt.display.displayKind);
        std::vector<std::string> edges;
        edges.reserve(stmt.eventEdges.size());
        for (EventEdge edge : stmt.eventEdges)
        {
            edges.push_back(edgeText(edge));
        }
        graph_.setAttr(op, "eventEdge", edges);
        if (edges.size() == 1)
        {
            graph_.setAttr(op, "clkPolarity", edges.front());
        }
    }

    void emitAssert(const LoweredStmt& stmt)
    {
        if (stmt.eventEdges.size() != stmt.eventOperands.size())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(stmt.location,
                                           "Skipping assert with mismatched event binding");
            }
            return;
        }
        if (stmt.eventEdges.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(stmt.location,
                                           "Skipping assert without edge-sensitive timing");
            }
            return;
        }

        grh::ir::ValueId updateCond = emitExpr(stmt.updateCond);
        grh::ir::ValueId condition = emitExpr(stmt.assertion.condition);
        if (!updateCond.valid() || !condition.valid())
        {
            return;
        }

        grh::ir::OperationId op =
            graph_.createOperation(grh::ir::OperationKind::kAssert,
                                   grh::ir::SymbolId::invalid());
        graph_.addOperand(op, updateCond);
        graph_.addOperand(op, condition);
        for (ExprNodeId evtId : stmt.eventOperands)
        {
            grh::ir::ValueId evt = emitExpr(evtId);
            if (!evt.valid())
            {
                return;
            }
            graph_.addOperand(op, evt);
        }

        if (!stmt.assertion.message.empty())
        {
            graph_.setAttr(op, "message", stmt.assertion.message);
        }
        if (!stmt.assertion.severity.empty())
        {
            graph_.setAttr(op, "severity", stmt.assertion.severity);
        }
        std::vector<std::string> edges;
        edges.reserve(stmt.eventEdges.size());
        for (EventEdge edge : stmt.eventEdges)
        {
            edges.push_back(edgeText(edge));
        }
        graph_.setAttr(op, "eventEdge", edges);
        if (edges.size() == 1)
        {
            graph_.setAttr(op, "clkPolarity", edges.front());
        }
    }

    std::optional<std::pair<int64_t, bool>> findDpiArgType(
        const DpiImportInfo& importInfo, std::string_view name,
        std::string_view direction) const
    {
        for (std::size_t i = 0; i < importInfo.argsName.size(); ++i)
        {
            if (importInfo.argsName[i] != name)
            {
                continue;
            }
            if (i >= importInfo.argsDirection.size() || i >= importInfo.argsWidth.size() ||
                i >= importInfo.argsSigned.size())
            {
                break;
            }
            if (!direction.empty() && importInfo.argsDirection[i] != direction)
            {
                break;
            }
            return std::pair<int64_t, bool>{importInfo.argsWidth[i], importInfo.argsSigned[i]};
        }
        return std::nullopt;
    }

    void emitDpiCall(const LoweredStmt& stmt)
    {
        if (stmt.eventEdges.size() != stmt.eventOperands.size())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(stmt.location,
                                           "Skipping DPI call with mismatched event binding");
            }
            return;
        }
        if (stmt.eventEdges.empty())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(stmt.location,
                                           "Skipping DPI call without edge-sensitive timing");
            }
            return;
        }

        const DpiCallStmt& dpi = stmt.dpiCall;
        const DpiImportInfo* importInfo = findDpiImport(dpi.targetImportSymbol);
        if (!importInfo)
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(
                    stmt.location,
                    std::string("Skipping DPI call without matching import ") +
                        dpi.targetImportSymbol);
            }
            return;
        }

        grh::ir::ValueId updateCond = emitExpr(stmt.updateCond);
        if (!updateCond.valid())
        {
            return;
        }

        grh::ir::OperationId op =
            graph_.createOperation(grh::ir::OperationKind::kDpicCall,
                                   grh::ir::SymbolId::invalid());
        graph_.addOperand(op, updateCond);
        for (ExprNodeId argId : dpi.inArgs)
        {
            grh::ir::ValueId arg = emitExpr(argId);
            if (!arg.valid())
            {
                return;
            }
            graph_.addOperand(op, arg);
        }
        for (ExprNodeId evtId : stmt.eventOperands)
        {
            grh::ir::ValueId evt = emitExpr(evtId);
            if (!evt.valid())
            {
                return;
            }
            graph_.addOperand(op, evt);
        }

        graph_.setAttr(op, "targetImportSymbol", dpi.targetImportSymbol);
        graph_.setAttr(op, "inArgName", dpi.inArgNames);
        graph_.setAttr(op, "outArgName", dpi.outArgNames);
        graph_.setAttr(op, "hasReturn", dpi.hasReturn);
        std::vector<std::string> edges;
        edges.reserve(stmt.eventEdges.size());
        for (EventEdge edge : stmt.eventEdges)
        {
            edges.push_back(edgeText(edge));
        }
        graph_.setAttr(op, "eventEdge", edges);
        if (edges.size() == 1)
        {
            graph_.setAttr(op, "clkPolarity", edges.front());
        }

        std::size_t resultOffset = 0;
        if (dpi.hasReturn)
        {
            if (dpi.results.empty())
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->warn(stmt.location,
                                               "DPI call missing return result");
                }
                return;
            }
            PlanSymbolId retSymbol = dpi.results.front();
            const int64_t width = importInfo->returnWidth > 0 ? importInfo->returnWidth : 1;
            const bool isSigned = importInfo->returnSigned;
            grh::ir::ValueId retValue = valueForSymbol(retSymbol);
            if (!retValue.valid())
            {
                retValue = createValue(retSymbol, static_cast<int32_t>(width), isSigned);
            }
            if (!retValue.valid())
            {
                return;
            }
            graph_.addResult(op, retValue);
            resultOffset = 1;
        }

        if (dpi.results.size() < resultOffset + dpi.outArgNames.size())
        {
            if (context_.diagnostics)
            {
                context_.diagnostics->warn(stmt.location,
                                           "DPI call result count mismatch");
            }
            return;
        }

        for (std::size_t i = 0; i < dpi.outArgNames.size(); ++i)
        {
            PlanSymbolId resultSymbol = dpi.results[resultOffset + i];
            grh::ir::ValueId resultValue = valueForSymbol(resultSymbol);
            if (!resultValue.valid())
            {
                auto meta = findDpiArgType(*importInfo, dpi.outArgNames[i], "output");
                int64_t width = meta ? meta->first : 1;
                bool isSigned = meta ? meta->second : false;
                resultValue = createValue(resultSymbol, static_cast<int32_t>(width), isSigned);
            }
            if (!resultValue.valid())
            {
                return;
            }
            graph_.addResult(op, resultValue);
        }
    }

    void emitInstances()
    {
        for (const auto& instanceInfo : plan_.instances)
        {
            if (!instanceInfo.instance)
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->warn(
                        slang::SourceLocation{},
                        "Skipping instance without symbol reference");
                }
                continue;
            }

            const slang::ast::InstanceSymbol& instance = *instanceInfo.instance;
            const slang::ast::InstanceBodySymbol& body = instance.body;
            std::vector<std::string> inputNames;
            std::vector<std::string> outputNames;
            std::vector<std::string> inoutNames;
            std::vector<grh::ir::ValueId> operands;
            std::vector<grh::ir::ValueId> results;
            bool ok = true;

            for (const slang::ast::Symbol* portSymbol : body.getPortList())
            {
                if (!portSymbol)
                {
                    continue;
                }
                const auto* port = portSymbol->as_if<slang::ast::PortSymbol>();
                if (!port || port->name.empty() || port->isNullPort)
                {
                    if (context_.diagnostics)
                    {
                        context_.diagnostics->warn(
                            portSymbol->location,
                            "Skipping instance with unsupported port declaration");
                    }
                    ok = false;
                    break;
                }

                const slang::ast::PortConnection* connection =
                    instance.getPortConnection(*port);
                const slang::ast::Expression* expr =
                    connection ? connection->getExpression() : nullptr;
                if (!expr || expr->bad())
                {
                    if (context_.diagnostics)
                    {
                        context_.diagnostics->warn(
                            port->location,
                            "Skipping instance with missing port connection");
                    }
                    ok = false;
                    break;
                }

                switch (port->direction)
                {
                case slang::ast::ArgumentDirection::In: {
                    inputNames.emplace_back(port->name);
                    grh::ir::ValueId value = emitConnectionExpr(*expr);
                    if (!value.valid())
                    {
                        ok = false;
                        break;
                    }
                    operands.push_back(value);
                    break;
                }
                case slang::ast::ArgumentDirection::Out: {
                    outputNames.emplace_back(port->name);
                    PlanSymbolId symbol = resolveSimpleSymbol(*expr);
                    if (!symbol.valid())
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                port->location,
                                "Skipping instance with unsupported output connection");
                        }
                        ok = false;
                        break;
                    }
                    if (const PortInfo* inout = resolveInoutPort(symbol))
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                port->location,
                                "Skipping instance output connection to inout port");
                        }
                        ok = false;
                        break;
                    }
                    grh::ir::ValueId value = valueForSymbol(symbol);
                    if (!value.valid())
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                port->location,
                                "Skipping instance with missing output binding");
                        }
                        ok = false;
                        break;
                    }
                    results.push_back(value);
                    break;
                }
                case slang::ast::ArgumentDirection::InOut: {
                    inoutNames.emplace_back(port->name);
                    const PortInfo* inoutPort = resolveInoutPort(*expr);
                    if (!inoutPort || !inoutPort->inoutSymbol)
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                port->location,
                                "Skipping instance with unsupported inout connection");
                        }
                        ok = false;
                        break;
                    }
                    const auto& binding = *inoutPort->inoutSymbol;
                    grh::ir::ValueId inValue = valueForSymbol(binding.inSymbol);
                    grh::ir::ValueId outValue = valueForSymbol(binding.outSymbol);
                    grh::ir::ValueId oeValue = valueForSymbol(binding.oeSymbol);
                    if (!inValue.valid() || !outValue.valid() || !oeValue.valid())
                    {
                        if (context_.diagnostics)
                        {
                            context_.diagnostics->warn(
                                port->location,
                                "Skipping instance with incomplete inout binding");
                        }
                        ok = false;
                        break;
                    }
                    operands.push_back(outValue);
                    operands.push_back(oeValue);
                    results.push_back(inValue);
                    break;
                }
                default:
                    if (context_.diagnostics)
                    {
                        context_.diagnostics->warn(
                            port->location,
                            "Skipping instance with unsupported port direction");
                    }
                    ok = false;
                    break;
                }

                if (!ok)
                {
                    break;
                }
            }

            if (!ok)
            {
                continue;
            }

            const std::string moduleNameText =
                instanceInfo.moduleSymbol.valid()
                    ? std::string(plan_.symbolTable.text(instanceInfo.moduleSymbol))
                    : std::string();
            std::string moduleName = moduleNameText;
            if (!instanceInfo.isBlackbox)
            {
                PlanKey childKey;
                childKey.body = &body;
                childKey.paramSignature = instanceInfo.paramSignature;
                moduleName = assembler_.resolveGraphName(childKey, moduleNameText);
            }
            if (moduleName.empty())
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->warn(
                        instance.location,
                        "Skipping instance with empty module name");
                }
                continue;
            }

            grh::ir::OperationKind kind = instanceInfo.isBlackbox
                                              ? grh::ir::OperationKind::kBlackbox
                                              : grh::ir::OperationKind::kInstance;
            grh::ir::OperationId op =
                graph_.createOperation(kind, grh::ir::SymbolId::invalid());
            for (const auto& operand : operands)
            {
                graph_.addOperand(op, operand);
            }
            for (const auto& result : results)
            {
                graph_.addResult(op, result);
            }
            graph_.setAttr(op, "moduleName", moduleName);
            graph_.setAttr(op, "inputPortName", inputNames);
            graph_.setAttr(op, "outputPortName", outputNames);
            if (!inoutNames.empty())
            {
                graph_.setAttr(op, "inoutPortName", inoutNames);
            }

            if (instanceInfo.instanceSymbol.valid())
            {
                std::string_view name = plan_.symbolTable.text(instanceInfo.instanceSymbol);
                if (!name.empty())
                {
                    graph_.setAttr(op, "instanceName", std::string(name));
                }
            }

            if (instanceInfo.isBlackbox && !instanceInfo.parameters.empty())
            {
                std::vector<std::string> paramNames;
                std::vector<std::string> paramValues;
                paramNames.reserve(instanceInfo.parameters.size());
                paramValues.reserve(instanceInfo.parameters.size());
                for (const auto& param : instanceInfo.parameters)
                {
                    if (!param.symbol.valid())
                    {
                        continue;
                    }
                    std::string_view name = plan_.symbolTable.text(param.symbol);
                    if (name.empty())
                    {
                        continue;
                    }
                    paramNames.emplace_back(name);
                    paramValues.emplace_back(param.value);
                }
                if (!paramNames.empty() && paramNames.size() == paramValues.size())
                {
                    graph_.setAttr(op, "parameterNames", paramNames);
                    graph_.setAttr(op, "parameterValues", paramValues);
                }
            }
        }
    }

    grh::ir::ValueId emitConnectionExpr(const slang::ast::Expression& expr)
    {
        ExprNodeId id = connectionLowerer_.lowerExpression(expr);
        if (id == kInvalidPlanIndex)
        {
            return grh::ir::ValueId::invalid();
        }
        return emitExpr(id);
    }

    PlanSymbolId resolveSimpleSymbol(const slang::ast::Expression& expr) const
    {
        if (const auto* assign = expr.as_if<slang::ast::AssignmentExpression>())
        {
            if (assign->isLValueArg())
            {
                return resolveSimpleSymbol(assign->left());
            }
            return resolveSimpleSymbol(assign->right());
        }
        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            return plan_.symbolTable.lookup(named->symbol.name);
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            return plan_.symbolTable.lookup(hier->symbol.name);
        }
        if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
        {
            return resolveSimpleSymbol(conversion->operand());
        }
        return {};
    }

    const PortInfo* resolveInoutPort(const slang::ast::Expression& expr) const
    {
        PlanSymbolId symbol = resolveSimpleSymbol(expr);
        return resolveInoutPort(symbol);
    }

    const PortInfo* resolveInoutPort(PlanSymbolId symbol) const
    {
        if (!symbol.valid())
        {
            return nullptr;
        }
        std::string_view name = plan_.symbolTable.text(symbol);
        if (name.empty())
        {
            return nullptr;
        }
        const PortInfo* port = findPortByName(plan_, name);
        if (!port)
        {
            port = findPortByInoutName(plan_, name);
        }
        if (!port || port->direction != PortDirection::Inout || !port->inoutSymbol)
        {
            return nullptr;
        }
        return port;
    }

    void registerExprNode()
    {
        valueByExpr_.push_back(grh::ir::ValueId::invalid());
        memoryReadIndexByExpr_.push_back(kInvalidMemoryReadIndex);
    }

    class ConnectionExprLowerer {
    public:
        explicit ConnectionExprLowerer(GraphAssemblyState& state)
            : state_(state)
        {
        }

        ExprNodeId lowerExpression(const slang::ast::Expression& expr)
        {
            if (auto it = lowered_.find(&expr); it != lowered_.end())
            {
                return it->second;
            }

            auto paramLiteral = [](const slang::ast::ParameterSymbol& param)
                -> std::optional<std::string> {
                slang::ConstantValue value = param.getValue();
                if (value.bad())
                {
                    return std::nullopt;
                }
                if (!value.isInteger())
                {
                    value = value.convertToInt();
                }
                if (!value.isInteger())
                {
                    return std::nullopt;
                }
                const slang::SVInt& literal = value.integer();
                if (literal.hasUnknown())
                {
                    return std::nullopt;
                }
                return literal.toString();
            };

            ExprNode node;
            node.location = expr.sourceRange.start();

            if (const slang::ConstantValue* constant = expr.getConstant())
            {
                if (constant->isInteger())
                {
                    const slang::SVInt& literal = constant->integer();
                    if (!literal.hasUnknown())
                    {
                        node.kind = ExprNodeKind::Constant;
                        node.literal = literal.toString();
                        return addNode(expr, std::move(node));
                    }
                }
            }

            if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
            {
                if (const auto* param =
                        named->symbol.as_if<slang::ast::ParameterSymbol>())
                {
                    if (auto literal = paramLiteral(*param))
                    {
                        node.kind = ExprNodeKind::Constant;
                        node.literal = *literal;
                        return addNode(expr, std::move(node));
                    }
                }
                node.kind = ExprNodeKind::Symbol;
                node.symbol = state_.plan_.symbolTable.lookup(named->symbol.name);
                if (const PortInfo* inout = state_.resolveInoutPort(node.symbol))
                {
                    node.symbol = inout->inoutSymbol->inSymbol;
                }
                if (!node.symbol.valid())
                {
                    reportUnsupported(expr, "Unknown symbol in connection expression");
                }
                return addNode(expr, std::move(node));
            }
            if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
            {
                if (const auto* param =
                        hier->symbol.as_if<slang::ast::ParameterSymbol>())
                {
                    if (auto literal = paramLiteral(*param))
                    {
                        node.kind = ExprNodeKind::Constant;
                        node.literal = *literal;
                        return addNode(expr, std::move(node));
                    }
                }
                node.kind = ExprNodeKind::Symbol;
                node.symbol = state_.plan_.symbolTable.lookup(hier->symbol.name);
                if (const PortInfo* inout = state_.resolveInoutPort(node.symbol))
                {
                    node.symbol = inout->inoutSymbol->inSymbol;
                }
                if (!node.symbol.valid())
                {
                    reportUnsupported(expr, "Unknown hierarchical symbol in connection");
                }
                return addNode(expr, std::move(node));
            }
            if (const auto* literal = expr.as_if<slang::ast::IntegerLiteral>())
            {
                node.kind = ExprNodeKind::Constant;
                node.literal = literal->getValue().toString();
                return addNode(expr, std::move(node));
            }
            if (const auto* literal = expr.as_if<slang::ast::UnbasedUnsizedIntegerLiteral>())
            {
                node.kind = ExprNodeKind::Constant;
                node.literal = literal->getValue().toString();
                return addNode(expr, std::move(node));
            }
            if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>())
            {
                return lowerExpression(conversion->operand());
            }
            if (const auto* unary = expr.as_if<slang::ast::UnaryExpression>())
            {
                const auto opKind = mapUnaryOp(unary->op);
                if (!opKind)
                {
                    reportUnsupported(expr, "Unsupported unary operator");
                    return kInvalidPlanIndex;
                }
                ExprNodeId operand = lowerExpression(unary->operand());
                if (operand == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                node.kind = ExprNodeKind::Operation;
                node.op = *opKind;
                node.operands = {operand};
                return addNode(expr, std::move(node));
            }
            if (const auto* binary = expr.as_if<slang::ast::BinaryExpression>())
            {
                const auto opKind = mapBinaryOp(binary->op);
                if (!opKind)
                {
                    reportUnsupported(expr, "Unsupported binary operator");
                    return kInvalidPlanIndex;
                }
                ExprNodeId lhs = lowerExpression(binary->left());
                ExprNodeId rhs = lowerExpression(binary->right());
                if (lhs == kInvalidPlanIndex || rhs == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                node.kind = ExprNodeKind::Operation;
                node.op = *opKind;
                node.operands = {lhs, rhs};
                return addNode(expr, std::move(node));
            }
            if (const auto* cond = expr.as_if<slang::ast::ConditionalExpression>())
            {
                if (cond->conditions.empty())
                {
                    reportUnsupported(expr, "Conditional expression missing condition");
                    return kInvalidPlanIndex;
                }
                if (cond->conditions.size() > 1)
                {
                    reportUnsupported(expr, "Conditional expression with patterns unsupported");
                }
                const slang::ast::Expression& condExpr = *cond->conditions.front().expr;
                ExprNodeId condId = lowerExpression(condExpr);
                ExprNodeId lhs = lowerExpression(cond->left());
                ExprNodeId rhs = lowerExpression(cond->right());
                if (condId == kInvalidPlanIndex || lhs == kInvalidPlanIndex ||
                    rhs == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                node.kind = ExprNodeKind::Operation;
                node.op = grh::ir::OperationKind::kMux;
                node.operands = {condId, lhs, rhs};
                return addNode(expr, std::move(node));
            }
            if (const auto* concat = expr.as_if<slang::ast::ConcatenationExpression>())
            {
                std::vector<ExprNodeId> operands;
                operands.reserve(concat->operands().size());
                for (const slang::ast::Expression* operand : concat->operands())
                {
                    if (!operand)
                    {
                        continue;
                    }
                    ExprNodeId id = lowerExpression(*operand);
                    if (id == kInvalidPlanIndex)
                    {
                        return kInvalidPlanIndex;
                    }
                    operands.push_back(id);
                }
                node.kind = ExprNodeKind::Operation;
                node.op = grh::ir::OperationKind::kConcat;
                node.operands = std::move(operands);
                return addNode(expr, std::move(node));
            }
            if (const auto* repl = expr.as_if<slang::ast::ReplicationExpression>())
            {
                ExprNodeId count = lowerExpression(repl->count());
                ExprNodeId concat = lowerExpression(repl->concat());
                if (count == kInvalidPlanIndex || concat == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                node.kind = ExprNodeKind::Operation;
                node.op = grh::ir::OperationKind::kReplicate;
                node.operands = {count, concat};
                return addNode(expr, std::move(node));
            }
            if (const auto* select = expr.as_if<slang::ast::ElementSelectExpression>())
            {
                ExprNodeId value = lowerExpression(select->value());
                ExprNodeId index = lowerExpression(select->selector());
                if (value == kInvalidPlanIndex || index == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                node.kind = ExprNodeKind::Operation;
                node.op = grh::ir::OperationKind::kSliceDynamic;
                node.operands = {value, index};
                return addNode(expr, std::move(node));
            }
            if (const auto* range = expr.as_if<slang::ast::RangeSelectExpression>())
            {
                ExprNodeId value = lowerExpression(range->value());
                ExprNodeId left = lowerExpression(range->left());
                ExprNodeId right = lowerExpression(range->right());
                if (value == kInvalidPlanIndex || left == kInvalidPlanIndex ||
                    right == kInvalidPlanIndex)
                {
                    return kInvalidPlanIndex;
                }
                node.kind = ExprNodeKind::Operation;
                node.op = grh::ir::OperationKind::kSliceDynamic;
                node.operands = {value, left, right};
                return addNode(expr, std::move(node));
            }

            reportUnsupported(expr, "Unsupported connection expression");
            return kInvalidPlanIndex;
        }

    private:
        ExprNodeId addNode(const slang::ast::Expression& expr, ExprNode node)
        {
            if (node.widthHint == 0)
            {
                uint64_t width = expr.type->getBitstreamWidth();
                if (width == 0)
                {
                    if (auto effective = expr.getEffectiveWidth())
                    {
                        width = *effective;
                    }
                }
                if (width > 0)
                {
                    const uint64_t maxValue =
                        static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
                    node.widthHint = width > maxValue
                                         ? std::numeric_limits<int32_t>::max()
                                         : static_cast<int32_t>(width);
                }
            }
            const ExprNodeId id =
                static_cast<ExprNodeId>(state_.lowering_.values.size());
            state_.lowering_.values.push_back(std::move(node));
            state_.registerExprNode();
            lowered_.emplace(&expr, id);
            return id;
        }

        void reportUnsupported(const slang::ast::Expression& expr, std::string_view message)
        {
            if (!state_.context_.diagnostics)
            {
                return;
            }
            state_.context_.diagnostics->todo(expr.sourceRange.start(), std::string(message));
        }

        GraphAssemblyState& state_;
        std::unordered_map<const slang::ast::Expression*, ExprNodeId> lowered_;
    };

    grh::ir::ValueId emitConstant(const ExprNode& node)
    {
        const std::string name = "__const_" + std::to_string(nextConstId_++);
        grh::ir::SymbolId symbol = graph_.internSymbol(name);
        const int32_t width = normalizeWidth(node.widthHint);
        grh::ir::ValueId value = graph_.createValue(symbol, width, false);
        grh::ir::OperationId op = graph_.createOperation(grh::ir::OperationKind::kConstant,
                                                         grh::ir::SymbolId::invalid());
        graph_.addResult(op, value);
        graph_.setAttr(op, "constValue", node.literal);
        return value;
    }

    grh::ir::ValueId emitExpr(ExprNodeId id)
    {
        if (id == kInvalidPlanIndex || id >= lowering_.values.size())
        {
            return grh::ir::ValueId::invalid();
        }
        if (valueByExpr_[id].valid())
        {
            return valueByExpr_[id];
        }
        if (memoryReadIndexByExpr_[id] != kInvalidMemoryReadIndex)
        {
            grh::ir::ValueId value = emitMemoryRead(id);
            if (value.valid())
            {
                return value;
            }
        }
        const ExprNode& node = lowering_.values[id];
        if (node.kind == ExprNodeKind::Constant)
        {
            grh::ir::ValueId value = emitConstant(node);
            valueByExpr_[id] = value;
            return value;
        }
        if (node.kind == ExprNodeKind::Symbol)
        {
            grh::ir::ValueId value = valueForSymbol(node.symbol);
            if (!value.valid() && context_.diagnostics)
            {
                context_.diagnostics->warn(node.location,
                                           "Graph assembly missing symbol value");
            }
            valueByExpr_[id] = value;
            return value;
        }
        if (node.kind != ExprNodeKind::Operation)
        {
            return grh::ir::ValueId::invalid();
        }

        std::vector<grh::ir::ValueId> operands;
        operands.reserve(node.operands.size());
        for (ExprNodeId operandId : node.operands)
        {
            grh::ir::ValueId operandValue = emitExpr(operandId);
            if (!operandValue.valid())
            {
                return grh::ir::ValueId::invalid();
            }
            operands.push_back(operandValue);
        }

        if (node.op == grh::ir::OperationKind::kReplicate && operands.size() >= 2)
        {
            auto count = evalConstInt(plan_, lowering_, node.operands[0]);
            if (!count || *count <= 0)
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->warn(
                        node.location,
                        "Replication count must be constant in graph assembly");
                }
                return grh::ir::ValueId::invalid();
            }
            grh::ir::OperationId op =
                graph_.createOperation(grh::ir::OperationKind::kReplicate,
                                       grh::ir::SymbolId::invalid());
            graph_.addOperand(op, operands[1]);
            graph_.setAttr(op, "rep", static_cast<int64_t>(*count));
            grh::ir::SymbolId sym = symbolForPlan(node.tempSymbol);
            if (!sym.valid())
            {
                sym = graph_.internSymbol("__expr_" + std::to_string(nextTempId_++));
            }
            const int32_t width = normalizeWidth(node.widthHint);
            grh::ir::ValueId result = graph_.createValue(sym, width, false);
            graph_.addResult(op, result);
            valueByExpr_[id] = result;
            return result;
        }

        if (node.op == grh::ir::OperationKind::kSliceDynamic && operands.size() >= 2)
        {
            grh::ir::OperationId op =
                graph_.createOperation(grh::ir::OperationKind::kSliceDynamic,
                                       grh::ir::SymbolId::invalid());
            graph_.addOperand(op, operands[0]);
            graph_.addOperand(op, operands[1]);
            const int32_t width = normalizeWidth(node.widthHint);
            graph_.setAttr(op, "sliceWidth", static_cast<int64_t>(width));
            grh::ir::SymbolId sym = symbolForPlan(node.tempSymbol);
            if (!sym.valid())
            {
                sym = graph_.internSymbol("__expr_" + std::to_string(nextTempId_++));
            }
            grh::ir::ValueId result = graph_.createValue(sym, width, false);
            graph_.addResult(op, result);
            valueByExpr_[id] = result;
            return result;
        }

        grh::ir::OperationId op =
            graph_.createOperation(node.op, grh::ir::SymbolId::invalid());
        for (const auto& operand : operands)
        {
            graph_.addOperand(op, operand);
        }

        grh::ir::SymbolId sym = symbolForPlan(node.tempSymbol);
        if (!sym.valid())
        {
            sym = graph_.internSymbol("__expr_" + std::to_string(nextTempId_++));
        }
        int32_t width = node.widthHint;
        if (width <= 0 && !operands.empty())
        {
            width = graph_.getValue(operands.front()).width();
        }
        width = normalizeWidth(width);
        grh::ir::ValueId result = graph_.createValue(sym, width, false);
        graph_.addResult(op, result);
        valueByExpr_[id] = result;
        return result;
    }

    static std::string edgeText(EventEdge edge)
    {
        switch (edge)
        {
        case EventEdge::Posedge:
            return "posedge";
        case EventEdge::Negedge:
            return "negedge";
        default:
            return "posedge";
        }
    }

    void emitWriteBack()
    {
        for (const auto& entry : writeBack_.entries)
        {
            if (!entry.target.valid())
            {
                continue;
            }
            grh::ir::ValueId targetValue = valueForSymbol(entry.target);
            if (!targetValue.valid())
            {
                if (context_.diagnostics)
                {
                    context_.diagnostics->warn(entry.location,
                                               "Write-back target missing value");
                }
                continue;
            }
            grh::ir::ValueId updateCond = emitExpr(entry.updateCond);
            grh::ir::ValueId nextValue = emitExpr(entry.nextValue);
            if (!nextValue.valid())
            {
                continue;
            }

            if (entry.domain == ControlDomain::Combinational)
            {
                grh::ir::OperationId op =
                    graph_.createOperation(grh::ir::OperationKind::kAssign,
                                           grh::ir::SymbolId::invalid());
                graph_.addOperand(op, nextValue);
                graph_.addResult(op, targetValue);
                continue;
            }

            if (entry.domain == ControlDomain::Sequential)
            {
                if (!updateCond.valid())
                {
                    continue;
                }
                grh::ir::SymbolId sym = makeOpSymbol(entry.target, "register");
                grh::ir::OperationId op =
                    graph_.createOperation(grh::ir::OperationKind::kRegister, sym);
                graph_.addOperand(op, updateCond);
                graph_.addOperand(op, nextValue);
                std::vector<std::string> edges;
                edges.reserve(entry.eventEdges.size());
                for (ExprNodeId evtId : entry.eventOperands)
                {
                    grh::ir::ValueId evt = emitExpr(evtId);
                    if (!evt.valid())
                    {
                        continue;
                    }
                    graph_.addOperand(op, evt);
                }
                for (EventEdge edge : entry.eventEdges)
                {
                    edges.push_back(edgeText(edge));
                }
                graph_.setAttr(op, "eventEdge", std::move(edges));
                graph_.addResult(op, targetValue);
                continue;
            }

            if (entry.domain == ControlDomain::Latch)
            {
                if (!updateCond.valid())
                {
                    continue;
                }
                grh::ir::SymbolId sym = makeOpSymbol(entry.target, "latch");
                grh::ir::OperationId op =
                    graph_.createOperation(grh::ir::OperationKind::kLatch, sym);
                graph_.addOperand(op, updateCond);
                graph_.addOperand(op, nextValue);
                graph_.addResult(op, targetValue);
                continue;
            }

            if (context_.diagnostics)
            {
                context_.diagnostics->warn(entry.location,
                                           "Skipping unsupported control domain");
            }
        }
    }

    ConvertContext& context_;
    GraphAssembler& assembler_;
    grh::ir::Graph& graph_;
    const ModulePlan& plan_;
    LoweringPlan& lowering_;
    const WriteBackPlan& writeBack_;
    std::vector<grh::ir::SymbolId> symbolIds_;
    std::vector<grh::ir::ValueId> valueBySymbol_;
    std::vector<grh::ir::ValueId> valueByExpr_;
    std::vector<grh::ir::OperationId> memoryOpBySymbol_;
    std::vector<std::string> memorySymbolName_;
    std::vector<int32_t> memoryReadIndexByExpr_;
    ConnectionExprLowerer connectionLowerer_;
    uint32_t nextConstId_ = 0;
    uint32_t nextTempId_ = 0;
    uint32_t nextOpId_ = 0;
    uint32_t nextMemValueId_ = 0;
    uint32_t nextMemReadValueId_ = 0;

    grh::ir::SymbolId makeOpSymbol(PlanSymbolId id, std::string_view suffix)
    {
        std::string base;
        if (id.valid())
        {
            base = std::string(plan_.symbolTable.text(id));
        }
        if (base.empty())
        {
            base = "__op";
        }
        std::string candidate = base + "__" + std::string(suffix);
        while (graph_.symbols().contains(candidate))
        {
            candidate = base + "__" + std::string(suffix) + "_" +
                        std::to_string(nextOpId_++);
        }
        return graph_.internSymbol(candidate);
    }
};

const std::string& GraphAssembler::resolveGraphName(const PlanKey& key,
                                                    std::string_view moduleName)
{
    auto it = graphNames_.find(key);
    if (it != graphNames_.end())
    {
        return it->second;
    }

    std::string base = moduleName.empty() ? std::string("convert_graph") : std::string(moduleName);
    std::string candidate = base;
    if (!key.paramSignature.empty())
    {
        const std::size_t hash = std::hash<std::string>{}(key.paramSignature);
        candidate = base + "__p" + std::to_string(hash);
    }
    std::string finalName = candidate;
    std::size_t suffix = 0;
    while (reservedGraphNames_.find(finalName) != reservedGraphNames_.end() ||
           netlist_.findGraph(finalName))
    {
        finalName = candidate + "_" + std::to_string(++suffix);
    }

    auto [inserted, added] = graphNames_.emplace(key, std::move(finalName));
    reservedGraphNames_.insert(inserted->second);
    return inserted->second;
}

grh::ir::Graph& GraphAssembler::build(const PlanKey& key, const ModulePlan& plan,
                                      LoweringPlan& lowering, const WriteBackPlan& writeBack)
{
    std::string moduleName;
    if (plan.moduleSymbol.valid())
    {
        moduleName = std::string(plan.symbolTable.text(plan.moduleSymbol));
    }
    const std::string& finalSymbol = resolveGraphName(key, moduleName);
    grh::ir::Graph& graph = netlist_.createGraph(std::string(finalSymbol));
    GraphAssemblyState state(context_, *this, graph, plan, lowering, writeBack);
    state.build();
    return graph;
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
    TypeResolverPass typeResolver(context);
    RWAnalyzerPass rwAnalyzer(context);
    ExprLowererPass exprLowerer(context);
    StmtLowererPass stmtLowerer(context);
    WriteBackPass writeBack(context);
    MemoryPortLowererPass memoryPortLowerer(context);
    GraphAssembler graphAssembler(context, netlist);
    std::unordered_set<PlanKey, PlanKeyHash> topKeys;
    std::unordered_map<PlanKey, std::vector<std::string>, PlanKeyHash> topAliases;
    for (const slang::ast::InstanceSymbol* topInstance : root.topInstances)
    {
        if (!topInstance)
        {
            continue;
        }
        ParameterSnapshot params = snapshotParameters(topInstance->body, nullptr);
        PlanKey topKey;
        topKey.body = &topInstance->body;
        topKey.paramSignature = params.signature;
        topKeys.insert(topKey);
        std::vector<std::string>& aliases = topAliases[topKey];
        if (!topInstance->name.empty())
        {
            aliases.emplace_back(topInstance->name);
        }
        if (!topInstance->getDefinition().name.empty())
        {
            aliases.emplace_back(topInstance->getDefinition().name);
        }
        enqueuePlanKey(context, topInstance->body, std::move(params.signature));
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
        typeResolver.resolve(plan);
        rwAnalyzer.analyze(plan);
        LoweringPlan lowering = exprLowerer.lower(plan);
        stmtLowerer.lower(plan, lowering);
        WriteBackPlan writeBackPlan = writeBack.lower(plan, lowering);
        memoryPortLowerer.lower(plan, lowering);
        grh::ir::Graph& graph = graphAssembler.build(key, plan, lowering, writeBackPlan);
        if (topKeys.find(key) != topKeys.end())
        {
            netlist.markAsTop(graph.symbol());
            auto aliasIt = topAliases.find(key);
            if (aliasIt != topAliases.end())
            {
                for (const std::string& alias : aliasIt->second)
                {
                    if (alias.empty())
                    {
                        continue;
                    }
                    const grh::ir::Graph* existing = netlist.findGraph(alias);
                    if (existing && existing->symbol() != graph.symbol())
                    {
                        if (context.diagnostics)
                        {
                            context.diagnostics->warn(
                                slang::SourceLocation{},
                                "Skipping top alias conflict for " + alias);
                        }
                        continue;
                    }
                    netlist.registerGraphAlias(alias, graph);
                }
            }
        }
        planCache_.setLoweringPlan(key, std::move(lowering));
        planCache_.setWriteBackPlan(key, std::move(writeBackPlan));
        planCache_.storePlan(key, std::move(plan));
    }
    return netlist;
}

} // namespace wolf_sv_parser
