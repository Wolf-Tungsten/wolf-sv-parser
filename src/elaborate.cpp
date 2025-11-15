#include "elaborate.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/numeric/ConstantValue.h"
#include "slang/numeric/SVInt.h"

namespace wolf_sv {

namespace {

std::size_t nextConverterInstanceId() {
    static std::atomic<std::size_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

std::size_t nextMemoryHelperId() {
    static std::atomic<std::size_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

std::string sanitizeForGraphName(std::string_view text, bool allowLeadingDigit = false) {
    std::string result;
    result.reserve(text.size());
    bool lastUnderscore = false;

    for (unsigned char raw : text) {
        char ch = static_cast<char>(raw);
        if (std::isalnum(raw) || ch == '_' || ch == '$') {
            result.push_back(ch);
            lastUnderscore = false;
            continue;
        }

        if (lastUnderscore) {
            continue;
        }

        result.push_back('_');
        lastUnderscore = true;
    }

    if (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    if (!allowLeadingDigit && !result.empty() &&
        std::isdigit(static_cast<unsigned char>(result.front()))) {
        result.insert(result.begin(), '_');
    }

    return result;
}

std::string parameterValueToString(const slang::ConstantValue& value) {
    if (value.bad()) {
        return "bad";
    }

    std::string sanitized = sanitizeForGraphName(value.toString(), /* allowLeadingDigit */ true);
    if (sanitized.empty()) {
        sanitized = "value";
    }
    return sanitized;
}

std::string typeParameterToString(const slang::ast::TypeParameterSymbol& param) {
    return sanitizeForGraphName(param.getTypeAlias().toString());
}

std::string deriveParameterSuffix(const slang::ast::InstanceBodySymbol& body) {
    std::vector<std::string> parts;
    for (const slang::ast::ParameterSymbolBase* paramBase : body.getParameters()) {
        if (!paramBase) {
            continue;
        }

        std::string paramName = sanitizeForGraphName(paramBase->symbol.name);
        if (paramName.empty()) {
            continue;
        }

        std::string paramValue;
        if (const auto* valueParam = paramBase->symbol.as_if<slang::ast::ParameterSymbol>()) {
            paramValue = parameterValueToString(valueParam->getValue());
        }
        else if (const auto* typeParam =
                     paramBase->symbol.as_if<slang::ast::TypeParameterSymbol>()) {
            paramValue = typeParameterToString(*typeParam);
        }

        if (paramValue.empty()) {
            continue;
        }

        parts.emplace_back(std::move(paramName) + "_" + std::move(paramValue));
    }

    if (parts.empty()) {
        return {};
    }

    std::string suffix;
    suffix.reserve(16 * parts.size());
    suffix.push_back('$');
    bool first = true;
    for (std::string& part : parts) {
        if (!first) {
            suffix.push_back('$');
        }
        first = false;
        suffix.append(part);
    }
    return suffix;
}

std::string deriveSymbolPath(const slang::ast::Symbol& symbol) {
    std::string path = symbol.getHierarchicalPath();
    if (!path.empty()) {
        return path;
    }
    if (!symbol.name.empty()) {
        return std::string(symbol.name);
    }
    return "<anonymous>";
}

int64_t clampBitWidth(uint64_t width, ElaborateDiagnostics* diagnostics,
                      const slang::ast::Symbol& symbol) {
    if (width == 0) {
        if (diagnostics) {
            diagnostics->nyi(symbol, "Port has indeterminate width; treating as 1-bit placeholder");
        }
        return 1;
    }

    constexpr auto maxValue = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (width > maxValue) {
        if (diagnostics) {
            diagnostics->nyi(symbol,
                             "Port width exceeds GRH limit; clamping to int64_t::max width");
        }
        return std::numeric_limits<int64_t>::max();
    }
    return static_cast<int64_t>(width);
}

void handleUnsupportedPort(const slang::ast::Symbol& symbol, std::string_view description,
                            ElaborateDiagnostics* diagnostics) {
    if (diagnostics) {
        std::string message = "Unsupported port form: ";
        message.append(description);
        diagnostics->nyi(symbol, message);
    }
}

struct TypeHelper {
    struct Field {
        std::string path;
        int64_t msb = 0;
        int64_t lsb = 0;
        bool isSigned = false;
    };

    struct Info {
        int64_t width = 0;
        bool isSigned = false;
        std::vector<Field> fields;
    };

    static Info analyze(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                        ElaborateDiagnostics* diagnostics) {
        Info info{};
        const uint64_t bitstreamWidth = type.getBitstreamWidth();
        info.width = clampBitWidth(bitstreamWidth, diagnostics, origin);
        info.isSigned = type.isSigned();
        if (info.width <= 0) {
            info.width = 1;
        }

        info.fields.reserve(4);
        const int64_t msb = info.width - 1;
        flatten(type, origin, origin.name.empty() ? std::string() : std::string(origin.name), msb,
                0, info.fields, diagnostics);
        if (info.fields.empty()) {
            std::string label = origin.name.empty() ? std::string("<value>")
                                                    : std::string(origin.name);
            info.fields.push_back(Field{std::move(label), msb, 0, info.isSigned});
        }
        return info;
    }

private:
    static void flatten(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                        const std::string& path, int64_t msb, int64_t lsb,
                        std::vector<Field>& out, ElaborateDiagnostics* diagnostics) {
        if (msb < lsb) {
            return;
        }

        const slang::ast::Type& canonical = type.getCanonicalType();
        if (canonical.kind == slang::ast::SymbolKind::PackedArrayType) {
            const auto& packed = canonical.as<slang::ast::PackedArrayType>();
            const int32_t step = packed.range.left >= packed.range.right ? -1 : 1;
            int64_t currentMsb = msb;
            for (int32_t idx = packed.range.left;; idx += step) {
                const int64_t elementWidth =
                    static_cast<int64_t>(packed.elementType.getBitstreamWidth());
                if (elementWidth <= 0) {
                    if (diagnostics) {
                        diagnostics->nyi(origin,
                                         "Encountered zero-width element in packed array flatten");
                    }
                    break;
                }

                const int64_t elementLsb = currentMsb - elementWidth + 1;
                std::string childPath = path;
                childPath.append("[");
                childPath.append(std::to_string(idx));
                childPath.append("]");

                flatten(packed.elementType, origin, childPath, currentMsb, elementLsb, out,
                        diagnostics);

                currentMsb = elementLsb - 1;
                if (idx == packed.range.right || currentMsb < lsb) {
                    break;
                }
            }
            return;
        }

        if (canonical.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
            const auto& unpacked = canonical.as<slang::ast::FixedSizeUnpackedArrayType>();
            int64_t currentMsb = msb;
            for (int32_t idx = unpacked.range.lower(); idx <= unpacked.range.upper(); ++idx) {
                const int64_t elementWidth =
                    static_cast<int64_t>(unpacked.elementType.getBitstreamWidth());
                if (elementWidth <= 0) {
                    if (diagnostics) {
                        diagnostics->nyi(origin,
                                         "Encountered zero-width element in unpacked array flatten");
                    }
                    break;
                }

                const int64_t elementLsb = currentMsb - elementWidth + 1;
                std::string childPath = path;
                childPath.append("[");
                childPath.append(std::to_string(idx));
                childPath.append("]");

                flatten(unpacked.elementType, origin, childPath, currentMsb, elementLsb, out,
                        diagnostics);

                currentMsb = elementLsb - 1;
                if (currentMsb < lsb) {
                    break;
                }
            }
            return;
        }

        if (canonical.kind == slang::ast::SymbolKind::PackedStructType ||
            canonical.kind == slang::ast::SymbolKind::UnpackedStructType) {
            const auto& structScope = canonical.as<slang::ast::Scope>();
            int64_t currentMsb = msb;
            for (const auto& field : structScope.membersOfType<slang::ast::FieldSymbol>()) {
                const slang::ast::Type& fieldType = field.getType();
                const int64_t fieldWidth = static_cast<int64_t>(fieldType.getBitstreamWidth());
                if (fieldWidth <= 0) {
                    if (diagnostics) {
                        diagnostics->nyi(origin,
                                         "Encountered zero-width struct field while flattening");
                    }
                    continue;
                }

                const int64_t fieldLsb = currentMsb - fieldWidth + 1;
                std::string childPath = path;
                if (!childPath.empty()) {
                    childPath.push_back('.');
                }
                childPath.append(field.name.empty() ? "<anon_field>"
                                                    : std::string(field.name));

                flatten(fieldType, field, childPath, currentMsb, fieldLsb, out, diagnostics);

                currentMsb = fieldLsb - 1;
                if (currentMsb < lsb) {
                    break;
                }
            }
            return;
        }

        // Treat all other kinds as leaf scalars.
        if (path.empty()) {
            std::string label = origin.name.empty() ? std::string("<value>")
                                                    : std::string(origin.name);
            out.push_back(Field{std::move(label), msb, lsb, canonical.isSigned()});
        }
        else {
            out.push_back(Field{path, msb, lsb, canonical.isSigned()});
        }
    }
};

enum class MemoDriverKind : uint8_t {
    None = 0,
    Net = 1 << 0,
    Reg = 1 << 1
};

constexpr MemoDriverKind operator|(MemoDriverKind lhs, MemoDriverKind rhs) {
    return static_cast<MemoDriverKind>(static_cast<uint8_t>(lhs) |
                                       static_cast<uint8_t>(rhs));
}

constexpr MemoDriverKind operator&(MemoDriverKind lhs, MemoDriverKind rhs) {
    return static_cast<MemoDriverKind>(static_cast<uint8_t>(lhs) &
                                       static_cast<uint8_t>(rhs));
}

inline MemoDriverKind& operator|=(MemoDriverKind& lhs, MemoDriverKind rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline bool hasDriver(MemoDriverKind value, MemoDriverKind flag) {
    return (value & flag) == flag && flag != MemoDriverKind::None;
}

const slang::ast::ValueSymbol*
resolveAssignedSymbol(const slang::ast::Expression& expr) {
    const slang::ast::Expression* current = &expr;
    while (current) {
        if (const auto* named = current->as_if<slang::ast::NamedValueExpression>()) {
            return &named->symbol;
        }
        if (const auto* hier = current->as_if<slang::ast::HierarchicalValueExpression>()) {
            return &hier->symbol;
        }
        if (const auto* select = current->as_if<slang::ast::ElementSelectExpression>()) {
            current = &select->value();
            continue;
        }
        if (const auto* range = current->as_if<slang::ast::RangeSelectExpression>()) {
            current = &range->value();
            continue;
        }
        if (const auto* member = current->as_if<slang::ast::MemberAccessExpression>()) {
            current = &member->value();
            continue;
        }
        if (const auto* conversion = current->as_if<slang::ast::ConversionExpression>()) {
            if (!conversion->isImplicit()) {
                break;
            }
            current = &conversion->operand();
            continue;
        }
        break;
    }
    return nullptr;
}

void collectAssignedSymbols(const slang::ast::Expression& expr,
                            slang::function_ref<void(const slang::ast::ValueSymbol&)> callback) {
    if (const auto* concat = expr.as_if<slang::ast::ConcatenationExpression>()) {
        for (const slang::ast::Expression* operand : concat->operands()) {
            if (operand) {
                collectAssignedSymbols(*operand, callback);
            }
        }
        return;
    }

    if (const auto* replication = expr.as_if<slang::ast::ReplicationExpression>()) {
        collectAssignedSymbols(replication->concat(), callback);
        return;
    }

    if (const auto* streaming = expr.as_if<slang::ast::StreamingConcatenationExpression>()) {
        for (const auto& stream : streaming->streams()) {
            collectAssignedSymbols(*stream.operand, callback);
        }
        return;
    }

    if (const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(expr)) {
        callback(*symbol);
    }
}

class AssignmentCollector : public slang::ast::ASTVisitor<AssignmentCollector, true, false> {
public:
    explicit AssignmentCollector(
        slang::function_ref<void(const slang::ast::Expression&)> onAssignment) :
        onAssignment_(onAssignment) {}

    void handle(const slang::ast::ExpressionStatement& stmt) {
        handleExpression(stmt.expr);
        visitDefault(stmt);
    }

    void handle(const slang::ast::ProceduralAssignStatement& stmt) {
        handleExpression(stmt.assignment);
        visitDefault(stmt);
    }

private:
    void handleExpression(const slang::ast::Expression& expr) {
        if (const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>()) {
            if (!assignment->isLValueArg()) {
                onAssignment_(assignment->left());
            }
        }
    }

    slang::function_ref<void(const slang::ast::Expression&)> onAssignment_;
};

const slang::ast::TimingControl* findTimingControl(const slang::ast::Statement& stmt) {
    if (const auto* timed = stmt.as_if<slang::ast::TimedStatement>()) {
        return &timed->timing;
    }

    if (const auto* block = stmt.as_if<slang::ast::BlockStatement>()) {
        return findTimingControl(block->body);
    }

    if (const auto* list = stmt.as_if<slang::ast::StatementList>()) {
        for (const slang::ast::Statement* child : list->list) {
            if (!child) {
                continue;
            }
            if (const auto* timing = findTimingControl(*child)) {
                return timing;
            }
        }
    }
    return nullptr;
}

bool containsEdgeSensitiveEvent(const slang::ast::TimingControl& timing) {
    using slang::ast::TimingControlKind;
    switch (timing.kind) {
    case TimingControlKind::SignalEvent: {
        const auto& signal = timing.as<slang::ast::SignalEventControl>();
        return signal.edge == slang::ast::EdgeKind::PosEdge ||
               signal.edge == slang::ast::EdgeKind::NegEdge ||
               signal.edge == slang::ast::EdgeKind::BothEdges;
    }
    case TimingControlKind::EventList: {
        const auto& list = timing.as<slang::ast::EventListControl>();
        for (const slang::ast::TimingControl* ctrl : list.events) {
            if (ctrl && containsEdgeSensitiveEvent(*ctrl)) {
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

bool isLevelSensitiveEventList(const slang::ast::TimingControl& timing) {
    using slang::ast::TimingControlKind;
    switch (timing.kind) {
    case TimingControlKind::SignalEvent: {
        const auto& signal = timing.as<slang::ast::SignalEventControl>();
        return signal.edge == slang::ast::EdgeKind::None;
    }
    case TimingControlKind::EventList: {
        const auto& list = timing.as<slang::ast::EventListControl>();
        bool hasSignal = false;
        for (const slang::ast::TimingControl* ctrl : list.events) {
            if (!ctrl) {
                continue;
            }
            if (!isLevelSensitiveEventList(*ctrl)) {
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

struct MemoryLayoutInfo {
    int64_t rowWidth = 0;
    int64_t rowCount = 0;
    bool isSigned = false;
};

std::optional<MemoryLayoutInfo>
deriveMemoryLayout(const slang::ast::Type& type, const slang::ast::ValueSymbol& symbol,
                   ElaborateDiagnostics* diagnostics) {
    const slang::ast::Type* current = &type;
    bool hasUnpacked = false;
    int64_t rows = 1;

    while (true) {
        const slang::ast::Type& canonical = current->getCanonicalType();
        if (canonical.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
            hasUnpacked = true;
            const auto& unpacked = canonical.as<slang::ast::FixedSizeUnpackedArrayType>();
            const uint64_t extent = unpacked.range.fullWidth();
            if (extent == 0) {
                if (diagnostics) {
                    diagnostics->nyi(symbol,
                                     "Unpacked array dimension must have positive extent");
                }
                return std::nullopt;
            }
            const uint64_t maxValue = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
            uint64_t total = static_cast<uint64_t>(rows);
            total *= extent;
            if (total > maxValue) {
                if (diagnostics) {
                    diagnostics->nyi(symbol,
                                     "Memory row count exceeds GRH limit; clamping to int64_t::max");
                }
                rows = std::numeric_limits<int64_t>::max();
            }
            else {
                rows = static_cast<int64_t>(total);
            }
            current = &unpacked.elementType;
            continue;
        }
        break;
    }

    if (!hasUnpacked) {
        return std::nullopt;
    }

    TypeHelper::Info info = TypeHelper::analyze(*current, symbol, diagnostics);
    MemoryLayoutInfo layout;
    layout.rowWidth = info.width > 0 ? info.width : 1;
    layout.rowCount = rows > 0 ? rows : 1;
    layout.isSigned = info.isSigned;
    return layout;
}

const slang::ast::SignalEventControl*
findEdgeEventControl(const slang::ast::TimingControl& timing) {
    using slang::ast::TimingControlKind;
    switch (timing.kind) {
    case TimingControlKind::SignalEvent:
        return &timing.as<slang::ast::SignalEventControl>();
    case TimingControlKind::EventList: {
        const auto& list = timing.as<slang::ast::EventListControl>();
        for (const slang::ast::TimingControl* entry : list.events) {
            if (!entry) {
                continue;
            }
            if (const auto* edge = findEdgeEventControl(*entry)) {
                return edge;
            }
        }
        return nullptr;
    }
    case TimingControlKind::RepeatedEvent:
        return findEdgeEventControl(timing.as<slang::ast::RepeatedEventControl>().event);
    default:
        return nullptr;
    }
}

std::optional<std::string>
deriveClockPolarity(const slang::ast::ProceduralBlockSymbol& block,
                    const slang::ast::ValueSymbol& symbol, ElaborateDiagnostics* diagnostics) {
    const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
    if (!timing) {
        if (diagnostics) {
            diagnostics->nyi(symbol,
                             "Sequential driver lacks timing control; unable to determine clock edge");
        }
        return std::nullopt;
    }

    const slang::ast::SignalEventControl* event = findEdgeEventControl(*timing);
    if (!event) {
        if (diagnostics) {
            diagnostics->nyi(symbol,
                             "Sequential driver timing control is not an edge-sensitive event");
        }
        return std::nullopt;
    }

    using slang::ast::EdgeKind;
    switch (event->edge) {
    case EdgeKind::PosEdge:
        return std::string("posedge");
    case EdgeKind::NegEdge:
        return std::string("negedge");
    case EdgeKind::BothEdges:
    case EdgeKind::None:
    default:
        if (diagnostics) {
            diagnostics->nyi(symbol,
                             "Sequential driver uses unsupported edge kind (dual-edge / level)");
        }
        return std::nullopt;
    }
}

void collectSignalEvents(const slang::ast::TimingControl& timing,
                         std::vector<const slang::ast::SignalEventControl*>& out) {
    using slang::ast::TimingControlKind;
    switch (timing.kind) {
    case TimingControlKind::SignalEvent:
        out.push_back(&timing.as<slang::ast::SignalEventControl>());
        break;
    case TimingControlKind::EventList: {
        const auto& list = timing.as<slang::ast::EventListControl>();
        for (const slang::ast::TimingControl* ctrl : list.events) {
            if (ctrl) {
                collectSignalEvents(*ctrl, out);
            }
        }
        break;
    }
    case TimingControlKind::RepeatedEvent:
        collectSignalEvents(timing.as<slang::ast::RepeatedEventControl>().event, out);
        break;
    default:
        break;
    }
}

struct AsyncResetEvent {
    const slang::ast::Expression* expr = nullptr;
    slang::ast::EdgeKind edge = {};
};

std::optional<AsyncResetEvent>
detectAsyncResetEvent(const slang::ast::ProceduralBlockSymbol& block,
                      ElaborateDiagnostics* diagnostics) {
    const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
    if (!timing) {
        return std::nullopt;
    }
    std::vector<const slang::ast::SignalEventControl*> events;
    collectSignalEvents(*timing, events);
    if (events.size() <= 1) {
        return std::nullopt;
    }
    if (events.size() > 2) {
        if (diagnostics) {
            diagnostics->nyi(block, "Multiple asynchronous reset events are not supported yet");
        }
        return std::nullopt;
    }
    const auto* resetEvent = events[1];
    if (!resetEvent) {
        return std::nullopt;
    }
    return AsyncResetEvent{.expr = &resetEvent->expr, .edge = resetEvent->edge};
}

const slang::ast::ValueSymbol* extractResetSymbol(const slang::ast::Expression& expr,
                                                  bool& activeHigh) {
    if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>()) {
        if (const auto* sym = named->symbol.as_if<slang::ast::ValueSymbol>()) {
            return sym;
        }
        return nullptr;
    }
    if (const auto* unary = expr.as_if<slang::ast::UnaryExpression>()) {
        using slang::ast::UnaryOperator;
        if (unary->op == UnaryOperator::LogicalNot || unary->op == UnaryOperator::BitwiseNot) {
            activeHigh = !activeHigh;
            return extractResetSymbol(unary->operand(), activeHigh);
        }
    }
    return nullptr;
}

struct SyncResetInfo {
    const slang::ast::ValueSymbol* symbol = nullptr;
    bool activeHigh = true;
};

const slang::ast::ConditionalStatement* findConditional(const slang::ast::Statement& stmt) {
    switch (stmt.kind) {
    case slang::ast::StatementKind::Conditional:
        return &stmt.as<slang::ast::ConditionalStatement>();
    case slang::ast::StatementKind::Block:
        return findConditional(stmt.as<slang::ast::BlockStatement>().body);
    case slang::ast::StatementKind::List: {
        const auto& list = stmt.as<slang::ast::StatementList>();
        for (const slang::ast::Statement* child : list.list) {
            if (child) {
                if (const auto* result = findConditional(*child)) {
                    return result;
                }
            }
        }
        break;
    }
    case slang::ast::StatementKind::Timed:
        return findConditional(stmt.as<slang::ast::TimedStatement>().stmt);
    default:
        break;
    }
    return nullptr;
}

std::optional<SyncResetInfo> detectSyncReset(const slang::ast::Statement& stmt) {
    const auto* conditional = findConditional(stmt);
    if (!conditional || conditional->conditions.size() != 1 || !conditional->ifFalse) {
        return std::nullopt;
    }

    const slang::ast::Expression* condExpr = conditional->conditions.front().expr;
    if (!condExpr) {
        return std::nullopt;
    }

    bool activeHigh = true;
    const slang::ast::ValueSymbol* symbol = extractResetSymbol(*condExpr, activeHigh);
    if (!symbol) {
        return std::nullopt;
    }
    return SyncResetInfo{symbol, activeHigh};
}

std::optional<SyncResetInfo> detectSyncReset(const slang::ast::ProceduralBlockSymbol& block) {
    return detectSyncReset(block.getBody());
}

MemoDriverKind classifyProceduralBlock(const slang::ast::ProceduralBlockSymbol& block) {
    using slang::ast::ProceduralBlockKind;
    switch (block.procedureKind) {
    case ProceduralBlockKind::AlwaysComb:
        return MemoDriverKind::Net;
    case ProceduralBlockKind::AlwaysLatch:
    case ProceduralBlockKind::AlwaysFF:
    case ProceduralBlockKind::Initial:
    case ProceduralBlockKind::Final:
        return MemoDriverKind::Reg;
    case ProceduralBlockKind::Always: {
        const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
        if (!timing) {
            return MemoDriverKind::Net;
        }
        if (timing->kind == slang::ast::TimingControlKind::ImplicitEvent) {
            return MemoDriverKind::Net;
        }
        return containsEdgeSensitiveEvent(*timing) ? MemoDriverKind::Reg : MemoDriverKind::Net;
    }
    default:
        return MemoDriverKind::None;
    }
}

bool isCombProceduralBlock(const slang::ast::ProceduralBlockSymbol& block) {
    using slang::ast::ProceduralBlockKind;
    if (block.procedureKind == ProceduralBlockKind::AlwaysComb) {
        return true;
    }
    if (block.procedureKind != ProceduralBlockKind::Always) {
        return false;
    }
    const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
    if (!timing) {
        return true;
    }
    if (timing->kind == slang::ast::TimingControlKind::ImplicitEvent) {
        return true;
    }
    return isLevelSensitiveEventList(*timing);
}

bool isSeqProceduralBlock(const slang::ast::ProceduralBlockSymbol& block) {
    using slang::ast::ProceduralBlockKind;
    if (block.procedureKind == ProceduralBlockKind::AlwaysFF) {
        return true;
    }
    if (block.procedureKind != ProceduralBlockKind::Always) {
        return false;
    }
    const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
    if (!timing) {
        return false;
    }
    return containsEdgeSensitiveEvent(*timing);
}
} // namespace

LHSConverter::LHSConverter(LHSConverter::Context context) :
    graph_(context.graph),
    netMemo_(context.netMemo),
    regMemo_(context.regMemo),
    origin_(context.origin),
    diagnostics_(context.diagnostics) {}

bool LHSConverter::lower(const slang::ast::AssignmentExpression& assignment, grh::Value& rhsValue,
                         std::vector<LHSConverter::WriteResult>& outResults) {
    pending_.clear();
    outResults.clear();

    const slang::ast::Type* lhsType = assignment.left().type;
    if (!lhsType || !lhsType->isBitstreamType() || !lhsType->isFixedSize()) {
        report("Assign LHS must be a fixed-size bitstream type");
        return false;
    }

    const int64_t lhsWidth = static_cast<int64_t>(lhsType->getBitstreamWidth());
    if (lhsWidth <= 0) {
        report("Assign LHS has zero width");
        return false;
    }
    if (rhsValue.width() != lhsWidth) {
        std::ostringstream oss;
        oss << "Assign width mismatch; lhs=" << lhsWidth << " rhs=" << rhsValue.width();
        report(oss.str());
        return false;
    }

    if (!processLhs(assignment.left(), rhsValue)) {
        pending_.clear();
        return false;
    }

    flushPending(outResults);
    pending_.clear();
    return true;
}

bool LHSConverter::processLhs(const slang::ast::Expression& expr, grh::Value& rhsValue) {
    if (const auto* concat = expr.as_if<slang::ast::ConcatenationExpression>()) {
        return handleConcatenation(*concat, rhsValue);
    }

    if (!allowReplication()) {
        if (expr.as_if<slang::ast::ReplicationExpression>()) {
            report("Replication is not supported on assign LHS");
            return false;
        }

        if (expr.as_if<slang::ast::StreamingConcatenationExpression>()) {
            report("Streaming concatenation is not supported on assign LHS");
            return false;
        }
    }

    return handleLeaf(expr, rhsValue);
}

bool LHSConverter::handleConcatenation(const slang::ast::ConcatenationExpression& concat,
                                       grh::Value& rhsValue) {
    const auto operands = concat.operands();
    if (operands.empty()) {
        report("Empty concatenation on assign LHS");
        return false;
    }

    int64_t remainingWidth = rhsValue.width();
    int64_t currentMsb = remainingWidth - 1;

    for (const slang::ast::Expression* operand : operands) {
        if (!operand || !operand->type) {
            report("Concatenation operand lacks type information");
            return false;
        }
        if (!operand->type->isBitstreamType() || !operand->type->isFixedSize()) {
            report("Concatenation operand must be a fixed-size bitstream");
            return false;
        }

        const int64_t operandWidth =
            static_cast<int64_t>(operand->type->getBitstreamWidth());
        if (operandWidth <= 0) {
            report("Concatenation operand has zero width");
            return false;
        }
        if (operandWidth > remainingWidth) {
            report("Concatenation operand width exceeds available RHS bits");
            return false;
        }

        const int64_t sliceLsb = currentMsb - operandWidth + 1;
        grh::Value* sliceValue = createSliceValue(rhsValue, sliceLsb, currentMsb, *operand);
        if (!sliceValue) {
            return false;
        }
        if (!processLhs(*operand, *sliceValue)) {
            return false;
        }
        currentMsb = sliceLsb - 1;
        remainingWidth -= operandWidth;
    }

    if (remainingWidth != 0) {
        report("Concatenation coverage does not match RHS width");
        return false;
    }
    return true;
}

bool LHSConverter::handleLeaf(const slang::ast::Expression& expr, grh::Value& rhsValue) {
    const SignalMemoEntry* entry = resolveMemoEntry(expr);
    if (!entry) {
        report("Assign LHS is not a memoized signal");
        return false;
    }

    std::string path;
    std::optional<BitRange> range = resolveBitRange(*entry, expr, path);
    if (!range) {
        return false;
    }

    const int64_t expectedWidth = range->msb - range->lsb + 1;
    if (rhsValue.width() != expectedWidth) {
        std::ostringstream oss;
        oss << "Assign slice width mismatch; target=" << expectedWidth
            << " rhs=" << rhsValue.width();
        report(oss.str());
        return false;
    }

    WriteBackMemo::Slice slice;
    slice.path = path.empty() && entry->symbol && !entry->symbol->name.empty() ?
                     std::string(entry->symbol->name) :
                     path;
    slice.msb = range->msb;
    slice.lsb = range->lsb;
    slice.value = &rhsValue;
    slice.originExpr = &expr;
    pending_[entry].push_back(std::move(slice));
    return true;
}

const SignalMemoEntry* LHSConverter::resolveMemoEntry(const slang::ast::Expression& expr) const {
    if (const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(expr)) {
        return findMemoEntry(*symbol);
    }
    return nullptr;
}

const SignalMemoEntry* LHSConverter::findMemoEntry(const slang::ast::ValueSymbol& symbol) const {
    for (const SignalMemoEntry& entry : netMemo_) {
        if (entry.symbol == &symbol) {
            return &entry;
        }
    }
    for (const SignalMemoEntry& entry : regMemo_) {
        if (entry.symbol == &symbol) {
            return &entry;
        }
    }
    return nullptr;
}

std::optional<LHSConverter::BitRange>
LHSConverter::resolveBitRange(const SignalMemoEntry& entry, const slang::ast::Expression& expr,
                              std::string& pathOut) {
    if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>()) {
        if (conversion->isImplicit()) {
            return resolveBitRange(entry, conversion->operand(), pathOut);
        }
    }

    if (const auto* range = expr.as_if<slang::ast::RangeSelectExpression>()) {
        std::string basePath;
        std::optional<BitRange> base = resolveBitRange(entry, range->value(), basePath);
        if (!base) {
            return std::nullopt;
        }
        return resolveRangeSelect(entry, *range, basePath, pathOut);
    }

    if (expr.as_if<slang::ast::ConcatenationExpression>() ||
        expr.as_if<slang::ast::StreamingConcatenationExpression>() ||
        expr.as_if<slang::ast::ReplicationExpression>()) {
        report("Unexpected concatenation form inside assign leaf");
        return std::nullopt;
    }

    std::optional<std::string> path = buildFieldPath(expr);
    if (!path) {
        report("Unable to derive flatten path for assign LHS");
        return std::nullopt;
    }

    pathOut = *path;
    if (pathOut.empty() && entry.symbol && !entry.symbol->name.empty()) {
        pathOut = entry.symbol->name;
    }

    if (auto direct = lookupRangeByPath(entry, pathOut)) {
        return direct;
    }

    if (entry.symbol && entry.symbol->name == pathOut) {
        BitRange range{entry.width > 0 ? entry.width - 1 : 0, 0};
        return range;
    }

    std::ostringstream oss;
    oss << "Flatten metadata missing for path " << pathOut;
    report(oss.str());
    return std::nullopt;
}

std::optional<LHSConverter::BitRange>
LHSConverter::resolveRangeSelect(const SignalMemoEntry& entry,
                                 const slang::ast::RangeSelectExpression& expr,
                                 const std::string& basePath, std::string& pathOut) {
    using slang::ast::RangeSelectionKind;

    auto makeIndexedPath = [&](int64_t index) {
        std::string path = basePath;
        path.push_back('[');
        path.append(std::to_string(index));
        path.push_back(']');
        return path;
    };

    auto fetchRange = [&](int64_t index) -> std::optional<BitRange> {
        const std::string path = makeIndexedPath(index);
        if (auto range = lookupRangeByPath(entry, path)) {
            return range;
        }
        std::ostringstream oss;
        oss << "Assign LHS index " << index << " is out of bounds for " << basePath;
        report(oss.str());
        return std::nullopt;
    };

    switch (expr.getSelectionKind()) {
    case RangeSelectionKind::Simple: {
        std::optional<int64_t> left = evaluateConstant(expr.left());
        std::optional<int64_t> right = evaluateConstant(expr.right());
        if (!left || !right) {
            report("Simple range select bounds must be constant");
            return std::nullopt;
        }
        const int64_t lower = std::min(*left, *right);
        const int64_t upper = std::max(*left, *right);
        std::optional<BitRange> first = fetchRange(upper);
        std::optional<BitRange> last = fetchRange(lower);
        if (!first || !last) {
            return std::nullopt;
        }
        BitRange range{std::max(first->msb, last->msb), std::min(first->lsb, last->lsb)};
        pathOut = basePath;
        pathOut.push_back('[');
        pathOut.append(std::to_string(*left));
        pathOut.push_back(':');
        pathOut.append(std::to_string(*right));
        pathOut.push_back(']');
        return range;
    }
    case RangeSelectionKind::IndexedUp: {
        std::optional<int64_t> base = evaluateConstant(expr.left());
        std::optional<int64_t> width = evaluateConstant(expr.right());
        if (!base || !width || *width <= 0) {
            report("Indexed up select requires constant base/width");
            return std::nullopt;
        }
        std::optional<BitRange> first = fetchRange(*base + *width - 1);
        std::optional<BitRange> last = fetchRange(*base);
        if (!first || !last) {
            return std::nullopt;
        }
        BitRange range{std::max(first->msb, last->msb), std::min(first->lsb, last->lsb)};
        pathOut = basePath;
        pathOut.push_back('[');
        pathOut.append(std::to_string(*base));
        pathOut.push_back('+');
        pathOut.append(std::to_string(*width));
        pathOut.push_back(']');
        return range;
    }
    case RangeSelectionKind::IndexedDown: {
        std::optional<int64_t> base = evaluateConstant(expr.left());
        std::optional<int64_t> width = evaluateConstant(expr.right());
        if (!base || !width || *width <= 0) {
            report("Indexed down select requires constant base/width");
            return std::nullopt;
        }
        std::optional<BitRange> first = fetchRange(*base);
        std::optional<BitRange> last = fetchRange(*base - *width + 1);
        if (!first || !last) {
            return std::nullopt;
        }
        BitRange range{std::max(first->msb, last->msb), std::min(first->lsb, last->lsb)};
        pathOut = basePath;
        pathOut.push_back('[');
        pathOut.append(std::to_string(*base));
        pathOut.append("-:");
        pathOut.append(std::to_string(*width));
        pathOut.push_back(']');
        return range;
    }
    default:
        report("Unsupported range select kind on assign LHS");
        return std::nullopt;
    }
}

std::optional<std::string> LHSConverter::buildFieldPath(const slang::ast::Expression& expr) {
    if (const auto* conversion = expr.as_if<slang::ast::ConversionExpression>()) {
        if (conversion->isImplicit()) {
            return buildFieldPath(conversion->operand());
        }
    }

    if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>()) {
        return named->symbol.name.empty() ? std::optional<std::string>{}
                                          : std::optional<std::string>(std::string(named->symbol.name));
    }

    if (const auto* member = expr.as_if<slang::ast::MemberAccessExpression>()) {
        std::optional<std::string> base = buildFieldPath(member->value());
        if (!base) {
            return std::nullopt;
        }
        std::string memberName(member->member.name);
        if (memberName.empty()) {
            report("Anonymous member access in assign LHS");
            return std::nullopt;
        }
        if (!base->empty()) {
            base->push_back('.');
        }
        base->append(memberName);
        return base;
    }

    if (const auto* element = expr.as_if<slang::ast::ElementSelectExpression>()) {
        std::optional<std::string> base = buildFieldPath(element->value());
        if (!base) {
            return std::nullopt;
        }
        std::optional<int64_t> index = evaluateConstant(element->selector());
        if (!index) {
            report("Element select index must be constant on assign LHS");
            return std::nullopt;
        }
        base->push_back('[');
        base->append(std::to_string(*index));
        base->push_back(']');
        return base;
    }

    if (expr.kind == slang::ast::ExpressionKind::HierarchicalValue) {
        report("Hierarchical assign targets are not supported");
    }

    return std::nullopt;
}

std::optional<int64_t> LHSConverter::evaluateConstant(const slang::ast::Expression& expr) {
    slang::ast::EvalContext& ctx = ensureEvalContext();
    ctx.reset();
    slang::ConstantValue value = expr.eval(ctx);
    if (!value || !value.isInteger() || value.hasUnknown()) {
        return std::nullopt;
    }
    return value.integer().as<int64_t>();
}

std::optional<LHSConverter::BitRange>
LHSConverter::lookupRangeByPath(const SignalMemoEntry& entry, std::string_view path) const {
    if (path.empty()) {
        if (entry.width <= 0) {
            return std::nullopt;
        }
        return BitRange{entry.width - 1, 0};
    }

    for (const SignalMemoField& field : entry.fields) {
        if (field.path == path) {
            return BitRange{field.msb, field.lsb};
        }
    }

    bool found = false;
    int64_t maxMsb = std::numeric_limits<int64_t>::min();
    int64_t minLsb = std::numeric_limits<int64_t>::max();
    for (const SignalMemoField& field : entry.fields) {
        if (!pathMatchesDescendant(path, field.path)) {
            continue;
        }
        found = true;
        maxMsb = std::max(maxMsb, field.msb);
        minLsb = std::min(minLsb, field.lsb);
    }

    if (found) {
        return BitRange{maxMsb, minLsb};
    }

    if (entry.symbol && entry.symbol->name == path) {
        return BitRange{entry.width > 0 ? entry.width - 1 : 0, 0};
    }
    return std::nullopt;
}

grh::Value* LHSConverter::createSliceValue(grh::Value& source, int64_t lsb, int64_t msb,
                                           const slang::ast::Expression& originExpr) {
    if (lsb == 0 && msb == source.width() - 1) {
        return &source;
    }
    if (lsb < 0 || msb < lsb || msb >= source.width()) {
        report("Assign RHS slice range is out of bounds");
        return nullptr;
    }

    const auto tag = static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(this));
    std::string opName =
        "_assign_slice_op_" + std::to_string(tag) + "_" + std::to_string(sliceCounter_);
    std::string valueName =
        "_assign_slice_val_" + std::to_string(tag) + "_" + std::to_string(sliceCounter_);
    ++sliceCounter_;

    grh::Operation& op = graph().createOperation(grh::OperationKind::kSliceStatic, opName);
    op.addOperand(source);
    op.setAttribute("sliceStart", lsb);
    op.setAttribute("sliceEnd", msb);

    const int64_t width = msb - lsb + 1;
    grh::Value& value = graph().createValue(
        valueName, width, originExpr.type ? originExpr.type->isSigned() : false);
    op.addResult(value);
    return &value;
}

void LHSConverter::report(std::string message) {
    if (diagnostics_ && origin_) {
        diagnostics_->nyi(*origin_, std::move(message));
    }
}

slang::ast::EvalContext& LHSConverter::ensureEvalContext() {
    if (!evalContext_) {
        assert(origin_ && "LHSConverter requires an origin symbol for evaluation");
        evalContext_ = std::make_unique<slang::ast::EvalContext>(*origin_);
    }
    return *evalContext_;
}
void LHSConverter::flushPending(std::vector<WriteResult>& outResults) {
    for (auto& [entry, slices] : pending_) {
        if (!entry || slices.empty()) {
            continue;
        }
        WriteResult result;
        result.target = entry;
        result.slices = std::move(slices);
        outResults.push_back(std::move(result));
    }
}

bool LHSConverter::pathMatchesDescendant(std::string_view parent, std::string_view candidate) {
    if (parent.empty()) {
        return false;
    }
    if (candidate.size() <= parent.size()) {
        return false;
    }
    if (candidate.compare(0, parent.size(), parent) != 0) {
        return false;
    }
    char next = candidate[parent.size()];
    return next == '.' || next == '[';
}

ContinuousAssignLHSConverter::ContinuousAssignLHSConverter(Context context, WriteBackMemo& memo) :
    LHSConverter(context), memo_(memo) {}

bool ContinuousAssignLHSConverter::convert(const slang::ast::AssignmentExpression& assignment,
                                           grh::Value& rhsValue) {
    using WriteResult = LHSConverter::WriteResult;
    std::vector<WriteResult> results;
    if (!lower(assignment, rhsValue, results)) {
        return false;
    }
    for (WriteResult& result : results) {
        if (!result.target) {
            continue;
        }
        memo_.recordWrite(*result.target, WriteBackMemo::AssignmentKind::Continuous, origin(),
                          std::move(result.slices));
    }
    return true;
}

AlwaysBlockRHSConverter::AlwaysBlockRHSConverter(Context context, AlwaysConverter& owner) :
    CombRHSConverter(context), owner_(owner) {}

AlwaysBlockLHSConverter::AlwaysBlockLHSConverter(Context context, AlwaysConverter& owner) :
    LHSConverter(context), owner_(owner) {}

grh::Value* AlwaysBlockRHSConverter::handleMemoEntry(
    const slang::ast::NamedValueExpression& expr, const SignalMemoEntry& entry) {
    return CombRHSConverter::handleMemoEntry(expr, entry);
}

grh::Value* AlwaysBlockRHSConverter::handleCustomNamedValue(
    const slang::ast::NamedValueExpression& expr) {
    if (const auto* symbol = expr.symbol.as_if<slang::ast::ValueSymbol>()) {
        if (grh::Value* value = owner_.lookupLoopValue(*symbol)) {
            return value;
        }
    }
    return CombRHSConverter::handleCustomNamedValue(expr);
}

grh::Value* CombAlwaysRHSConverter::handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                                    const SignalMemoEntry& entry) {
    if (grh::Value* shadow = owner_.lookupShadowValue(entry)) {
        return shadow;
    }
    return AlwaysBlockRHSConverter::handleMemoEntry(expr, entry);
}

grh::Value* SeqAlwaysRHSConverter::handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                                   const SignalMemoEntry& entry) {
    return AlwaysBlockRHSConverter::handleMemoEntry(expr, entry);
}

grh::Value* SeqAlwaysRHSConverter::convertElementSelect(
    const slang::ast::ElementSelectExpression& expr) {
    auto* seqOwner = static_cast<SeqAlwaysConverter*>(&owner_);
    if (const SignalMemoEntry* entry = findMemoEntryFromExpression(expr.value())) {
        if (entry->stateOp && entry->stateOp->kind() == grh::OperationKind::kMemory) {
            grh::Value* addrValue = convert(expr.selector());
            if (!addrValue) {
                return nullptr;
            }
            if (seqOwner) {
                return seqOwner->buildMemorySyncRead(*entry, *addrValue, expr);
            }
        }
    }
    return AlwaysBlockRHSConverter::convertElementSelect(expr);
}

bool AlwaysBlockLHSConverter::convert(const slang::ast::AssignmentExpression& assignment,
                                     grh::Value& rhsValue) {
    using WriteResult = LHSConverter::WriteResult;
    std::vector<WriteResult> results;
    if (!lower(assignment, rhsValue, results)) {
        return false;
    }
    for (WriteResult& result : results) {
        if (!result.target) {
            continue;
        }
        owner_.handleEntryWrite(*result.target, std::move(result.slices));
    }
    return true;
}

namespace {

const slang::ast::Expression* skipImplicitConversions(const slang::ast::Expression& expr) {
    const slang::ast::Expression* current = &expr;
    while (const auto* conversion = current->as_if<slang::ast::ConversionExpression>()) {
        if (!conversion->isImplicit()) {
            break;
        }
        current = &conversion->operand();
    }
    return current;
}

} // namespace

bool SeqAlwaysLHSConverter::convert(const slang::ast::AssignmentExpression& assignment,
                                    grh::Value& rhsValue) {
    auto* seqOwner = static_cast<SeqAlwaysConverter*>(&owner_);

    const slang::ast::Expression* root = skipImplicitConversions(assignment.left());
    const slang::ast::Expression* cursor = root;
    const SignalMemoEntry* memoryEntry = nullptr;
    const slang::ast::ElementSelectExpression* baseElement = nullptr;

    while (cursor) {
        if (const auto* element = cursor->as_if<slang::ast::ElementSelectExpression>()) {
            const slang::ast::Expression* inner = skipImplicitConversions(element->value());
            if (const auto* named = inner->as_if<slang::ast::NamedValueExpression>()) {
                if (const auto* symbol = named->symbol.as_if<slang::ast::ValueSymbol>()) {
                    const SignalMemoEntry* candidate = findMemoEntry(*symbol);
                    if (candidate && candidate->stateOp &&
                        candidate->stateOp->kind() == grh::OperationKind::kMemory) {
                        memoryEntry = candidate;
                        baseElement = element;
                        break;
                    }
                }
            }
            cursor = inner;
            continue;
        }
        if (const auto* member = cursor->as_if<slang::ast::MemberAccessExpression>()) {
            cursor = skipImplicitConversions(member->value());
            continue;
        }
        if (const auto* range = cursor->as_if<slang::ast::RangeSelectExpression>()) {
            cursor = skipImplicitConversions(range->value());
            continue;
        }
        break;
    }

    auto emitUnsupported = [&](std::string_view message) {
        if (diagnostics()) {
            diagnostics()->nyi(owner_.block(), std::string(message));
        }
    };

    if (memoryEntry && baseElement) {
        if (!owner_.rhsConverter_ || !seqOwner) {
            emitUnsupported("Seq always memory write lacks RHS converter context");
            return true;
        }

        grh::Value* addrValue = owner_.rhsConverter_->convert(baseElement->selector());
        if (!addrValue) {
            return true;
        }

        const slang::ast::Expression* normalizedRoot = root;
        const bool rootIsBase = normalizedRoot == baseElement;
        const int64_t entryWidth = seqOwner->memoryRowWidth(*memoryEntry);

        if (rootIsBase) {
            if (rhsValue.width() != entryWidth) {
                emitUnsupported("Memory word write width mismatch");
                return true;
            }
            seqOwner->recordMemoryWordWrite(*memoryEntry, assignment, *addrValue, rhsValue);
            return true;
        }

        const auto* bitSelect = normalizedRoot->as_if<slang::ast::ElementSelectExpression>();
        if (!bitSelect) {
            emitUnsupported("Memory assignment must target full row or single bit");
            return true;
        }
        const slang::ast::Expression* bitBase = skipImplicitConversions(bitSelect->value());
        if (bitBase != baseElement) {
            emitUnsupported("Nested memory indexing beyond single-bit is not supported yet");
            return true;
        }

        if (rhsValue.width() != 1) {
            emitUnsupported("Memory single-bit write expects 1-bit RHS");
            return true;
        }

        grh::Value* bitIndexValue = owner_.rhsConverter_->convert(bitSelect->selector());
        if (!bitIndexValue) {
            return true;
        }

        seqOwner->recordMemoryBitWrite(*memoryEntry, assignment, *addrValue, *bitIndexValue,
                                       rhsValue);
        return true;
    }

    return AlwaysBlockLHSConverter::convert(assignment, rhsValue);
}

AlwaysConverter::LoopScopeGuard::LoopScopeGuard(
    AlwaysConverter& owner, std::vector<const slang::ast::ValueSymbol*> symbols) :
    owner_(owner), active_(true) {
    owner_.pushLoopScope(std::move(symbols));
}

AlwaysConverter::LoopScopeGuard::~LoopScopeGuard() {
    if (active_) {
        owner_.popLoopScope();
    }
}

void AlwaysConverter::LoopScopeGuard::dismiss() {
    active_ = false;
}

AlwaysConverter::LoopContextGuard::LoopContextGuard(AlwaysConverter& owner) :
    owner_(owner) {
    owner_.loopContextStack_.push_back(1);
}

AlwaysConverter::LoopContextGuard::~LoopContextGuard() {
    if (active_) {
        owner_.loopContextStack_.pop_back();
    }
}

void AlwaysConverter::LoopContextGuard::dismiss() {
    active_ = false;
}

AlwaysConverter::AlwaysConverter(grh::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                                 std::span<const SignalMemoEntry> regMemo, WriteBackMemo& memo,
                                 const slang::ast::ProceduralBlockSymbol& block,
                                 ElaborateDiagnostics* diagnostics) :
    graph_(graph),
    netMemo_(netMemo),
    regMemo_(regMemo),
    memo_(memo),
    block_(block),
    diagnostics_(diagnostics) {
    shadowStack_.emplace_back();
    controlContextStack_.push_back(true);
}

void AlwaysConverter::setConverters(std::unique_ptr<AlwaysBlockRHSConverter> rhs,
                                    std::unique_ptr<AlwaysBlockLHSConverter> lhs) {
    rhsConverter_ = std::move(rhs);
    lhsConverter_ = std::move(lhs);
}

void AlwaysConverter::traverse() {
    visitStatement(block_.getBody());
}

CombAlwaysConverter::CombAlwaysConverter(grh::Graph& graph,
                                         std::span<const SignalMemoEntry> netMemo,
                                         std::span<const SignalMemoEntry> regMemo,
                                         WriteBackMemo& memo,
                                         const slang::ast::ProceduralBlockSymbol& block,
                                         ElaborateDiagnostics* diagnostics) :
    AlwaysConverter(graph, netMemo, regMemo, memo, block, diagnostics) {
    auto rhs = std::make_unique<CombAlwaysRHSConverter>(
        RHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = regMemo,
                              .origin = &block,
                              .diagnostics = diagnostics},
        *this);
    auto lhs = std::make_unique<CombAlwaysLHSConverter>(
        LHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = {},
                              .origin = &block,
                              .diagnostics = diagnostics},
        *this);
    setConverters(std::move(rhs), std::move(lhs));
}

void CombAlwaysConverter::run() {
    traverse();
    flushProceduralWrites();
}

std::string_view CombAlwaysConverter::modeLabel() const {
    return "comb always";
}

bool CombAlwaysConverter::allowBlockingAssignments() const {
    return true;
}

bool CombAlwaysConverter::allowNonBlockingAssignments() const {
    return false;
}

bool CombAlwaysConverter::requireNonBlockingAssignments() const {
    return false;
}

SeqAlwaysConverter::SeqAlwaysConverter(grh::Graph& graph,
                                       std::span<const SignalMemoEntry> netMemo,
                                       std::span<const SignalMemoEntry> regMemo, WriteBackMemo& memo,
                                       const slang::ast::ProceduralBlockSymbol& block,
                                       ElaborateDiagnostics* diagnostics) :
    AlwaysConverter(graph, netMemo, regMemo, memo, block, diagnostics) {
    auto rhs = std::make_unique<SeqAlwaysRHSConverter>(
        RHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = regMemo,
                              .origin = &block,
                              .diagnostics = diagnostics},
        *this);
    auto lhs = std::make_unique<SeqAlwaysLHSConverter>(
        LHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = regMemo,
                              .origin = &block,
                              .diagnostics = diagnostics},
        *this);
    setConverters(std::move(rhs), std::move(lhs));
}

void SeqAlwaysConverter::run() {
    traverse();
    flushProceduralWrites();
    planSequentialFinalize();
}

std::string_view SeqAlwaysConverter::modeLabel() const {
    return "seq always";
}

bool SeqAlwaysConverter::allowBlockingAssignments() const {
    return false;
}

bool SeqAlwaysConverter::allowNonBlockingAssignments() const {
    return true;
}

bool SeqAlwaysConverter::requireNonBlockingAssignments() const {
    return true;
}

void SeqAlwaysConverter::planSequentialFinalize() {
    grh::Value* clockValue = ensureClockValue();
    if (!clockValue) {
        return;
    }

    const bool registerWrites = finalizeRegisterWrites(*clockValue);
    const bool memoryWrites = finalizeMemoryWrites(*clockValue);

    if (!registerWrites && !memoryWrites && diagnostics()) {
        diagnostics()->todo(block(),
                            "Seq always block produced no sequential write intents to finalize");
    }
}

bool SeqAlwaysConverter::finalizeRegisterWrites(grh::Value& clockValue) {
    bool consumedAny = false;

    for (WriteBackMemo::Entry& entry : memo().entriesMutable()) {
        if (entry.consumed) {
            continue;
        }
        if (entry.originSymbol != &block()) {
            continue;
        }
        if (entry.kind != WriteBackMemo::AssignmentKind::Procedural) {
            continue;
        }
        if (!entry.target || !entry.target->stateOp) {
            reportFinalizeIssue(entry, "Sequential write target lacks register metadata");
            entry.consumed = true;
            continue;
        }

        grh::Operation* stateOp = entry.target->stateOp;
        if (stateOp->kind() == grh::OperationKind::kMemory) {
            continue;
        }

        grh::Value* dataValue = buildDataOperand(entry);
        if (!dataValue) {
            continue;
        }

        auto resetContext = buildResetContext(*entry.target);
        std::optional<ResetExtraction> resetExtraction;
        if (resetContext && resetContext->kind != ResetContext::Kind::None) {
            if (!resetContext->signal) {
                reportFinalizeIssue(entry, "Reset signal is unavailable for this register");
                continue;
            }
            resetExtraction =
                extractResetBranches(*dataValue, *resetContext->signal, resetContext->activeHigh,
                                     entry);
            if (!resetExtraction) {
                continue;
            }
        }

        if (!attachClockOperand(*stateOp, clockValue, entry)) {
            continue;
        }
        if (resetContext && resetContext->kind != ResetContext::Kind::None) {
            if (!resetExtraction ||
                !attachResetOperands(*stateOp, *resetContext->signal,
                                     *resetExtraction->resetValue, entry)) {
                continue;
            }
        }
        if (!attachDataOperand(*stateOp, *dataValue, entry)) {
            continue;
        }

        entry.consumed = true;
        consumedAny = true;
    }

    return consumedAny;
}

bool SeqAlwaysConverter::finalizeMemoryWrites(grh::Value& clockValue) {
    bool emitted = false;
    auto reportMemoryIssue = [&](const SignalMemoEntry* entry, const slang::ast::Expression* /*origin*/,
                                 std::string_view message) {
        if (!diagnostics()) {
            return;
        }
        std::ostringstream oss;
        oss << "Seq always memory elaboration failure";
        if (entry && entry->symbol && !entry->symbol->name.empty()) {
            oss << " (signal=" << entry->symbol->name << ")";
        }
        oss << ": " << message;
        diagnostics()->nyi(block(), oss.str());
    };

    grh::Value* enableValue = ensureMemoryEnableValue();
    if (!enableValue && (!memoryWrites_.empty() || !memoryBitWrites_.empty())) {
        reportMemoryIssue(nullptr, nullptr, "failed to create memory enable value");
        memoryWrites_.clear();
        memoryBitWrites_.clear();
        return false;
    }

    auto emitWritePort = [&](const MemoryWriteIntent& intent) {
        if (!intent.entry || !intent.addr || !intent.data) {
            return;
        }
        if (!intent.entry->stateOp || intent.entry->stateOp->kind() != grh::OperationKind::kMemory) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory entry lacks kMemory state op");
            return;
        }
        const int64_t rowWidth = memoryRowWidth(*intent.entry);
        if (intent.data->width() != rowWidth) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory data width mismatch");
            return;
        }

        grh::Operation& port =
            graph().createOperation(grh::OperationKind::kMemoryWritePort,
                                    makeFinalizeOpName(*intent.entry, "mem_wr"));
        port.setAttribute("memSymbol", intent.entry->stateOp->symbol());
        port.addOperand(clockValue);
        port.addOperand(*intent.addr);
        port.addOperand(*enableValue);
        port.addOperand(*intent.data);
        emitted = true;
    };

    for (const MemoryWriteIntent& intent : memoryWrites_) {
        emitWritePort(intent);
    }
    memoryWrites_.clear();

    auto emitMaskWrite = [&](const MemoryBitWriteIntent& intent) {
        if (!intent.entry || !intent.addr || !intent.bitValue || !intent.bitIndex) {
            return;
        }
        if (!intent.entry->stateOp || intent.entry->stateOp->kind() != grh::OperationKind::kMemory) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory entry lacks kMemory state op");
            return;
        }
        const int64_t rowWidth = memoryRowWidth(*intent.entry);
        grh::Value* dataValue =
            buildShiftedBitValue(*intent.bitValue, *intent.bitIndex, rowWidth, "mem_bit_data");
        grh::Value* maskValue =
            buildShiftedMask(*intent.bitIndex, rowWidth, "mem_bit_mask");
        if (!dataValue || !maskValue) {
            reportMemoryIssue(intent.entry, intent.originExpr,
                              "failed to synthesize memory bit intent");
            return;
        }

        grh::Operation& port =
            graph().createOperation(grh::OperationKind::kMemoryMaskWritePort,
                                    makeFinalizeOpName(*intent.entry, "mem_mask_wr"));
        port.setAttribute("memSymbol", intent.entry->stateOp->symbol());
        port.addOperand(clockValue);
        port.addOperand(*intent.addr);
        port.addOperand(*enableValue);
        port.addOperand(*dataValue);
        port.addOperand(*maskValue);
        emitted = true;
    };

    for (const MemoryBitWriteIntent& intent : memoryBitWrites_) {
        emitMaskWrite(intent);
    }
    memoryBitWrites_.clear();

    return emitted;
}

grh::Value* SeqAlwaysConverter::ensureClockValue() {
    if (cachedClockValue_) {
        return cachedClockValue_;
    }
    if (clockDeriveAttempted_) {
        return nullptr;
    }
    clockDeriveAttempted_ = true;
    std::optional<grh::Value*> derived = deriveClockValue();
    if (derived) {
        cachedClockValue_ = *derived;
    }
    return cachedClockValue_;
}

grh::Value* SeqAlwaysConverter::ensureMemoryEnableValue() {
    if (memoryEnableOne_) {
        return memoryEnableOne_;
    }
    grh::Operation& op =
        graph().createOperation(grh::OperationKind::kConstant, makeMemoryHelperOpName("en"));
    grh::Value& value =
        graph().createValue(makeMemoryHelperValueName("en"), 1, /*isSigned=*/false);
    op.addResult(value);
    op.setAttribute("constValue", "1'h1");
    memoryEnableOne_ = &value;
    return memoryEnableOne_;
}

void SeqAlwaysConverter::recordMemoryWordWrite(const SignalMemoEntry& entry,
                                               const slang::ast::Expression& origin,
                                               grh::Value& addrValue, grh::Value& dataValue) {
    MemoryWriteIntent intent;
    intent.entry = &entry;
    intent.originExpr = &origin;
    intent.addr = &addrValue;
    intent.data = &dataValue;
    memoryWrites_.push_back(intent);
}

void SeqAlwaysConverter::recordMemoryBitWrite(const SignalMemoEntry& entry,
                                              const slang::ast::Expression& origin,
                                              grh::Value& addrValue, grh::Value& bitIndex,
                                              grh::Value& bitValue) {
    MemoryBitWriteIntent intent;
    intent.entry = &entry;
    intent.originExpr = &origin;
    intent.addr = &addrValue;
    intent.bitIndex = &bitIndex;
    intent.bitValue = &bitValue;
    memoryBitWrites_.push_back(intent);
}

grh::Value* SeqAlwaysConverter::buildMemorySyncRead(const SignalMemoEntry& entry,
                                                    grh::Value& addrValue,
                                                    const slang::ast::Expression& originExpr) {
    if (!entry.stateOp || entry.stateOp->kind() != grh::OperationKind::kMemory) {
        if (diagnostics()) {
            diagnostics()->nyi(block(),
                               "Seq always memory read target is not backed by kMemory operation");
        }
        return nullptr;
    }

    grh::Value* clkValue = ensureClockValue();
    grh::Value* enValue = ensureMemoryEnableValue();
    if (!clkValue || !enValue) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Seq always memory read lacks clock or enable");
        }
        return nullptr;
    }

