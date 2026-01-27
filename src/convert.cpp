#include "convert.hpp"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"

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
    std::unordered_set<uint64_t> rwKeys;
    std::unordered_set<uint64_t> memKeys;

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

    void recordRead(const slang::ast::ValueSymbol& symbol, ControlDomain domain)
    {
        recordAccess(resolveSignal(symbol), domain, false);
    }

    void recordWrite(const slang::ast::ValueSymbol& symbol, ControlDomain domain)
    {
        recordAccess(resolveSignal(symbol), domain, true);
    }

private:
    void recordAccess(SignalId id, ControlDomain domain, bool isWrite)
    {
        if (id == kInvalidPlanIndex)
        {
            return;
        }
        const uint64_t key = encodeRWKey(id, domain, isWrite);
        if (rwKeys.insert(key).second)
        {
            plan.rwOps.push_back(RWOp{id, domain, isWrite});
        }

        if (plan.signals[id].memoryRows > 0)
        {
            if (isWrite)
            {
                recordMemoryPort(id, false, true, domain);
            }
            else
            {
                recordMemoryPort(id, true, false, domain);
            }
        }
    }

    void recordMemoryPort(SignalId id, bool isRead, bool isWrite, ControlDomain domain)
    {
        const bool isSync = domain == ControlDomain::Sequential;
        const bool isMasked = false;
        const bool hasReset = false;
        const uint64_t key = encodeMemKey(id, isRead, isWrite, isMasked, isSync, hasReset);
        if (!memKeys.insert(key).second)
        {
            return;
        }
        MemoryPortInfo info;
        info.memory = id;
        info.isRead = isRead;
        info.isWrite = isWrite;
        info.isMasked = isMasked;
        info.isSync = isSync;
        info.hasReset = hasReset;
        plan.memPorts.push_back(std::move(info));
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
        recordSymbol(expr.symbol);
    }

    void handle(const slang::ast::HierarchicalValueExpression& expr)
    {
        recordSymbol(expr.symbol);
    }

private:
    void recordSymbol(const slang::ast::ValueSymbol& symbol)
    {
        if (inLValue_)
        {
            state_.recordWrite(symbol, domain_);
        }
        else
        {
            state_.recordRead(symbol, domain_);
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
        planCache_.storePlan(key, std::move(plan));
    }
    return netlist;
}

} // namespace wolf_sv_parser
