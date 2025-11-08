#include "elaborate.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
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

namespace wolf_sv {

namespace {

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
    Wire = 1 << 0,
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

MemoDriverKind classifyProceduralBlock(const slang::ast::ProceduralBlockSymbol& block) {
    using slang::ast::ProceduralBlockKind;
    switch (block.procedureKind) {
    case ProceduralBlockKind::AlwaysComb:
        return MemoDriverKind::Wire;
    case ProceduralBlockKind::AlwaysLatch:
    case ProceduralBlockKind::AlwaysFF:
    case ProceduralBlockKind::Initial:
    case ProceduralBlockKind::Final:
        return MemoDriverKind::Reg;
    case ProceduralBlockKind::Always: {
        const slang::ast::TimingControl* timing = findTimingControl(block.getBody());
        if (!timing) {
            return MemoDriverKind::Wire;
        }
        if (timing->kind == slang::ast::TimingControlKind::ImplicitEvent) {
            return MemoDriverKind::Wire;
        }
        return containsEdgeSensitiveEvent(*timing) ? MemoDriverKind::Reg : MemoDriverKind::Wire;
    }
    default:
        return MemoDriverKind::None;
    }
}
} // namespace

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
    }

    return netlist;
}

std::span<const SignalMemoEntry>
Elaborate::peekWireMemo(const slang::ast::InstanceBodySymbol& body) const {
    if (auto it = wireMemo_.find(&body); it != wireMemo_.end()) {
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
    collectSignalMemos(body);

    for (const slang::ast::Symbol& member : body.members()) {
        if (const auto* childInstance = member.as_if<slang::ast::InstanceSymbol>()) {
            processInstance(*childInstance, graph, netlist);
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

    emitModulePlaceholder(instance, graph);
    if (diagnostics_) {
        diagnostics_->todo(body,
                           "Module body elaboration incomplete; emitted placeholder operation");
    }
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

void Elaborate::collectSignalMemos(const slang::ast::InstanceBodySymbol& body) {
    std::unordered_map<const slang::ast::ValueSymbol*, SignalMemoEntry> candidates;

    auto registerCandidate = [&](const slang::ast::ValueSymbol& symbol) {
        const slang::ast::Type& type = symbol.getType();
        if (!type.isIntegral()) {
            return;
        }

        SignalMemoEntry entry;
        entry.symbol = &symbol;
        entry.type = &type;
        entry.width = clampBitWidth(type.getBitstreamWidth(), diagnostics_, symbol);
        if (entry.width <= 0) {
            entry.width = 1;
        }
        entry.isSigned = type.isSigned();
        candidates.emplace(&symbol, entry);
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
        wireMemo_[&body].clear();
        regMemo_[&body].clear();
        return;
    }

    std::unordered_map<const slang::ast::ValueSymbol*, MemoDriverKind> driverKinds;
    driverKinds.reserve(candidates.size());

    auto markDriver = [&](const slang::ast::ValueSymbol& symbol, MemoDriverKind driver) {
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
        if (hasDriver(state, MemoDriverKind::Wire) && hasDriver(state, MemoDriverKind::Reg)) {
            if (diagnostics_) {
                diagnostics_->nyi(symbol,
                                   "Signal has conflicting wire/reg drivers (combinational vs sequential)");
            }
        }
    };

    for (const slang::ast::Symbol& member : body.members()) {
        if (const auto* assign = member.as_if<slang::ast::ContinuousAssignSymbol>()) {
            const slang::ast::Expression& expr = assign->getAssignment();
            if (const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>()) {
                collectAssignedSymbols(
                    assignment->left(), [&](const slang::ast::ValueSymbol& symbol) {
                        markDriver(symbol, MemoDriverKind::Wire);
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
                    lhs, [&](const slang::ast::ValueSymbol& symbol) { markDriver(symbol, driver); });
            };
            AssignmentCollector collector(handleAssignment);
            block->getBody().visit(collector);
            continue;
        }
    }

    std::vector<SignalMemoEntry> wires;
    std::vector<SignalMemoEntry> regs;
    wires.reserve(candidates.size());
    regs.reserve(candidates.size());

    for (const auto& [symbol, entry] : candidates) {
        MemoDriverKind driver = MemoDriverKind::None;
        if (auto it = driverKinds.find(symbol); it != driverKinds.end()) {
            driver = it->second;
        }

        const bool wireOnly = hasDriver(driver, MemoDriverKind::Wire) &&
                              !hasDriver(driver, MemoDriverKind::Reg);
        const bool regOnly = hasDriver(driver, MemoDriverKind::Reg) &&
                             !hasDriver(driver, MemoDriverKind::Wire);

        if (wireOnly) {
            wires.push_back(entry);
        }
        else if (regOnly) {
            regs.push_back(entry);
        }
    }

    auto byName = [](const SignalMemoEntry& lhs, const SignalMemoEntry& rhs) {
        return lhs.symbol->name < rhs.symbol->name;
    };
    std::sort(wires.begin(), wires.end(), byName);
    std::sort(regs.begin(), regs.end(), byName);
    wireMemo_[&body] = std::move(wires);
    regMemo_[&body] = std::move(regs);
}

} // namespace wolf_sv