    const slang::ast::Type* type = originExpr.type;
    int64_t width = memoryRowWidth(entry);
    bool isSigned = entry.isSigned;
    if (type && type->isBitstreamType() && type->isFixedSize()) {
        width = static_cast<int64_t>(type->getBitstreamWidth());
        isSigned = type->isSigned();
    }

    grh::Operation& port =
        graph().createOperation(grh::OperationKind::kMemorySyncReadPort,
                                makeFinalizeOpName(entry, "mem_sync_rd"));
    port.setAttribute("memSymbol", entry.stateOp->symbol());
    port.addOperand(*clkValue);
    port.addOperand(addrValue);
    port.addOperand(*enValue);

    grh::Value& result =
        graph().createValue(makeFinalizeValueName(entry, "mem_sync_rd"), width, isSigned);
    port.addResult(result);
    return &result;
}

int64_t SeqAlwaysConverter::memoryRowWidth(const SignalMemoEntry& entry) const {
    if (entry.stateOp) {
        const auto& attrs = entry.stateOp->attributes();
        auto it = attrs.find("width");
        if (it != attrs.end() && std::holds_alternative<int64_t>(it->second)) {
            int64_t width = std::get<int64_t>(it->second);
            if (width > 0) {
                return width;
            }
        }
    }
    return entry.width > 0 ? entry.width : 1;
}

