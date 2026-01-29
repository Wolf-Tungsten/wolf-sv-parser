#include "convert.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Statement.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
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
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/text/SourceManager.h"

#include <algorithm>
#include <cctype>
#include <limits>
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
    std::vector<int32_t> unpackedDims;
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
            info.unpackedDims.push_back(
                clampDim(extent, origin, diagnostics, "Unpacked array dimension"));

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

        ExprNode node;
        node.location = expr.sourceRange.start();

        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(named->symbol.name);
            if (!node.symbol.valid())
            {
                reportUnsupported(expr, "Unknown symbol in expression");
            }
            return addNode(expr, std::move(node));
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(hier->symbol.name);
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

    static constexpr uint32_t kMaxLoopIterations = 4096;

    ModulePlan& plan;
    ConvertDiagnostics* diagnostics;
    LoweringPlan& lowering;
    std::unordered_map<const slang::ast::Expression*, ExprNodeId> lowered;
    std::unordered_map<const slang::ast::AssignmentExpression*, ExprNodeId> assignmentRoots;
    uint32_t nextTemp = 0;
    std::size_t nextRoot = 0;
    ControlDomain domain = ControlDomain::Unknown;
    std::vector<ExprNodeId> guardStack;

    StmtLowererState(ModulePlan& plan, ConvertDiagnostics* diagnostics, LoweringPlan& lowering)
        : plan(plan), diagnostics(diagnostics), lowering(lowering)
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
            visitStatement(timed->stmt);
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
        if (const auto* exprStmt = stmt.as_if<slang::ast::ExpressionStatement>())
        {
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
            if (!tryUnrollFor(*forLoop))
            {
                reportUnsupported(stmt, "For-loop lowering uses unconditional guards");
                visitStatement(forLoop->body);
            }
            return;
        }
        if (const auto* repeatLoop = stmt.as_if<slang::ast::RepeatLoopStatement>())
        {
            scanExpression(repeatLoop->count);
            if (!tryUnrollRepeat(*repeatLoop))
            {
                reportUnsupported(stmt, "Repeat-loop lowering uses unconditional guards");
                visitStatement(repeatLoop->body);
            }
            return;
        }
        if (const auto* whileLoop = stmt.as_if<slang::ast::WhileLoopStatement>())
        {
            reportError(stmt, "While-loop lowering is unsupported");
            return;
        }
        if (const auto* doWhileLoop = stmt.as_if<slang::ast::DoWhileLoopStatement>())
        {
            reportError(stmt, "Do-while lowering is unsupported");
            return;
        }
        if (const auto* foreverLoop = stmt.as_if<slang::ast::ForeverLoopStatement>())
        {
            reportError(stmt, "Forever-loop lowering is unsupported");
            return;
        }
        if (const auto* foreachLoop = stmt.as_if<slang::ast::ForeachLoopStatement>())
        {
            scanExpression(foreachLoop->arrayRef);
            if (!tryUnrollForeach(*foreachLoop))
            {
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

        ExprNodeId baseGuard = currentGuard();
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

        ExprNodeId baseGuard = currentGuard();
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
        bool isPartial = false;
        PlanSymbolId target = resolveLValueSymbol(expr.left(), isPartial);
        if (!target.valid())
        {
            reportUnsupported(expr, "Unsupported LHS in assignment");
            return;
        }
        if (isPartial)
        {
            reportUnsupported(expr, "Partial assignment lowering not implemented");
            return;
        }
        if (value == kInvalidPlanIndex)
        {
            return;
        }

        WriteIntent intent;
        intent.target = target;
        intent.value = value;
        intent.guard = currentGuard();
        intent.domain = domain;
        intent.isNonBlocking = expr.isNonBlocking();
        intent.location = expr.sourceRange.start();
        lowering.writes.push_back(std::move(intent));
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

private:
    ExprNodeId currentGuard() const
    {
        if (guardStack.empty())
        {
            return kInvalidPlanIndex;
        }
        return guardStack.back();
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
        if (containsLoopControl(stmt.body))
        {
            return false;
        }

        slang::ast::EvalContext ctx(*plan.body);
        std::optional<int64_t> count = evalConstantInt(stmt.count, ctx);
        if (!count || *count < 0)
        {
            return false;
        }
        if (static_cast<uint64_t>(*count) > kMaxLoopIterations)
        {
            return false;
        }

        for (int64_t i = 0; i < *count; ++i)
        {
            visitStatement(stmt.body);
        }
        return true;
    }

    bool tryUnrollFor(const slang::ast::ForLoopStatement& stmt)
    {
        if (!stmt.stopExpr || containsLoopControl(stmt.body))
        {
            return false;
        }

        slang::ast::EvalContext ctx(*plan.body);
        if (!prepareForLoopState(stmt, ctx))
        {
            return false;
        }

        uint32_t iterations = 0;
        while (iterations < kMaxLoopIterations)
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

    bool tryUnrollForeach(const slang::ast::ForeachLoopStatement& stmt)
    {
        if (containsLoopControl(stmt.body) || stmt.loopDims.empty())
        {
            return false;
        }

        uint64_t total = 1;
        for (const auto& dim : stmt.loopDims)
        {
            if (!dim.range)
            {
                return false;
            }
            const uint64_t width = dim.range->fullWidth();
            if (width == 0)
            {
                return false;
            }
            if (total > kMaxLoopIterations / width)
            {
                return false;
            }
            total *= width;
        }

        if (total > kMaxLoopIterations)
        {
            return false;
        }

        for (uint64_t i = 0; i < total; ++i)
        {
            visitStatement(stmt.body);
        }
        return true;
    }

    bool containsLoopControl(const slang::ast::Statement& stmt) const
    {
        struct ControlVisitor : public slang::ast::ASTVisitor<ControlVisitor, false, true> {
            bool found = false;

            void handle(const slang::ast::BreakStatement&)
            {
                found = true;
            }

            void handle(const slang::ast::ContinueStatement&)
            {
                found = true;
            }
        };

        ControlVisitor visitor;
        stmt.visit(visitor);
        return visitor.found;
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

        ExprNode node;
        node.location = expr.sourceRange.start();

        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(named->symbol.name);
            if (!node.symbol.valid())
            {
                reportUnsupported(expr, "Unknown symbol in expression");
            }
            return addNode(&expr, std::move(node));
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            node.kind = ExprNodeKind::Symbol;
            node.symbol = plan.symbolTable.lookup(hier->symbol.name);
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
            if (conversion->isImplicit())
            {
                return lowerExpression(conversion->operand());
            }
            reportUnsupported(expr, "Unsupported explicit conversion in expression");
            return kInvalidPlanIndex;
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

    PlanSymbolId resolveLValueSymbol(const slang::ast::Expression& expr, bool& isPartial)
    {
        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>())
        {
            return plan.symbolTable.lookup(named->symbol.name);
        }
        if (const auto* hier = expr.as_if<slang::ast::HierarchicalValueExpression>())
        {
            return plan.symbolTable.lookup(hier->symbol.name);
        }
        if (const auto* select = expr.as_if<slang::ast::ElementSelectExpression>())
        {
            isPartial = true;
            return resolveLValueSymbol(select->value(), isPartial);
        }
        if (const auto* range = expr.as_if<slang::ast::RangeSelectExpression>())
        {
            isPartial = true;
            return resolveLValueSymbol(range->value(), isPartial);
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
};

void lowerStmtGenerateBlock(const slang::ast::GenerateBlockSymbol& block,
                            StmtLowererState& state);
void lowerStmtGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                                 StmtLowererState& state);

void lowerStmtProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                              StmtLowererState& state)
{
    const ControlDomain saved = state.domain;
    state.domain = classifyProceduralBlock(block);
    state.visitStatement(block.getBody());
    state.domain = saved;
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

    StmtLowererState state(plan, context_.diagnostics, lowering);
    for (const slang::ast::Symbol& member : plan.body->members())
    {
        lowerStmtMemberSymbol(member, state);
    }
}

grh::ir::Graph& GraphAssembler::build(const ModulePlan& plan)
{
    std::string symbol;
    if (plan.moduleSymbol.valid())
    {
        symbol = std::string(plan.symbolTable.text(plan.moduleSymbol));
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
    TypeResolverPass typeResolver(context);
    RWAnalyzerPass rwAnalyzer(context);
    ExprLowererPass exprLowerer(context);
    StmtLowererPass stmtLowerer(context);
    for (const slang::ast::InstanceSymbol* topInstance : root.topInstances)
    {
        if (!topInstance)
        {
            continue;
        }
        ParameterSnapshot params = snapshotParameters(topInstance->body, nullptr);
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
        planCache_.setLoweringPlan(key, std::move(lowering));
        planCache_.storePlan(key, std::move(plan));
    }
    return netlist;
}

} // namespace wolf_sv_parser