grh::Value* SeqAlwaysConverter::createConcatWithZeroPadding(grh::Value& value, int64_t padWidth,
                                                            std::string_view label) {
    if (padWidth < 0) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Negative padding requested for memory bit value");
        }
        return nullptr;
    }
    if (padWidth == 0) {
        return &value;
    }
    grh::Value* zeroPad = createZeroValue(padWidth);
    if (!zeroPad) {
        return nullptr;
    }
    grh::Operation& concat =
        graph().createOperation(grh::OperationKind::kConcat, makeMemoryHelperOpName(label));
    concat.addOperand(*zeroPad);
    concat.addOperand(value);

    grh::Value& wide =
        graph().createValue(makeMemoryHelperValueName(label), padWidth + value.width(),
                            value.isSigned());
    concat.addResult(wide);
    return &wide;
}

grh::Value* SeqAlwaysConverter::buildShiftedBitValue(grh::Value& sourceBit, grh::Value& bitIndex,
                                                     int64_t targetWidth, std::string_view label) {
    if (targetWidth <= 0) {
        return nullptr;
    }
    const int64_t padWidth = targetWidth - sourceBit.width();
    grh::Value* extended = createConcatWithZeroPadding(sourceBit, padWidth, label);
    if (!extended) {
        return nullptr;
    }

    grh::Operation& shl =
        graph().createOperation(grh::OperationKind::kShl, makeMemoryHelperOpName(label));
    shl.addOperand(*extended);
    shl.addOperand(bitIndex);
    grh::Value& shifted =
        graph().createValue(makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    shl.addResult(shifted);
    return &shifted;
}

grh::Value* SeqAlwaysConverter::buildShiftedMask(grh::Value& bitIndex, int64_t targetWidth,
                                                 std::string_view label) {
    if (targetWidth <= 0) {
        return nullptr;
    }
    slang::SVInt literal(static_cast<slang::bitwidth_t>(targetWidth), 1, /*isSigned=*/false);
    grh::Operation& constOp =
        graph().createOperation(grh::OperationKind::kConstant, makeMemoryHelperOpName(label));
    grh::Value& baseValue =
        graph().createValue(makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    constOp.addResult(baseValue);
    constOp.setAttribute(
        "constValue",
        literal.toString(slang::LiteralBase::Hex, true, literal.getBitWidth()));
    grh::Value* base = &baseValue;
    if (!base) {
        return nullptr;
    }

    grh::Operation& shl =
        graph().createOperation(grh::OperationKind::kShl, makeMemoryHelperOpName(label));
    shl.addOperand(*base);
    shl.addOperand(bitIndex);
    grh::Value& shifted =
        graph().createValue(makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    shl.addResult(shifted);
    return &shifted;
}

std::string SeqAlwaysConverter::makeMemoryHelperOpName(std::string_view suffix) {
    std::string name = "_seq_mem_op_";
    name.append(suffix);
    name.push_back('_');
    name.append(std::to_string(nextMemoryHelperId()));
    return name;
}

std::string SeqAlwaysConverter::makeMemoryHelperValueName(std::string_view suffix) {
    std::string name = "_seq_mem_val_";
    name.append(suffix);
    name.push_back('_');
    name.append(std::to_string(nextMemoryHelperId()));
    return name;
}

std::optional<grh::Value*> SeqAlwaysConverter::deriveClockValue() {
    const slang::ast::TimingControl* timing = findTimingControl(block().getBody());
    if (!timing) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential block lacks timing control");
        }
        return std::nullopt;
    }

    std::vector<const slang::ast::SignalEventControl*> events;
    collectSignalEvents(*timing, events);
    if (events.empty()) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential block timing control has no edge events");
        }
        return std::nullopt;
    }

    const auto* clockEvent = events.front();
    if (!clockEvent) {
        return std::nullopt;
    }

    if (clockEvent->edge == slang::ast::EdgeKind::None) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential block clock must be edge-sensitive");
        }
        return std::nullopt;
    }

    grh::Value* clkValue = convertTimingExpr(clockEvent->expr);
    if (!clkValue && diagnostics()) {
        diagnostics()->nyi(block(), "Failed to lower sequential clock expression");
    }
    return clkValue;
}

grh::Value* SeqAlwaysConverter::convertTimingExpr(const slang::ast::Expression& expr) {
    if (auto it = timingValueCache_.find(&expr); it != timingValueCache_.end()) {
        return it->second;
    }

    if (!rhsConverter_) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "RHS converter unavailable for timing expressions");
        }
        return nullptr;
    }

    grh::Value* value = rhsConverter_->convert(expr);
    if (value) {
        timingValueCache_[&expr] = value;
    }
    return value;
}

grh::Value* SeqAlwaysConverter::buildDataOperand(const WriteBackMemo::Entry& entry) {
    if (!entry.target || !entry.target->value) {
        reportFinalizeIssue(entry, "Sequential write target lacks register value handle");
        return nullptr;
    }

    const int64_t targetWidth = entry.target->width > 0 ? entry.target->width : 1;
    if (entry.target->value->width() != targetWidth) {
        reportFinalizeIssue(entry, "Register value width mismatch in sequential write");
        return nullptr;
    }

    if (entry.slices.empty()) {
        reportFinalizeIssue(entry, "Sequential write has no RHS slices to compose");
        return nullptr;
    }

    std::vector<WriteBackMemo::Slice> slices = entry.slices;
    std::sort(slices.begin(), slices.end(), [](const WriteBackMemo::Slice& lhs,
                                               const WriteBackMemo::Slice& rhs) {
        if (lhs.msb != rhs.msb) {
            return lhs.msb > rhs.msb;
        }
        return lhs.lsb > rhs.lsb;
    });

    std::vector<grh::Value*> components;
    components.reserve(slices.size() + 2);

    auto appendHoldRange = [&](int64_t msb, int64_t lsb) -> bool {
        if (msb < lsb) {
            return true;
        }
        grh::Value* hold = createHoldSlice(entry, msb, lsb);
        if (!hold) {
            return false;
        }
        components.push_back(hold);
        return true;
    };

    int64_t expectedMsb = targetWidth - 1;
    for (const WriteBackMemo::Slice& slice : slices) {
        if (!slice.value) {
            reportFinalizeIssue(entry, "Sequential write slice is missing RHS value");
            return nullptr;
        }
        if (slice.msb < slice.lsb) {
            reportFinalizeIssue(entry, "Sequential write slice has invalid bit range");
            return nullptr;
        }
        if (slice.msb > expectedMsb) {
            reportFinalizeIssue(entry, "Sequential write slice exceeds register width");
            return nullptr;
        }
        if (slice.msb < expectedMsb) {
            if (!appendHoldRange(expectedMsb, slice.msb + 1)) {
                return nullptr;
            }
        }
        components.push_back(slice.value);
        expectedMsb = slice.lsb - 1;
    }

    if (expectedMsb >= 0) {
        if (!appendHoldRange(expectedMsb, 0)) {
            return nullptr;
        }
    }

    if (components.empty()) {
        return entry.target->value;
    }

    if (components.size() == 1) {
        return components.front();
    }

    grh::Operation& concat =
        graph().createOperation(grh::OperationKind::kConcat,
                                makeFinalizeOpName(*entry.target, "seq_concat"));
    for (grh::Value* component : components) {
        concat.addOperand(*component);
    }

    grh::Value& composed =
        graph().createValue(makeFinalizeValueName(*entry.target, "seq_concat"), targetWidth,
                            entry.target->isSigned);
    concat.addResult(composed);
    return &composed;
}

grh::Value* SeqAlwaysConverter::createHoldSlice(const WriteBackMemo::Entry& entry, int64_t msb,
                                                int64_t lsb) {
    if (!entry.target || !entry.target->value) {
        reportFinalizeIssue(entry, "Register hold slice missing target value");
        return nullptr;
    }

    grh::Value* source = entry.target->value;
    if (!source) {
        reportFinalizeIssue(entry, "Register hold slice has null source value");
        return nullptr;
    }

    if (lsb < 0 || msb < lsb || msb >= source->width()) {
        reportFinalizeIssue(entry, "Register hold slice range is out of bounds");
        return nullptr;
    }

    if (lsb == 0 && msb == source->width() - 1) {
        return source;
    }

    grh::Operation& sliceOp = graph().createOperation(grh::OperationKind::kSliceStatic,
                                                      makeFinalizeOpName(*entry.target, "hold"));
    sliceOp.addOperand(*source);
    sliceOp.setAttribute("sliceStart", lsb);
    sliceOp.setAttribute("sliceEnd", msb);

    grh::Value& result =
        graph().createValue(makeFinalizeValueName(*entry.target, "hold"), msb - lsb + 1,
                            source->isSigned());
    sliceOp.addResult(result);
    return &result;
}

bool SeqAlwaysConverter::attachClockOperand(grh::Operation& stateOp, grh::Value& clkValue,
                                            const WriteBackMemo::Entry& entry) {
    const auto& operands = stateOp.operands();
    if (operands.empty()) {
        stateOp.addOperand(clkValue);
        return true;
    }

    if (operands.front() != &clkValue) {
        reportFinalizeIssue(entry, "Register already bound to a different clock operand");
        return false;
    }
    return true;
}

bool SeqAlwaysConverter::attachDataOperand(grh::Operation& stateOp, grh::Value& dataValue,
                                           const WriteBackMemo::Entry& entry) {
    const auto& operands = stateOp.operands();
    std::size_t expected = 1;
    if (stateOp.kind() == grh::OperationKind::kRegisterRst ||
        stateOp.kind() == grh::OperationKind::kRegisterARst) {
        expected = 3;
    }
    if (operands.size() != expected) {
        reportFinalizeIssue(entry, "Register operands not ready for data attachment");
        return false;
    }

    if (!stateOp.results().empty()) {
        if (grh::Value* q = stateOp.results().front()) {
            if (q->width() != dataValue.width()) {
                reportFinalizeIssue(entry, "Register data width does not match Q output width");
                return false;
            }
        }
    }

    stateOp.addOperand(dataValue);
    return true;
}

void SeqAlwaysConverter::reportFinalizeIssue(const WriteBackMemo::Entry& entry,
                                             std::string_view message) {
    if (!diagnostics()) {
        return;
    }

    const slang::ast::Symbol* origin = entry.originSymbol;
    if (!origin && entry.target) {
        origin = entry.target->symbol;
    }
    if (!origin) {
        origin = &block();
    }
    diagnostics()->nyi(*origin, std::string(message));
}

std::string SeqAlwaysConverter::makeFinalizeOpName(const SignalMemoEntry& entry,
                                                   std::string_view suffix) {
    std::string base;
    if (entry.symbol && !entry.symbol->name.empty()) {
        base = sanitizeForGraphName(entry.symbol->name);
    }
    if (base.empty()) {
        base = "_seq";
    }
    base.push_back('_');
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(finalizeNameCounter_++));
    return base;
}

std::string SeqAlwaysConverter::makeFinalizeValueName(const SignalMemoEntry& entry,
                                                      std::string_view suffix) {
    std::string base;
    if (entry.symbol && !entry.symbol->name.empty()) {
        base = sanitizeForGraphName(entry.symbol->name);
    }
    if (base.empty()) {
        base = "_seq_val";
    }
    base.push_back('_');
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(finalizeNameCounter_++));
    return base;
}

std::optional<SeqAlwaysConverter::ResetContext>
SeqAlwaysConverter::buildResetContext(const SignalMemoEntry& entry) {
    if (!entry.stateOp) {
        return std::nullopt;
    }
    ResetContext context;
    switch (entry.stateOp->kind()) {
    case grh::OperationKind::kRegisterARst:
        if (!entry.asyncResetExpr) {
            return std::nullopt;
        }
        if (entry.asyncResetEdge != slang::ast::EdgeKind::PosEdge &&
            entry.asyncResetEdge != slang::ast::EdgeKind::NegEdge) {
            return std::nullopt;
        }
        context.kind = ResetContext::Kind::Async;
        context.signal = resolveAsyncResetSignal(*entry.asyncResetExpr);
        context.activeHigh = entry.asyncResetEdge == slang::ast::EdgeKind::PosEdge;
        return context.signal ? std::optional<ResetContext>(context) : std::nullopt;
    case grh::OperationKind::kRegisterRst:
        if (!entry.syncResetSymbol) {
            return std::nullopt;
        }
        context.kind = ResetContext::Kind::Sync;
        context.signal = resolveSyncResetSignal(*entry.syncResetSymbol);
        context.activeHigh = entry.syncResetActiveHigh;
        return context.signal ? std::optional<ResetContext>(context) : std::nullopt;
    default:
        break;
    }
    return std::nullopt;
}

std::optional<bool> SeqAlwaysConverter::matchResetCondition(grh::Value& condition,
                                                            grh::Value& resetSignal) {
    if (&condition == &resetSignal) {
        return /* inverted */ false;
    }
    if (grh::Operation* op = condition.definingOp()) {
        if (op->kind() == grh::OperationKind::kLogicNot && op->operands().size() == 1 &&
            op->operands().front() == &resetSignal) {
            return true;
        }
    }
    return std::nullopt;
}

std::optional<SeqAlwaysConverter::ResetExtraction>
SeqAlwaysConverter::extractResetBranches(grh::Value& dataValue, grh::Value& resetSignal,
                                         bool activeHigh, const WriteBackMemo::Entry& entry) {
    grh::Operation* muxOp = dataValue.definingOp();
    if (!muxOp || muxOp->kind() != grh::OperationKind::kMux || muxOp->operands().size() != 3) {
        reportFinalizeIssue(entry, "Expected mux structure to derive reset value");
        return std::nullopt;
    }
    grh::Value* condition = muxOp->operands().front();
    auto match = matchResetCondition(*condition, resetSignal);
    if (!match) {
        reportFinalizeIssue(entry, "Reset mux condition does not reference reset signal");
        return std::nullopt;
    }

    const bool conditionTrueWhenSignalHigh = !*match;
    const bool resetWhenSignalHigh = activeHigh;
    const bool resetBranchIsTrue = conditionTrueWhenSignalHigh == resetWhenSignalHigh;
    grh::Value* resetValue = muxOp->operands()[resetBranchIsTrue ? 1 : 2];
    if (!resetValue) {
        reportFinalizeIssue(entry, "Reset branch value is missing");
        return std::nullopt;
    }
    if (resetValue->width() != dataValue.width()) {
        reportFinalizeIssue(entry, "Reset branch width mismatch");
        return std::nullopt;
    }
    return ResetExtraction{resetValue};
}

bool SeqAlwaysConverter::attachResetOperands(grh::Operation& stateOp, grh::Value& rstSignal,
                                             grh::Value& resetValue,
                                             const WriteBackMemo::Entry& entry) {
    auto& operands = stateOp.operands();
    if (stateOp.kind() != grh::OperationKind::kRegisterRst &&
        stateOp.kind() != grh::OperationKind::kRegisterARst) {
        reportFinalizeIssue(entry, "Register does not expect reset operands");
        return false;
    }
    if (operands.size() < 1) {
        reportFinalizeIssue(entry, "Register clock must be attached before reset operand");
        return false;
    }
    if (operands.size() > 1) {
        if (operands.size() < 3) {
            reportFinalizeIssue(entry, "Register reset operand already attached");
        }
        return false;
    }
    if (resetValue.width() != entry.target->width) {
        reportFinalizeIssue(entry, "Reset value width mismatch");
        return false;
    }
    stateOp.addOperand(rstSignal);
    stateOp.addOperand(resetValue);
    return true;
}

grh::Value* SeqAlwaysConverter::resolveAsyncResetSignal(const slang::ast::Expression& expr) {
    if (auto it = timingValueCache_.find(&expr); it != timingValueCache_.end()) {
        return it->second;
    }
    grh::Value* value = convertTimingExpr(expr);
    if (value) {
        timingValueCache_[&expr] = value;
    }
    return value;
}

grh::Value* SeqAlwaysConverter::resolveSyncResetSignal(const slang::ast::ValueSymbol& symbol) {
    if (auto it = syncResetCache_.find(&symbol); it != syncResetCache_.end()) {
        return it->second;
    }
    grh::Value* value = graph().findValue(symbol.name);
    if (value) {
        syncResetCache_[&symbol] = value;
    }
    return value;
}

void AlwaysConverter::visitStatement(const slang::ast::Statement& stmt) {
    if (stmt.kind == slang::ast::StatementKind::Break) {
        handleLoopControlRequest(LoopControl::Break, stmt);
        return;
    }
    if (stmt.kind == slang::ast::StatementKind::Continue) {
        handleLoopControlRequest(LoopControl::Continue, stmt);
        return;
    }
    if (stmt.kind == slang::ast::StatementKind::VariableDeclaration) {
        return;
    }
    if (const auto* list = stmt.as_if<slang::ast::StatementList>()) {
        visitStatementList(*list);
        return;
    }
    if (const auto* block = stmt.as_if<slang::ast::BlockStatement>()) {
        visitBlock(*block);
        return;
    }
    if (const auto* timed = stmt.as_if<slang::ast::TimedStatement>()) {
        visitStatement(timed->stmt);
        return;
    }
    if (const auto* conditional = stmt.as_if<slang::ast::ConditionalStatement>()) {
        visitConditional(*conditional);
        return;
    }
    if (const auto* caseStmt = stmt.as_if<slang::ast::CaseStatement>()) {
        visitCase(*caseStmt);
        return;
    }
    if (const auto* exprStmt = stmt.as_if<slang::ast::ExpressionStatement>()) {
        visitExpressionStatement(*exprStmt);
        return;
    }
    if (const auto* procAssign = stmt.as_if<slang::ast::ProceduralAssignStatement>()) {
        visitProceduralAssign(*procAssign);
        return;
    }
    if (const auto* forLoop = stmt.as_if<slang::ast::ForLoopStatement>()) {
        visitForLoop(*forLoop);
        return;
    }
    if (const auto* foreachLoop = stmt.as_if<slang::ast::ForeachLoopStatement>()) {
        visitForeachLoop(*foreachLoop);
        return;
    }

    using slang::ast::StatementKind;
    switch (stmt.kind) {
    case StatementKind::Empty:
        return;
    case StatementKind::Case:
    case StatementKind::PatternCase:
        reportControlFlowTodo("case");
        return;
    case StatementKind::RepeatLoop:
    case StatementKind::WhileLoop:
    case StatementKind::DoWhileLoop:
    case StatementKind::ForeverLoop:
        reportControlFlowTodo("loop");
        return;
    default:
        reportUnsupportedStmt(stmt);
        return;
    }
}

void AlwaysConverter::visitStatementList(const slang::ast::StatementList& list) {
    for (const slang::ast::Statement* child : list.list) {
        if (!child) {
            continue;
        }
        visitStatement(*child);
        if (loopControlTargetsCurrentLoop()) {
            break;
        }
    }
}

void AlwaysConverter::visitBlock(const slang::ast::BlockStatement& block) {
    visitStatement(block.body);
}

void AlwaysConverter::visitExpressionStatement(
    const slang::ast::ExpressionStatement& stmt) {
    const slang::ast::Expression& expr = stmt.expr;

    if (const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>()) {
        handleAssignment(*assignment, expr);
        return;
    }

    reportUnsupportedStmt(stmt);
}

void AlwaysConverter::visitProceduralAssign(
    const slang::ast::ProceduralAssignStatement& stmt) {
    if (const auto* assignment = stmt.assignment.as_if<slang::ast::AssignmentExpression>()) {
        handleAssignment(*assignment, stmt.assignment);
        return;
    }
    reportUnsupportedStmt(stmt);
}

void AlwaysConverter::visitForLoop(const slang::ast::ForLoopStatement& stmt) {
    if (stmt.loopVars.empty()) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) +
                                  " for-loop without inline variable declarations is not supported yet";
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }
    if (!stmt.stopExpr) {
        if (diagnostics_) {
            std::string message =
                std::string(modeLabel()) +
                " for-loop requires a statically evaluable stop expression";
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }

    slang::ast::EvalContext& ctx = ensureEvalContext();
    ctx.reset();

    std::vector<ForLoopVarState> states;
    states.reserve(stmt.loopVars.size());
    if (!prepareForLoopState(stmt, states, ctx)) {
        ctx.reset();
        return;
    }

    std::vector<const slang::ast::ValueSymbol*> scopeSymbols;
    scopeSymbols.reserve(states.size());
    for (const ForLoopVarState& state : states) {
        if (state.symbol) {
            scopeSymbols.push_back(state.symbol);
        }
    }
    LoopScopeGuard scope(*this, std::move(scopeSymbols));
    LoopContextGuard loopContext(*this);

    std::size_t iterationCount = 0;
    while (true) {
        bool condition = true;
        if (!evaluateForLoopCondition(stmt, ctx, condition)) {
            ctx.reset();
            return;
        }
        if (!condition) {
            break;
        }
        if (iterationCount++ >= kMaxLoopIterations) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " for-loop exceeded maximum unrolled iterations";
                diagnostics_->nyi(block_, std::move(message));
            }
            break;
        }

        if (!updateLoopBindings(states, ctx)) {
            ctx.reset();
            return;
        }

        visitStatement(stmt.body);

        if (loopControlTargetsCurrentLoop()) {
            if (pendingLoopControl_ == LoopControl::Break) {
                pendingLoopControl_ = LoopControl::None;
                break;
            }
            if (pendingLoopControl_ == LoopControl::Continue) {
                pendingLoopControl_ = LoopControl::None;
            }
        }

        if (!executeForLoopSteps(stmt, ctx)) {
            ctx.reset();
            return;
        }
    }

    ctx.reset();
}

void AlwaysConverter::visitForeachLoop(const slang::ast::ForeachLoopStatement& stmt) {
    if (stmt.loopDims.empty()) {
        return;
    }

    std::vector<ForeachDimState> dims;
    dims.reserve(stmt.loopDims.size());
    std::vector<const slang::ast::ValueSymbol*> scopeSymbols;

    for (const auto& dim : stmt.loopDims) {
        if (!dim.range) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " foreach requires static dimension ranges";
                diagnostics_->nyi(block_, std::move(message));
            }
            return;
        }
        if (!dim.loopVar) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " foreach skipping dimensions is not supported yet";
                diagnostics_->nyi(block_, std::move(message));
            }
            return;
        }
        const slang::ast::Type& type = dim.loopVar->getType();
        if (!type.isIntegral()) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " foreach loop variable must be integral";
                diagnostics_->nyi(*dim.loopVar, std::move(message));
            }
            return;
        }

        ForeachDimState state;
        state.loopVar = dim.loopVar;
        state.start = dim.range->left;
        state.stop = dim.range->right;
        state.step = state.start <= state.stop ? 1 : -1;
        dims.push_back(state);
        scopeSymbols.push_back(dim.loopVar);
    }

    if (dims.empty()) {
        return;
    }

    LoopScopeGuard scope(*this, std::move(scopeSymbols));
    LoopContextGuard loopContext(*this);
    std::size_t iterationCount = 0;
    if (!runForeachRecursive(stmt, std::span<const ForeachDimState>(dims), 0, iterationCount)) {
        return;
    }
}

void AlwaysConverter::visitConditional(const slang::ast::ConditionalStatement& stmt) {
    if (stmt.conditions.empty()) {
        reportUnsupportedStmt(stmt);
        return;
    }
    if (stmt.conditions.size() != 1 || stmt.conditions.front().pattern) {
        reportControlFlowTodo("patterned if");
        return;
    }
    const slang::ast::Expression& conditionExpr = *stmt.conditions.front().expr;
    if (std::optional<bool> staticValue = evaluateStaticCondition(conditionExpr)) {
        ShadowFrame baseSnapshot = currentFrame();
        if (*staticValue) {
            ShadowFrame trueFrame = runWithShadowFrame(baseSnapshot, stmt.ifTrue);
            currentFrame() = std::move(trueFrame);
        }
        else if (stmt.ifFalse) {
            ShadowFrame falseFrame = runWithShadowFrame(baseSnapshot, *stmt.ifFalse);
            currentFrame() = std::move(falseFrame);
        }
        else {
            currentFrame() = std::move(baseSnapshot);
        }
        return;
    }

    grh::Value* conditionValue = rhsConverter_->convert(conditionExpr);
    if (!conditionValue) {
        return;
    }

    ShadowFrame baseSnapshot = currentFrame();
    ShadowFrame trueFrame = runWithShadowFrame(baseSnapshot, stmt.ifTrue, /*isStaticContext=*/false);
    ShadowFrame falseFrame =
        stmt.ifFalse ? runWithShadowFrame(baseSnapshot, *stmt.ifFalse, /*isStaticContext=*/false)
                     : baseSnapshot;

    auto merged =
        mergeShadowFrames(*conditionValue, std::move(trueFrame), std::move(falseFrame), stmt, "if");
    if (!merged) {
        return;
    }
    currentFrame() = std::move(*merged);
}

void AlwaysConverter::visitCase(const slang::ast::CaseStatement& stmt) {
    using slang::ast::CaseStatementCondition;
    if (stmt.condition == CaseStatementCondition::Inside) {
        reportControlFlowTodo("case inside condition");
        return;
    }

    checkCaseUniquePriority(stmt);

    slang::ast::EvalContext& ctx = ensureEvalContext();
    ctx.reset();
    seedEvalContextWithLoopValues(ctx);
    auto [knownBranch, isKnown] = stmt.getKnownBranch(ctx);
    if (isKnown) {
        ShadowFrame baseSnapshot = currentFrame();
        if (knownBranch) {
            ShadowFrame branchFrame = runWithShadowFrame(baseSnapshot, *knownBranch);
            currentFrame() = std::move(branchFrame);
        }
        else {
            currentFrame() = std::move(baseSnapshot);
        }
        return;
    }

    grh::Value* controlValue = rhsConverter_->convert(stmt.expr);
    if (!controlValue) {
        return;
    }

    ShadowFrame baseSnapshot = currentFrame();
    std::vector<CaseBranch> branches;
    branches.reserve(stmt.items.size());

    for (const auto& item : stmt.items) {
        grh::Value* match = buildCaseMatch(item, *controlValue, stmt.condition);
        if (!match) {
            return;
        }
        ShadowFrame branchFrame =
            runWithShadowFrame(baseSnapshot, *item.stmt, /*isStaticContext=*/false);
        CaseBranch branch;
        branch.match = match;
        branch.frame = std::move(branchFrame);
        branches.push_back(std::move(branch));
    }

    ShadowFrame accumulator =
        stmt.defaultCase ? runWithShadowFrame(baseSnapshot, *stmt.defaultCase,
                                              /*isStaticContext=*/false)
                         : baseSnapshot;

    if (branches.empty()) {
        currentFrame() = std::move(accumulator);
        return;
    }

    for (auto it = branches.rbegin(); it != branches.rend(); ++it) {
        auto merged =
            mergeShadowFrames(*it->match, std::move(it->frame), std::move(accumulator), stmt,
                              "case");
        if (!merged) {
            return;
        }
        accumulator = std::move(*merged);
    }

    currentFrame() = std::move(accumulator);
}

void AlwaysConverter::handleAssignment(const slang::ast::AssignmentExpression& expr,
                                       const slang::ast::Expression& /*originExpr*/) {
    const bool isNonBlocking = expr.isNonBlocking();
    if (isNonBlocking && !allowNonBlockingAssignments()) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) +
                                  " does not allow non-blocking assignments yet";
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }
    if (!isNonBlocking && (!allowBlockingAssignments() || requireNonBlockingAssignments())) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) +
                                  " requires non-blocking procedural assignments";
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }
    if (!rhsConverter_ || !lhsConverter_) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) +
                                  " converters are not initialized (internal error)";
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }

    grh::Value* rhsValue = rhsConverter_->convert(expr.right());
    if (!rhsValue) {
        return;
    }

    lhsConverter_->convert(expr, *rhsValue);
}

void AlwaysConverter::flushProceduralWrites() {
    if (shadowStack_.empty()) {
        return;
    }
    ShadowFrame& root = shadowStack_.front();
    for (auto& [entry, state] : root.map) {
        if (!entry || state.slices.empty()) {
            continue;
        }
        memo_.recordWrite(*entry, WriteBackMemo::AssignmentKind::Procedural, &block_,
                         std::move(state.slices));
    }
}

void AlwaysConverter::reportControlFlowTodo(std::string_view label) {
    if (reportedControlFlowTodo_ || !diagnostics_) {
        return;
    }
    std::string message = std::string(modeLabel());
    message.append(" control flow (");
    message.append(label);
    message.append(") is not implemented yet");
    diagnostics_->todo(block_, std::move(message));
    reportedControlFlowTodo_ = true;
}

void AlwaysConverter::reportUnsupportedStmt(const slang::ast::Statement& stmt) {
    if (!diagnostics_) {
        return;
    }
    std::string message = "Unsupported statement kind in ";
    message.append(modeLabel());
    message.append(" (kind = ");
    message.append(std::to_string(static_cast<int>(stmt.kind)));
    message.push_back(')');
    diagnostics_->nyi(block_, std::move(message));
}

void AlwaysConverter::handleEntryWrite(const SignalMemoEntry& entry,
                                           std::vector<WriteBackMemo::Slice> slices) {
    if (slices.empty()) {
        return;
    }
    ShadowState& state = currentFrame().map[&entry];
    for (WriteBackMemo::Slice& slice : slices) {
        insertShadowSlice(state, slice);
    }
    state.dirty = true;
    state.composed = nullptr;
    currentFrame().touched.insert(&entry);
}

void AlwaysConverter::insertShadowSlice(ShadowState& state,
                                            const WriteBackMemo::Slice& slice) {
    WriteBackMemo::Slice copy = slice;
    auto& entries = state.slices;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const WriteBackMemo::Slice& existing) {
                                     return !(copy.msb < existing.lsb || copy.lsb > existing.msb);
                                 }),
                  entries.end());

    auto insertPos =
        std::find_if(entries.begin(), entries.end(), [&](const WriteBackMemo::Slice& existing) {
            if (copy.msb != existing.msb) {
                return copy.msb > existing.msb;
            }
            return copy.lsb > existing.lsb;
        });
    entries.insert(insertPos, std::move(copy));
}

grh::Value* AlwaysConverter::lookupShadowValue(const SignalMemoEntry& entry) {
    ShadowFrame& frame = currentFrame();
    auto it = frame.map.find(&entry);
    if (it == frame.map.end()) {
        return nullptr;
    }
    ShadowState& state = it->second;
    if (!state.dirty && state.composed) {
        return state.composed;
    }
    return rebuildShadowValue(entry, state);
}

void AlwaysConverter::handleLoopControlRequest(LoopControl kind,
                                                   const slang::ast::Statement& /*origin*/) {
    if (!currentContextStatic()) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) + " ";
            message.append(kind == LoopControl::Break ? "break" : "continue");
            message.append(" requires statically known control flow");
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }
    if (loopContextStack_.empty()) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) + " ";
            message.append(kind == LoopControl::Break ? "break" : "continue");
            message.append(" used outside of a loop is not supported");
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }
    pendingLoopControl_ = kind;
    pendingLoopDepth_ = loopContextStack_.size();
}

grh::Value* AlwaysConverter::rebuildShadowValue(const SignalMemoEntry& entry,
                                                    ShadowState& state) {
    if (state.slices.empty()) {
        state.composed = nullptr;
        state.dirty = false;
        return nullptr;
    }

    const int64_t targetWidth = entry.width > 0 ? entry.width : 1;
    int64_t expectedMsb = targetWidth - 1;
    std::vector<grh::Value*> components;
    components.reserve(state.slices.size() + 2);

    for (const WriteBackMemo::Slice& slice : state.slices) {
        const int64_t gapWidth = expectedMsb - slice.msb;
        if (gapWidth > 0) {
            grh::Value* zero = createZeroValue(gapWidth);
            if (!zero) {
                state.composed = nullptr;
                state.dirty = false;
                return nullptr;
            }
            components.push_back(zero);
            expectedMsb -= gapWidth;
        }
        components.push_back(slice.value);
        expectedMsb = slice.lsb - 1;
    }

    if (expectedMsb >= 0) {
        grh::Value* zero = createZeroValue(expectedMsb + 1);
        if (!zero) {
            state.composed = nullptr;
            state.dirty = false;
            return nullptr;
        }
        components.push_back(zero);
    }

    grh::Value* composed = nullptr;
    if (components.size() == 1) {
        composed = components.front();
    }
    else {
        grh::Operation& concat =
            graph_.createOperation(grh::OperationKind::kConcat,
                                   makeShadowOpName(entry, "shadow_concat"));
        for (grh::Value* operand : components) {
            concat.addOperand(*operand);
        }
        grh::Value& value =
            graph_.createValue(makeShadowValueName(entry, "shadow"), targetWidth, entry.isSigned);
        concat.addResult(value);
        composed = &value;
    }

    state.composed = composed;
    state.dirty = false;
    return composed;
}

grh::Value* AlwaysConverter::createZeroValue(int64_t width) {
    if (width <= 0) {
        return nullptr;
    }
    if (auto it = zeroCache_.find(width); it != zeroCache_.end()) {
        return it->second;
    }

    std::string opName = "_comb_zero_";
    opName.append(std::to_string(shadowNameCounter_++));
    std::string valueName = "_comb_zero_val_";
    valueName.append(std::to_string(shadowNameCounter_++));

    grh::Operation& op = graph_.createOperation(grh::OperationKind::kConstant, opName);
    grh::Value& value = graph_.createValue(valueName, width, /*isSigned=*/false);
    op.addResult(value);
    std::ostringstream oss;
    oss << width << "'h0";
    op.setAttribute("constValue", oss.str());
    zeroCache_[width] = &value;
    return &value;
}

std::string AlwaysConverter::makeShadowOpName(const SignalMemoEntry& entry,
                                                  std::string_view suffix) {
    std::string base;
    if (entry.symbol && !entry.symbol->name.empty()) {
        base = sanitizeForGraphName(entry.symbol->name);
    }
    if (base.empty()) {
        base = "_comb_always";
    }
    base.push_back('_');
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(shadowNameCounter_++));
    return base;
}

std::string AlwaysConverter::makeShadowValueName(const SignalMemoEntry& entry,
                                                     std::string_view suffix) {
    std::string base;
    if (entry.symbol && !entry.symbol->name.empty()) {
        base = sanitizeForGraphName(entry.symbol->name);
    }
    if (base.empty()) {
        base = "_comb_value";
    }
    base.push_back('_');
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(shadowNameCounter_++));
    return base;
}

AlwaysConverter::ShadowFrame& AlwaysConverter::currentFrame() {
    assert(!shadowStack_.empty());
    return shadowStack_.back();
}

const AlwaysConverter::ShadowFrame& AlwaysConverter::currentFrame() const {
    assert(!shadowStack_.empty());
    return shadowStack_.back();
}

AlwaysConverter::ShadowFrame
AlwaysConverter::runWithShadowFrame(const ShadowFrame& seed,
                                        const slang::ast::Statement& stmt) {
    return runWithShadowFrame(seed, stmt, currentContextStatic());
}

AlwaysConverter::ShadowFrame
AlwaysConverter::runWithShadowFrame(const ShadowFrame& seed,
                                        const slang::ast::Statement& stmt,
                                        bool isStaticContext) {
    ShadowFrame frame;
    frame.map = seed.map;
    shadowStack_.push_back(std::move(frame));
    bool parentStatic = currentContextStatic();
    controlContextStack_.push_back(parentStatic && isStaticContext);
    visitStatement(stmt);
    ShadowFrame result = std::move(shadowStack_.back());
    shadowStack_.pop_back();
    controlContextStack_.pop_back();
    return result;
}

std::optional<AlwaysConverter::ShadowFrame>
AlwaysConverter::mergeShadowFrames(grh::Value& condition, ShadowFrame&& trueFrame,
                                       ShadowFrame&& falseFrame,
                                       const slang::ast::Statement& /*originStmt*/,
                                       std::string_view label) {
    std::unordered_set<const SignalMemoEntry*> coverage;
    coverage.insert(trueFrame.touched.begin(), trueFrame.touched.end());
    coverage.insert(falseFrame.touched.begin(), falseFrame.touched.end());

    if (coverage.empty()) {
        return std::move(falseFrame);
    }

    for (const SignalMemoEntry* entry : coverage) {
        if (!entry) {
            reportLatchIssue("comb always branch references unknown target");
            return std::nullopt;
        }
        if (trueFrame.touched.count(entry) == 0 || falseFrame.touched.count(entry) == 0) {
            reportLatchIssue("comb always branch coverage incomplete", entry);
            return std::nullopt;
        }

        auto trueIt = trueFrame.map.find(entry);
        auto falseIt = falseFrame.map.find(entry);
        if (trueIt == trueFrame.map.end() || falseIt == falseFrame.map.end()) {
            reportLatchIssue("comb always branch shadow state missing target", entry);
            return std::nullopt;
        }

        grh::Value* trueValue = rebuildShadowValue(*entry, trueIt->second);
        grh::Value* falseValue = rebuildShadowValue(*entry, falseIt->second);
        if (!trueValue || !falseValue) {
            return std::nullopt;
        }

        grh::Value* muxValue =
            createMuxForEntry(*entry, condition, *trueValue, *falseValue, label);
        if (!muxValue) {
            return std::nullopt;
        }

        ShadowState mergedState;
        mergedState.slices.push_back(buildFullSlice(*entry, *muxValue));
        mergedState.composed = muxValue;
        mergedState.dirty = false;
        falseIt->second = std::move(mergedState);
    }

    falseFrame.touched.insert(coverage.begin(), coverage.end());
    return std::move(falseFrame);
}

WriteBackMemo::Slice AlwaysConverter::buildFullSlice(const SignalMemoEntry& entry,
                                                         grh::Value& value) {
    WriteBackMemo::Slice slice;
    if (entry.symbol && !entry.symbol->name.empty()) {
        slice.path = std::string(entry.symbol->name);
    }
    const int64_t width = entry.width > 0 ? entry.width : 1;
    slice.msb = width - 1;
    slice.lsb = 0;
    slice.value = &value;
    slice.originExpr = nullptr;
    return slice;
}

grh::Value* AlwaysConverter::createMuxForEntry(const SignalMemoEntry& entry,
                                                   grh::Value& condition, grh::Value& onTrue,
                                                   grh::Value& onFalse, std::string_view label) {
    const int64_t width = entry.width > 0 ? entry.width : 1;
    if (onTrue.width() != width || onFalse.width() != width) {
        reportLatchIssue("comb always mux width mismatch", &entry);
        return nullptr;
    }

    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kMux, makeShadowOpName(entry, label));
    op.addOperand(condition);
    op.addOperand(onTrue);
    op.addOperand(onFalse);
    grh::Value& result =
        graph_.createValue(makeShadowValueName(entry, label), width, entry.isSigned);
    op.addResult(result);
    return &result;
}

grh::Value* AlwaysConverter::buildCaseMatch(const slang::ast::CaseStatement::ItemGroup& item,
                                                grh::Value& controlValue,
                                                slang::ast::CaseStatementCondition condition) {
    std::vector<grh::Value*> terms;
    terms.reserve(item.expressions.size());

    for (const slang::ast::Expression* expr : item.expressions) {
        if (!expr) {
            continue;
        }
        grh::Value* rhsVal = rhsConverter_->convert(*expr);
        if (!rhsVal) {
            return nullptr;
        }
        grh::Value* term = nullptr;
        if (condition == slang::ast::CaseStatementCondition::WildcardXOrZ ||
            condition == slang::ast::CaseStatementCondition::WildcardJustZ) {
            term = buildWildcardEquality(controlValue, *rhsVal, *expr, condition);
        }
        if (!term) {
            term = buildEquality(controlValue, *rhsVal, "case_eq");
        }
        if (!term) {
            return nullptr;
        }
        terms.push_back(term);
    }

    if (terms.empty()) {
        reportLatchIssue("comb always case item lacks expressions");
        return nullptr;
    }

    grh::Value* match = terms.front();
    for (std::size_t idx = 1; idx < terms.size(); ++idx) {
        grh::Value* combined = buildLogicOr(*match, *terms[idx]);
        if (!combined) {
            return nullptr;
        }
        match = combined;
    }
    return match;
}

grh::Value* AlwaysConverter::buildEquality(grh::Value& lhs, grh::Value& rhs,
                                               std::string_view hint) {
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kEq, makeControlOpName(hint));
    op.addOperand(lhs);
    op.addOperand(rhs);
    grh::Value& result = graph_.createValue(makeControlValueName(hint), 1, /*isSigned=*/false);
    op.addResult(result);
    return &result;
}

grh::Value* AlwaysConverter::buildLogicOr(grh::Value& lhs, grh::Value& rhs) {
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kOr, makeControlOpName("case_or"));
    op.addOperand(lhs);
    op.addOperand(rhs);
    grh::Value& result = graph_.createValue(makeControlValueName("case_or"), 1,
                                            /*isSigned=*/false);
    op.addResult(result);
    return &result;
}

grh::Value* AlwaysConverter::buildWildcardEquality(
    grh::Value& controlValue, grh::Value& rhsValue, const slang::ast::Expression& rhsExpr,
    slang::ast::CaseStatementCondition condition) {
    const int64_t width = controlValue.width();
    if (width <= 0) {
        return nullptr;
    }

    std::optional<slang::SVInt> literalOpt = evaluateConstantInt(rhsExpr, /*allowUnknown=*/true);
    if (!literalOpt) {
        return nullptr;
    }
    slang::SVInt literal = literalOpt->resize(static_cast<slang::bitwidth_t>(width));

    std::string maskLiteral = std::to_string(width);
    maskLiteral.append("'b");
    bool hasWildcard = false;
    for (int64_t bit = width - 1; bit >= 0; --bit) {
        slang::logic_t value = literal[static_cast<int32_t>(bit)];
        bool wildcard = false;
    if (condition == slang::ast::CaseStatementCondition::WildcardXOrZ) {
            wildcard = value.isUnknown();
        }
    else if (condition == slang::ast::CaseStatementCondition::WildcardJustZ) {
            wildcard = value.value == slang::logic_t::Z_VALUE;
    }
        if (wildcard) {
            maskLiteral.push_back('0');
            hasWildcard = true;
        }
        else {
            maskLiteral.push_back('1');
        }
    }

    if (!hasWildcard) {
        return nullptr;
    }

    slang::SVInt maskValue = slang::SVInt::fromString(maskLiteral);
    grh::Operation& xorOp =
        graph_.createOperation(grh::OperationKind::kXor, makeControlOpName("case_wild_xor"));
    xorOp.addOperand(controlValue);
    xorOp.addOperand(rhsValue);
    grh::Value& xorResult =
        graph_.createValue(makeControlValueName("case_wild_xor"), width, /*isSigned=*/false);
    xorOp.addResult(xorResult);

    grh::Value* maskConst = createLiteralValue(maskValue, /*isSigned=*/false, "case_wild_mask");
    if (!maskConst) {
        return nullptr;
    }

    grh::Operation& andOp =
        graph_.createOperation(grh::OperationKind::kAnd, makeControlOpName("case_wild_and"));
    andOp.addOperand(xorResult);
    andOp.addOperand(*maskConst);
    grh::Value& masked =
        graph_.createValue(makeControlValueName("case_wild_and"), width, /*isSigned=*/false);
    andOp.addResult(masked);

    grh::Value* zero = createZeroValue(width);
    if (!zero) {
        return nullptr;
    }
    return buildEquality(masked, *zero, "case_wild_eq0");
}

grh::Value* AlwaysConverter::createLiteralValue(const slang::SVInt& literal, bool isSigned,
                                                    std::string_view hint) {
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kConstant, makeControlOpName(hint));
    grh::Value& value = graph_.createValue(makeControlValueName(hint),
                                           static_cast<int64_t>(literal.getBitWidth()),
                                           isSigned);
    op.addResult(value);
    op.setAttribute("constValue",
                    literal.toString(slang::LiteralBase::Hex, true, literal.getBitWidth()));
    return &value;
}

std::string AlwaysConverter::makeControlOpName(std::string_view suffix) {
    std::string base = "_comb_ctrl_op_";
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(controlNameCounter_++));
    return base;
}

std::string AlwaysConverter::makeControlValueName(std::string_view suffix) {
    std::string base = "_comb_ctrl_val_";
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(controlNameCounter_++));
    return base;
}

void AlwaysConverter::reportLatchIssue(std::string_view context,
                                           const SignalMemoEntry* entry) {
    if (!diagnostics_) {
        return;
    }
    std::string message(context);
    if (entry && entry->symbol && !entry->symbol->name.empty()) {
        message.append(" (signal = ");
        message.append(std::string(entry->symbol->name));
        message.push_back(')');
    }
    diagnostics_->nyi(block_, std::move(message));
}

void AlwaysConverter::checkCaseUniquePriority(const slang::ast::CaseStatement& stmt) {
    using slang::ast::UniquePriorityCheck;
    UniquePriorityCheck check = stmt.check;
    if (check != UniquePriorityCheck::Unique && check != UniquePriorityCheck::Unique0) {
        return;
    }

    std::unordered_map<std::string, const slang::ast::Expression*> seen;
    for (const auto& item : stmt.items) {
        for (const slang::ast::Expression* expr : item.expressions) {
            if (!expr) {
                continue;
            }
            std::optional<slang::SVInt> constant = evaluateConstantInt(*expr, /*allowUnknown=*/false);
            if (!constant) {
                continue;
            }
            std::string key = std::to_string(constant->getBitWidth());
            key.push_back(':');
            key.append(constant->toString(slang::LiteralBase::Hex, true));
            auto [it, inserted] = seen.emplace(key, expr);
            if (!inserted) {
                if (diagnostics_) {
                    std::string message = "unique case items overlap on constant value ";
                    message.append(constant->toString(slang::LiteralBase::Hex, true));
                    diagnostics_->nyi(block_, std::move(message));
                }
                return;
            }
        }
    }
}

std::optional<slang::SVInt>
AlwaysConverter::evaluateConstantInt(const slang::ast::Expression& expr, bool allowUnknown) {
    if (!expr.type || !expr.type->isIntegral()) {
        return std::nullopt;
    }
    slang::ast::EvalContext& ctx = ensureEvalContext();
    ctx.reset();
    seedEvalContextWithLoopValues(ctx);
    slang::ConstantValue value = expr.eval(ctx);
    if (!value || !value.isInteger()) {
        return std::nullopt;
    }
    if (value.hasUnknown() && !allowUnknown) {
        return std::nullopt;
    }
    return value.integer();
}

std::optional<bool> AlwaysConverter::evaluateStaticCondition(
    const slang::ast::Expression& expr) {
    if (!expr.type) {
        return std::nullopt;
    }
    slang::ast::EvalContext& ctx = ensureEvalContext();
    ctx.reset();
    seedEvalContextWithLoopValues(ctx);
    slang::ConstantValue value = expr.eval(ctx);
    if (!value || value.hasUnknown()) {
        return std::nullopt;
    }
    if (value.isTrue()) {
        return true;
    }
    if (value.isFalse()) {
        return false;
    }
    return std::nullopt;
}

slang::ast::EvalContext& AlwaysConverter::ensureEvalContext() {
    if (!evalContext_) {
        evalContext_ = std::make_unique<slang::ast::EvalContext>(block_);
    }
    return *evalContext_;
}

slang::ast::EvalContext& AlwaysConverter::ensureLoopEvalContext() {
    if (!loopEvalContext_) {
        loopEvalContext_ = std::make_unique<slang::ast::EvalContext>(block_);
    }
    return *loopEvalContext_;
}

void AlwaysConverter::seedEvalContextWithLoopValues(slang::ast::EvalContext& ctx) {
    if (loopValueMap_.empty()) {
        return;
    }
    for (const auto& [symbol, info] : loopValueMap_) {
        if (!symbol) {
            continue;
        }
        slang::ConstantValue literal(info.literal);
        ctx.createLocal(symbol, std::move(literal));
    }
}

bool AlwaysConverter::prepareForLoopState(const slang::ast::ForLoopStatement& stmt,
                                              std::vector<ForLoopVarState>& states,
                                              slang::ast::EvalContext& ctx) {
    slang::ast::EvalContext initEvalCtx(block_);
    auto evaluateInitializer = [&](const slang::ast::Expression& expr)
        -> std::optional<slang::SVInt> {
        initEvalCtx.reset();
        slang::ConstantValue value = expr.eval(initEvalCtx);
        if (!value || !value.isInteger() || value.hasUnknown()) {
            return std::nullopt;
        }
        return value.integer();
    };

    auto addLoopVar = [&](const slang::ast::ValueSymbol& symbol,
                          const slang::ast::Expression& initExpr) -> bool {
        const slang::ast::Type& type = symbol.getType();
        if (!type.isIntegral()) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " for-loop variable must be integral";
                diagnostics_->nyi(symbol, std::move(message));
            }
            return false;
        }

        std::optional<slang::SVInt> initValue = evaluateInitializer(initExpr);
        if (!initValue) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " for-loop initializer must be constant";
                diagnostics_->nyi(symbol, std::move(message));
            }
            return false;
        }

        const int64_t rawWidth = static_cast<int64_t>(type.getBitstreamWidth());
        const slang::bitwidth_t width = static_cast<slang::bitwidth_t>(rawWidth > 0 ? rawWidth : 1);
        slang::SVInt sized = initValue->resize(width);
        sized.setSigned(type.isSigned());
        slang::ConstantValue initConst(sized);
        slang::ConstantValue* storage = ctx.createLocal(&symbol, std::move(initConst));
        if (!storage) {
            return false;
        }

        ForLoopVarState state;
        state.symbol = &symbol;
        states.push_back(state);
        return true;
    };

    if (!stmt.loopVars.empty()) {
        for (const slang::ast::VariableSymbol* var : stmt.loopVars) {
            if (!var) {
                continue;
            }
            const slang::ast::Expression* initExpr = var->getInitializer();
            if (!initExpr) {
                if (diagnostics_) {
                    std::string message =
                        std::string(modeLabel()) + " for-loop variable requires an initializer";
                    diagnostics_->nyi(*var, std::move(message));
                }
                return false;
            }
            if (!addLoopVar(*var, *initExpr)) {
                return false;
            }
        }
    }
    else {
        if (stmt.initializers.empty()) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " for-loop requires an initializer";
                diagnostics_->nyi(block_, std::move(message));
            }
            return false;
        }
        for (const slang::ast::Expression* initExpr : stmt.initializers) {
            const auto* assign = initExpr ? initExpr->as_if<slang::ast::AssignmentExpression>()
                                          : nullptr;
            if (!assign) {
                if (diagnostics_) {
                    std::string message =
                        std::string(modeLabel()) +
                        " for-loop initializer must be an assignment";
                    diagnostics_->nyi(block_, std::move(message));
                }
                return false;
            }
            const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(assign->left());
            if (!symbol) {
                if (diagnostics_) {
                    std::string message =
                        std::string(modeLabel()) +
                        " for-loop initializer must target a variable symbol";
                    diagnostics_->nyi(block_, std::move(message));
                }
                return false;
            }
            if (!addLoopVar(*symbol, assign->right())) {
                return false;
            }
        }
    }

    if (states.empty()) {
        if (diagnostics_) {
            std::string message =
                std::string(modeLabel()) + " for-loop has no supported loop variables";
            diagnostics_->nyi(block_, std::move(message));
        }
        return false;
    }

    return true;
}

bool AlwaysConverter::evaluateForLoopCondition(const slang::ast::ForLoopStatement& stmt,
                                                   slang::ast::EvalContext& ctx, bool& result) {
    if (!stmt.stopExpr) {
        result = false;
        return true;
    }

    slang::ConstantValue cond = stmt.stopExpr->eval(ctx);
    if (!cond || cond.hasUnknown()) {
        if (diagnostics_) {
            std::string message =
                std::string(modeLabel()) + " for-loop stop expression must be constant";
            diagnostics_->nyi(block_, std::move(message));
        }
        return false;
    }

    if (cond.isTrue()) {
        result = true;
        return true;
    }
    if (cond.isFalse()) {
        result = false;
        return true;
    }

    if (cond.isInteger()) {
        const slang::SVInt& intVal = cond.integer();
        slang::SVInt zero(intVal.getBitWidth(), 0, intVal.isSigned());
        slang::logic_t eqZero = intVal == zero;
        result = !static_cast<bool>(eqZero);
        return true;
    }

    if (diagnostics_) {
        std::string message =
            std::string(modeLabel()) + " for-loop stop expression is not boolean";
        diagnostics_->nyi(block_, std::move(message));
    }
    return false;
}

bool AlwaysConverter::executeForLoopSteps(const slang::ast::ForLoopStatement& stmt,
                                              slang::ast::EvalContext& ctx) {
    for (const slang::ast::Expression* step : stmt.steps) {
        if (!step) {
            continue;
        }
        slang::ConstantValue value = step->eval(ctx);
        if (!value) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " for-loop step expression failed evaluation";
                diagnostics_->nyi(block_, std::move(message));
            }
            return false;
        }
    }
    return true;
}

bool AlwaysConverter::updateLoopBindings(std::span<const ForLoopVarState> states,
                                             slang::ast::EvalContext& ctx) {
    for (const ForLoopVarState& state : states) {
        if (!state.symbol) {
            continue;
        }
        slang::ConstantValue* storage = ctx.findLocal(state.symbol);
        if (!storage || !storage->isInteger()) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) +
                    " for-loop variable evaluated to non-integer value";
                if (state.symbol && !state.symbol->name.empty()) {
                    message.append(" (");
                    message.append(std::string(state.symbol->name));
                    message.push_back(')');
                }
                diagnostics_->nyi(*state.symbol, std::move(message));
            }
            return false;
        }

        slang::SVInt value = storage->integer();
        if (!assignLoopValue(*state.symbol, value)) {
            return false;
        }
    }
    return true;
}

bool AlwaysConverter::assignLoopValue(const slang::ast::ValueSymbol& symbol,
                                          const slang::SVInt& value) {
    const slang::ast::Type& type = symbol.getType();
    const int64_t rawWidth = static_cast<int64_t>(type.getBitstreamWidth());
    const slang::bitwidth_t width = static_cast<slang::bitwidth_t>(rawWidth > 0 ? rawWidth : 1);
    slang::SVInt resized = value.resize(width);
    resized.setSigned(type.isSigned());

    std::string hint =
        symbol.name.empty() ? std::string("loop_idx") : sanitizeForGraphName(symbol.name);
    grh::Value* literal = createLiteralValue(resized, type.isSigned(), hint);
    if (!literal) {
        return false;
    }

    LoopValueInfo info;
    info.literal = resized;
    info.value = literal;
    loopValueMap_[&symbol] = std::move(info);
    return true;
}

bool AlwaysConverter::runForeachRecursive(const slang::ast::ForeachLoopStatement& stmt,
                                              std::span<const ForeachDimState> dims,
                                              std::size_t depth, std::size_t& iterationCount) {
    if (depth >= dims.size()) {
        if (iterationCount++ >= kMaxLoopIterations) {
            if (diagnostics_) {
                std::string message =
                    std::string(modeLabel()) + " foreach loop exceeded maximum unrolled iterations";
                diagnostics_->nyi(block_, std::move(message));
            }
            return false;
        }
        visitStatement(stmt.body);
        if (loopControlTargetsCurrentLoop()) {
            if (pendingLoopControl_ == LoopControl::Break) {
                pendingLoopControl_ = LoopControl::None;
                return false;
            }
            if (pendingLoopControl_ == LoopControl::Continue) {
                pendingLoopControl_ = LoopControl::None;
            }
        }
        return true;
    }

    const ForeachDimState& dim = dims[depth];
    for (int32_t index = dim.start;; index += dim.step) {
        if (dim.loopVar) {
            slang::SVInt literal(index);
            if (!assignLoopValue(*dim.loopVar, literal)) {
                return false;
            }
        }

        if (!runForeachRecursive(stmt, dims, depth + 1, iterationCount)) {
            return false;
        }

        if (loopControlTargetsCurrentLoop()) {
            if (pendingLoopControl_ == LoopControl::Break) {
                pendingLoopControl_ = LoopControl::None;
                return false;
            }
            if (pendingLoopControl_ == LoopControl::Continue) {
                pendingLoopControl_ = LoopControl::None;
            }
        }

        if (index == dim.stop) {
            break;
        }
    }

    return true;
}

void AlwaysConverter::pushLoopScope(std::vector<const slang::ast::ValueSymbol*> symbols) {
    loopScopeStack_.push_back(std::move(symbols));
}

void AlwaysConverter::popLoopScope() {
    if (loopScopeStack_.empty()) {
        return;
    }
    for (const slang::ast::ValueSymbol* symbol : loopScopeStack_.back()) {
        loopValueMap_.erase(symbol);
    }
    loopScopeStack_.pop_back();
}

grh::Value* AlwaysConverter::lookupLoopValue(const slang::ast::ValueSymbol& symbol) const {
    auto it = loopValueMap_.find(&symbol);
    if (it == loopValueMap_.end()) {
        return nullptr;
    }
    return it->second.value;
}

bool AlwaysConverter::currentContextStatic() const {
    if (controlContextStack_.empty()) {
        return true;
    }
    return controlContextStack_.back();
}

bool AlwaysConverter::loopControlTargetsCurrentLoop() const {
    if (pendingLoopControl_ == LoopControl::None) {
        return false;
    }
    return pendingLoopDepth_ == loopContextStack_.size();
}

void WriteBackMemo::recordWrite(const SignalMemoEntry& target, AssignmentKind kind,
                                const slang::ast::Symbol* originSymbol,
                                std::vector<Slice> slices) {
    for (Entry& entry : entries_) {
        if (entry.target == &target && entry.kind == kind) {
            if (!entry.originSymbol && originSymbol) {
                entry.originSymbol = originSymbol;
            }
            for (Slice& slice : slices) {
                entry.slices.push_back(std::move(slice));
            }
            return;
        }
    }

    Entry entry;
    entry.target = &target;
    entry.kind = kind;
    entry.originSymbol = originSymbol;
    entry.slices = std::move(slices);
    entries_.push_back(std::move(entry));
}

void WriteBackMemo::clear() {
    entries_.clear();
}

std::string WriteBackMemo::makeOperationName(const Entry& entry, std::string_view suffix) {
    std::string base;
    if (entry.target && entry.target->symbol && !entry.target->symbol->name.empty()) {
        base = sanitizeForGraphName(entry.target->symbol->name);
    }
    if (base.empty()) {
        base = "_write_back";
    }
    base.push_back('_');
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(nameCounter_++));
    return base;
}

std::string WriteBackMemo::makeValueName(const Entry& entry, std::string_view suffix) {
    std::string base;
    if (entry.target && entry.target->symbol && !entry.target->symbol->name.empty()) {
        base = sanitizeForGraphName(entry.target->symbol->name);
    }
    if (base.empty()) {
        base = "_write_back_val";
    }
    base.push_back('_');
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(nameCounter_++));
    return base;
}

const slang::ast::Symbol* WriteBackMemo::originFor(const Entry& entry) const {
    if (entry.originSymbol) {
        return entry.originSymbol;
    }
    if (entry.target && entry.target->symbol) {
        return entry.target->symbol;
    }
    return nullptr;
}

void WriteBackMemo::reportIssue(const Entry& entry, std::string message,
                                ElaborateDiagnostics* diagnostics) const {
    if (!diagnostics) {
        return;
    }
    if (const slang::ast::Symbol* symbol = originFor(entry)) {
        diagnostics->nyi(*symbol, std::move(message));
    }
}

grh::Value* WriteBackMemo::composeSlices(Entry& entry, grh::Graph& graph,
                                         ElaborateDiagnostics* diagnostics) {
    if (!entry.target) {
        reportIssue(entry, "Write-back target is missing memo metadata", diagnostics);
        return nullptr;
    }
    if (entry.slices.empty()) {
        reportIssue(entry, "Write-back entry has no slices to compose", diagnostics);
        return nullptr;
    }

    std::sort(entry.slices.begin(), entry.slices.end(),
              [](const Slice& lhs, const Slice& rhs) {
                  if (lhs.msb != rhs.msb) {
                      return lhs.msb > rhs.msb;
                  }
                  return lhs.lsb > rhs.lsb;
              });

    const int64_t targetWidth = entry.target->width > 0 ? entry.target->width : 1;
    int64_t expectedMsb = targetWidth - 1;
    std::vector<grh::Value*> components;
    components.reserve(entry.slices.size() + 2);

    for (const Slice& slice : entry.slices) {
        if (!slice.value) {
            reportIssue(entry, "Write-back slice is missing RHS value", diagnostics);
            return nullptr;
        }
        if (slice.msb < slice.lsb) {
            reportIssue(entry, "Write-back slice has invalid bit range", diagnostics);
            return nullptr;
        }

        if (slice.msb > expectedMsb) {
            std::ostringstream oss;
            oss << "Write-back slice exceeds target width; slice msb=" << slice.msb
                << " expected at most " << expectedMsb;
            reportIssue(entry, oss.str(), diagnostics);
            return nullptr;
        }

        const int64_t gapWidth = expectedMsb - slice.msb;
        if (gapWidth > 0) {
            grh::Value* zero = createZeroValue(entry, gapWidth, graph);
            if (!zero) {
                reportIssue(entry, "Failed to create zero-fill value for write-back gap",
                            diagnostics);
                return nullptr;
            }
            components.push_back(zero);
            expectedMsb -= gapWidth;
        }

        if (slice.msb != expectedMsb) {
            std::ostringstream oss;
            oss << "Write-back bookkeeping error; slice msb=" << slice.msb
                << " but expected " << expectedMsb;
            reportIssue(entry, oss.str(), diagnostics);
            return nullptr;
        }

        const int64_t sliceWidth = slice.msb - slice.lsb + 1;
        if (slice.value->width() != sliceWidth) {
            std::ostringstream oss;
            oss << "Write-back slice width mismatch; slice covers " << sliceWidth
                << " bits but RHS value width is " << slice.value->width();
            reportIssue(entry, oss.str(), diagnostics);
            return nullptr;
        }

        components.push_back(slice.value);
        expectedMsb = slice.lsb - 1;
    }

    if (expectedMsb >= 0) {
        grh::Value* zero = createZeroValue(entry, expectedMsb + 1, graph);
        if (!zero) {
            reportIssue(entry, "Failed to create zero-fill value for trailing gap", diagnostics);
            return nullptr;
        }
        components.push_back(zero);
        expectedMsb = -1;
    }

    if (components.empty()) {
        reportIssue(entry, "Write-back entry produced no value components", diagnostics);
        return nullptr;
    }

    if (components.size() == 1) {
        return components.front();
    }

    grh::Operation& concat =
        graph.createOperation(grh::OperationKind::kConcat, makeOperationName(entry, "concat"));
    for (grh::Value* component : components) {
        concat.addOperand(*component);
    }

    grh::Value& composed =
        graph.createValue(makeValueName(entry, "concat"), targetWidth, entry.target->isSigned);
    concat.addResult(composed);
    return &composed;
}

void WriteBackMemo::attachToTarget(const Entry& entry, grh::Value& composedValue,
                                   grh::Graph& graph, ElaborateDiagnostics* diagnostics) {
    if (!entry.target) {
        reportIssue(entry, "Missing target when attaching write-back value", diagnostics);
        return;
    }

    if (!entry.target->stateOp) {
        grh::Value* targetValue = entry.target->value;
        if (!targetValue) {
            reportIssue(entry, "Net write-back lacks GRH value handle", diagnostics);
            return;
        }
        if (targetValue->width() != composedValue.width()) {
            std::ostringstream oss;
            oss << "Net write-back width mismatch; target width=" << targetValue->width()
                << " source width=" << composedValue.width();
            reportIssue(entry, oss.str(), diagnostics);
            return;
        }
        grh::Operation& assign =
            graph.createOperation(grh::OperationKind::kAssign, makeOperationName(entry, "assign"));
        assign.addOperand(composedValue);
        assign.addResult(*targetValue);
        return;
    }

    grh::Operation* stateOp = entry.target->stateOp;
    if (!stateOp) {
        reportIssue(entry, "Sequential write-back missing state operation", diagnostics);
        return;
    }

    if (stateOp->kind() == grh::OperationKind::kMemory) {
        reportIssue(entry, "Memory write-back is not implemented yet", diagnostics);
        return;
    }

    if (!stateOp->operands().empty()) {
        reportIssue(entry, "State operation already has a data operand", diagnostics);
        return;
    }

    if (!stateOp->results().empty()) {
        grh::Value* stateValue = stateOp->results().front();
        if (stateValue && stateValue->width() != composedValue.width()) {
            std::ostringstream oss;
            oss << "Register write-back width mismatch; state width=" << stateValue->width()
                << " source width=" << composedValue.width();
            reportIssue(entry, oss.str(), diagnostics);
            return;
        }
    }

    stateOp->addOperand(composedValue);
}

grh::Value* WriteBackMemo::createZeroValue(const Entry& entry, int64_t width, grh::Graph& graph) {
    if (width <= 0) {
        return nullptr;
    }

    grh::Operation& op =
        graph.createOperation(grh::OperationKind::kConstant, makeOperationName(entry, "zero"));
    grh::Value& value = graph.createValue(makeValueName(entry, "zero"), width, /*isSigned=*/false);
    op.addResult(value);
    std::ostringstream oss;
    oss << width << "'h0";
    op.setAttribute("constValue", oss.str());
    return &value;
}

void WriteBackMemo::finalize(grh::Graph& graph, ElaborateDiagnostics* diagnostics) {
    for (Entry& entry : entries_) {
        if (entry.consumed) {
            continue;
        }
        grh::Value* composed = composeSlices(entry, graph, diagnostics);
        if (!composed) {
            continue;
        }
        attachToTarget(entry, *composed, graph, diagnostics);
    }
    entries_.clear();
}

//===---------------------------------------------------------------------===//
// RHSConverter implementation
//===---------------------------------------------------------------------===//

RHSConverter::RHSConverter(Context context) : graph_(context.graph), origin_(context.origin),
                                              diagnostics_(context.diagnostics),
                                              netMemo_(context.netMemo), regMemo_(context.regMemo) {
    instanceId_ = nextConverterInstanceId();
}

grh::Value* RHSConverter::convert(const slang::ast::Expression& expr) {
    if (!graph_) {
        return nullptr;
    }

    if (auto it = cache_.find(&expr); it != cache_.end()) {
        return it->second;
    }

    grh::Value* value = convertExpression(expr);
    if (value) {
        cache_[&expr] = value;
    }
    return value;
}

std::string RHSConverter::makeValueName(std::string_view hint, std::size_t index) const {
    std::string base = hint.empty() ? std::string("_rhs_value")
                                    : sanitizeForGraphName(hint, /* allowLeadingDigit */ false);
    if (base.empty()) {
        base = "_rhs_value";
    }
    base.push_back('_');
    base.append(std::to_string(instanceId_));
    base.push_back('_');
    base.append(std::to_string(index));
    return base;
}

std::string RHSConverter::makeOperationName(std::string_view hint, std::size_t index) const {
    std::string base = hint.empty() ? std::string("_rhs_op")
                                    : sanitizeForGraphName(hint, /* allowLeadingDigit */ false);
    if (base.empty()) {
        base = "_rhs_op";
    }
    base.push_back('_');
    base.append(std::to_string(instanceId_));
    base.push_back('_');
    base.append(std::to_string(index));
    return base;
}

grh::Value* RHSConverter::convertExpression(const slang::ast::Expression& expr) {
    using slang::ast::ExpressionKind;
    switch (expr.kind) {
    case ExpressionKind::NamedValue:
        return convertNamedValue(expr);
    case ExpressionKind::IntegerLiteral:
    case ExpressionKind::UnbasedUnsizedIntegerLiteral:
        return convertLiteral(expr);
    case ExpressionKind::ElementSelect:
        return convertElementSelect(expr.as<slang::ast::ElementSelectExpression>());
    case ExpressionKind::RangeSelect:
        return convertRangeSelect(expr.as<slang::ast::RangeSelectExpression>());
    case ExpressionKind::MemberAccess:
        return convertMemberAccess(expr.as<slang::ast::MemberAccessExpression>());
    case ExpressionKind::UnaryOp:
        return convertUnary(expr.as<slang::ast::UnaryExpression>());
    case ExpressionKind::BinaryOp:
        return convertBinary(expr.as<slang::ast::BinaryExpression>());
    case ExpressionKind::ConditionalOp:
        return convertConditional(expr.as<slang::ast::ConditionalExpression>());
    case ExpressionKind::Concatenation:
        return convertConcatenation(expr.as<slang::ast::ConcatenationExpression>());
    case ExpressionKind::Replication:
        return convertReplication(expr.as<slang::ast::ReplicationExpression>());
    case ExpressionKind::Conversion:
        return convertConversion(expr.as<slang::ast::ConversionExpression>());
    default:
        reportUnsupported("expression kind", expr);
        return nullptr;
    }
}

grh::Value* RHSConverter::convertNamedValue(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        const auto& named = expr.as<slang::ast::NamedValueExpression>();

        if (const SignalMemoEntry* entry = findMemoEntry(named.symbol)) {
            if (grh::Value* memoHandler = handleMemoEntry(named, *entry)) {
                return memoHandler;
            }
            if (grh::Value* value = resolveMemoValue(*entry)) {
                return value;
            }
        }

        if (grh::Value* custom = handleCustomNamedValue(named)) {
            return custom;
        }

        if (grh::Value* fallback = resolveGraphValue(named.symbol)) {
            return fallback;
        }

        if (grh::Value* paramValue = materializeParameterValue(named)) {
            return paramValue;
        }
    }

    reportUnsupported("named value", expr);
    return nullptr;
}

grh::Value* RHSConverter::materializeParameterValue(const slang::ast::NamedValueExpression& expr) {
    const auto* param = expr.symbol.as_if<slang::ast::ParameterSymbol>();
    if (!param) {
        return nullptr;
    }

    const slang::ConstantValue& constValue = param->getValue(expr.sourceRange);
    if (!constValue || !constValue.isInteger()) {
        if (diagnostics_ && origin_) {
            std::string message = "Parameter ";
            message.append(std::string(param->name));
            message.append(" has unsupported value type for RHS conversion");
            diagnostics_->nyi(*origin_, std::move(message));
        }
        return nullptr;
    }

    const slang::ast::Type* type = expr.type;
    if (!type) {
        type = &param->getType();
    }
    if (!type) {
        if (diagnostics_ && origin_) {
            std::string message = "Parameter ";
            message.append(std::string(param->name));
            message.append(" lacks resolved type information");
            diagnostics_->nyi(*origin_, std::move(message));
        }
        return nullptr;
    }

    const slang::SVInt& literal = constValue.integer();
    std::string_view hint = param->name.empty() ? "param" : param->name;
    return createConstantValue(literal, *type, hint);
}

grh::Value* RHSConverter::convertLiteral(const slang::ast::Expression& expr) {
    switch (expr.kind) {
    case slang::ast::ExpressionKind::IntegerLiteral: {
        const auto& literal = expr.as<slang::ast::IntegerLiteral>();
        return createConstantValue(literal.getValue(), *expr.type, "const");
    }
    case slang::ast::ExpressionKind::UnbasedUnsizedIntegerLiteral: {
        const auto& literal = expr.as<slang::ast::UnbasedUnsizedIntegerLiteral>();
        return createConstantValue(literal.getValue(), *expr.type, "const");
    }
    default:
        reportUnsupported("literal", expr);
        return nullptr;
    }
}

grh::Value* RHSConverter::convertUnary(const slang::ast::UnaryExpression& expr) {
    grh::Value* operand = convert(expr.operand());
    if (!operand) {
        return nullptr;
    }

    using slang::ast::UnaryOperator;
    switch (expr.op) {
    case UnaryOperator::Plus:
        return operand;
    case UnaryOperator::Minus: {
        grh::Value* zero = createZeroValue(*expr.type, "neg_zero");
        if (!zero) {
            return nullptr;
        }
        return buildBinaryOp(grh::OperationKind::kSub, *zero, *operand, expr, "neg");
    }
    case UnaryOperator::BitwiseNot:
        return buildUnaryOp(grh::OperationKind::kNot, *operand, expr, "not");
    case UnaryOperator::LogicalNot:
        return buildUnaryOp(grh::OperationKind::kLogicNot, *operand, expr, "lnot");
    case UnaryOperator::BitwiseAnd:
        return buildUnaryOp(grh::OperationKind::kReduceAnd, *operand, expr, "red_and");
    case UnaryOperator::BitwiseOr:
        return buildUnaryOp(grh::OperationKind::kReduceOr, *operand, expr, "red_or");
    case UnaryOperator::BitwiseXor:
        return buildUnaryOp(grh::OperationKind::kReduceXor, *operand, expr, "red_xor");
    case UnaryOperator::BitwiseNand:
        return buildUnaryOp(grh::OperationKind::kReduceNand, *operand, expr, "red_nand");
    case UnaryOperator::BitwiseNor:
        return buildUnaryOp(grh::OperationKind::kReduceNor, *operand, expr, "red_nor");
    case UnaryOperator::BitwiseXnor:
        return buildUnaryOp(grh::OperationKind::kReduceXnor, *operand, expr, "red_xnor");
    default:
        reportUnsupported("unary operator", expr);
        return nullptr;
    }
}

grh::Value* RHSConverter::convertBinary(const slang::ast::BinaryExpression& expr) {
    grh::Value* lhs = convert(expr.left());
    grh::Value* rhs = convert(expr.right());
    if (!lhs || !rhs) {
        return nullptr;
    }

    using slang::ast::BinaryOperator;
    auto opKind = grh::OperationKind::kAssign;
    bool supported = true;
    switch (expr.op) {
    case BinaryOperator::Add:
        opKind = grh::OperationKind::kAdd;
        break;
    case BinaryOperator::Subtract:
        opKind = grh::OperationKind::kSub;
        break;
    case BinaryOperator::Multiply:
        opKind = grh::OperationKind::kMul;
        break;
    case BinaryOperator::Divide:
        opKind = grh::OperationKind::kDiv;
        break;
    case BinaryOperator::Mod:
        opKind = grh::OperationKind::kMod;
        break;
    case BinaryOperator::BinaryAnd:
        opKind = grh::OperationKind::kAnd;
        break;
    case BinaryOperator::BinaryOr:
        opKind = grh::OperationKind::kOr;
        break;
    case BinaryOperator::BinaryXor:
        opKind = grh::OperationKind::kXor;
        break;
    case BinaryOperator::BinaryXnor:
        opKind = grh::OperationKind::kXnor;
        break;
    case BinaryOperator::Equality:
    case BinaryOperator::CaseEquality:
        opKind = grh::OperationKind::kEq;
        break;
    case BinaryOperator::Inequality:
    case BinaryOperator::CaseInequality:
        opKind = grh::OperationKind::kNe;
        break;
    case BinaryOperator::GreaterThan:
        opKind = grh::OperationKind::kGt;
        break;
    case BinaryOperator::GreaterThanEqual:
        opKind = grh::OperationKind::kGe;
        break;
    case BinaryOperator::LessThan:
        opKind = grh::OperationKind::kLt;
        break;
    case BinaryOperator::LessThanEqual:
        opKind = grh::OperationKind::kLe;
        break;
    case BinaryOperator::LogicalAnd:
        opKind = grh::OperationKind::kLogicAnd;
        break;
    case BinaryOperator::LogicalOr:
        opKind = grh::OperationKind::kLogicOr;
        break;
    case BinaryOperator::LogicalShiftLeft:
    case BinaryOperator::ArithmeticShiftLeft:
        opKind = grh::OperationKind::kShl;
        break;
    case BinaryOperator::LogicalShiftRight:
        opKind = grh::OperationKind::kLShr;
        break;
    case BinaryOperator::ArithmeticShiftRight:
        opKind = grh::OperationKind::kAShr;
        break;
    case BinaryOperator::WildcardEquality:
    case BinaryOperator::WildcardInequality:
    case BinaryOperator::LogicalImplication:
    case BinaryOperator::LogicalEquivalence:
    case BinaryOperator::Power:
        supported = false;
        break;
    default:
        supported = false;
        break;
    }

    if (!supported) {
        reportUnsupported("binary operator", expr);
        return nullptr;
    }

    return buildBinaryOp(opKind, *lhs, *rhs, expr, "bin");
}

grh::Value* RHSConverter::convertConditional(const slang::ast::ConditionalExpression& expr) {
    if (expr.conditions.empty()) {
        reportUnsupported("conditional (missing condition)", expr);
        return nullptr;
    }

    const auto& condition = expr.conditions.front();
    if (condition.pattern) {
        reportUnsupported("patterned conditional", expr);
        return nullptr;
    }

    grh::Value* condValue = convert(*condition.expr);
    grh::Value* trueValue = convert(expr.left());
    grh::Value* falseValue = convert(expr.right());
    if (!condValue || !trueValue || !falseValue) {
        return nullptr;
    }

    return buildMux(*condValue, *trueValue, *falseValue, expr);
}

grh::Value* RHSConverter::convertConcatenation(
    const slang::ast::ConcatenationExpression& expr) {
    grh::Operation& op = createOperation(grh::OperationKind::kConcat, "concat");
    for (const slang::ast::Expression* operandExpr : expr.operands()) {
        if (!operandExpr) {
            continue;
        }
        grh::Value* operandValue = convert(*operandExpr);
        if (!operandValue) {
            return nullptr;
        }
        op.addOperand(*operandValue);
    }

    grh::Value& result = createTemporaryValue(*expr.type, "concat");
    op.addResult(result);
    return &result;
}

grh::Value* RHSConverter::convertReplication(const slang::ast::ReplicationExpression& expr) {
    std::optional<int64_t> replicateCount = evaluateConstantInt(expr.count());
    if (!replicateCount || *replicateCount <= 0) {
        reportUnsupported("replication count", expr);
        return nullptr;
    }

    grh::Value* operand = convert(expr.concat());
    if (!operand) {
        return nullptr;
    }

    grh::Operation& op = createOperation(grh::OperationKind::kReplicate, "replicate");
    op.addOperand(*operand);
    op.setAttribute("rep", static_cast<int64_t>(*replicateCount));
    grh::Value& result = createTemporaryValue(*expr.type, "replicate");
    op.addResult(result);
    return &result;
}

grh::Value* RHSConverter::convertConversion(const slang::ast::ConversionExpression& expr) {
    grh::Value* operand = convert(expr.operand());
    if (!operand) {
        return nullptr;
    }

    const TypeInfo info = deriveTypeInfo(*expr.type);
    if (operand->width() == info.width && operand->isSigned() == info.isSigned) {
        return operand;
    }
    return buildAssign(*operand, expr, "convert");
}

grh::Value* RHSConverter::convertElementSelect(const slang::ast::ElementSelectExpression& expr) {
    reportUnsupported("array select", expr);
    return nullptr;
}

grh::Value* RHSConverter::convertRangeSelect(const slang::ast::RangeSelectExpression& expr) {
    reportUnsupported("range select", expr);
    return nullptr;
}

grh::Value* RHSConverter::convertMemberAccess(const slang::ast::MemberAccessExpression& expr) {
    reportUnsupported("member access", expr);
    return nullptr;
}

grh::Value* RHSConverter::handleMemoEntry(const slang::ast::NamedValueExpression&,
                                          const SignalMemoEntry&) {
    return nullptr;
}

grh::Value* RHSConverter::handleCustomNamedValue(const slang::ast::NamedValueExpression&) {
    return nullptr;
}

grh::Value& RHSConverter::createTemporaryValue(const slang::ast::Type& type,
                                               std::string_view hint) {
    const TypeInfo info = deriveTypeInfo(type);
    std::string name = makeValueName(hint, valueCounter_++);
    return graph_->createValue(name, info.width > 0 ? info.width : 1, info.isSigned);
}

grh::Operation& RHSConverter::createOperation(grh::OperationKind kind, std::string_view hint) {
    std::string name = makeOperationName(hint, operationCounter_++);
    return graph_->createOperation(kind, name);
}

grh::Value* RHSConverter::createConstantValue(const slang::SVInt& value,
                                              const slang::ast::Type& type,
                                              std::string_view hint) {
    grh::Operation& op = createOperation(grh::OperationKind::kConstant, hint);
    grh::Value& result = createTemporaryValue(type, hint);
    op.addResult(result);
    op.setAttribute("constValue", formatConstantLiteral(value, type));
    return &result;
}

grh::Value* RHSConverter::createZeroValue(const slang::ast::Type& type,
                                          std::string_view hint) {
    const TypeInfo info = deriveTypeInfo(type);
    slang::SVInt literal(static_cast<slang::bitwidth_t>(info.width), uint64_t(0), info.isSigned);
    return createConstantValue(literal, type, hint);
}

grh::Value* RHSConverter::buildUnaryOp(grh::OperationKind kind, grh::Value& operand,
                                       const slang::ast::Expression& originExpr,
                                       std::string_view hint) {
    grh::Operation& op = createOperation(kind, hint);
    op.addOperand(operand);
    grh::Value& result = createTemporaryValue(*originExpr.type, hint);
    op.addResult(result);
    return &result;
}

grh::Value* RHSConverter::buildBinaryOp(grh::OperationKind kind, grh::Value& lhs, grh::Value& rhs,
                                        const slang::ast::Expression& originExpr,
                                        std::string_view hint) {
    grh::Operation& op = createOperation(kind, hint);
    op.addOperand(lhs);
    op.addOperand(rhs);
    grh::Value& result = createTemporaryValue(*originExpr.type, hint);
    op.addResult(result);
    return &result;
}

grh::Value* RHSConverter::buildMux(grh::Value& cond, grh::Value& onTrue, grh::Value& onFalse,
                                   const slang::ast::Expression& originExpr) {
    grh::Operation& op = createOperation(grh::OperationKind::kMux, "mux");
    op.addOperand(cond);
    op.addOperand(onTrue);
    op.addOperand(onFalse);
    grh::Value& result = createTemporaryValue(*originExpr.type, "mux");
    op.addResult(result);
    return &result;
}

grh::Value* RHSConverter::buildAssign(grh::Value& input, const slang::ast::Expression& originExpr,
                                      std::string_view hint) {
    grh::Operation& op = createOperation(grh::OperationKind::kAssign, hint);
    op.addOperand(input);
    grh::Value& result = createTemporaryValue(*originExpr.type, hint);
    op.addResult(result);
    return &result;
}

const SignalMemoEntry* RHSConverter::findMemoEntry(const slang::ast::ValueSymbol& symbol) const {
    auto finder = [&](std::span<const SignalMemoEntry> memo) -> const SignalMemoEntry* {
        for (const SignalMemoEntry& entry : memo) {
            if (entry.symbol == &symbol) {
                return &entry;
            }
        }
        return nullptr;
    };

    if (const SignalMemoEntry* entry = finder(netMemo_)) {
        return entry;
    }
    return finder(regMemo_);
}

grh::Value* RHSConverter::resolveMemoValue(const SignalMemoEntry& entry) {
    if (entry.value) {
        return entry.value;
    }

    if (entry.stateOp) {
        const grh::OperationKind kind = entry.stateOp->kind();
        if (kind == grh::OperationKind::kRegister || kind == grh::OperationKind::kRegisterRst ||
            kind == grh::OperationKind::kRegisterARst) {
            const std::vector<grh::Value*>& results = entry.stateOp->results();
            if (!results.empty() && results.front()) {
                return results.front();
            }
        }
    }

    if (diagnostics_ && origin_) {
        std::string symbolName = entry.symbol ? std::string(entry.symbol->name) : std::string();
        diagnostics_->nyi(*origin_,
                          "Memo entry missing GRH value for symbol " + symbolName);
    }
    return nullptr;
}

grh::Value* RHSConverter::resolveGraphValue(const slang::ast::ValueSymbol& symbol) {
    if (!graph_) {
        return nullptr;
    }

    const std::string_view symbolName = symbol.name;
    if (symbolName.empty()) {
        return nullptr;
    }

    return graph_->findValue(symbolName);
}

RHSConverter::TypeInfo RHSConverter::deriveTypeInfo(const slang::ast::Type& type) const {
    TypeInfo info;
    if (!type.isBitstreamType() || !type.isFixedSize()) {
        if (diagnostics_ && origin_) {
            diagnostics_->nyi(*origin_,
                              "RHS conversion requires fixed-size bitstream type: " +
                                  type.toString());
        }
        info.width = 1;
        info.isSigned = false;
        return info;
    }

    uint64_t bitWidth = type.getBitstreamWidth();
    if (bitWidth == 0) {
        bitWidth = 1;
    }
    const uint64_t maxWidth = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (bitWidth > maxWidth) {
        bitWidth = maxWidth;
    }

    info.width = static_cast<int64_t>(bitWidth);
    info.isSigned = type.isSigned();
    return info;
}

std::string RHSConverter::formatConstantLiteral(const slang::SVInt& value,
                                                const slang::ast::Type& type) const {
    (void)type;
    return value.toString(slang::LiteralBase::Hex, /* includeBase */ true,
                          slang::SVInt::MAX_BITS);
}

void RHSConverter::reportUnsupported(std::string_view what,
                                     const slang::ast::Expression& expr) {
    if (!diagnostics_ || !origin_) {
        return;
    }

    std::string message = "Unsupported RHS " + std::string(what);
    message.append(" (kind=");
    message.append(std::to_string(static_cast<int>(expr.kind)));
    message.push_back(' ');
    message.append(slang::ast::toString(expr.kind));
    message.push_back(')');
    diagnostics_->nyi(*origin_, std::move(message));
}

slang::ast::EvalContext& RHSConverter::ensureEvalContext() {
    if (!evalContext_) {
        evalContext_ = std::make_unique<slang::ast::EvalContext>(*origin_);
    }
    return *evalContext_;
}

std::optional<int64_t> RHSConverter::evaluateConstantInt(const slang::ast::Expression& expr) {
    if (!origin_) {
        return std::nullopt;
    }

    slang::ast::EvalContext& ctx = ensureEvalContext();
    ctx.reset();
    slang::ConstantValue value = expr.eval(ctx);
    if (!value || !value.isInteger() || value.hasUnknown()) {
        return std::nullopt;
    }

    std::optional<int64_t> asInt = value.integer().as<int64_t>();
    if (!asInt) {
        return std::nullopt;
    }
    return *asInt;
}

//===---------------------------------------------------------------------===//
// CombRHSConverter implementation
//===---------------------------------------------------------------------===//

CombRHSConverter::CombRHSConverter(Context context) : RHSConverter(context) {}

const SignalMemoEntry*
CombRHSConverter::findMemoEntryFromExpression(const slang::ast::Expression& expr) const {
    if (const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(expr)) {
        return findMemoEntry(*symbol);
    }
    return nullptr;
}

std::optional<CombRHSConverter::SliceRange>
CombRHSConverter::deriveStructFieldSlice(const slang::ast::MemberAccessExpression& expr) const {
    using slang::ast::FieldSymbol;
    const auto* field = expr.member.as_if<FieldSymbol>();
    if (!field) {
        return std::nullopt;
    }

    const slang::ast::Type& containerType = *expr.value().type;
    const uint64_t totalWidth = containerType.getBitstreamWidth();
    if (totalWidth == 0) {
        return std::nullopt;
    }

    const slang::ast::Type& canonical = containerType.getCanonicalType();
    const auto* scope = canonical.as_if<slang::ast::Scope>();
    if (!scope) {
        return std::nullopt;
    }

    int64_t currentMsb = static_cast<int64_t>(totalWidth) - 1;
    for (const slang::ast::FieldSymbol& candidate :
         scope->membersOfType<slang::ast::FieldSymbol>()) {
        const int64_t fieldWidth =
            static_cast<int64_t>(candidate.getType().getBitstreamWidth());
        if (fieldWidth <= 0) {
            continue;
        }
        const int64_t fieldLsb = currentMsb - fieldWidth + 1;
        if (&candidate == field) {
            return SliceRange{currentMsb, fieldLsb};
        }
        currentMsb = fieldLsb - 1;
    }
    return std::nullopt;
}

grh::Value* CombRHSConverter::buildStaticSlice(grh::Value& input, int64_t sliceStart,
                                               int64_t sliceEnd,
                                               const slang::ast::Expression& originExpr,
                                               std::string_view hint) {
    if (sliceStart < 0 || sliceEnd < sliceStart) {
        reportUnsupported("static slice range", originExpr);
        return nullptr;
    }

    grh::Operation& op = createOperation(grh::OperationKind::kSliceStatic, hint);
    op.addOperand(input);
    op.setAttribute("sliceStart", sliceStart);
    op.setAttribute("sliceEnd", sliceEnd);
    grh::Value& result = createTemporaryValue(*originExpr.type, hint);
    op.addResult(result);
    return &result;
}

grh::Value* CombRHSConverter::buildDynamicSlice(grh::Value& input, grh::Value& offset,
                                                int64_t sliceWidth,
                                                const slang::ast::Expression& originExpr,
                                                std::string_view hint) {
    if (sliceWidth <= 0) {
        reportUnsupported("dynamic slice width", originExpr);
        return nullptr;
    }
    grh::Operation& op = createOperation(grh::OperationKind::kSliceDynamic, hint);
    op.addOperand(input);
    op.addOperand(offset);
    op.setAttribute("sliceWidth", sliceWidth);
    grh::Value& result = createTemporaryValue(*originExpr.type, hint);
    op.addResult(result);
    return &result;
}

grh::Value* CombRHSConverter::buildArraySlice(grh::Value& input, grh::Value& index,
                                              int64_t sliceWidth,
                                              const slang::ast::Expression& originExpr) {
    if (sliceWidth <= 0) {
        reportUnsupported("array slice width", originExpr);
        return nullptr;
    }
    grh::Operation& op = createOperation(grh::OperationKind::kSliceArray, "array_slice");
    op.addOperand(input);
    op.addOperand(index);
    op.setAttribute("sliceWidth", sliceWidth);
    grh::Value& result = createTemporaryValue(*originExpr.type, "array_slice");
    op.addResult(result);
    return &result;
}

grh::Value* CombRHSConverter::buildMemoryRead(const SignalMemoEntry& entry,
                                              const slang::ast::ElementSelectExpression& expr) {
    if (!entry.stateOp || entry.stateOp->kind() != grh::OperationKind::kMemory) {
        reportUnsupported("memory read target", expr);
        return nullptr;
    }

    grh::Value* addr = convert(expr.selector());
    if (!addr) {
        return nullptr;
    }

    grh::Operation& op = createOperation(grh::OperationKind::kMemoryAsyncReadPort, "mem_read");
    op.addOperand(*addr);
    op.setAttribute("memSymbol", entry.stateOp->symbol());
    grh::Value& result = createTemporaryValue(*expr.type, "mem_read");
    op.addResult(result);
    return &result;
}

grh::Value* CombRHSConverter::createIntConstant(int64_t value, const slang::ast::Type& type,
                                                std::string_view hint) {
    uint64_t bitWidth = 32;
    if (type.isBitstreamType() && type.isFixedSize()) {
        bitWidth = type.getBitstreamWidth();
    }
    if (bitWidth == 0) {
        bitWidth = 1;
    }
    slang::SVInt literal(static_cast<slang::bitwidth_t>(bitWidth), value, type.isSigned());
    return createConstantValue(literal, type, hint);
}

grh::Value*
CombRHSConverter::convertElementSelect(const slang::ast::ElementSelectExpression& expr) {
    if (const SignalMemoEntry* entry = findMemoEntryFromExpression(expr.value())) {
        if (entry->stateOp && entry->stateOp->kind() == grh::OperationKind::kMemory) {
            return buildMemoryRead(*entry, expr);
        }
    }

    grh::Value* input = convert(expr.value());
    grh::Value* index = convert(expr.selector());
    if (!input || !index) {
        return nullptr;
    }

    const TypeInfo info = deriveTypeInfo(*expr.type);
    return buildArraySlice(*input, *index, info.width, expr);
}

grh::Value*
CombRHSConverter::convertRangeSelect(const slang::ast::RangeSelectExpression& expr) {
    grh::Value* input = convert(expr.value());
    if (!input) {
        return nullptr;
    }

    using slang::ast::RangeSelectionKind;
    switch (expr.getSelectionKind()) {
    case RangeSelectionKind::Simple: {
        std::optional<int64_t> left = evaluateConstantInt(expr.left());
        std::optional<int64_t> right = evaluateConstantInt(expr.right());
        if (!left || !right) {
            reportUnsupported("static range bounds", expr);
            return nullptr;
        }
        const int64_t sliceStart = std::min(*left, *right);
        const int64_t sliceEnd = std::max(*left, *right);
        return buildStaticSlice(*input, sliceStart, sliceEnd, expr, "range_slice");
    }
    case RangeSelectionKind::IndexedUp: {
        grh::Value* offset = convert(expr.left());
        if (!offset) {
            return nullptr;
        }
        std::optional<int64_t> width = evaluateConstantInt(expr.right());
        if (!width || *width <= 0) {
            reportUnsupported("indexed range width", expr);
            return nullptr;
        }
        return buildDynamicSlice(*input, *offset, *width, expr, "range_up");
    }
    case RangeSelectionKind::IndexedDown: {
        grh::Value* base = convert(expr.left());
        if (!base) {
            return nullptr;
        }
        std::optional<int64_t> width = evaluateConstantInt(expr.right());
        if (!width || *width <= 0) {
            reportUnsupported("indexed range width", expr);
            return nullptr;
        }
        grh::Value* offset = base;
        if (*width > 1) {
            grh::Value* widthValue =
                createIntConstant(*width - 1, *expr.left().type, "range_down_width");
            if (!widthValue) {
                return nullptr;
            }
            offset = buildBinaryOp(grh::OperationKind::kSub, *base, *widthValue, expr.left(),
                                   "range_down_off");
            if (!offset) {
                return nullptr;
            }
        }
        return buildDynamicSlice(*input, *offset, *width, expr, "range_down");
    }
    default:
        reportUnsupported("range select kind", expr);
        return nullptr;
    }
}

grh::Value*
CombRHSConverter::convertMemberAccess(const slang::ast::MemberAccessExpression& expr) {
    std::optional<SliceRange> slice = deriveStructFieldSlice(expr);
    if (!slice) {
        reportUnsupported("struct member access", expr);
        return nullptr;
    }

    grh::Value* input = convert(expr.value());
    if (!input) {
        return nullptr;
    }

    const int64_t sliceStart = std::min(slice->lsb, slice->msb);
    const int64_t sliceEnd = std::max(slice->lsb, slice->msb);
    return buildStaticSlice(*input, sliceStart, sliceEnd, expr, "member_slice");
}

void ElaborateDiagnostics::todo(const slang::ast::Symbol& symbol, std::string message) {
    add(ElaborateDiagnosticKind::Todo, symbol, std::move(message));
}

void ElaborateDiagnostics::nyi(const slang::ast::Symbol& symbol, std::string message) {
    add(ElaborateDiagnosticKind::NotYetImplemented, symbol, std::move(message));
}

void ElaborateDiagnostics::add(ElaborateDiagnosticKind kind, const slang::ast::Symbol& symbol,
                               std::string message) {
    ElaborateDiagnostic diagnostic{
        .kind = kind,
        .message = std::move(message),
        .originSymbol = deriveSymbolPath(symbol),
        .location = symbol.location.valid() ? std::optional(symbol.location) : std::nullopt};
    messages_.push_back(std::move(diagnostic));
}

Elaborate::Elaborate(ElaborateDiagnostics* diagnostics, ElaborateOptions options) :
    diagnostics_(diagnostics), options_(options) {}

grh::Netlist Elaborate::convert(const slang::ast::RootSymbol& root) {
    grh::Netlist netlist;

    for (const slang::ast::InstanceSymbol* topInstance : root.topInstances) {
        if (!topInstance) {
            continue;
        }

        if (!topInstance->isModule()) {
            if (diagnostics_) {
                diagnostics_->nyi(*topInstance, "Only module instances are supported as top level");
            }
            continue;
        }

        bool newlyCreated = false;
        grh::Graph* graph = materializeGraph(*topInstance, netlist, newlyCreated);
        if (!graph) {
            continue;
        }

        convertInstanceBody(*topInstance, *graph, netlist);
        netlist.markAsTop(graph->name());
        if (!topInstance->name.empty()) {
            netlist.registerGraphAlias(std::string(topInstance->name), *graph);
        }
    }

    return netlist;
}

std::span<const SignalMemoEntry>
Elaborate::peekNetMemo(const slang::ast::InstanceBodySymbol& body) const {
    if (auto it = netMemo_.find(&body); it != netMemo_.end()) {
        return it->second;
    }
    return {};
}

std::span<const SignalMemoEntry>
Elaborate::peekRegMemo(const slang::ast::InstanceBodySymbol& body) const {
    if (auto it = regMemo_.find(&body); it != regMemo_.end()) {
        return it->second;
    }
    return {};
}

grh::Graph* Elaborate::materializeGraph(const slang::ast::InstanceSymbol& instance,
                                        grh::Netlist& netlist, bool& wasCreated) {
    const slang::ast::InstanceBodySymbol* canonicalBody = instance.getCanonicalBody();
    const slang::ast::InstanceBodySymbol* keyBody =
        canonicalBody ? canonicalBody : &instance.body;

    if (auto it = graphByBody_.find(keyBody); it != graphByBody_.end()) {
        wasCreated = false;
        graphByBody_[&instance.body] = it->second;
        return it->second;
    }

    std::string baseName;
    const auto& definition = instance.body.getDefinition();
    if (!definition.name.empty()) {
        baseName.assign(definition.name);
    }
    else if (!instance.name.empty()) {
        baseName.assign(instance.name);
    }
    else {
        baseName = instance.getHierarchicalPath();
        if (baseName.empty()) {
            baseName = "_anonymous_module";
        }
    }

    if (const std::string paramSuffix = deriveParameterSuffix(*keyBody); !paramSuffix.empty()) {
        baseName.append(paramSuffix);
    }

    auto [usageIt, inserted] = graphNameUsage_.try_emplace(baseName, 0);
    std::string graphName = baseName;
    if (usageIt->second > 0) {
        graphName.append("_");
        graphName.append(std::to_string(usageIt->second));
    }
    ++usageIt->second;

    grh::Graph& graph = netlist.createGraph(graphName);
    graphByBody_[keyBody] = &graph;
    graphByBody_[&instance.body] = &graph;
    wasCreated = true;
    return &graph;
}

void Elaborate::populatePorts(const slang::ast::InstanceSymbol& instance,
                              const slang::ast::InstanceBodySymbol& body, grh::Graph& graph) {

    for (const slang::ast::Symbol* portSymbol : body.getPortList()) {
        if (!portSymbol) {
            continue;
        }

        if (const auto* port = portSymbol->as_if<slang::ast::PortSymbol>()) {
            if (port->isNullPort) {
                handleUnsupportedPort(*port, "null ports are not supported", diagnostics_);
                continue;
            }

            if (port->name.empty()) {
                handleUnsupportedPort(*port, "anonymous ports are not supported", diagnostics_);
                continue;
            }

            const slang::ast::Type& type = port->getType();
            if (!type.isFixedSize() || !type.isBitstreamType()) {
                if (diagnostics_) {
                    std::ostringstream oss;
                    oss << "Port type is not a fixed-size bitstream (type: " << type.toString()
                        << "); using placeholder width";
                    diagnostics_->nyi(*port, oss.str());
                }
            }

            TypeHelper::Info typeInfo = TypeHelper::analyze(type, *port, diagnostics_);
            const int64_t width = typeInfo.width > 0 ? typeInfo.width : 1;
            const bool isSigned = typeInfo.isSigned;

            std::string portName(port->name);
            grh::Value& value = graph.createValue(portName, width, isSigned);
            registerValueForSymbol(*port, value);
            if (const auto* internal = port->internalSymbol ?
                                           port->internalSymbol->as_if<slang::ast::ValueSymbol>()
                                           : nullptr) {
                registerValueForSymbol(*internal, value);
            }

            switch (port->direction) {
            case slang::ast::ArgumentDirection::In:
                graph.bindInputPort(portName, value);
                break;
            case slang::ast::ArgumentDirection::Out:
                graph.bindOutputPort(portName, value);
                break;
            case slang::ast::ArgumentDirection::InOut:
            case slang::ast::ArgumentDirection::Ref:
                handleUnsupportedPort(*port,
                                      std::string("direction ") +
                                          std::string(slang::ast::toString(port->direction)),
                                      diagnostics_);
                // Leave as internal signal placeholder.
                break;
            default:
                handleUnsupportedPort(*port, "unknown direction", diagnostics_);
                break;
            }
            continue;
        }

        if (const auto* multi = portSymbol->as_if<slang::ast::MultiPortSymbol>()) {
            handleUnsupportedPort(*multi, "multi-port aggregations", diagnostics_);
            continue;
        }

        if (const auto* iface = portSymbol->as_if<slang::ast::InterfacePortSymbol>()) {
            handleUnsupportedPort(*iface, "interface ports", diagnostics_);
            continue;
        }

        handleUnsupportedPort(*portSymbol, "unhandled symbol kind", diagnostics_);
    }
}

void Elaborate::emitModulePlaceholder(const slang::ast::InstanceSymbol& instance,
                                      grh::Graph& graph) {
    if (!options_.emitPlaceholders) {
        return;
    }

    std::string opName = "_module_placeholder";
    if (placeholderCounter_ > 0) {
        opName.append("_");
        opName.append(std::to_string(placeholderCounter_));
    }
    ++placeholderCounter_;

    grh::Operation& op = graph.createOperation(grh::OperationKind::kBlackbox, opName);

    const auto& definition = instance.body.getDefinition();
    std::string moduleName = !definition.name.empty() ? std::string(definition.name)
                                                      : std::string(instance.name);
    if (moduleName.empty()) {
        moduleName = "anonymous_module";
    }

    op.setAttribute("module_name", moduleName);
    op.setAttribute("status", std::string("TODO: module body elaboration pending"));

    if (diagnostics_) {
        diagnostics_->todo(instance, "Module body elaboration pending");
    }
}

void Elaborate::convertInstanceBody(const slang::ast::InstanceSymbol& instance,
                                    grh::Graph& graph, grh::Netlist& netlist) {
    const slang::ast::InstanceBodySymbol* canonicalBody = instance.getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body =
        canonicalBody ? *canonicalBody : instance.body;

    if (!processedBodies_.insert(&body).second) {
        return;
    }

    populatePorts(instance, body, graph);
    emitModulePlaceholder(instance, graph);
    collectSignalMemos(body);
    materializeSignalMemos(body, graph);
    ensureWriteBackMemo(body);

    for (const slang::ast::Symbol& member : body.members()) {
        if (const auto* childInstance = member.as_if<slang::ast::InstanceSymbol>()) {
            processInstance(*childInstance, graph, netlist);
            continue;
        }

        if (const auto* continuous = member.as_if<slang::ast::ContinuousAssignSymbol>()) {
            processContinuousAssign(*continuous, body, graph);
            continue;
        }

        if (const auto* block = member.as_if<slang::ast::ProceduralBlockSymbol>()) {
            if (isCombProceduralBlock(*block)) {
                processCombAlways(*block, body, graph);
            }
            else if (isSeqProceduralBlock(*block)) {
                processSeqAlways(*block, body, graph);
            }
            else if (diagnostics_) {
                diagnostics_->nyi(*block, "Procedural block kind is not supported yet");
            }
            continue;
        }

        if (const auto* instanceArray = member.as_if<slang::ast::InstanceArraySymbol>()) {
            processInstanceArray(*instanceArray, graph, netlist);
            continue;
        }

        if (const auto* generateBlock = member.as_if<slang::ast::GenerateBlockSymbol>()) {
            processGenerateBlock(*generateBlock, graph, netlist);
            continue;
        }

        if (const auto* generateArray = member.as_if<slang::ast::GenerateBlockArraySymbol>()) {
            processGenerateBlockArray(*generateArray, graph, netlist);
            continue;
        }

        // Other symbol kinds will be handled in later stages.
    }

    finalizeWriteBackMemo(body, graph);
}

void Elaborate::processInstanceArray(const slang::ast::InstanceArraySymbol& array,
                                     grh::Graph& graph, grh::Netlist& netlist) {
    for (const slang::ast::Symbol* element : array.elements) {
        if (!element) {
            continue;
        }

        if (const auto* childInstance = element->as_if<slang::ast::InstanceSymbol>()) {
            processInstance(*childInstance, graph, netlist);
            continue;
        }

        if (const auto* nestedArray = element->as_if<slang::ast::InstanceArraySymbol>()) {
            processInstanceArray(*nestedArray, graph, netlist);
            continue;
        }

        if (const auto* generateBlock = element->as_if<slang::ast::GenerateBlockSymbol>()) {
            processGenerateBlock(*generateBlock, graph, netlist);
            continue;
        }

        if (const auto* generateArray = element->as_if<slang::ast::GenerateBlockArraySymbol>()) {
            processGenerateBlockArray(*generateArray, graph, netlist);
        }
    }
}

void Elaborate::processGenerateBlock(const slang::ast::GenerateBlockSymbol& block,
                                     grh::Graph& graph, grh::Netlist& netlist) {
    if (block.isUninstantiated) {
        return;
    }

    for (const slang::ast::Symbol& member : block.members()) {
        if (const auto* childInstance = member.as_if<slang::ast::InstanceSymbol>()) {
            processInstance(*childInstance, graph, netlist);
            continue;
        }

        if (const auto* instanceArray = member.as_if<slang::ast::InstanceArraySymbol>()) {
            processInstanceArray(*instanceArray, graph, netlist);
            continue;
        }

        if (const auto* nestedBlock = member.as_if<slang::ast::GenerateBlockSymbol>()) {
            processGenerateBlock(*nestedBlock, graph, netlist);
            continue;
        }

        if (const auto* nestedArray = member.as_if<slang::ast::GenerateBlockArraySymbol>()) {
            processGenerateBlockArray(*nestedArray, graph, netlist);
            continue;
        }
    }
}

void Elaborate::processGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                                          grh::Graph& graph, grh::Netlist& netlist) {
    if (!array.valid) {
        if (diagnostics_) {
            diagnostics_->nyi(array, "Generate block array is not elaborated");
        }
        return;
    }

    for (const slang::ast::GenerateBlockSymbol* entry : array.entries) {
        if (!entry) {
            continue;
        }
        processGenerateBlock(*entry, graph, netlist);
    }
}

void Elaborate::processContinuousAssign(const slang::ast::ContinuousAssignSymbol& assign,
                                        const slang::ast::InstanceBodySymbol& body,
                                        grh::Graph& graph) {
    const slang::ast::Expression& expr = assign.getAssignment();
    const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>();
    if (!assignment) {
        if (diagnostics_) {
            diagnostics_->nyi(assign, "Continuous assign payload is not an assignment expression");
        }
        return;
    }

    std::span<const SignalMemoEntry> netMemo = peekNetMemo(body);
    std::span<const SignalMemoEntry> regMemo = peekRegMemo(body);

    RHSConverter::Context context{
        .graph = &graph,
        .netMemo = netMemo,
        .regMemo = regMemo,
        .origin = &assign,
        .diagnostics = diagnostics_};
    CombRHSConverter converter(context);
    grh::Value* rhsValue = converter.convert(assignment->right());
    if (!rhsValue) {
        return;
    }

    WriteBackMemo& memo = ensureWriteBackMemo(body);
    LHSConverter::Context lhsContext{
        .graph = &graph,
        .netMemo = netMemo,
        .regMemo = {},
        .origin = &assign,
        .diagnostics = diagnostics_};
    ContinuousAssignLHSConverter lhsConverter(lhsContext, memo);
    lhsConverter.convert(*assignment, *rhsValue);
}

void Elaborate::processCombAlways(const slang::ast::ProceduralBlockSymbol& block,
                                  const slang::ast::InstanceBodySymbol& body,
                                  grh::Graph& graph) {
    std::span<const SignalMemoEntry> netMemo = peekNetMemo(body);
    std::span<const SignalMemoEntry> regMemo = peekRegMemo(body);
    WriteBackMemo& memo = ensureWriteBackMemo(body);
    CombAlwaysConverter converter(graph, netMemo, regMemo, memo, block, diagnostics_);
    converter.run();
}

void Elaborate::processSeqAlways(const slang::ast::ProceduralBlockSymbol& block,
                                 const slang::ast::InstanceBodySymbol& body,
                                 grh::Graph& graph) {
    std::span<const SignalMemoEntry> netMemo = peekNetMemo(body);
    std::span<const SignalMemoEntry> regMemo = peekRegMemo(body);
    WriteBackMemo& memo = ensureWriteBackMemo(body);
    SeqAlwaysConverter converter(graph, netMemo, regMemo, memo, block, diagnostics_);
    converter.run();
}

void Elaborate::processInstance(const slang::ast::InstanceSymbol& childInstance,
                                grh::Graph& parentGraph, grh::Netlist& netlist) {
    if (!childInstance.isModule()) {
        if (diagnostics_) {
            diagnostics_->nyi(childInstance, "Only module instances are supported");
        }
        return;
    }

    bool childCreated = false;
    grh::Graph* childGraph = materializeGraph(childInstance, netlist, childCreated);
    if (!childGraph) {
        return;
    }

    convertInstanceBody(childInstance, *childGraph, netlist);
    createInstanceOperation(childInstance, parentGraph, *childGraph);
}

void Elaborate::createInstanceOperation(const slang::ast::InstanceSymbol& childInstance,
                                        grh::Graph& parentGraph, grh::Graph& targetGraph) {
    std::string baseName =
        childInstance.name.empty() ? std::string("inst") : std::string(childInstance.name);
    std::string opName = makeUniqueOperationName(parentGraph, baseName);
    grh::Operation& op = parentGraph.createOperation(grh::OperationKind::kInstance, opName);

    std::string instanceName =
        childInstance.name.empty() ? deriveSymbolPath(childInstance) : std::string(childInstance.name);
    if (instanceName.empty()) {
        instanceName = "_inst_" + std::to_string(instanceCounter_++);
    }

    std::vector<std::string> inputPortNames;
    std::vector<std::string> outputPortNames;

    for (const slang::ast::Symbol* portSymbol : childInstance.body.getPortList()) {
        if (!portSymbol) {
            continue;
        }

        if (const auto* port = portSymbol->as_if<slang::ast::PortSymbol>()) {
            const auto* connection = childInstance.getPortConnection(*port);
            if (!connection) {
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Missing port connection during hierarchy elaboration");
                }
                continue;
            }

            const slang::ast::Expression* expr = connection->getExpression();
            if (!expr) {
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Port connection without explicit expression");
                }
                continue;
            }

            grh::Value* value = resolveConnectionValue(*expr, parentGraph, port);
            if (!value) {
                continue;
            }

            switch (port->direction) {
            case slang::ast::ArgumentDirection::In:
                op.addOperand(*value);
                inputPortNames.emplace_back(port->name);
                break;
            case slang::ast::ArgumentDirection::Out:
                op.addResult(*value);
                outputPortNames.emplace_back(port->name);
                break;
            case slang::ast::ArgumentDirection::InOut:
            case slang::ast::ArgumentDirection::Ref:
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "InOut/Ref port directions are not supported yet");
                }
                break;
            default:
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Unknown port direction in hierarchy elaboration");
                }
                break;
            }
            continue;
        }

        if (const auto* multi = portSymbol->as_if<slang::ast::MultiPortSymbol>()) {
            if (diagnostics_) {
                diagnostics_->nyi(*multi, "Multi-port aggregation is not supported yet");
            }
            continue;
        }

        if (const auto* iface = portSymbol->as_if<slang::ast::InterfacePortSymbol>()) {
            if (diagnostics_) {
                diagnostics_->nyi(*iface, "Interface ports are not supported yet");
            }
            continue;
        }
    }

    op.setAttribute("moduleName", targetGraph.name());
    op.setAttribute("instanceName", instanceName);
    op.setAttribute("inputPortName", inputPortNames);
    op.setAttribute("outputPortName", outputPortNames);
}

grh::Value* Elaborate::ensureValueForSymbol(const slang::ast::ValueSymbol& symbol,
                                            grh::Graph& graph) {
    if (auto it = valueCache_.find(&symbol); it != valueCache_.end()) {
        return it->second;
    }

    const slang::ast::Type& type = symbol.getType();
    TypeHelper::Info info = TypeHelper::analyze(type, symbol, diagnostics_);
    std::string baseName = symbol.name.empty() ? std::string("_value") : std::string(symbol.name);
    std::string candidate = baseName;
    std::size_t attempt = 0;
    while (graph.findValue(candidate)) {
        candidate = baseName;
        candidate.push_back('_');
        candidate.append(std::to_string(++attempt));
    }

    grh::Value& value = graph.createValue(candidate, info.width > 0 ? info.width : 1,
                                          info.isSigned);
    registerValueForSymbol(symbol, value);
    return &value;
}

grh::Value* Elaborate::resolveConnectionValue(const slang::ast::Expression& expr,
                                              grh::Graph& graph,
                                              const slang::ast::Symbol* origin) {
    switch (expr.kind) {
    case slang::ast::ExpressionKind::NamedValue: {
        const auto& named = expr.as<slang::ast::NamedValueExpression>();
        return ensureValueForSymbol(named.symbol, graph);
    }
    case slang::ast::ExpressionKind::Assignment: {
        const auto& assign = expr.as<slang::ast::AssignmentExpression>();
        if (assign.isLValueArg()) {
            return resolveConnectionValue(assign.left(), graph, origin);
        }

        if (diagnostics_ && origin) {
            diagnostics_->nyi(*origin, "Assignment port connections are not supported yet");
        }
        return nullptr;
    }
    case slang::ast::ExpressionKind::HierarchicalValue:
        if (diagnostics_ && origin) {
            diagnostics_->nyi(*origin, "Hierarchical port connections are not supported yet");
        }
        return nullptr;
    default:
        if (diagnostics_ && origin) {
            std::string message = "Unsupported port connection expression (kind = ";
            message.append(std::to_string(static_cast<int>(expr.kind)));
            message.push_back(')');
            diagnostics_->nyi(*origin, std::move(message));
        }
        return nullptr;
    }
}

std::string Elaborate::makeUniqueOperationName(grh::Graph& graph, std::string baseName) {
    if (baseName.empty()) {
        baseName = "_inst";
    }

    std::string candidate = baseName;
    std::size_t suffix = 0;
    while (graph.findOperation(candidate)) {
        candidate = baseName;
        candidate.push_back('_');
        candidate.append(std::to_string(++suffix));
    }
    return candidate;
}

void Elaborate::registerValueForSymbol(const slang::ast::Symbol& symbol, grh::Value& value) {
    valueCache_[&symbol] = &value;
}

void Elaborate::materializeSignalMemos(const slang::ast::InstanceBodySymbol& body,
                                       grh::Graph& graph) {
    ensureNetValues(body, graph);
    ensureRegState(body, graph);
}

void Elaborate::ensureNetValues(const slang::ast::InstanceBodySymbol& body, grh::Graph& graph) {
    auto it = netMemo_.find(&body);
    if (it == netMemo_.end()) {
        return;
    }

    for (SignalMemoEntry& entry : it->second) {
        if (!entry.symbol) {
            continue;
        }
        grh::Value* value = ensureValueForSymbol(*entry.symbol, graph);
        entry.value = value;
    }
}

void Elaborate::ensureRegState(const slang::ast::InstanceBodySymbol& body, grh::Graph& graph) {
    auto it = regMemo_.find(&body);
    if (it == regMemo_.end()) {
        return;
    }

    std::unordered_map<const slang::ast::ProceduralBlockSymbol*, std::optional<AsyncResetEvent>>
        asyncCache;
    std::unordered_map<const slang::ast::ProceduralBlockSymbol*, std::optional<SyncResetInfo>>
        syncCache;

    for (SignalMemoEntry& entry : it->second) {
        if (!entry.symbol || !entry.type) {
            continue;
        }
        if (entry.stateOp) {
            continue;
        }
        if (const auto layout = deriveMemoryLayout(*entry.type, *entry.symbol, diagnostics_)) {
            std::string opName = makeOperationNameForSymbol(*entry.symbol, "memory", graph);
            grh::Operation& op = graph.createOperation(grh::OperationKind::kMemory, opName);
            op.setAttribute("width", layout->rowWidth);
            op.setAttribute("row", layout->rowCount);
            op.setAttribute("isSigned", layout->isSigned);
            entry.stateOp = &op;
            continue;
        }

        grh::Value* value = ensureValueForSymbol(*entry.symbol, graph);
        entry.value = value;
        if (!value) {
            continue;
        }

        if (!entry.drivingBlock) {
            if (diagnostics_) {
                diagnostics_->nyi(*entry.symbol,
                                  "Sequential signal lacks associated procedural block metadata");
            }
            continue;
        }

        std::optional<AsyncResetEvent> asyncInfo;
        if (auto itAsync = asyncCache.find(entry.drivingBlock); itAsync != asyncCache.end()) {
            asyncInfo = itAsync->second;
        }
        else {
            asyncInfo = detectAsyncResetEvent(*entry.drivingBlock, diagnostics_);
            asyncCache.emplace(entry.drivingBlock, asyncInfo);
        }

        std::optional<SyncResetInfo> syncInfo;
        if (!asyncInfo) {
            if (auto itSync = syncCache.find(entry.drivingBlock); itSync != syncCache.end()) {
                syncInfo = itSync->second;
            }
            else {
                syncInfo = detectSyncReset(*entry.drivingBlock);
                syncCache.emplace(entry.drivingBlock, syncInfo);
            }
        }

        std::optional<std::string> clkPolarity =
            deriveClockPolarity(*entry.drivingBlock, *entry.symbol, diagnostics_);
        if (!clkPolarity) {
            continue;
        }

        auto makeRstLevel = [](bool activeHigh) {
            return activeHigh ? std::string("1'b1") : std::string("1'b0");
        };

        grh::OperationKind opKind = grh::OperationKind::kRegister;
        std::optional<std::string> rstLevel;
        if (asyncInfo) {
            entry.asyncResetExpr = asyncInfo->expr;
            entry.asyncResetEdge = asyncInfo->edge;
            if (entry.asyncResetExpr && entry.asyncResetEdge != slang::ast::EdgeKind::None &&
                entry.asyncResetEdge != slang::ast::EdgeKind::BothEdges) {
                const bool activeHigh = entry.asyncResetEdge == slang::ast::EdgeKind::PosEdge;
                rstLevel = makeRstLevel(activeHigh);
                opKind = grh::OperationKind::kRegisterARst;
            }
            else if (diagnostics_) {
                diagnostics_->nyi(*entry.symbol,
                                  "Async reset edge kind is not supported for this register");
            }
        }
        else if (syncInfo && syncInfo->symbol) {
            entry.syncResetSymbol = syncInfo->symbol;
            entry.syncResetActiveHigh = syncInfo->activeHigh;
            rstLevel = makeRstLevel(syncInfo->activeHigh);
            opKind = grh::OperationKind::kRegisterRst;
        }

        std::string opName = makeOperationNameForSymbol(*entry.symbol, "register", graph);
        grh::Operation& op = graph.createOperation(opKind, opName);
        op.addResult(*value);
        op.setAttribute("clkPolarity", *clkPolarity);
        if (rstLevel) {
            op.setAttribute("rstLevel", *rstLevel);
        }
        entry.stateOp = &op;
    }
}

WriteBackMemo& Elaborate::ensureWriteBackMemo(const slang::ast::InstanceBodySymbol& body) {
    return writeBackMemo_[&body];
}

void Elaborate::finalizeWriteBackMemo(const slang::ast::InstanceBodySymbol& body,
                                      grh::Graph& graph) {
    auto it = writeBackMemo_.find(&body);
    if (it == writeBackMemo_.end()) {
        return;
    }
    it->second.finalize(graph, diagnostics_);
}

std::string Elaborate::makeOperationNameForSymbol(const slang::ast::ValueSymbol& symbol,
                                                  std::string_view fallback, grh::Graph& graph) {
    if (!symbol.name.empty()) {
        std::string symbolName(symbol.name);
        if (!graph.findOperation(symbolName)) {
            return symbolName;
        }
    }

    std::string base = symbol.name.empty() ? std::string(fallback) : std::string(symbol.name);
    if (base.empty()) {
        base = fallback.empty() ? std::string("_state") : std::string(fallback);
    }
    return makeUniqueOperationName(graph, std::move(base));
}

void Elaborate::collectSignalMemos(const slang::ast::InstanceBodySymbol& body) {
    std::unordered_map<const slang::ast::ValueSymbol*, SignalMemoEntry> candidates;

    auto registerCandidate = [&](const slang::ast::ValueSymbol& symbol) {
        const slang::ast::Type& type = symbol.getType();
        if (!type.isFixedSize() || !type.isBitstreamType()) {
            if (diagnostics_) {
                diagnostics_->nyi(symbol,
                                  "Skipping memoization for non bitstream, fixed-size signal type");
            }
            return;
        }

        TypeHelper::Info info = TypeHelper::analyze(type, symbol, diagnostics_);

        SignalMemoEntry entry;
        entry.symbol = &symbol;
        entry.type = &type;
        entry.width = info.width > 0 ? info.width : 1;
        entry.isSigned = info.isSigned;
        entry.fields.reserve(info.fields.size());
        for (const auto& field : info.fields) {
            entry.fields.push_back(
                SignalMemoField{field.path, field.msb, field.lsb, field.isSigned});
        }

        candidates.emplace(&symbol, std::move(entry));
    };

    for (const slang::ast::Symbol& member : body.members()) {
        if (const auto* net = member.as_if<slang::ast::NetSymbol>()) {
            registerCandidate(*net);
            continue;
        }

        if (const auto* variable = member.as_if<slang::ast::VariableSymbol>()) {
            registerCandidate(*variable);
            continue;
        }
    }

    if (candidates.empty()) {
        netMemo_[&body].clear();
        regMemo_[&body].clear();
        return;
    }

    std::unordered_map<const slang::ast::ValueSymbol*, MemoDriverKind> driverKinds;
    driverKinds.reserve(candidates.size());
    std::unordered_map<const slang::ast::ValueSymbol*, const slang::ast::ProceduralBlockSymbol*>
        regDriverBlocks;
    regDriverBlocks.reserve(candidates.size());
    std::unordered_set<const slang::ast::ValueSymbol*> regDriverConflicts;

    auto markDriver = [&](const slang::ast::ValueSymbol& symbol, MemoDriverKind driver,
                          const slang::ast::ProceduralBlockSymbol* block = nullptr) {
        if (driver == MemoDriverKind::None) {
            return;
        }
        auto candidateIt = candidates.find(&symbol);
        if (candidateIt == candidates.end()) {
            return;
        }

        auto [driverIt, _] = driverKinds.emplace(&symbol, MemoDriverKind::None);
        MemoDriverKind& state = driverIt->second;
        if (hasDriver(state, driver)) {
            return;
        }

        state |= driver;
        if (driver == MemoDriverKind::Reg && block) {
            auto [ownerIt, inserted] = regDriverBlocks.emplace(&symbol, block);
            if (!inserted && ownerIt->second != block) {
                if (regDriverConflicts.insert(&symbol).second && diagnostics_) {
                    diagnostics_->nyi(symbol, "Signal is driven by multiple sequential blocks; unsupported");
                }
            }
        }
        if (hasDriver(state, MemoDriverKind::Net) && hasDriver(state, MemoDriverKind::Reg)) {
            if (diagnostics_) {
                diagnostics_->nyi(symbol,
                                   "Signal has conflicting net/reg drivers (combinational vs sequential)");
            }
        }
    };

    for (const slang::ast::Symbol& member : body.members()) {
        if (const auto* assign = member.as_if<slang::ast::ContinuousAssignSymbol>()) {
            const slang::ast::Expression& expr = assign->getAssignment();
            if (const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>()) {
                collectAssignedSymbols(
                    assignment->left(), [&](const slang::ast::ValueSymbol& symbol) {
                        markDriver(symbol, MemoDriverKind::Net);
                    });
            }
            continue;
        }

        if (const auto* block = member.as_if<slang::ast::ProceduralBlockSymbol>()) {
            MemoDriverKind driver = classifyProceduralBlock(*block);
            if (driver == MemoDriverKind::None) {
                continue;
            }

            auto handleAssignment = [&](const slang::ast::Expression& lhs) {
                collectAssignedSymbols(
                    lhs, [&](const slang::ast::ValueSymbol& symbol) {
                        markDriver(symbol, driver, block);
                    });
            };
            AssignmentCollector collector(handleAssignment);
            block->getBody().visit(collector);
            continue;
        }
    }

    std::vector<SignalMemoEntry> nets;
    std::vector<SignalMemoEntry> regs;
    nets.reserve(candidates.size());
    regs.reserve(candidates.size());

    for (auto& [symbol, entry] : candidates) {
        MemoDriverKind driver = MemoDriverKind::None;
        if (auto it = driverKinds.find(symbol); it != driverKinds.end()) {
            driver = it->second;
        }

        const bool netOnly = hasDriver(driver, MemoDriverKind::Net) &&
                             !hasDriver(driver, MemoDriverKind::Reg);
        const bool regOnly = hasDriver(driver, MemoDriverKind::Reg) &&
                             !hasDriver(driver, MemoDriverKind::Net);

        if (netOnly) {
            nets.push_back(entry);
        }
        else if (regOnly) {
            if (regDriverConflicts.count(symbol) > 0) {
                continue;
            }
            if (auto blockIt = regDriverBlocks.find(symbol); blockIt != regDriverBlocks.end()) {
                entry.drivingBlock = blockIt->second;
            }
            regs.push_back(entry);
        }
    }

    auto byName = [](const SignalMemoEntry& lhs, const SignalMemoEntry& rhs) {
        return lhs.symbol->name < rhs.symbol->name;
    };
    std::sort(nets.begin(), nets.end(), byName);
    std::sort(regs.begin(), regs.end(), byName);
    netMemo_[&body] = std::move(nets);
    regMemo_[&body] = std::move(regs);
}

} // namespace wolf_sv
