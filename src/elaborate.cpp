#include "elaborate.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/numeric/ConstantValue.h"
#include "slang/numeric/SVInt.h"
#include "slang/text/SourceManager.h"

namespace wolf_sv_parser {

namespace {

std::size_t nextConverterInstanceId() {
    static std::atomic<std::size_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

std::size_t nextMemoryHelperId() {
    static std::atomic<std::size_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

SymbolId internSymbol(grh::ir::Graph& graph, std::string_view text) {
    return graph.internSymbol(text);
}

ValueId createValue(grh::ir::Graph& graph, std::string_view name, int32_t width, bool isSigned) {
    return graph.createValue(graph.internSymbol(name), width, isSigned);
}

OperationId createOperation(grh::ir::Graph& graph, grh::ir::OperationKind kind, std::string_view name) {
    return graph.createOperation(kind, graph.internSymbol(name));
}

void addOperand(grh::ir::Graph& graph, OperationId op, ValueId value) {
    graph.addOperand(op, value);
}

void addResult(grh::ir::Graph& graph, OperationId op, ValueId value) {
    graph.addResult(op, value);
}

void setAttr(grh::ir::Graph& graph, OperationId op, std::string_view key, grh::ir::AttributeValue value) {
    graph.setAttr(op, key, std::move(value));
}

void clearAttr(grh::ir::Graph& graph, OperationId op, std::string_view key) {
    graph.eraseAttr(op, key);
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

std::string toLowerCopy(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (unsigned char raw : text) {
        lowered.push_back(static_cast<char>(std::tolower(raw)));
    }
    return lowered;
}

bool isDisplaySystemTaskName(std::string_view name) {
    const std::string lowered = toLowerCopy(name);
    return lowered == "$display" || lowered == "$write" || lowered == "$strobe";
}

std::string normalizeDisplayKind(std::string_view name) {
    std::string lowered = toLowerCopy(name);
    if (!lowered.empty() && lowered.front() == '$') {
        lowered.erase(lowered.begin());
    }
    return lowered.empty() ? std::string("display") : lowered;
}

std::optional<grh::ir::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      slang::SourceLocation start,
                                      slang::SourceLocation end = {}) {
    if (!sourceManager || !start.valid()) {
        return std::nullopt;
    }
    const auto original = sourceManager->getFullyOriginalLoc(start);
    if (!original.valid() || !sourceManager->isFileLoc(original)) {
        return std::nullopt;
    }
    grh::ir::SrcLoc info;
    const auto& fullPath = sourceManager->getFullPath(original.buffer());
    std::filesystem::path path = fullPath.empty()
                                     ? std::filesystem::path(sourceManager->getFileName(original))
                                     : fullPath;
    std::error_code ec;
    std::filesystem::path baseDir = std::filesystem::current_path();
    std::filesystem::path relative = std::filesystem::relative(path, baseDir, ec);
    if (!ec && !relative.empty()) {
        const std::string relStr = relative.generic_string();
        if (relStr.rfind("..", 0) == std::string::npos) {
            path = relative;
        } else if (!path.filename().empty()) {
            path = path.filename();
        }
    }
    info.file = path.generic_string();
    info.line = sourceManager->getLineNumber(original);
    info.column = sourceManager->getColumnNumber(original);

    auto resolveEnd = [&](slang::SourceLocation loc) {
        auto endLoc = sourceManager->getFullyOriginalLoc(loc);
        if (endLoc.valid() && sourceManager->isFileLoc(endLoc)) {
            info.endLine = sourceManager->getLineNumber(endLoc);
            info.endColumn = sourceManager->getColumnNumber(endLoc);
        }
    };

    if (end.valid()) {
        resolveEnd(end);
    }
    else {
        resolveEnd(start);
    }
    if (info.file.empty()) {
        return std::nullopt;
    }
    return info;
}

std::optional<grh::ir::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      const slang::ast::Symbol* symbol) {
    if (!symbol) {
        return std::nullopt;
    }
    return makeSrcLoc(sourceManager, symbol->location, symbol->location);
}

std::optional<grh::ir::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      const slang::ast::Expression* expr) {
    if (!expr) {
        return std::nullopt;
    }
    return makeSrcLoc(sourceManager, expr->sourceRange.start(), expr->sourceRange.end());
}

std::optional<grh::ir::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      const slang::ast::Statement* stmt) {
    if (!stmt) {
        return std::nullopt;
    }
    return makeSrcLoc(sourceManager, stmt->sourceRange.start(), stmt->sourceRange.end());
}

void applyValueSrcLoc(grh::ir::Graph& graph, ValueId value, const std::optional<grh::ir::SrcLoc>& info) {
    if (info) {
        graph.setValueSrcLoc(value, *info);
    }
}

void applyOpSrcLoc(grh::ir::Graph& graph, OperationId op, const std::optional<grh::ir::SrcLoc>& info) {
    if (info) {
        graph.setOpSrcLoc(op, *info);
    }
}

// Backward-compat helper names.
inline std::optional<grh::ir::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                slang::SourceLocation start,
                                                slang::SourceLocation end = {}) {
    return makeSrcLoc(sm, start, end);
}
inline std::optional<grh::ir::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                const slang::ast::Symbol* sym) {
    return makeSrcLoc(sm, sym);
}
inline std::optional<grh::ir::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                const slang::ast::Expression* expr) {
    return makeSrcLoc(sm, expr);
}
inline std::optional<grh::ir::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                const slang::ast::Statement* stmt) {
    return makeSrcLoc(sm, stmt);
}
inline void applyDebug(grh::ir::Graph& graph, ValueId value, const std::optional<grh::ir::SrcLoc>& info) {
    applyValueSrcLoc(graph, value, info);
}
inline void applyDebug(grh::ir::Graph& graph, OperationId op, const std::optional<grh::ir::SrcLoc>& info) {
    applyOpSrcLoc(graph, op, info);
}

bool hasBlackboxAttribute(const slang::ast::InstanceBodySymbol& body) {
    auto checkAttrs = [](std::span<const slang::ast::AttributeSymbol* const> attrs) {
        for (const slang::ast::AttributeSymbol* attr : attrs) {
            if (!attr) {
                continue;
            }
            const std::string lowered = toLowerCopy(attr->name);
            if (lowered == "blackbox" || lowered == "black_box" || lowered == "syn_black_box") {
                return true;
            }
        }
        return false;
    };

    slang::ast::Compilation& compilation = body.getCompilation();
    if (checkAttrs(compilation.getAttributes(body.getDefinition()))) {
        return true;
    }
    return checkAttrs(compilation.getAttributes(body));
}

bool hasBlackboxImplementation(const slang::ast::InstanceBodySymbol& body) {
    for (const slang::ast::Symbol& member : body.members()) {
        if (member.as_if<slang::ast::ContinuousAssignSymbol>() ||
            member.as_if<slang::ast::ProceduralBlockSymbol>() ||
            member.as_if<slang::ast::InstanceSymbol>() ||
            member.as_if<slang::ast::InstanceArraySymbol>() ||
            member.as_if<slang::ast::GenerateBlockSymbol>() ||
            member.as_if<slang::ast::GenerateBlockArraySymbol>()) {
            return true;
        }
    }
    return false;
}

bool classifyAssertSystemTask(std::string_view name, std::string& severity) {
    std::string lowered = toLowerCopy(name);
    if (!lowered.empty() && lowered.front() == '$') {
        lowered.erase(lowered.begin());
    }
    if (lowered == "fatal") {
        severity = "fatal";
        return true;
    }
    if (lowered == "error") {
        severity = "error";
        return true;
    }
    if (lowered == "warning") {
        severity = "warning";
        return true;
    }
    return false;
}

std::optional<std::string> tryExtractMessageLiteral(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::StringLiteral) {
        const auto& literal = expr.as<slang::ast::StringLiteral>();
        return std::string(literal.getValue());
    }
    return std::nullopt;
}

std::string deriveParameterSuffix(const slang::ast::InstanceBodySymbol& body) {
    std::vector<std::string> parts;
    for (const slang::ast::ParameterSymbolBase* paramBase : body.getParameters()) {
        if (!paramBase) {
            continue;
        }

        // Localparams are compile-time constants and should not alter graph names.
        if (paramBase->isLocalParam()) {
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
        bool widthKnown = false;
        std::vector<Field> fields;
    };

    static uint64_t computeFixedWidth(const slang::ast::Type& type,
                                      const slang::ast::Symbol& origin,
                                      ElaborateDiagnostics* diagnostics) {
        const uint64_t bitstreamWidth = type.getBitstreamWidth();
        if (bitstreamWidth > 0) {
            return bitstreamWidth;
        }
        if (type.hasFixedRange()) {
            const uint64_t selectable = type.getSelectableWidth();
            if (selectable > 0) {
                return selectable;
            }
        }

        const slang::ast::Type& canonical = type.getCanonicalType();
        auto accumulateStruct = [&](const slang::ast::Scope& scope, bool isUnion) -> uint64_t {
            uint64_t total = 0;
            uint64_t maxWidth = 0;
            for (const auto& field : scope.membersOfType<slang::ast::FieldSymbol>()) {
                const uint64_t fieldWidth =
                    computeFixedWidth(field.getType(), field, diagnostics);
                if (fieldWidth == 0) {
                    continue;
                }
                total += fieldWidth;
                if (fieldWidth > maxWidth) {
                    maxWidth = fieldWidth;
                }
            }
            return isUnion ? maxWidth : total;
        };

        switch (canonical.kind) {
        case slang::ast::SymbolKind::PackedArrayType: {
            const auto& packed = canonical.as<slang::ast::PackedArrayType>();
            const uint64_t elementWidth = computeFixedWidth(packed.elementType, origin, diagnostics);
            if (elementWidth == 0) {
                return 0;
            }
            const uint64_t elements = static_cast<uint64_t>(packed.range.width());
            return elementWidth * elements;
        }
        case slang::ast::SymbolKind::FixedSizeUnpackedArrayType: {
            const auto& unpacked = canonical.as<slang::ast::FixedSizeUnpackedArrayType>();
            const uint64_t elementWidth = computeFixedWidth(unpacked.elementType, origin, diagnostics);
            if (elementWidth == 0) {
                return 0;
            }
            const uint64_t elements = static_cast<uint64_t>(unpacked.range.width());
            return elementWidth * elements;
        }
        case slang::ast::SymbolKind::PackedStructType:
            return accumulateStruct(canonical.as<slang::ast::Scope>(), /* isUnion */ false);
        case slang::ast::SymbolKind::UnpackedStructType:
            return accumulateStruct(canonical.as<slang::ast::Scope>(), /* isUnion */ false);
        case slang::ast::SymbolKind::PackedUnionType:
            return accumulateStruct(canonical.as<slang::ast::Scope>(), /* isUnion */ true);
        case slang::ast::SymbolKind::UnpackedUnionType:
            return accumulateStruct(canonical.as<slang::ast::Scope>(), /* isUnion */ true);
        case slang::ast::SymbolKind::TypeAlias: {
            const auto& alias = canonical.as<slang::ast::TypeAliasType>();
            return computeFixedWidth(alias.targetType.getType(), origin, diagnostics);
        }
        default:
            break;
        }
        return bitstreamWidth;
    }

    static Info analyze(const slang::ast::Type& type, const slang::ast::Symbol& origin,
                        ElaborateDiagnostics* diagnostics) {
        Info info{};
        const uint64_t fixedWidth = computeFixedWidth(type, origin, diagnostics);
        info.widthKnown = fixedWidth > 0;
        info.width = clampBitWidth(fixedWidth, diagnostics, origin);
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
        if (const auto* assign = current->as_if<slang::ast::AssignmentExpression>()) {
            current = &assign->left();
            continue;
        }
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

class DpiCallCollector : public slang::ast::ASTVisitor<DpiCallCollector, true, false> {
public:
    DpiCallCollector(
        const std::unordered_map<const slang::ast::SubroutineSymbol*, const DpiImportEntry*>& map,
        slang::function_ref<void(const slang::ast::CallExpression&)> onCall) :
        dpiMap_(map), onCall_(onCall) {}

    void handle(const slang::ast::ExpressionStatement& stmt) {
        if (const auto* call = stmt.expr.as_if<slang::ast::CallExpression>()) {
            onCall_(*call);
        }
        visitDefault(stmt);
    }

private:
    const std::unordered_map<const slang::ast::SubroutineSymbol*, const DpiImportEntry*>& dpiMap_;
    slang::function_ref<void(const slang::ast::CallExpression&)> onCall_;
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

const slang::ast::Expression* findAssignedRhs(const slang::ast::Statement& stmt,
                                              const slang::ast::ValueSymbol& target) {
    if (const auto* exprStmt = stmt.as_if<slang::ast::ExpressionStatement>()) {
        if (const auto* assign = exprStmt->expr.as_if<slang::ast::AssignmentExpression>()) {
            if (const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(assign->left())) {
                if (symbol == &target) {
                    return &assign->right();
                }
            }
        }
    }
    if (const auto* procAssign = stmt.as_if<slang::ast::ProceduralAssignStatement>()) {
        if (const auto* assign = procAssign->assignment.as_if<slang::ast::AssignmentExpression>()) {
            if (const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(assign->left())) {
                if (symbol == &target) {
                    return &assign->right();
                }
            }
        }
    }
    if (const auto* timed = stmt.as_if<slang::ast::TimedStatement>()) {
        return findAssignedRhs(timed->stmt, target);
    }
    if (const auto* list = stmt.as_if<slang::ast::StatementList>()) {
        for (const slang::ast::Statement* child : list->list) {
            if (!child) {
                continue;
            }
            if (const slang::ast::Expression* rhs = findAssignedRhs(*child, target)) {
                return rhs;
            }
        }
    }
    if (const auto* block = stmt.as_if<slang::ast::BlockStatement>()) {
        return findAssignedRhs(block->body, target);
    }
    if (const auto* conditional = stmt.as_if<slang::ast::ConditionalStatement>()) {
        if (const slang::ast::Expression* rhs = findAssignedRhs(conditional->ifTrue, target)) {
            return rhs;
        }
        if (conditional->ifFalse) {
            if (const slang::ast::Expression* rhs = findAssignedRhs(*conditional->ifFalse, target)) {
                return rhs;
            }
        }
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
    if (auto syncInfo = detectSyncReset(block.getBody())) {
        for (const slang::ast::SignalEventControl* event : events) {
            if (!event) {
                continue;
            }
            bool activeHigh = true;
            if (const slang::ast::ValueSymbol* symbol =
                    extractResetSymbol(event->expr, activeHigh)) {
                if (symbol == syncInfo->symbol) {
                    resetEvent = event;
                    break;
                }
            }
        }
    }
    if (!resetEvent) {
        return std::nullopt;
    }
    return AsyncResetEvent{.expr = &resetEvent->expr, .edge = resetEvent->edge};
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
    memMemo_(context.memMemo),
    inoutOverrides_(context.inoutOverrides),
    origin_(context.origin),
    diagnostics_(context.diagnostics),
    sourceManager_(context.sourceManager),
    preferredBlock_(context.preferredBlock) {
    instanceId_ = nextConverterInstanceId();
}

bool LHSConverter::lower(const slang::ast::AssignmentExpression& assignment, ValueId rhsValue,
                         std::vector<LHSConverter::WriteResult>& outResults) {
    return lowerExpression(assignment.left(), rhsValue, outResults);
}

bool LHSConverter::lowerExpression(const slang::ast::Expression& expr, ValueId rhsValue,
                                   std::vector<LHSConverter::WriteResult>& outResults) {
    pending_.clear();
    outResults.clear();

    const slang::ast::Type* exprType = expr.type;
    if (!exprType || !exprType->isBitstreamType() || !exprType->isFixedSize()) {
        report("Assign LHS must be a fixed-size bitstream type");
        return false;
    }

    const int64_t lhsWidth = static_cast<int64_t>(exprType->getBitstreamWidth());
    if (lhsWidth <= 0) {
        report("Assign LHS has zero width");
        return false;
    }
    if (graph().getValue(rhsValue).width() != lhsWidth) {
        std::ostringstream oss;
        oss << "Assign width mismatch; lhs=" << lhsWidth
            << " rhs=" << graph().getValue(rhsValue).width();
        report(oss.str());
        return false;
    }

    if (!processLhs(expr, rhsValue)) {
        pending_.clear();
        return false;
    }

    flushPending(outResults);
    pending_.clear();
    return true;
}

bool LHSConverter::processLhs(const slang::ast::Expression& expr, ValueId rhsValue) {
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
                                       ValueId rhsValue) {
    const auto operands = concat.operands();
    if (operands.empty()) {
        report("Empty concatenation on assign LHS");
        return false;
    }

    int64_t remainingWidth = graph().getValue(rhsValue).width();
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
        ValueId sliceValue = createSliceValue(rhsValue, sliceLsb, currentMsb, *operand);
        if (!sliceValue.valid()) {
            return false;
        }
        if (!processLhs(*operand, sliceValue)) {
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

bool LHSConverter::handleLeaf(const slang::ast::Expression& expr, ValueId rhsValue) {
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
    if (graph().getValue(rhsValue).width() != expectedWidth) {
        std::ostringstream oss;
        oss << "Assign slice width mismatch; target=" << expectedWidth
            << " rhs=" << graph().getValue(rhsValue).width();
        report(oss.str());
        return false;
    }

    WriteBackMemo::Slice slice;
    slice.path = path.empty() && entry->symbol && !entry->symbol->name.empty() ?
                     std::string(entry->symbol->name) :
                     path;
    slice.msb = range->msb;
    slice.lsb = range->lsb;
    slice.value = rhsValue;
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
    if (inoutOverrides_) {
        auto it = inoutOverrides_->find(&symbol);
        if (it != inoutOverrides_->end()) {
            return it->second;
        }
    }
    // Prefer memory classification over reg when both views contain the symbol.
    for (const SignalMemoEntry& entry : netMemo_) {
        if (entry.symbol == &symbol) {
            return &entry;
        }
    }
    for (const SignalMemoEntry& entry : memMemo_) {
        if (entry.symbol == &symbol) {
            return &entry;
        }
    }
    const SignalMemoEntry* fallback = nullptr;
    for (const SignalMemoEntry& entry : regMemo_) {
        if (entry.symbol == &symbol) {
            if (preferredBlock_ && entry.drivingBlock && entry.drivingBlock != preferredBlock_) {
                if (!fallback) {
                    fallback = &entry;
                }
                continue;
            }
            return &entry;
        }
    }
    return fallback;
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
        std::string message = "Unable to derive flatten path for assign LHS";
        message.append(" (kind=");
        message.append(slang::ast::toString(expr.kind));
        message.push_back(')');
        report(std::move(message));
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
    if (const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>()) {
        return buildFieldPath(assignment->left());
    }
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
    seedEvalContextForLHS(ctx);
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

ValueId LHSConverter::createSliceValue(ValueId source, int64_t lsb, int64_t msb,
                                       const slang::ast::Expression& originExpr) {
    const int32_t sourceWidth = graph().getValue(source).width();
    if (lsb == 0 && msb == sourceWidth - 1) {
        return source;
    }
    if (lsb < 0 || msb < lsb || msb >= sourceWidth) {
        report("Assign RHS slice range is out of bounds");
        return ValueId::invalid();
    }

    std::string opName = "_assign_slice_op_" + std::to_string(instanceId_) + "_" +
                         std::to_string(sliceCounter_);
    std::string valueName = "_assign_slice_val_" + std::to_string(instanceId_) + "_" +
                            std::to_string(sliceCounter_);
    ++sliceCounter_;

    OperationId op = createOperation(graph(), grh::ir::OperationKind::kSliceStatic, opName);
    applyDebug(graph(), op, makeDebugInfo(sourceManager_, &originExpr));
    graph().addOperand(op, source);
    setAttr(graph(), op, "sliceStart", lsb);
    setAttr(graph(), op, "sliceEnd", msb);

    const int64_t width = msb - lsb + 1;
    ValueId value = createValue(graph(), valueName, width,
                                originExpr.type ? originExpr.type->isSigned() : false);
    applyDebug(graph(), value, makeDebugInfo(sourceManager_, &originExpr));
    graph().addResult(op, value);
    return value;
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
                                           ValueId rhsValue) {
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

ValueId AlwaysBlockRHSConverter::handleMemoEntry(
    const slang::ast::NamedValueExpression& expr, const SignalMemoEntry& entry) {
    if (const auto* symbol = expr.symbol.as_if<slang::ast::ValueSymbol>()) {
        ValueId loop = owner_.lookupLoopValue(*symbol);
        if (loop.valid()) {
            return loop;
        }
    }
    return CombRHSConverter::handleMemoEntry(expr, entry);
}

void AlwaysBlockLHSConverter::seedEvalContextForLHS(slang::ast::EvalContext& ctx) {
    owner_.seedEvalContextWithLoopValues(ctx);
}

ValueId AlwaysBlockRHSConverter::handleCustomNamedValue(
    const slang::ast::NamedValueExpression& expr) {
    if (const auto* symbol = expr.symbol.as_if<slang::ast::ValueSymbol>()) {
        ValueId value = owner_.lookupLoopValue(*symbol);
        if (value.valid()) {
            return value;
        }
    }
    return CombRHSConverter::handleCustomNamedValue(expr);
}

ValueId CombAlwaysRHSConverter::handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                                    const SignalMemoEntry& entry) {
    ValueId shadow = owner_.lookupShadowValue(entry);
    if (shadow.valid()) {
        return shadow;
    }
    return AlwaysBlockRHSConverter::handleMemoEntry(expr, entry);
}

ValueId SeqAlwaysRHSConverter::handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                                   const SignalMemoEntry& entry) {
    auto* seqOwner = static_cast<SeqAlwaysConverter*>(&owner_);
    if (seqOwner && seqOwner->useSeqShadowValues()) {
        ValueId shadow = owner_.lookupShadowValue(entry);
        if (shadow.valid()) {
            return shadow;
        }
    }
    return AlwaysBlockRHSConverter::handleMemoEntry(expr, entry);
}

ValueId SeqAlwaysRHSConverter::convertElementSelect(
    const slang::ast::ElementSelectExpression& expr) {
    auto* seqOwner = static_cast<SeqAlwaysConverter*>(&owner_);
    if (const SignalMemoEntry* entry = findMemoEntryFromExpression(expr.value())) {
        if (entry->stateOp.valid() &&
            owner_.graph().getOperation(entry->stateOp).kind() == grh::ir::OperationKind::kMemory) {
            ValueId addrValue = convert(expr.selector());
            if (!addrValue.valid()) {
                return ValueId::invalid();
            }
            if (seqOwner) {
                // Use current guard as enable for sync read if available.
                ValueId en = owner_.currentGuardValue();
                return seqOwner->buildMemorySyncRead(*entry, addrValue, expr, en);
            }
        }
    }
    return AlwaysBlockRHSConverter::convertElementSelect(expr);
}

bool AlwaysBlockLHSConverter::convert(const slang::ast::AssignmentExpression& assignment,
                                     ValueId rhsValue) {
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

bool AlwaysBlockLHSConverter::convertExpression(const slang::ast::Expression& expr,
                                                ValueId rhsValue) {
    using WriteResult = LHSConverter::WriteResult;
    std::vector<WriteResult> results;
    if (!lowerExpression(expr, rhsValue, results)) {
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
                                    ValueId rhsValue) {
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
                    if (candidate && candidate->stateOp.valid() &&
                        owner_.graph().getOperation(candidate->stateOp).kind() ==
                            grh::ir::OperationKind::kMemory) {
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

        ValueId addrValue = owner_.rhsConverter_->convert(baseElement->selector());
        if (!addrValue.valid()) {
            return true;
        }

        const slang::ast::Expression* normalizedRoot = root;
        const bool rootIsBase = normalizedRoot == baseElement;
        ValueId currentEn = owner_.currentGuardValue();
        const int64_t entryWidth = seqOwner->memoryRowWidth(*memoryEntry);

        if (rootIsBase) {
            if (owner_.graph().getValue(rhsValue).width() != entryWidth) {
                emitUnsupported("Memory word write width mismatch");
                return true;
            }
            seqOwner->recordMemoryWordWrite(*memoryEntry, assignment, addrValue, rhsValue,
                                            currentEn);
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

        if (owner_.graph().getValue(rhsValue).width() != 1) {
            emitUnsupported("Memory single-bit write expects 1-bit RHS");
            return true;
        }

        ValueId bitIndexValue = owner_.rhsConverter_->convert(bitSelect->selector());
        if (!bitIndexValue.valid()) {
            return true;
        }

        seqOwner->recordMemoryBitWrite(*memoryEntry, assignment, addrValue, bitIndexValue,
                                       rhsValue, currentEn);
        return true;
    }

    if (const auto* element = root->as_if<slang::ast::ElementSelectExpression>()) {
        std::optional<int64_t> staticIndex = evaluateConstant(element->selector());
        if (!staticIndex) {
            if (handleDynamicElementAssign(*element, rhsValue)) {
                return true;
            }
        }
    }

    return AlwaysBlockLHSConverter::convert(assignment, rhsValue);
}

bool SeqAlwaysLHSConverter::convertExpression(const slang::ast::Expression& expr,
                                              ValueId rhsValue) {
    const slang::ast::Expression* root = skipImplicitConversions(expr);
    const slang::ast::Expression* cursor = root;
    while (cursor) {
        if (const auto* element = cursor->as_if<slang::ast::ElementSelectExpression>()) {
            const slang::ast::Expression* inner = skipImplicitConversions(element->value());
            if (const auto* named = inner->as_if<slang::ast::NamedValueExpression>()) {
                if (const auto* symbol = named->symbol.as_if<slang::ast::ValueSymbol>()) {
                    const SignalMemoEntry* candidate = findMemoEntry(*symbol);
                    if (candidate && candidate->stateOp.valid() &&
                        owner_.graph().getOperation(candidate->stateOp).kind() ==
                            grh::ir::OperationKind::kMemory) {
                        if (diagnostics()) {
                            diagnostics()->nyi(owner_.block(),
                                               "DPI 输出参数写 memory 暂不支持");
                        }
                        return true;
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

    if (const auto* element = root->as_if<slang::ast::ElementSelectExpression>()) {
        std::optional<int64_t> staticIndex = evaluateConstant(element->selector());
        if (!staticIndex) {
            if (handleDynamicElementAssign(*element, rhsValue)) {
                return true;
            }
        }
    }

    return AlwaysBlockLHSConverter::convertExpression(expr, rhsValue);
}

bool SeqAlwaysLHSConverter::handleDynamicElementAssign(
    const slang::ast::ElementSelectExpression& element, ValueId rhsValue) {
    auto* seqOwner = static_cast<SeqAlwaysConverter*>(&owner_);
    if (!seqOwner) {
        return false;
    }

    const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(element);
    const SignalMemoEntry* entry = symbol ? findMemoEntry(*symbol) : nullptr;
    if (!entry || (entry->stateOp.valid() &&
                   owner_.graph().getOperation(entry->stateOp).kind() ==
                       grh::ir::OperationKind::kMemory)) {
        return false;
    }

    if (!owner_.rhsConverter_) {
        if (diagnostics()) {
            diagnostics()->nyi(owner_.block(), "Dynamic LHS index requires RHS converter context");
        }
        return true;
    }

    ValueId indexValue = owner_.rhsConverter_->convert(element.selector());
    if (!indexValue.valid()) {
        return true;
    }

    const int64_t targetWidth = entry->width > 0 ? entry->width : 1;
    const int32_t rhsWidth = owner_.graph().getValue(rhsValue).width();
    if (rhsWidth <= 0 || rhsWidth > targetWidth) {
        if (diagnostics()) {
            diagnostics()->nyi(owner_.block(), "Dynamic bit select RHS width mismatch");
        }
        return true;
    }

    ValueId baseValue = owner_.lookupShadowValue(*entry);
    if (!baseValue.valid()) {
        baseValue = entry->value;
    }
    if (!baseValue.valid()) {
        baseValue = owner_.createZeroValue(targetWidth);
    }
    if (!baseValue.valid()) {
        return true;
    }

    ValueId maskValue =
        seqOwner->buildShiftedMask(indexValue, targetWidth, "lhs_dyn_mask");
    if (!maskValue.valid()) {
        return true;
    }

    const auto debugInfo = makeDebugInfo(owner_.sourceManager_, &element);
    OperationId invMaskOp = createOperation(
        owner_.graph_, grh::ir::OperationKind::kNot, owner_.makeControlOpName("lhs_dyn_inv_mask"));
    applyDebug(owner_.graph_, invMaskOp, debugInfo);
    addOperand(owner_.graph_, invMaskOp, maskValue);
    ValueId invMaskVal = createValue(owner_.graph_, owner_.makeControlValueName("lhs_dyn_inv_mask"),
                                     targetWidth, /*isSigned=*/false);
    applyDebug(owner_.graph_, invMaskVal, debugInfo);
    addResult(owner_.graph_, invMaskOp, invMaskVal);

    OperationId holdOp = createOperation(
        owner_.graph_, grh::ir::OperationKind::kAnd, owner_.makeControlOpName("lhs_dyn_hold"));
    applyDebug(owner_.graph_, holdOp, debugInfo);
    addOperand(owner_.graph_, holdOp, baseValue);
    addOperand(owner_.graph_, holdOp, invMaskVal);
    ValueId holdVal = createValue(owner_.graph_, owner_.makeControlValueName("lhs_dyn_hold"),
                                  targetWidth, entry->isSigned);
    applyDebug(owner_.graph_, holdVal, debugInfo);
    addResult(owner_.graph_, holdOp, holdVal);

    const int64_t padWidth = targetWidth - rhsWidth;
    ValueId paddedRhs = rhsWidth == targetWidth
                                ? rhsValue
                                : seqOwner->createConcatWithZeroPadding(rhsValue, padWidth,
                                                                        "lhs_dyn_rhs_pad");
    if (!paddedRhs.valid()) {
        return true;
    }

    ValueId shiftedData =
        seqOwner->buildShiftedBitValue(paddedRhs, indexValue, targetWidth, "lhs_dyn_data");
    if (!shiftedData.valid()) {
        return true;
    }

    OperationId mergeOp = createOperation(
        owner_.graph_, grh::ir::OperationKind::kOr, owner_.makeControlOpName("lhs_dyn_merge"));
    applyDebug(owner_.graph_, mergeOp, debugInfo);
    addOperand(owner_.graph_, mergeOp, holdVal);
    addOperand(owner_.graph_, mergeOp, shiftedData);
    ValueId mergedVal = createValue(owner_.graph_, owner_.makeControlValueName("lhs_dyn_merge"),
                                    targetWidth, entry->isSigned);
    applyDebug(owner_.graph_, mergedVal, debugInfo);
    addResult(owner_.graph_, mergeOp, mergedVal);

    ValueId finalVal = mergedVal;
    ValueId guard = owner_.currentGuardValue();
    if (guard.valid()) {
        ValueId guardBit = owner_.coerceToCondition(guard);
        if (guardBit.valid()) {
            OperationId muxOp = createOperation(
                owner_.graph_, grh::ir::OperationKind::kMux, owner_.makeControlOpName("lhs_dyn_guard"));
            applyDebug(owner_.graph_, muxOp, debugInfo);
            addOperand(owner_.graph_, muxOp, guardBit);
            addOperand(owner_.graph_, muxOp, mergedVal);
            addOperand(owner_.graph_, muxOp, baseValue);
            ValueId muxVal = createValue(owner_.graph_, owner_.makeControlValueName("lhs_dyn_guard"),
                                         targetWidth, entry->isSigned);
            applyDebug(owner_.graph_, muxVal, debugInfo);
            addResult(owner_.graph_, muxOp, muxVal);
            finalVal = muxVal;
        }
    }

    WriteBackMemo::Slice slice;
    if (entry->symbol && !entry->symbol->name.empty()) {
        slice.path = std::string(entry->symbol->name);
    }
    slice.msb = targetWidth - 1;
    slice.lsb = 0;
    slice.value = finalVal;
    slice.originExpr = &element;

    std::vector<WriteBackMemo::Slice> slices;
    slices.push_back(std::move(slice));
    owner_.handleEntryWrite(*entry, std::move(slices));
    return true;
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

AlwaysConverter::AlwaysConverter(grh::ir::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                                 std::span<const SignalMemoEntry> regMemo,
                                 std::span<const SignalMemoEntry> memMemo,
                                 std::span<const DpiImportEntry> dpiImports, WriteBackMemo& memo,
                                 const slang::ast::ProceduralBlockSymbol& block,
                                 ElaborateDiagnostics* diagnostics,
                                 const slang::SourceManager* sourceManager) :
    graph_(graph),
    netMemo_(netMemo),
    regMemo_(regMemo),
    memMemo_(memMemo),
    dpiImports_(dpiImports),
    memo_(memo),
    block_(block),
    diagnostics_(diagnostics),
    sourceManager_(sourceManager) {
    shadowStack_.emplace_back();
    controlContextStack_.push_back(true);
    controlInstanceId_ = nextConverterInstanceId();
    for (const DpiImportEntry& entry : dpiImports_) {
        if (entry.symbol) {
            dpiImportMap_.emplace(entry.symbol, &entry);
        }
    }
}

void AlwaysConverter::setConverters(std::unique_ptr<AlwaysBlockRHSConverter> rhs,
                                    std::unique_ptr<AlwaysBlockLHSConverter> lhs) {
    rhsConverter_ = std::move(rhs);
    lhsConverter_ = std::move(lhs);
}

void AlwaysConverter::traverse() {
    visitStatement(block_.getBody());
}

CombAlwaysConverter::CombAlwaysConverter(grh::ir::Graph& graph,
                                         std::span<const SignalMemoEntry> netMemo,
                                         std::span<const SignalMemoEntry> regMemo,
                                         std::span<const SignalMemoEntry> memMemo,
                                         std::span<const DpiImportEntry> dpiImports,
                                         WriteBackMemo& memo,
                                         const slang::ast::ProceduralBlockSymbol& block,
                                         ElaborateDiagnostics* diagnostics,
                                         const slang::SourceManager* sourceManager) :
    AlwaysConverter(graph, netMemo, regMemo, memMemo, dpiImports, memo, block, diagnostics,
                    sourceManager) {
    const bool isAlwaysLatch =
        block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysLatch;
    auto rhs = std::make_unique<CombAlwaysRHSConverter>(
        RHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = regMemo,
                              .memMemo = memMemo,
                              .origin = &block,
                              .diagnostics = diagnostics,
                              .sourceManager = sourceManager_,
                              .preferredBlock = &block},
        *this);
    auto lhs = std::make_unique<CombAlwaysLHSConverter>(
        LHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = isAlwaysLatch ? regMemo : std::span<const SignalMemoEntry>(),
                              .memMemo = memMemo,
                              .origin = &block,
                              .diagnostics = diagnostics,
                              .sourceManager = sourceManager_,
                              .preferredBlock = &block},
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
    return true;
}

bool CombAlwaysConverter::requireNonBlockingAssignments() const {
    return false;
}

bool CombAlwaysConverter::handleDisplaySystemTask(const slang::ast::CallExpression& call,
                                                  const slang::ast::ExpressionStatement&) {
    if (diagnostics()) {
        std::string msg = "$display-like task ";
        msg.append(std::string(call.getSubroutineName()));
        msg.append(" ignored in comb always; only sequential displays are modeled");
        diagnostics()->nyi(block(), std::move(msg));
    }
    return true;
}

bool CombAlwaysConverter::handleDpiCall(const slang::ast::CallExpression& call,
                                        const DpiImportEntry&,
                                        const slang::ast::ExpressionStatement&) {
    if (diagnostics()) {
        std::string msg = "组合 always 中的 DPI 调用 ";
        msg.append(std::string(call.getSubroutineName()));
        msg.append(" 被忽略；仅支持时序 always");
        diagnostics()->nyi(block(), std::move(msg));
    }
    return true;
}

bool CombAlwaysConverter::handleAssertionIntent(const slang::ast::Expression*,
                                                const slang::ast::ExpressionStatement* origin,
                                                std::string_view,
                                                std::string_view) {
    if (diagnostics() && origin) {
        diagnostics()->nyi(block(), "组合 always 中的 assert 被忽略；GRH 仅建模时序断言");
    }
    return true;
}

SeqAlwaysConverter::SeqAlwaysConverter(grh::ir::Graph& graph,
                                       std::span<const SignalMemoEntry> netMemo,
                                       std::span<const SignalMemoEntry> regMemo,
                                       std::span<const SignalMemoEntry> memMemo,
                                       std::span<const DpiImportEntry> dpiImports,
                                       WriteBackMemo& memo,
                                       const slang::ast::ProceduralBlockSymbol& block,
                                       ElaborateDiagnostics* diagnostics,
                                       const slang::SourceManager* sourceManager) :
    AlwaysConverter(graph, netMemo, regMemo, memMemo, dpiImports, memo, block, diagnostics,
                    sourceManager) {
    auto rhs = std::make_unique<SeqAlwaysRHSConverter>(
        RHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = regMemo,
                              .memMemo = memMemo,
                              .origin = &block,
                              .diagnostics = diagnostics,
                              .sourceManager = sourceManager_,
                              .preferredBlock = &block},
        *this);
    auto lhs = std::make_unique<SeqAlwaysLHSConverter>(
        LHSConverter::Context{.graph = &graph,
                              .netMemo = netMemo,
                              .regMemo = regMemo,
                              .memMemo = memMemo,
                              .origin = &block,
                              .diagnostics = diagnostics,
                              .sourceManager = sourceManager_,
                              .preferredBlock = &block},
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

void SeqAlwaysConverter::recordAssignmentKind(bool isNonBlocking) {
    if (isNonBlocking) {
        seenNonBlockingAssignments_ = true;
    }
    else {
        seenBlockingAssignments_ = true;
    }
}

bool SeqAlwaysConverter::useSeqShadowValues() const {
    // Even when a sequential block mixes blocking and non-blocking assignments, blocking writes
    // must be visible to subsequent statements (including RHS of later non-blocking assignments).
    // Track shadow values whenever blocking assignments are present so we honor intra-block
    // ordering instead of treating the whole block as non-blocking.
    return seenBlockingAssignments_;
}

bool SeqAlwaysConverter::handleDisplaySystemTask(const slang::ast::CallExpression& call,
                                                 const slang::ast::ExpressionStatement&) {
    ValueId clkValue = ensureClockValue();
    if (!clkValue.valid()) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential display lacks resolved clock");
        }
        return true;
    }

    ValueId enableValue = ValueId::invalid();
    ValueId guard = currentGuardValue();
    if (guard.valid()) {
        enableValue = coerceToCondition(guard);
    }
    else {
        enableValue = createOneValue(1);
    }
    if (!enableValue.valid()) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Failed to derive enable for display operation");
        }
        return true;
    }

    const auto args = call.arguments();
    std::vector<const slang::ast::Expression*> valueExprs;
    valueExprs.reserve(args.size());
    std::string formatString;

    auto addValueArgument = [&](const slang::ast::Expression* expr) -> bool {
        if (!expr || expr->kind == slang::ast::ExpressionKind::EmptyArgument) {
            if (diagnostics()) {
                std::string msg = std::string(call.getSubroutineName());
                msg.append(" contains unsupported empty argument");
                diagnostics()->nyi(block(), std::move(msg));
            }
            return false;
        }
        valueExprs.push_back(expr);
        return true;
    };

    const bool hasLiteralFormat =
        !args.empty() && args.front() &&
        args.front()->kind == slang::ast::ExpressionKind::StringLiteral;

    if (hasLiteralFormat) {
        const auto& literal = args.front()->as<slang::ast::StringLiteral>();
        formatString = std::string(literal.getValue());
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (!addValueArgument(args[i])) {
                return true;
            }
        }
    }
    else {
        if (!args.empty()) {
            const slang::ast::Expression* first = args.front();
            if (first && first->type && first->type->canBeStringLike()) {
                if (diagnostics()) {
                    std::string msg = std::string(call.getSubroutineName());
                    msg.append(" requires literal format strings for GRH display conversion");
                    diagnostics()->nyi(block(), std::move(msg));
                }
                return true;
            }
        }

        for (const slang::ast::Expression* expr : args) {
            if (!addValueArgument(expr)) {
                return true;
            }
            if (!formatString.empty()) {
                formatString.push_back(' ');
            }
            formatString.append("%0d");
        }
    }

    std::vector<ValueId> valueOperands;
    valueOperands.reserve(valueExprs.size());
    for (const slang::ast::Expression* expr : valueExprs) {
        ValueId value = rhsConverter_ ? rhsConverter_->convert(*expr) : ValueId::invalid();
        if (!value.valid()) {
            return true;
        }
        valueOperands.push_back(value);
    }

    OperationId op =
        createOperation(graph(), grh::ir::OperationKind::kDisplay, makeControlOpName("display"));
    applyDebug(graph(), op, makeDebugInfo(sourceManager_, &call));
    addOperand(graph(), op, clkValue);
    addOperand(graph(), op, enableValue);
    for (ValueId operand : valueOperands) {
        addOperand(graph(), op, operand);
    }
    if (clockPolarityAttr_) {
        setAttr(graph(), op, "clkPolarity", *clockPolarityAttr_);
    }
    setAttr(graph(), op, "formatString", formatString);
    setAttr(graph(), op, "displayKind", normalizeDisplayKind(call.getSubroutineName()));
    return true;
}

bool SeqAlwaysConverter::handleDpiCall(const slang::ast::CallExpression& call,
                                       const DpiImportEntry& entry,
                                       const slang::ast::ExpressionStatement&) {
    ValueId clkValue = ensureClockValue();
    if (!clkValue.valid()) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential DPI call lacks resolved clock");
        }
        return true;
    }

    ValueId enableValue = ValueId::invalid();
    ValueId guard = currentGuardValue();
    if (guard.valid()) {
        enableValue = coerceToCondition(guard);
    }
    else {
        enableValue = createOneValue(1);
    }
    if (!enableValue.valid()) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential DPI call failed to derive enable signal");
        }
        return true;
    }

    if (!rhsConverter_ || !lhsConverter_) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential DPI call missing converter context");
        }
        return true;
    }

    const auto args = call.arguments();
    if (args.size() != entry.args.size()) {
        if (diagnostics()) {
            std::ostringstream oss;
            oss << "DPI 调用形参与声明不匹配: expected " << entry.args.size() << " got "
                << args.size();
            diagnostics()->nyi(block(), oss.str());
        }
        return true;
    }

    std::vector<ValueId> inputOperands;
    std::vector<ValueId> inoutInputOperands;
    std::vector<std::string> inputNames;
    std::vector<std::string> inoutNames;
    std::vector<ValueId> outputValues;
    std::vector<ValueId> inoutOutputValues;
    std::vector<std::string> outputNames;
    inputOperands.reserve(entry.args.size());
    inoutInputOperands.reserve(entry.args.size());
    inputNames.reserve(entry.args.size());
    inoutNames.reserve(entry.args.size());
    outputValues.reserve(entry.args.size());
    inoutOutputValues.reserve(entry.args.size());
    outputNames.reserve(entry.args.size());

    auto handleOutputArg = [&](const DpiImportArg& argInfo, const slang::ast::Expression* actual,
                               std::vector<ValueId>& outValues,
                               std::vector<std::string>& outNames) -> bool {
        std::string valueName = makeControlValueName("dpic_out");
        ValueId resultValue =
            createValue(graph(), valueName, argInfo.width > 0 ? argInfo.width : 1,
                        argInfo.isSigned);
        applyDebug(graph(), resultValue, makeDebugInfo(sourceManager_, actual));
        if (!lhsConverter_->convertExpression(*actual, resultValue)) {
            const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(*actual);
            const SignalMemoEntry* entry =
                symbol ? findMemoEntryForSymbol(*symbol) : nullptr;
            if (!entry) {
                if (diagnostics()) {
                    std::string msg = "Failed to convert DPI output argument LHS for ";
                    msg.append(argInfo.name);
                    msg.append(" (expr kind=");
                    msg.append(std::to_string(static_cast<int>(actual->kind)));
                    msg.push_back(')');
                    diagnostics()->nyi(block(), std::move(msg));
                }
                return false;
            }
            WriteBackMemo::Slice slice = buildFullSlice(*entry, resultValue);
            slice.originExpr = actual;
            std::vector<WriteBackMemo::Slice> slices;
            slices.push_back(std::move(slice));
            handleEntryWrite(*entry, std::move(slices));
        }
        outValues.push_back(resultValue);
        outNames.push_back(argInfo.name);
        return true;
    };

    for (std::size_t idx = 0; idx < entry.args.size(); ++idx) {
        const DpiImportArg& argInfo = entry.args[idx];
        const slang::ast::Expression* actual = args[idx];
        if (!actual) {
            if (diagnostics()) {
                diagnostics()->nyi(block(), "DPI 调用参数缺失表达式");
            }
            return true;
        }
        const slang::ast::Expression* actualExpr = actual;
        if (actualExpr->kind == slang::ast::ExpressionKind::Assignment) {
            const auto& assign = actualExpr->as<slang::ast::AssignmentExpression>();
            if (assign.isLValueArg()) {
                actualExpr = &assign.left();
            }
        }
        if (argInfo.direction == slang::ast::ArgumentDirection::In) {
            ValueId value = rhsConverter_->convert(*actualExpr);
            if (!value.valid()) {
                return true;
            }
            if (graph().getValue(value).width() != argInfo.width) {
                if (diagnostics()) {
                    std::ostringstream oss;
                    oss << "DPI input arg width mismatch: expected " << argInfo.width
                        << " actual " << graph().getValue(value).width();
                    diagnostics()->nyi(block(), oss.str());
                }
                return true;
            }
            inputOperands.push_back(value);
            inputNames.push_back(argInfo.name);
        }
        else if (argInfo.direction == slang::ast::ArgumentDirection::Out) {
            if (!handleOutputArg(argInfo, actualExpr, outputValues, outputNames)) {
                return true;
            }
        }
        else {
            ValueId value = rhsConverter_->convert(*actualExpr);
            if (!value.valid()) {
                return true;
            }
            if (graph().getValue(value).width() != argInfo.width) {
                if (diagnostics()) {
                    std::ostringstream oss;
                    oss << "DPI input arg width mismatch: expected " << argInfo.width
                        << " actual " << graph().getValue(value).width();
                    diagnostics()->nyi(block(), oss.str());
                }
                return true;
            }
            inoutInputOperands.push_back(value);
            if (!handleOutputArg(argInfo, actualExpr, inoutOutputValues, inoutNames)) {
                return true;
            }
        }
    }

    OperationId op =
        createOperation(graph(), grh::ir::OperationKind::kDpicCall, makeControlOpName("dpic_call"));
    applyDebug(graph(), op, makeDebugInfo(sourceManager_, &call));
    addOperand(graph(), op, clkValue);
    addOperand(graph(), op, enableValue);
    for (ValueId operand : inputOperands) {
        addOperand(graph(), op, operand);
    }
    for (ValueId operand : inoutInputOperands) {
        addOperand(graph(), op, operand);
    }
    for (ValueId result : outputValues) {
        addResult(graph(), op, result);
    }
    for (ValueId result : inoutOutputValues) {
        addResult(graph(), op, result);
    }
    if (clockPolarityAttr_) {
        setAttr(graph(), op, "clkPolarity", *clockPolarityAttr_);
    }
    if (entry.importOp.valid()) {
        setAttr(graph(), op, "targetImportSymbol",
                std::string(graph().getOperation(entry.importOp).symbolText()));
    }
    else if (entry.symbol) {
        setAttr(graph(), op, "targetImportSymbol", std::string(entry.symbol->name));
        if (diagnostics()) {
            diagnostics()->nyi(block(), "DPI import operation 未在当前 graph 中创建");
        }
    }
    else if (diagnostics()) {
        diagnostics()->nyi(block(), "DPI import operation metadata缺失");
    }
    setAttr(graph(), op, "inArgName", inputNames);
    setAttr(graph(), op, "outArgName", outputNames);
    if (!inoutNames.empty()) {
        setAttr(graph(), op, "inoutArgName", inoutNames);
    }
    return true;
}

bool SeqAlwaysConverter::handleAssertionIntent(const slang::ast::Expression* condition,
                                               const slang::ast::ExpressionStatement* origin,
                                               std::string_view message,
                                               std::string_view severity) {
    ValueId clkValue = ensureClockValue();
    if (!clkValue.valid()) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential assert lacks resolved clock");
        }
        return true;
    }

    ValueId guard = currentGuardValue();
    ValueId condBit = ValueId::invalid();
    if (condition && rhsConverter_) {
        ValueId condValue = rhsConverter_->convert(*condition);
        if (!condValue.valid()) {
            if (diagnostics() && origin) {
                diagnostics()->nyi(block(), "Failed to lower assert condition");
            }
            return true;
        }
        condBit = coerceToCondition(condValue);
    }
    if (!condBit.valid()) {
        return true;
    }

    ValueId finalCond = condBit;
    if (guard.valid()) {
        ValueId guardBit = coerceToCondition(guard);
        if (!guardBit.valid()) {
            return true;
        }
        // guard -> cond  === !guard || cond
        ValueId notGuard = buildLogicNot(guardBit);
        if (!notGuard.valid()) {
            return true;
        }
        finalCond = buildLogicOr(notGuard, condBit);
        if (!finalCond.valid()) {
            return true;
        }
    }

    OperationId op =
        createOperation(graph(), grh::ir::OperationKind::kAssert, makeControlOpName("assert"));
    applyDebug(graph(), op, origin ? makeDebugInfo(sourceManager_, origin)
                                   : makeDebugInfo(sourceManager_, condition));
    addOperand(graph(), op, clkValue);
    addOperand(graph(), op, finalCond);
    if (clockPolarityAttr_) {
        setAttr(graph(), op, "clkPolarity", *clockPolarityAttr_);
    }
    if (!message.empty()) {
        setAttr(graph(), op, "message", std::string(message));
    }
    if (!severity.empty()) {
        setAttr(graph(), op, "severity", std::string(severity));
    }
    return true;
}

void SeqAlwaysConverter::planSequentialFinalize() {
    ValueId clockValue = ensureClockValue();
    if (!clockValue.valid()) {
        return;
    }

    const bool registerWrites = finalizeRegisterWrites(clockValue);
    const bool memoryWrites = finalizeMemoryWrites(clockValue);

    // If nothing to finalize, stay silent (some blocks may be empty after static pruning).
}

bool SeqAlwaysConverter::finalizeRegisterWrites(ValueId clockValue) {
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
        if (!entry.target) {
            reportFinalizeIssue(entry, "Sequential write target lacks register metadata");
            entry.consumed = true;
            continue;
        }

        if (entry.target->multiDriver) {
            const auto debugInfo =
                makeDebugInfo(sourceManager_, entry.target ? entry.target->symbol : nullptr);
            for (const auto& slice : entry.slices) {
                if (!slice.value.valid()) {
                    reportFinalizeIssue(entry, "Multi-driver register slice missing RHS value");
                    continue;
                }
                const int64_t sliceWidth = slice.msb - slice.lsb + 1;
                OperationId split =
                    createOperation(graph(), grh::ir::OperationKind::kRegister,
                                            makeFinalizeOpName(*entry.target, "split"));
                applyDebug(graph(), split, debugInfo);
                if (clockPolarityAttr_) {
                    setAttr(graph(), split, "clkPolarity", *clockPolarityAttr_);
                }
                addOperand(graph(), split, clockValue);
                ValueId regVal = createValue(graph(), 
                    makeFinalizeValueName(*entry.target, "split"), sliceWidth, entry.target->isSigned);
                applyDebug(graph(), regVal, debugInfo);
                addResult(graph(), split, regVal);
                if (!attachDataOperand(split, slice.value, entry)) {
                    continue;
                }
                memo().recordMultiDriverPart(*entry.target,
                                             WriteBackMemo::MultiDriverPart{slice.msb, slice.lsb,
                                                                            regVal});
            }
            entry.consumed = true;
            consumedAny = true;
            continue;
        }

        OperationId stateOp = entry.target->stateOp;
        if (!stateOp) {
            reportFinalizeIssue(entry, "Sequential write target lacks register metadata");
            entry.consumed = true;
            continue;
        }
        if (graph().getOperation(stateOp).kind() == grh::ir::OperationKind::kMemory) {
            continue;
        }

        ValueId dataValue = buildDataOperand(entry);
        if (!dataValue) {
            continue;
        }
        if (dataValue.graph != graph().id()) {
            reportFinalizeIssue(entry, "Sequential write data operand belongs to a different graph");
            continue;
        }

        auto resetContext = buildResetContext(*entry.target);
        std::optional<ResetExtraction> resetExtraction;
        bool resetActive = resetContext && resetContext->kind != ResetContext::Kind::None;
        if (resetActive) {
            if (!resetContext->signal) {
                reportFinalizeIssue(entry, "Reset signal is unavailable for this register");
                continue;
            }
            if (resetContext->kind == ResetContext::Kind::Async) {
                if (valueDependsOnSignal(dataValue, resetContext->signal)) {
                    resetExtraction =
                        extractResetBranches(dataValue, resetContext->signal,
                                             resetContext->activeHigh, entry);
                }
                if (!resetExtraction) {
                    resetExtraction = extractAsyncResetAssignment(*entry.target, *resetContext);
                }
                if (!resetExtraction) {
                    continue;
                }
            }
            else {
                if (valueDependsOnSignal(dataValue, resetContext->signal)) {
                    resetExtraction =
                        extractResetBranches(dataValue, resetContext->signal,
                                             resetContext->activeHigh, entry);
                }
                if (!resetExtraction) {
                    resetExtraction = extractAsyncResetAssignment(*entry.target, *resetContext);
                }
                if (!resetExtraction) {
                    resetActive = false;
                    resetContext.reset();
                    switch (graph().getOperation(stateOp).kind()) {
                    case grh::ir::OperationKind::kRegisterRst:
                    case grh::ir::OperationKind::kRegisterArst:
                        graph().setOpKind(stateOp, grh::ir::OperationKind::kRegister);
                        break;
                    case grh::ir::OperationKind::kRegisterEnRst:
                    case grh::ir::OperationKind::kRegisterEnArst:
                        graph().setOpKind(stateOp, grh::ir::OperationKind::kRegisterEn);
                        break;
                    default:
                        break;
                    }
                    clearAttr(graph(), stateOp, "rstPolarity");
                }
            }
        }

        if (!attachClockOperand(stateOp, clockValue, entry)) {
            continue;
        }
        if (resetActive) {
            if (!resetExtraction ||
                !attachResetOperands(stateOp, resetContext->signal,
                                     resetExtraction->resetValue, entry)) {
                continue;
            }
        }
        // Attempt to extract enable pattern for both plain and reset registers, using peeled data if available.
        ValueId targetValue = entry.target ? entry.target->value : ValueId::invalid();
        if (targetValue && targetValue.graph != graph().id()) {
            if (entry.target && entry.target->symbol && !entry.target->symbol->name.empty()) {
                targetValue = graph().findValue(entry.target->symbol->name);
            }
        }
        if (targetValue && targetValue.graph == graph().id()) {
            struct EnableInfo {
                ValueId enBit = ValueId::invalid();
                ValueId newData = ValueId::invalid();
                std::string enLevel;
            };
            auto detectEnable = [&](ValueId candidate) -> std::optional<EnableInfo> {
                if (!candidate) {
                    return std::nullopt;
                }
                OperationId mux = graph().getValue(candidate).definingOp();
                if (!mux || graph().getOperation(mux).kind() != grh::ir::OperationKind::kMux ||
                    graph().getOperation(mux).operands().size() != 3) {
                    return std::nullopt;
                }
                ValueId cond = graph().getOperation(mux).operands()[0];
                ValueId tVal = graph().getOperation(mux).operands()[1];
                ValueId fVal = graph().getOperation(mux).operands()[2];

                ValueId q = targetValue;
                ValueId enRaw = ValueId::invalid();
                ValueId newData = ValueId::invalid();
                bool activeLow = false;
                if (fVal == q) {
                    // mux(en, d, Q)
                    enRaw = cond;
                    newData = tVal;
                } else if (tVal == q) {
                    // mux(en, Q, d) => load when !en
                    enRaw = cond;
                    newData = fVal;
                    activeLow = true;
                }
                if (!enRaw || !newData) {
                    return std::nullopt;
                }
                if (graph().getValue(newData).width() != graph().getValue(q).width()) {
                    return std::nullopt;
                }
                ValueId enBit = coerceToCondition(enRaw);
                if (!enBit) {
                    return std::nullopt;
                }
                EnableInfo info;
                info.enBit = enBit;
                info.newData = newData;
                info.enLevel = activeLow ? "low" : "high";
                return info;
            };
            ValueId analysisInput =
                (resetExtraction && resetExtraction->dataWithoutReset)
                    ? resetExtraction->dataWithoutReset
                    : dataValue;
            std::optional<EnableInfo> enableInfo = detectEnable(analysisInput);
            if (!enableInfo && resetExtraction && analysisInput != dataValue) {
                enableInfo = detectEnable(dataValue);
            }
            if (enableInfo) {
                const std::string &enLevel = enableInfo->enLevel;
                switch (graph().getOperation(stateOp).kind()) {
                case grh::ir::OperationKind::kRegister:
                    graph().setOpKind(stateOp, grh::ir::OperationKind::kRegisterEn);
                    addOperand(graph(), stateOp, enableInfo->enBit); // [clk, en]
                    setAttr(graph(), stateOp, "enLevel", enLevel);
                    break;
                case grh::ir::OperationKind::kRegisterRst:
                    graph().setOpKind(stateOp, grh::ir::OperationKind::kRegisterEnRst);
                    addOperand(graph(), stateOp, enableInfo->enBit); // [clk, rst, resetValue, en]
                    if (graph().getOperation(stateOp).operands().size() == 4) {
                        ValueId rstVal = graph().getOperation(stateOp).operands()[2];
                        if (rstVal) {
                            graph().replaceOperand(stateOp, 2, enableInfo->enBit);
                            graph().replaceOperand(stateOp, 3, rstVal);
                        }
                    }
                    setAttr(graph(), stateOp, "enLevel", enLevel);
                    break;
                case grh::ir::OperationKind::kRegisterArst:
                    graph().setOpKind(stateOp, grh::ir::OperationKind::kRegisterEnArst);
                    addOperand(graph(), stateOp, enableInfo->enBit);
                    if (graph().getOperation(stateOp).operands().size() == 4) {
                        ValueId rstVal = graph().getOperation(stateOp).operands()[2];
                        if (rstVal) {
                            graph().replaceOperand(stateOp, 2, enableInfo->enBit);
                            graph().replaceOperand(stateOp, 3, rstVal);
                        }
                    }
                    setAttr(graph(), stateOp, "enLevel", enLevel);
                    break;
                default:
                    break;
                }
                dataValue = enableInfo->newData; // Use unguarded data as register D
            }
        }
        if (!attachDataOperand(stateOp, dataValue, entry)) {
            continue;
        }

        entry.consumed = true;
        consumedAny = true;
    }

    return consumedAny;
}

bool SeqAlwaysConverter::finalizeMemoryWrites(ValueId clockValue) {
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

    if (!clockPolarityAttr_) {
        reportMemoryIssue(nullptr, nullptr, "clock polarity for memory ports is missing");
        return false;
    }

    auto resetCtxOpt = deriveBlockResetContext();
    const ResetContext* resetCtx =
        resetCtxOpt && resetCtxOpt->signal ? &*resetCtxOpt : nullptr;
    auto resetPolarityString = [&](const ResetContext& ctx) {
        return ctx.activeHigh ? std::string("high") : std::string("low");
    };

    auto emitWritePort = [&](const MemoryWriteIntent& intent) {
        if (!intent.entry || !intent.addr || !intent.data) {
            return;
        }
        if (!intent.entry->stateOp ||
            graph().getOperation(intent.entry->stateOp).kind() != grh::ir::OperationKind::kMemory) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory entry lacks kMemory state op");
            return;
        }
        const int64_t rowWidth = memoryRowWidth(*intent.entry);
        if (graph().getValue(intent.data).width() != rowWidth) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory data width mismatch");
            return;
        }
        ValueId enableValue =
            intent.enable ? coerceToCondition(intent.enable) : ensureMemoryEnableValue();
        if (!enableValue) {
            reportMemoryIssue(intent.entry, intent.originExpr, "failed to resolve write enable");
            return;
        }

        grh::ir::OperationKind opKind = grh::ir::OperationKind::kMemoryWritePort;
        if (resetCtx) {
            opKind = resetCtx->kind == ResetContext::Kind::Async
                         ? grh::ir::OperationKind::kMemoryWritePortArst
                         : grh::ir::OperationKind::kMemoryWritePortRst;
        }
        OperationId port =
            createOperation(graph(), opKind, makeFinalizeOpName(*intent.entry, "mem_wr"));
        applyDebug(graph(), port, intent.originExpr ? makeDebugInfo(sourceManager_, intent.originExpr)
                                           : makeDebugInfo(sourceManager_, intent.entry->symbol));
        setAttr(graph(), port, "memSymbol",
                std::string(graph().getOperation(intent.entry->stateOp).symbolText()));
        setAttr(graph(), port, "enLevel", std::string("high"));
        if (resetCtx) {
            setAttr(graph(), port, "rstPolarity", resetPolarityString(*resetCtx));
        }
        applyClockPolarity(port, "memory write port");
        addOperand(graph(), port, clockValue);
        if (resetCtx && resetCtx->signal) {
            addOperand(graph(), port, resetCtx->signal);
        }
        addOperand(graph(), port, intent.addr);
        addOperand(graph(), port, enableValue);
        addOperand(graph(), port, intent.data);
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
        if (!intent.entry->stateOp ||
            graph().getOperation(intent.entry->stateOp).kind() != grh::ir::OperationKind::kMemory) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory entry lacks kMemory state op");
            return;
        }
        const int64_t rowWidth = memoryRowWidth(*intent.entry);
        ValueId dataValue =
            buildShiftedBitValue(intent.bitValue, intent.bitIndex, rowWidth, "mem_bit_data");
        ValueId maskValue =
            buildShiftedMask(intent.bitIndex, rowWidth, "mem_bit_mask");
        if (!dataValue || !maskValue) {
            reportMemoryIssue(intent.entry, intent.originExpr,
                              "failed to synthesize memory bit intent");
            return;
        }
        ValueId enableValue =
            intent.enable ? coerceToCondition(intent.enable) : ensureMemoryEnableValue();
        if (!enableValue) {
            reportMemoryIssue(intent.entry, intent.originExpr, "failed to resolve mask write enable");
            return;
        }

        grh::ir::OperationKind opKind = grh::ir::OperationKind::kMemoryMaskWritePort;
        if (resetCtx) {
            opKind = resetCtx->kind == ResetContext::Kind::Async
                         ? grh::ir::OperationKind::kMemoryMaskWritePortArst
                         : grh::ir::OperationKind::kMemoryMaskWritePortRst;
        }
        OperationId port =
            createOperation(graph(), opKind, makeFinalizeOpName(*intent.entry, "mem_mask_wr"));
        applyDebug(graph(), port, intent.originExpr ? makeDebugInfo(sourceManager_, intent.originExpr)
                                           : makeDebugInfo(sourceManager_, intent.entry->symbol));
        setAttr(graph(), port, "memSymbol",
                std::string(graph().getOperation(intent.entry->stateOp).symbolText()));
        setAttr(graph(), port, "enLevel", std::string("high"));
        if (resetCtx) {
            setAttr(graph(), port, "rstPolarity", resetPolarityString(*resetCtx));
        }
        applyClockPolarity(port, "memory mask write port");
        addOperand(graph(), port, clockValue);
        if (resetCtx && resetCtx->signal) {
            addOperand(graph(), port, resetCtx->signal);
        }
        addOperand(graph(), port, intent.addr);
        addOperand(graph(), port, enableValue);
        addOperand(graph(), port, dataValue);
        addOperand(graph(), port, maskValue);
        emitted = true;
    };

    for (const MemoryBitWriteIntent& intent : memoryBitWrites_) {
        emitMaskWrite(intent);
    }
    memoryBitWrites_.clear();

    return emitted;
}

ValueId SeqAlwaysConverter::ensureClockValue() {
    if (cachedClockValue_) {
        return cachedClockValue_;
    }
    if (clockDeriveAttempted_) {
        return ValueId::invalid();
    }
    clockDeriveAttempted_ = true;
    std::optional<ValueId> derived = deriveClockValue();
    if (derived) {
        cachedClockValue_ = *derived;
    }
    return cachedClockValue_;
}

ValueId SeqAlwaysConverter::ensureMemoryEnableValue() {
    if (memoryEnableOne_) {
        return memoryEnableOne_;
    }
    OperationId op =
        createOperation(graph(), grh::ir::OperationKind::kConstant, makeMemoryHelperOpName("en"));
    applyDebug(graph(), op, makeDebugInfo(sourceManager_, &block()));
    ValueId value =
        createValue(graph(), makeMemoryHelperValueName("en"), 1, /*isSigned=*/false);
    applyDebug(graph(), value, makeDebugInfo(sourceManager_, &block()));
    addResult(graph(), op, value);
    setAttr(graph(), op, "constValue", "1'h1");
    memoryEnableOne_ = value;
    return memoryEnableOne_;
}

void SeqAlwaysConverter::recordMemoryWordWrite(const SignalMemoEntry& entry,
                                               const slang::ast::Expression& origin,
                                               ValueId addrValue, ValueId dataValue,
                                               ValueId enable) {
    ValueId normalizedAddr = normalizeMemoryAddress(entry, addrValue, &origin);
    if (!normalizedAddr) {
        return;
    }
    MemoryWriteIntent intent;
    intent.entry = &entry;
    intent.originExpr = &origin;
    intent.addr = normalizedAddr;
    intent.data = dataValue;
    intent.enable = enable;
    memoryWrites_.push_back(intent);
}

void SeqAlwaysConverter::recordMemoryBitWrite(const SignalMemoEntry& entry,
                                              const slang::ast::Expression& origin,
                                              ValueId addrValue, ValueId bitIndex,
                                              ValueId bitValue, ValueId enable) {
    ValueId normalizedAddr = normalizeMemoryAddress(entry, addrValue, &origin);
    if (!normalizedAddr) {
        return;
    }
    MemoryBitWriteIntent intent;
    intent.entry = &entry;
    intent.originExpr = &origin;
    intent.addr = normalizedAddr;
    intent.bitIndex = bitIndex;
    intent.bitValue = bitValue;
    intent.enable = enable;
    memoryBitWrites_.push_back(intent);
}

ValueId SeqAlwaysConverter::buildMemorySyncRead(const SignalMemoEntry& entry,
                                                    ValueId addrValue,
                                                    const slang::ast::Expression& originExpr,
                                                    ValueId enableOverride) {
    if (!entry.stateOp || graph().getOperation(entry.stateOp).kind() != grh::ir::OperationKind::kMemory) {
        if (diagnostics()) {
            diagnostics()->nyi(block(),
                               "Seq always memory read target is not backed by kMemory operation");
        }
        return ValueId::invalid();
    }

    ValueId clkValue = ensureClockValue();
    if (!clockPolarityAttr_) {
        if (diagnostics()) {
            diagnostics()->nyi(block(),
                               "Seq always memory sync read lacks clock polarity attribute");
        }
        return ValueId::invalid();
    }
    ValueId enValue = enableOverride ? coerceToCondition(enableOverride)
                                     : ensureMemoryEnableValue();
    if (!clkValue || !enValue) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Seq always memory read lacks clock or enable");
        }
        return ValueId::invalid();
    }

    ValueId normalizedAddr = normalizeMemoryAddress(entry, addrValue, &originExpr);
    if (!normalizedAddr) {
        return ValueId::invalid();
    }

    const slang::ast::Type* type = originExpr.type;
    int64_t width = memoryRowWidth(entry);
    bool isSigned = entry.isSigned;
    if (type && type->isBitstreamType() && type->isFixedSize()) {
        width = static_cast<int64_t>(type->getBitstreamWidth());
        isSigned = type->isSigned();
    }

    auto resetCtxOpt = deriveBlockResetContext();
    const ResetContext* resetCtx =
        resetCtxOpt && resetCtxOpt->signal ? &*resetCtxOpt : nullptr;
    grh::ir::OperationKind kind = grh::ir::OperationKind::kMemorySyncReadPort;
    if (resetCtx) {
        kind = resetCtx->kind == ResetContext::Kind::Async
                   ? grh::ir::OperationKind::kMemorySyncReadPortArst
                   : grh::ir::OperationKind::kMemorySyncReadPortRst;
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
    OperationId port =
        createOperation(graph(), kind, makeFinalizeOpName(entry, "mem_sync_rd"));
    applyDebug(graph(), port, debugInfo);
    setAttr(graph(), port, "memSymbol",
            std::string(graph().getOperation(entry.stateOp).symbolText()));
    setAttr(graph(), port, "enLevel", std::string("high"));
    if (resetCtx) {
        setAttr(graph(), port, "rstPolarity", resetCtx->activeHigh ? "high" : "low");
    }
    applyClockPolarity(port, "memory sync read port");
    addOperand(graph(), port, clkValue);
    if (resetCtx && resetCtx->signal) {
        addOperand(graph(), port, resetCtx->signal);
    }
    addOperand(graph(), port, normalizedAddr);
    addOperand(graph(), port, enValue);

    ValueId result =
        createValue(graph(), makeFinalizeValueName(entry, "mem_sync_rd"), width, isSigned);
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), port, result);
    return result;
}

int64_t SeqAlwaysConverter::memoryRowWidth(const SignalMemoEntry& entry) const {
    if (entry.stateOp) {
        auto widthAttr = graph().getOperation(entry.stateOp).attr("width");
        if (widthAttr) {
            const int64_t* width = std::get_if<int64_t>(&*widthAttr);
            if (width && *width > 0) {
                return *width;
            }
        }
    }
    return entry.width > 0 ? entry.width : 1;
}

std::optional<int64_t> SeqAlwaysConverter::memoryRowCount(const SignalMemoEntry& entry) const {
    if (entry.stateOp) {
        auto rowAttr = graph().getOperation(entry.stateOp).attr("row");
        if (rowAttr) {
            const int64_t* rows = std::get_if<int64_t>(&*rowAttr);
            if (rows && *rows > 0) {
                return *rows;
            }
        }
    }
    if (entry.type && entry.symbol) {
        if (auto layout = deriveMemoryLayout(*entry.type, *entry.symbol, diagnostics())) {
            if (layout->rowCount > 0) {
                return layout->rowCount;
            }
        }
    }
    return std::nullopt;
}

int64_t SeqAlwaysConverter::memoryAddrWidth(const SignalMemoEntry& entry) const {
    std::optional<int64_t> rowsOpt = memoryRowCount(entry);
    if (!rowsOpt || *rowsOpt <= 0) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "无法推导 memory 地址位宽，默认使用 1 bit");
        }
        return 1;
    }

    const uint64_t rows = static_cast<uint64_t>(*rowsOpt);
    if (rows <= 1) {
        return 1;
    }
    uint64_t minusOne = rows - 1;
    int64_t width = 0;
    while (minusOne) {
        ++width;
        minusOne >>= 1;
    }
    return width > 0 ? width : 1;
}

ValueId SeqAlwaysConverter::normalizeMemoryAddress(const SignalMemoEntry& entry,
                                                       ValueId addrValue,
                                                       const slang::ast::Expression* originExpr) {
    (void)originExpr;
    const int64_t targetWidth = memoryAddrWidth(entry);
    if (targetWidth <= 0) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "memory 地址位宽无效");
        }
        return ValueId::invalid();
    }

    const auto debugInfo = originExpr ? makeDebugInfo(sourceManager_, originExpr)
                                      : makeDebugInfo(sourceManager_, entry.symbol);
    ValueId current = addrValue;
    const int64_t currentWidth = graph().getValue(current).width() > 0 ? graph().getValue(current).width() : 1;

    if (currentWidth > targetWidth) {
        OperationId slice =
            createOperation(graph(), grh::ir::OperationKind::kSliceStatic,
                                    makeMemoryHelperOpName("addr_trunc"));
        applyDebug(graph(), slice, debugInfo);
        addOperand(graph(), slice, current);
        setAttr(graph(), slice, "sliceStart", int64_t(0));
        setAttr(graph(), slice, "sliceEnd", targetWidth - 1);
        ValueId sliced =
            createValue(graph(), makeMemoryHelperValueName("addr_trunc"), targetWidth,
                                /*isSigned=*/false);
        applyDebug(graph(), sliced, debugInfo);
        addResult(graph(), slice, sliced);
        current = sliced;
    }
    else if (currentWidth < targetWidth) {
        const int64_t padWidth = targetWidth - currentWidth;
        ValueId zero = createZeroValue(padWidth);
        if (!zero) {
            if (diagnostics()) {
                diagnostics()->nyi(block(), "memory 地址 zero-extend 失败");
            }
            return ValueId::invalid();
        }
        OperationId concat =
            createOperation(graph(), grh::ir::OperationKind::kConcat,
                                    makeMemoryHelperOpName("addr_zext"));
        applyDebug(graph(), concat, debugInfo);
        addOperand(graph(), concat, zero);
        addOperand(graph(), concat, current);
        ValueId extended =
            createValue(graph(), makeMemoryHelperValueName("addr_zext"), targetWidth,
                                /*isSigned=*/false);
        applyDebug(graph(), extended, debugInfo);
        addResult(graph(), concat, extended);
        current = extended;
    }

    if (graph().getValue(current).isSigned()) {
        OperationId assign =
            createOperation(graph(), grh::ir::OperationKind::kAssign,
                                    makeMemoryHelperOpName("addr_cast"));
        applyDebug(graph(), assign, debugInfo);
        addOperand(graph(), assign, current);
        ValueId casted =
            createValue(graph(), makeMemoryHelperValueName("addr_cast"), graph().getValue(current).width(),
                                /*isSigned=*/false);
        applyDebug(graph(), casted, debugInfo);
        addResult(graph(), assign, casted);
        current = casted;
    }

    return current;
}

bool SeqAlwaysConverter::applyClockPolarity(OperationId op, std::string_view context) {
    if (!clockPolarityAttr_) {
        if (diagnostics()) {
            std::string msg = "Seq always ";
            msg.append(context);
            msg.append(" 缺少 clkPolarity");
            diagnostics()->nyi(block(), std::move(msg));
        }
        return false;
    }
    setAttr(graph(), op, "clkPolarity", *clockPolarityAttr_);
    return true;
}

ValueId SeqAlwaysConverter::createConcatWithZeroPadding(ValueId value, int64_t padWidth,
                                                            std::string_view label) {
    if (padWidth < 0) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Negative padding requested for memory bit value");
        }
        return ValueId::invalid();
    }
    if (padWidth == 0) {
        return value;
    }
    ValueId zeroPad = createZeroValue(padWidth);
    if (!zeroPad) {
        return ValueId::invalid();
    }
    const auto debugInfo = makeDebugInfo(sourceManager_, &block());
    OperationId concat =
        createOperation(graph(), grh::ir::OperationKind::kConcat, makeMemoryHelperOpName(label));
    applyDebug(graph(), concat, debugInfo);
    addOperand(graph(), concat, zeroPad);
    addOperand(graph(), concat, value);

    ValueId wide =
        createValue(graph(), makeMemoryHelperValueName(label), padWidth + graph().getValue(value).width(),
                    graph().getValue(value).isSigned());
    applyDebug(graph(), wide, debugInfo);
    addResult(graph(), concat, wide);
    return wide;
}

ValueId SeqAlwaysConverter::buildShiftedBitValue(ValueId sourceBit, ValueId bitIndex,
                                                     int64_t targetWidth, std::string_view label) {
    if (targetWidth <= 0) {
        return ValueId::invalid();
    }
    const int64_t padWidth = targetWidth - graph().getValue(sourceBit).width();
    ValueId extended = createConcatWithZeroPadding(sourceBit, padWidth, label);
    if (!extended) {
        return ValueId::invalid();
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, &block());
    OperationId shl =
        createOperation(graph(), grh::ir::OperationKind::kShl, makeMemoryHelperOpName(label));
    applyDebug(graph(), shl, debugInfo);
    addOperand(graph(), shl, extended);
    addOperand(graph(), shl, bitIndex);
    ValueId shifted =
        createValue(graph(), makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    applyDebug(graph(), shifted, debugInfo);
    addResult(graph(), shl, shifted);
    return shifted;
}

ValueId SeqAlwaysConverter::buildShiftedMask(ValueId bitIndex, int64_t targetWidth,
                                                 std::string_view label) {
    if (targetWidth <= 0) {
        return ValueId::invalid();
    }
    slang::SVInt literal(static_cast<slang::bitwidth_t>(targetWidth), 1, /*isSigned=*/false);
    const auto debugInfo = makeDebugInfo(sourceManager_, &block());
    OperationId constOp =
        createOperation(graph(), grh::ir::OperationKind::kConstant, makeMemoryHelperOpName(label));
    applyDebug(graph(), constOp, debugInfo);
    ValueId baseValue =
        createValue(graph(), makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    applyDebug(graph(), baseValue, debugInfo);
    addResult(graph(), constOp, baseValue);
    setAttr(graph(), constOp, "constValue",
            literal.toString(slang::LiteralBase::Hex, true, literal.getBitWidth()));
    ValueId base = baseValue;
    if (!base) {
        return ValueId::invalid();
    }

    OperationId shl =
        createOperation(graph(), grh::ir::OperationKind::kShl, makeMemoryHelperOpName(label));
    applyDebug(graph(), shl, debugInfo);
    addOperand(graph(), shl, base);
    addOperand(graph(), shl, bitIndex);
    ValueId shifted =
        createValue(graph(), makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    applyDebug(graph(), shifted, debugInfo);
    addResult(graph(), shl, shifted);
    return shifted;
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

std::optional<ValueId> SeqAlwaysConverter::deriveClockValue() {
    const slang::ast::TimingControl* timing = findTimingControl(block().getBody());
    clockPolarityAttr_.reset();
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

    switch (clockEvent->edge) {
    case slang::ast::EdgeKind::PosEdge:
        clockPolarityAttr_ = "posedge";
        break;
    case slang::ast::EdgeKind::NegEdge:
        clockPolarityAttr_ = "negedge";
        break;
    default:
        break;
    }

    ValueId clkValue = convertTimingExpr(clockEvent->expr);
    if (!clkValue && diagnostics()) {
        diagnostics()->nyi(block(), "Failed to lower sequential clock expression");
    }
    return clkValue;
}

ValueId SeqAlwaysConverter::convertTimingExpr(const slang::ast::Expression& expr) {
    if (auto it = timingValueCache_.find(&expr); it != timingValueCache_.end()) {
        return it->second;
    }

    if (!rhsConverter_) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "RHS converter unavailable for timing expressions");
        }
        return ValueId::invalid();
    }

    ValueId value = rhsConverter_->convert(expr);
    if (value) {
        timingValueCache_[&expr] = value;
    }
    return value;
}

std::optional<SeqAlwaysConverter::ResetContext> SeqAlwaysConverter::deriveBlockResetContext() {
    if (blockResetDerived_) {
        if (blockResetContext_.kind == ResetContext::Kind::None || !blockResetContext_.signal) {
            return std::nullopt;
        }
        return blockResetContext_;
    }
    blockResetDerived_ = true;
    blockResetContext_ = ResetContext{};

    if (auto asyncInfo = detectAsyncResetEvent(block(), diagnostics())) {
        if (asyncInfo->edge == slang::ast::EdgeKind::PosEdge ||
            asyncInfo->edge == slang::ast::EdgeKind::NegEdge) {
            ValueId rstValue = asyncInfo->expr ? resolveAsyncResetSignal(*asyncInfo->expr)
                                               : ValueId::invalid();
            if (rstValue) {
                blockResetContext_.kind = ResetContext::Kind::Async;
                blockResetContext_.signal = rstValue;
                blockResetContext_.activeHigh = asyncInfo->edge == slang::ast::EdgeKind::PosEdge;
                return blockResetContext_;
            }
        }
        else if (diagnostics()) {
            diagnostics()->nyi(block(), "Async reset edge kind is not supported for this sequential block");
        }
    }

    if (auto syncInfo = detectSyncReset(block().getBody())) {
        if (syncInfo->symbol) {
            if (ValueId rstValue = resolveSyncResetSignal(*syncInfo->symbol)) {
                blockResetContext_.kind = ResetContext::Kind::Sync;
                blockResetContext_.signal = rstValue;
                blockResetContext_.activeHigh = syncInfo->activeHigh;
                return blockResetContext_;
            }
        }
    }

    blockResetContext_.kind = ResetContext::Kind::None;
    blockResetContext_.signal = ValueId::invalid();
    blockResetContext_.activeHigh = true;
    return std::nullopt;
}

ValueId SeqAlwaysConverter::buildDataOperand(const WriteBackMemo::Entry& entry) {
    if (!entry.target || !entry.target->value) {
        reportFinalizeIssue(entry, "Sequential write target lacks register value handle");
        return ValueId::invalid();
    }

    ValueId targetValue = entry.target->value;
    if (targetValue && targetValue.graph != graph().id()) {
        if (entry.target->symbol && !entry.target->symbol->name.empty()) {
            targetValue = graph().findValue(entry.target->symbol->name);
        }
        if (!targetValue || targetValue.graph != graph().id()) {
            reportFinalizeIssue(entry, "Register value belongs to a different graph");
            return ValueId::invalid();
        }
    }

    const int64_t targetWidth = entry.target->width > 0 ? entry.target->width : 1;
    if (graph().getValue(targetValue).width() != targetWidth) {
        reportFinalizeIssue(entry, "Register value width mismatch in sequential write");
        return ValueId::invalid();
    }

    if (entry.slices.empty()) {
        reportFinalizeIssue(entry, "Sequential write has no RHS slices to compose");
        return ValueId::invalid();
    }

    std::vector<WriteBackMemo::Slice> slices = entry.slices;
    std::sort(slices.begin(), slices.end(), [](const WriteBackMemo::Slice& lhs,
                                               const WriteBackMemo::Slice& rhs) {
        if (lhs.msb != rhs.msb) {
            return lhs.msb > rhs.msb;
        }
        return lhs.lsb > rhs.lsb;
    });

    std::vector<ValueId> components;
    components.reserve(slices.size() + 2);

    auto appendHoldRange = [&](int64_t msb, int64_t lsb) -> bool {
        if (msb < lsb) {
            return true;
        }
        ValueId hold = createHoldSlice(entry, targetValue, msb, lsb);
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
            return ValueId::invalid();
        }
        if (slice.value.graph != graph().id()) {
            reportFinalizeIssue(entry, "Sequential write slice belongs to a different graph");
            return ValueId::invalid();
        }
        if (slice.msb < slice.lsb) {
            reportFinalizeIssue(entry, "Sequential write slice has invalid bit range");
            return ValueId::invalid();
        }
        if (slice.msb > expectedMsb) {
            reportFinalizeIssue(entry, "Sequential write slice exceeds register width");
            return ValueId::invalid();
        }
        if (slice.msb < expectedMsb) {
            if (!appendHoldRange(expectedMsb, slice.msb + 1)) {
                return ValueId::invalid();
            }
        }
        components.push_back(slice.value);
        expectedMsb = slice.lsb - 1;
    }

    const auto debugInfo =
        makeDebugInfo(sourceManager_, entry.target ? entry.target->symbol : nullptr);
    if (expectedMsb >= 0) {
        if (!appendHoldRange(expectedMsb, 0)) {
            return ValueId::invalid();
        }
    }

    if (components.empty()) {
        return targetValue;
    }

    if (components.size() == 1) {
        return components.front();
    }

    // Prefer a two-operand top-level concat: [hold_hi , compact_lo],
    // where compact_lo may itself be a concat tree of the remaining pieces.
    if (components.size() > 2) {
        // Build compact_lo from components[1..end] as a left-assoc concat chain.
        auto buildChain = [&](std::vector<ValueId>::const_iterator begin,
                              std::vector<ValueId>::const_iterator end) -> ValueId {
            if (begin == end) {
                return ValueId::invalid();
            }
            ValueId acc = *begin;
            for (auto it = begin + 1; it != end; ++it) {
                OperationId c =
                    createOperation(graph(), grh::ir::OperationKind::kConcat,
                                            makeFinalizeOpName(*entry.target, "seq_concat_lo"));
                applyDebug(graph(), c, debugInfo);
                addOperand(graph(), c, acc);
                addOperand(graph(), c, *it);
                const int64_t w = graph().getValue(acc).width() + graph().getValue(*it).width();
                ValueId v =
                    createValue(graph(), makeFinalizeValueName(*entry.target, "seq_concat_lo"),
                                        w, entry.target->isSigned);
                applyDebug(graph(), v, debugInfo);
                addResult(graph(), c, v);
                acc = v;
            }
            return acc;
        };

        ValueId hi = components.front();
        ValueId lo = buildChain(components.begin() + 1, components.end());
        if (!lo) {
            return hi;
        }

        OperationId concatTop =
            createOperation(graph(), grh::ir::OperationKind::kConcat,
                                    makeFinalizeOpName(*entry.target, "seq_concat"));
        applyDebug(graph(), concatTop, debugInfo);
        addOperand(graph(), concatTop, hi);
        addOperand(graph(), concatTop, lo);
        ValueId composed =
            createValue(graph(), makeFinalizeValueName(*entry.target, "seq_concat"),
                                targetWidth, entry.target->isSigned);
        applyDebug(graph(), composed, debugInfo);
        addResult(graph(), concatTop, composed);
        return composed;
    }

    OperationId concat =
        createOperation(graph(), grh::ir::OperationKind::kConcat,
                                makeFinalizeOpName(*entry.target, "seq_concat"));
    applyDebug(graph(), concat, debugInfo);
    for (ValueId component : components) {
        addOperand(graph(), concat, component);
    }

    ValueId composed =
        createValue(graph(), makeFinalizeValueName(*entry.target, "seq_concat"), targetWidth,
                            entry.target->isSigned);
    applyDebug(graph(), composed, debugInfo);
    addResult(graph(), concat, composed);
    return composed;
}

ValueId SeqAlwaysConverter::createHoldSlice(const WriteBackMemo::Entry& entry, ValueId source,
                                                int64_t msb, int64_t lsb) {
    if (!entry.target || !source) {
        reportFinalizeIssue(entry, "Register hold slice missing target value");
        return ValueId::invalid();
    }

    if (source.graph != graph().id()) {
        reportFinalizeIssue(entry, "Register hold slice has mismatched graph value");
        return ValueId::invalid();
    }

    if (lsb < 0 || msb < lsb || msb >= graph().getValue(source).width()) {
        reportFinalizeIssue(entry, "Register hold slice range is out of bounds");
        return ValueId::invalid();
    }

    if (lsb == 0 && msb == graph().getValue(source).width() - 1) {
        return source;
    }

    const auto debugInfo =
        makeDebugInfo(sourceManager_, entry.target ? entry.target->symbol : nullptr);
    OperationId sliceOp = createOperation(graph(), grh::ir::OperationKind::kSliceStatic,
                                                      makeFinalizeOpName(*entry.target, "hold"));
    applyDebug(graph(), sliceOp, debugInfo);
    addOperand(graph(), sliceOp, source);
    setAttr(graph(), sliceOp, "sliceStart", lsb);
    setAttr(graph(), sliceOp, "sliceEnd", msb);

    ValueId result =
        createValue(graph(), makeFinalizeValueName(*entry.target, "hold"), msb - lsb + 1,
                    graph().getValue(source).isSigned());
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), sliceOp, result);
    return result;
}

bool SeqAlwaysConverter::attachClockOperand(OperationId stateOp, ValueId clkValue,
                                            const WriteBackMemo::Entry& entry) {
    const grh::ir::Operation opView = graph().getOperation(stateOp);
    auto operands = opView.operands();
    if (operands.empty()) {
        addOperand(graph(), stateOp, clkValue);
        return true;
    }

    if (operands.front() != clkValue) {
        reportFinalizeIssue(entry, "Register already bound to a different clock operand");
        return false;
    }
    return true;
}

bool SeqAlwaysConverter::attachDataOperand(OperationId stateOp, ValueId dataValue,
                                           const WriteBackMemo::Entry& entry) {
    const grh::ir::Operation opView = graph().getOperation(stateOp);
    auto operands = opView.operands();
    std::size_t expected = 1;
    if (opView.kind() == grh::ir::OperationKind::kRegisterEn) {
        expected = 2;
    } else if (opView.kind() == grh::ir::OperationKind::kRegisterRst ||
               opView.kind() == grh::ir::OperationKind::kRegisterArst) {
        expected = 3;
    } else if (opView.kind() == grh::ir::OperationKind::kRegisterEnRst ||
               opView.kind() == grh::ir::OperationKind::kRegisterEnArst) {
        expected = 4;
    }
    if (operands.size() != expected) {
        reportFinalizeIssue(entry, "Register operands not ready for data attachment");
        return false;
    }

    auto results = opView.results();
    if (!results.empty()) {
        ValueId q = results.front();
        if (q) {
            if (q.graph != graph().id()) {
                reportFinalizeIssue(entry, "Register result belongs to a different graph");
                return false;
            }
            if (graph().getValue(q).width() != graph().getValue(dataValue).width()) {
                reportFinalizeIssue(entry, "Register data width does not match Q output width");
                return false;
            }
        }
    }

    addOperand(graph(), stateOp, dataValue);
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
    base.append(std::to_string(controlInstanceId_));
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
    base.append(std::to_string(controlInstanceId_));
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
    switch (graph().getOperation(entry.stateOp).kind()) {
    case grh::ir::OperationKind::kRegisterArst:
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
    case grh::ir::OperationKind::kRegisterRst:
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

std::optional<bool> SeqAlwaysConverter::matchResetCondition(ValueId condition,
                                                            ValueId resetSignal) {
    const bool debugReset = std::getenv("WOLF_DEBUG_RESET") != nullptr;
    if (condition == resetSignal) {
        return /* inverted */ false;
    }
    OperationId op = graph().getValue(condition).definingOp();
    if (op) {
        const grh::ir::Operation opView = graph().getOperation(op);
        auto operands = opView.operands();
        if (debugReset) {
            const ValueId operand0 = operands.empty() ? ValueId::invalid() : operands.front();
            std::fprintf(stderr,
                         "[reset-debug] condition op kind=%d operands=%zu cond_width=%lld "
                         "reset_width=%lld operand0_valid=%d reset_valid=%d\n",
                         static_cast<int>(opView.kind()),
                         operands.size(),
                         static_cast<long long>(graph().getValue(condition).width()),
                         static_cast<long long>(graph().getValue(resetSignal).width()),
                         operand0.valid(),
                         resetSignal.valid());
            if (!operands.empty()) {
                OperationId child = graph().getValue(operands.front()).definingOp();
                std::fprintf(stderr, "[reset-debug] operand0 child kind=%d\n",
                             child ? static_cast<int>(graph().getOperation(child).kind()) : -1);
            }
        }
        if (operands.size() == 1 && operands.front() == resetSignal) {
            if (opView.kind() == grh::ir::OperationKind::kLogicNot) {
                return true;
            }
            // Treat a bitwise inversion of the reset signal as an active-low reset condition when
            // the widths match (e.g., "~aresetn" on a 1-bit reset).
            if (opView.kind() == grh::ir::OperationKind::kNot &&
                graph().getValue(condition).width() == graph().getValue(resetSignal).width()) {
                return true;
            }
        }
    }
    if (debugReset) {
        std::fprintf(stderr, "[reset-debug] reset condition did not match expected forms\n");
    }
    return std::nullopt;
}

bool SeqAlwaysConverter::valueDependsOnSignal(ValueId root, ValueId needle) const {
    std::vector<ValueId> stack;
    stack.push_back(root);
    std::unordered_set<ValueId, grh::ir::ValueIdHash> visited;
    while (!stack.empty()) {
        ValueId current = stack.back();
        stack.pop_back();
        if (!current) {
            continue;
        }
        if (current.graph != graph().id()) {
            continue;
        }
        if (!visited.insert(current).second) {
            continue;
        }
        if (current == needle) {
            return true;
        }
        if (OperationId op = graph().getValue(current).definingOp()) {
            const grh::ir::Operation opView = graph().getOperation(op);
            for (ValueId operand : opView.operands()) {
                if (operand) {
                    stack.push_back(operand);
                }
            }
        }
    }
    return false;
}

std::optional<SeqAlwaysConverter::ResetExtraction>
SeqAlwaysConverter::extractResetBranches(ValueId dataValue, ValueId resetSignal,
                                         bool activeHigh, const WriteBackMemo::Entry& entry) {
    const bool debugReset = std::getenv("WOLF_DEBUG_RESET") != nullptr;
    // Some guarded registers get sliced and re-concatenated during shadow merge. If all slices
    // come from the same mux in natural bit order, peel the concat back to the mux so we can
    // inspect its reset branch.
    auto tryCollapseConcat = [&](ValueId candidate) -> ValueId {
        if (!entry.target) {
            return candidate;
        }
        const int64_t targetWidth = entry.target->width > 0 ? entry.target->width
                                                            : graph().getValue(candidate).width();
        OperationId concatOp = graph().getValue(candidate).definingOp();
        if (!concatOp) {
            return candidate;
        }
        const grh::ir::Operation concatView = graph().getOperation(concatOp);
        if (concatView.kind() != grh::ir::OperationKind::kConcat) {
            return candidate;
        }

        std::vector<ValueId> parts;
        auto collectSlices = [&](auto&& self, ValueId node) -> bool {
            if (!node || node.graph != graph().id()) {
                return false;
            }
            OperationId op = graph().getValue(node).definingOp();
            if (!op) {
                return false;
            }
            const grh::ir::Operation opView = graph().getOperation(op);
            if (opView.kind() == grh::ir::OperationKind::kConcat) {
                for (ValueId operand : opView.operands()) {
                    if (!operand) {
                        return false;
                    }
                    if (!self(self, operand)) {
                        return false;
                    }
                }
                return true;
            }
            if (opView.kind() == grh::ir::OperationKind::kSliceStatic &&
                opView.operands().size() == 1) {
                parts.push_back(node);
                return true;
            }
            return false;
        };

        if (!collectSlices(collectSlices, candidate)) {
            return candidate;
        }

        ValueId commonBase = ValueId::invalid();
        int64_t expectedMsb = targetWidth - 1;
        for (ValueId part : parts) {
            if (!part) {
                return candidate;
            }
            OperationId sliceOp = graph().getValue(part).definingOp();
            if (!sliceOp) {
                return candidate;
            }
            const grh::ir::Operation sliceView = graph().getOperation(sliceOp);
            if (sliceView.kind() != grh::ir::OperationKind::kSliceStatic ||
                sliceView.operands().size() != 1) {
                return candidate;
            }
            ValueId base = sliceView.operands().front();
            if (!base) {
                return candidate;
            }

            auto startAttr = sliceView.attr("sliceStart");
            auto endAttr = sliceView.attr("sliceEnd");
            const int64_t* start = startAttr ? std::get_if<int64_t>(&*startAttr) : nullptr;
            const int64_t* end = endAttr ? std::get_if<int64_t>(&*endAttr) : nullptr;
            if (!start || !end) {
                return candidate;
            }
            if ((*end - *start + 1) != graph().getValue(part).width()) {
                return candidate;
            }
            if (*end != expectedMsb) {
                return candidate;
            }
            expectedMsb = *start - 1;

            if (!commonBase) {
                commonBase = base;
            } else if (commonBase != base) {
                return candidate;
            }
        }

        if (expectedMsb != -1) {
            return candidate;
        }
        if (!commonBase || graph().getValue(commonBase).width() != targetWidth) {
            return candidate;
        }
        return commonBase;
    };

    ValueId muxValue = tryCollapseConcat(dataValue);
    OperationId muxOp = muxValue ? graph().getValue(muxValue).definingOp() : OperationId::invalid();
    if (!muxOp || graph().getOperation(muxOp).kind() != grh::ir::OperationKind::kMux || graph().getOperation(muxOp).operands().size() != 3) {
        if (debugReset) {
            auto logOp = [&](const char* label, OperationId op) {
                if (!op) {
                    std::fprintf(stderr, "[reset-debug] %s: <null>\n", label);
                    return;
                }
                std::fprintf(stderr, "[reset-debug] %s: kind=%d operands=%zu\n", label,
                             static_cast<int>(graph().getOperation(op).kind()), graph().getOperation(op).operands().size());
                std::size_t idx = 0;
                for (ValueId operand : graph().getOperation(op).operands()) {
                    OperationId child = operand ? graph().getValue(operand).definingOp() : OperationId::invalid();
                    std::fprintf(stderr, "  operand[%zu]: value_width=%lld child_kind=%d\n", idx,
                                 operand ? static_cast<long long>(graph().getValue(operand).width()) : -1LL,
                                 child ? static_cast<int>(graph().getOperation(child).kind()) : -1);
                    ++idx;
                }
            };
            logOp("dataOp", graph().getValue(dataValue).definingOp());
            logOp("collapsedOp", muxOp);
        }
        reportFinalizeIssue(entry, "Expected mux structure to derive reset value");
        return std::nullopt;
    }
    ValueId condition = graph().getOperation(muxOp).operands().front();
    auto match = matchResetCondition(condition, resetSignal);
    if (!match) {
        reportFinalizeIssue(entry, "Reset mux condition does not reference reset signal");
        return std::nullopt;
    }

    const bool conditionTrueWhenSignalHigh = !*match;
    const bool resetWhenSignalHigh = activeHigh;
    const bool resetBranchIsTrue = conditionTrueWhenSignalHigh == resetWhenSignalHigh;
    ValueId resetValue = graph().getOperation(muxOp).operands()[resetBranchIsTrue ? 1 : 2];
    ValueId dataWithoutReset = graph().getOperation(muxOp).operands()[resetBranchIsTrue ? 2 : 1];
    if (!resetValue) {
        reportFinalizeIssue(entry, "Reset branch value is missing");
        return std::nullopt;
    }
    if (graph().getValue(resetValue).width() != graph().getValue(dataValue).width()) {
        reportFinalizeIssue(entry, "Reset branch width mismatch");
        return std::nullopt;
    }
    return ResetExtraction{resetValue, dataWithoutReset};
}

std::optional<SeqAlwaysConverter::ResetExtraction>
SeqAlwaysConverter::extractAsyncResetAssignment(const SignalMemoEntry& entry,
                                                const ResetContext& context) {
    if (!entry.symbol || !rhsConverter_) {
        return std::nullopt;
    }
    const auto* conditional = findConditional(block().getBody());
    if (!conditional || conditional->conditions.size() != 1 || !conditional->ifFalse) {
        return std::nullopt;
    }
    const slang::ast::Expression* condExpr = conditional->conditions.front().expr;
    if (!condExpr) {
        return std::nullopt;
    }

    bool condActiveHigh = true;
    const slang::ast::ValueSymbol* condSymbol = extractResetSymbol(*condExpr, condActiveHigh);
    if (!condSymbol) {
        return std::nullopt;
    }
    if (entry.asyncResetExpr) {
        bool resetActiveHigh = true;
        if (const slang::ast::ValueSymbol* resetSymbol =
                extractResetSymbol(*entry.asyncResetExpr, resetActiveHigh)) {
            if (resetSymbol != condSymbol) {
                return std::nullopt;
            }
        }
    }

    const bool resetBranchIsTrue = condActiveHigh == context.activeHigh;
    const slang::ast::Statement& resetStmt =
        resetBranchIsTrue ? conditional->ifTrue : *conditional->ifFalse;
    const slang::ast::Statement& dataStmt =
        resetBranchIsTrue ? *conditional->ifFalse : conditional->ifTrue;

    const slang::ast::Expression* resetRhs = findAssignedRhs(resetStmt, *entry.symbol);
    const slang::ast::Expression* dataRhs = findAssignedRhs(dataStmt, *entry.symbol);
    if (!resetRhs || !dataRhs) {
        return std::nullopt;
    }

    ValueId resetValue = rhsConverter_->convert(*resetRhs);
    ValueId dataWithoutReset = rhsConverter_->convert(*dataRhs);
    if (!resetValue || !dataWithoutReset) {
        return std::nullopt;
    }
    if (graph().getValue(resetValue).width() != graph().getValue(dataWithoutReset).width()) {
        return std::nullopt;
    }

    auto tryBuildEnableMux = [&](const slang::ast::Statement& stmt,
                                 ValueId dataValue) -> ValueId {
        if (!entry.symbol || !dataValue) {
            return ValueId::invalid();
        }
        const auto* conditional = findConditional(stmt);
        if (!conditional || conditional->conditions.size() != 1 ||
            conditional->conditions.front().pattern) {
            return ValueId::invalid();
        }
        const slang::ast::Expression* condExpr = conditional->conditions.front().expr;
        if (!condExpr) {
            return ValueId::invalid();
        }
        const slang::ast::Expression* trueRhs =
            findAssignedRhs(conditional->ifTrue, *entry.symbol);
        const slang::ast::Expression* falseRhs =
            conditional->ifFalse ? findAssignedRhs(*conditional->ifFalse, *entry.symbol)
                                 : nullptr;
        if (!trueRhs || falseRhs) {
            return ValueId::invalid();
        }
        ValueId holdValue = entry.value;
        if (holdValue && holdValue.graph != graph().id()) {
            if (entry.symbol && !entry.symbol->name.empty()) {
                holdValue = graph().findValue(entry.symbol->name);
            }
        }
        if (!holdValue || holdValue.graph != graph().id()) {
            return ValueId::invalid();
        }
        if (graph().getValue(holdValue).width() != graph().getValue(dataValue).width()) {
            return ValueId::invalid();
        }

        bool activeLow = false;
        const slang::ast::Expression* baseCond = condExpr;
        while (const auto* unary = baseCond->as_if<slang::ast::UnaryExpression>()) {
            using slang::ast::UnaryOperator;
            if (unary->op == UnaryOperator::LogicalNot ||
                unary->op == UnaryOperator::BitwiseNot) {
                activeLow = !activeLow;
                baseCond = &unary->operand();
                continue;
            }
            break;
        }

        ValueId condValue = rhsConverter_->convert(*baseCond);
        if (!condValue) {
            return ValueId::invalid();
        }
        ValueId condBit = coerceToCondition(condValue);
        if (!condBit) {
            return ValueId::invalid();
        }

        const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
        OperationId mux =
            createOperation(graph(), grh::ir::OperationKind::kMux,
                            makeFinalizeOpName(entry, "seq_en"));
        applyDebug(graph(), mux, debugInfo);
        addOperand(graph(), mux, condBit);
        addOperand(graph(), mux, activeLow ? holdValue : dataValue);
        addOperand(graph(), mux, activeLow ? dataValue : holdValue);
        ValueId muxValue =
            createValue(graph(), makeFinalizeValueName(entry, "seq_en"),
                        graph().getValue(dataValue).width(), entry.isSigned);
        applyDebug(graph(), muxValue, debugInfo);
        addResult(graph(), mux, muxValue);
        return muxValue;
    };

    if (ValueId muxValue = tryBuildEnableMux(dataStmt, dataWithoutReset)) {
        dataWithoutReset = muxValue;
    }
    return ResetExtraction{resetValue, dataWithoutReset};
}

bool SeqAlwaysConverter::attachResetOperands(OperationId stateOp, ValueId rstSignal,
                                             ValueId resetValue,
                                             const WriteBackMemo::Entry& entry) {
    const grh::ir::Operation opView = graph().getOperation(stateOp);
    auto operands = opView.operands();
    if (opView.kind() != grh::ir::OperationKind::kRegisterRst &&
        opView.kind() != grh::ir::OperationKind::kRegisterArst &&
        opView.kind() != grh::ir::OperationKind::kRegisterEnRst &&
        opView.kind() != grh::ir::OperationKind::kRegisterEnArst) {
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
    if (graph().getValue(resetValue).width() != entry.target->width) {
        reportFinalizeIssue(entry, "Reset value width mismatch");
        return false;
    }
    addOperand(graph(), stateOp, rstSignal);
    addOperand(graph(), stateOp, resetValue);
    return true;
}

ValueId SeqAlwaysConverter::resolveAsyncResetSignal(const slang::ast::Expression& expr) {
    if (auto it = timingValueCache_.find(&expr); it != timingValueCache_.end()) {
        return it->second;
    }
    ValueId value = convertTimingExpr(expr);
    if (value) {
        timingValueCache_[&expr] = value;
    }
    return value;
}

ValueId SeqAlwaysConverter::resolveSyncResetSignal(const slang::ast::ValueSymbol& symbol) {
    if (auto it = syncResetCache_.find(&symbol); it != syncResetCache_.end()) {
        return it->second;
    }
    ValueId value = graph().findValue(symbol.name);
    if (value) {
        syncResetCache_[&symbol] = value;
    }
    return value;
}

void AlwaysConverter::visitStatement(const slang::ast::Statement& stmt) {
    if (stmt.kind == slang::ast::StatementKind::Invalid) {
        reportInvalidStmt(stmt);
        return;
    }
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
    if (const auto* immediate = stmt.as_if<slang::ast::ImmediateAssertionStatement>()) {
        visitImmediateAssertion(*immediate);
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

    if (expr.kind == slang::ast::ExpressionKind::Call) {
        const auto& call = expr.as<slang::ast::CallExpression>();
        if (handleSystemCall(call, stmt)) {
            return;
        }
        const DpiImportEntry* dpiEntry = nullptr;
        if (const auto* subroutine =
                std::get_if<const slang::ast::SubroutineSymbol*>(&call.subroutine)) {
            dpiEntry = findDpiImport(**subroutine);
        }
        if (!dpiEntry) {
            std::string_view name = call.getSubroutineName();
            for (const DpiImportEntry& entry : dpiImports_) {
                if (entry.symbol && entry.symbol->name == name) {
                    dpiEntry = &entry;
                    break;
                }
            }
        }
        if (dpiEntry) {
            if (handleDpiCall(call, *dpiEntry, stmt)) {
                return;
            }
        }
    }

    reportUnsupportedStmt(stmt);
}

void AlwaysConverter::visitImmediateAssertion(
    const slang::ast::ImmediateAssertionStatement& stmt) {
    using slang::ast::AssertionKind;
    if (stmt.assertionKind != AssertionKind::Assert) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) + " unsupported assertion kind: ";
            message.append(std::to_string(static_cast<int>(stmt.assertionKind)));
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }
    // Deferred / final immediate assertions are not supported.
    if (stmt.isDeferred || stmt.isFinal) {
        if (diagnostics_) {
            diagnostics_->nyi(block_, std::string(modeLabel()) +
                                       " deferred/final immediate assertion not supported");
        }
        return;
    }
    std::string severity = "error";
    std::string message;
    if (stmt.ifFalse) {
        if (const auto* exprStmt = stmt.ifFalse->as_if<slang::ast::ExpressionStatement>()) {
            if (const auto* call = exprStmt->expr.as_if<slang::ast::CallExpression>()) {
                std::string taskSeverity;
                if (call->isSystemCall() &&
                    classifyAssertSystemTask(call->getSubroutineName(), taskSeverity)) {
                    severity = taskSeverity;
                    if (!call->arguments().empty()) {
                        if (auto literal = tryExtractMessageLiteral(*call->arguments().front())) {
                            message = *literal;
                        }
                    }
                }
            }
        }
    }
    handleAssertionIntent(&stmt.cond, nullptr, message, severity);
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

        if (rhsConverter_) {
            rhsConverter_->clearCache();
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
        // Iterate in ascending index order regardless of declared range direction,
        // to match foreach semantics expected by tests (e.g. low bits first).
        int32_t lo = std::min(dim.range->left, dim.range->right);
        int32_t hi = std::max(dim.range->left, dim.range->right);
        state.start = lo;
        state.stop = hi;
        state.step = 1;
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

    ValueId rawCondition = rhsConverter_ ? rhsConverter_->convert(conditionExpr)
                                         : ValueId::invalid();
    if (!rawCondition) {
        return;
    }

    if (!isSequential()) {
        ShadowFrame baseSnapshot = currentFrame();
        ShadowFrame trueFrame =
            runWithShadowFrame(baseSnapshot, stmt.ifTrue, /*isStaticContext=*/false);
        ShadowFrame falseFrame = stmt.ifFalse
                                     ? runWithShadowFrame(baseSnapshot, *stmt.ifFalse,
                                                          /*isStaticContext=*/false)
                                     : baseSnapshot;

        auto merged = mergeShadowFrames(rawCondition, std::move(trueFrame), std::move(falseFrame),
                                        stmt, "if");
        if (!merged) {
            return;
        }
        currentFrame() = std::move(*merged);
        return;
    }

    // Sequential: push guards for true/false branches, accumulate writes in child frames,
    // and merge with hold semantics.
    ValueId condBit = coerceToCondition(rawCondition);
    if (!condBit) {
        return;
    }
    ValueId notCond = buildLogicNot(condBit);
    if (!notCond) {
        return;
    }

    ShadowFrame baseSnapshot = currentFrame();
    pushGuard(condBit);
    ShadowFrame trueFrame = runWithShadowFrame(baseSnapshot, stmt.ifTrue, /*isStaticContext=*/false);
    popGuard();

    ShadowFrame falseFrame;
    if (stmt.ifFalse) {
        pushGuard(notCond);
        falseFrame =
            runWithShadowFrame(baseSnapshot, *stmt.ifFalse, /*isStaticContext=*/false);
        popGuard();
    }
    else {
        falseFrame = baseSnapshot;
    }

    auto merged =
        mergeShadowFrames(condBit, std::move(trueFrame), std::move(falseFrame), stmt, "if");
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

    ValueId controlValue = rhsConverter_ ? rhsConverter_->convert(stmt.expr)
                                         : ValueId::invalid();
    if (!controlValue) {
        return;
    }

    ShadowFrame baseSnapshot = currentFrame();
    const bool combinationalFullCase = isCombinationalFullCase(stmt);
    std::vector<CaseBranch> branches;
    branches.reserve(stmt.items.size());

    ValueId anyMatch = ValueId::invalid();
    for (const auto& item : stmt.items) {
        ValueId match = buildCaseMatch(item, controlValue, stmt.condition);
        if (!match) {
            return;
        }
        if (isSequential()) {
            pushGuard(match);
        }
        ShadowFrame branchFrame =
            runWithShadowFrame(baseSnapshot, *item.stmt, /*isStaticContext=*/false);
        if (isSequential()) {
            popGuard();
        }
        CaseBranch branch;
        branch.match = match;
        branch.frame = std::move(branchFrame);
        branches.push_back(std::move(branch));
        if (!anyMatch) {
            anyMatch = match;
        }
        else if (isSequential()) {
            anyMatch = buildLogicOr(anyMatch, match);
        }
    }

    ShadowFrame accumulator;
    if (stmt.defaultCase) {
        if (isSequential() && anyMatch) {
            ValueId notAny = buildLogicNot(anyMatch);
            pushGuard(notAny);
            accumulator = runWithShadowFrame(baseSnapshot, *stmt.defaultCase,
                                             /*isStaticContext=*/false);
            popGuard();
        }
        else {
            accumulator =
                runWithShadowFrame(baseSnapshot, *stmt.defaultCase, /*isStaticContext=*/false);
        }
    }
    else {
        if (combinationalFullCase && !branches.empty()) {
            // Fully covered combinational case without default: treat the final branch as the
            // implicit catch-all to avoid inferring a hold/latch that feeds back the output.
            accumulator = std::move(branches.back().frame);
            branches.pop_back();
        }
        else {
            accumulator = baseSnapshot;
        }
    }

    if (branches.empty()) {
        currentFrame() = std::move(accumulator);
        return;
    }

    for (auto it = branches.rbegin(); it != branches.rend(); ++it) {
        auto merged =
            mergeShadowFrames(it->match, std::move(it->frame), std::move(accumulator), stmt,
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
    bool effectiveNonBlocking = isNonBlocking;
    struct FlagGuard {
        bool& ref;
        bool saved;
        FlagGuard(bool& target, bool value) : ref(target), saved(target) { ref = value; }
        ~FlagGuard() { ref = saved; }
    } flagGuard(currentAssignmentIsNonBlocking_, isNonBlocking);
    recordAssignmentKind(isNonBlocking);
    if (isNonBlocking && !allowNonBlockingAssignments()) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) +
                                  " does not allow non-blocking assignments yet";
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }
    if (!isNonBlocking) {
        if (requireNonBlockingAssignments()) {
            effectiveNonBlocking = true;
            if (diagnostics_) {
                std::string message =
                    "blocking assignment inside sequential always/always_ff "
                    "(discouraged coding style; blocking semantics applied)";
                diagnostics_->warn(block_, std::move(message));
            }
        }
        else if (!allowBlockingAssignments()) {
            if (diagnostics_) {
                std::string message = std::string(modeLabel()) +
                                      " does not allow blocking procedural assignments";
                diagnostics_->nyi(block_, std::move(message));
            }
            return;
        }
    }
    if (effectiveNonBlocking && !isSequential()) {
        if (diagnostics_) {
            diagnostics_->warn(block_,
                               "comb always uses non-blocking assignment; treated as blocking");
        }
    }
    if (!rhsConverter_ || !lhsConverter_) {
        if (diagnostics_) {
            std::string message = std::string(modeLabel()) +
                                  " converters are not initialized (internal error)";
            diagnostics_->nyi(block_, std::move(message));
        }
        return;
    }

    ValueId rhsValue = rhsConverter_->convert(expr.right());
    if (!rhsValue) {
        return;
    }

    lhsConverter_->convert(expr, rhsValue);
}

bool AlwaysConverter::handleSystemCall(const slang::ast::CallExpression& call,
                                       const slang::ast::ExpressionStatement& stmt) {
    if (!call.isSystemCall()) {
        return false;
    }

    std::string_view name = call.getSubroutineName();
    if (isDisplaySystemTaskName(name)) {
        return handleDisplaySystemTask(call, stmt);
    }
    std::string severity;
    if (classifyAssertSystemTask(name, severity)) {
        std::string message;
        if (!call.arguments().empty()) {
            if (auto literal = tryExtractMessageLiteral(*call.arguments().front())) {
                message = *literal;
            }
        }
        handleAssertionIntent(nullptr, &stmt, message, severity);
        return true;
    }
    return false;
}

bool AlwaysConverter::handleDisplaySystemTask(const slang::ast::CallExpression&,
                                              const slang::ast::ExpressionStatement&) {
    return false;
}

bool AlwaysConverter::handleDpiCall(const slang::ast::CallExpression&,
                                    const DpiImportEntry&,
                                    const slang::ast::ExpressionStatement&) {
    return false;
}

bool AlwaysConverter::handleAssertionIntent(const slang::ast::Expression*,
                                            const slang::ast::ExpressionStatement*,
                                            std::string_view,
                                            std::string_view) {
    return false;
}

const DpiImportEntry*
AlwaysConverter::findDpiImport(const slang::ast::SubroutineSymbol& symbol) const {
    auto it = dpiImportMap_.find(&symbol);
    if (it != dpiImportMap_.end()) {
        return it->second;
    }
    for (const DpiImportEntry& entry : dpiImports_) {
        if (entry.symbol && entry.symbol->name == symbol.name) {
            return &entry;
        }
    }
    return nullptr;
}

namespace {

void insertShadowSliceList(std::vector<WriteBackMemo::Slice>& entries,
                           const WriteBackMemo::Slice& slice,
                           const std::function<ValueId(const WriteBackMemo::Slice&, int64_t, int64_t)>& sliceExisting);

} // namespace

void AlwaysConverter::flushProceduralWrites() {
    if (shadowStack_.empty()) {
        return;
    }
    ShadowFrame& root = shadowStack_.front();
    for (auto& [entry, state] : root.map) {
        if (!entry) {
            continue;
        }
        std::vector<WriteBackMemo::Slice> merged = std::move(state.slices);
        auto sliceExisting = [&](const WriteBackMemo::Slice& existing, int64_t msb, int64_t lsb) {
            return sliceExistingValue(existing, msb, lsb);
        };
        for (const auto& nb : state.nbaSlices) {
            insertShadowSliceList(merged, nb, sliceExisting);
        }
        if (merged.empty()) {
            continue;
        }
        memo_.recordWrite(*entry, WriteBackMemo::AssignmentKind::Procedural, &block_,
                         std::move(merged));
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

void AlwaysConverter::reportInvalidStmt(const slang::ast::Statement& stmt) {
    if (!diagnostics_) {
        return;
    }

    auto emitWireAssignmentDiag = [&](const slang::ast::ValueSymbol& target) {
        std::string message = std::string(modeLabel());
        message.append(" performs procedural assignment to wire ");
        if (!target.name.empty()) {
            message.append(target.name);
        }
        else {
            message.append("signal");
        }
        message.append("; declare it as logic/reg or use a continuous assign");
        diagnostics_->nyi(target, std::move(message));
    };

    auto reportWireAssignment = [&](const slang::ast::Expression& expr) -> bool {
        const auto* assign = expr.as_if<slang::ast::AssignmentExpression>();
        if (!assign) {
            return false;
        }
        if (const slang::ast::ValueSymbol* target = resolveAssignedSymbol(assign->left())) {
            if (target->kind == slang::ast::SymbolKind::Net) {
                emitWireAssignmentDiag(*target);
                return true;
            }
        }
        return false;
    };

    auto findWireAssignment = [&](const slang::ast::Statement& root) -> const slang::ast::ValueSymbol* {
        const auto checkExpr = [&](const slang::ast::Expression& expr)
                                   -> const slang::ast::ValueSymbol* {
            const auto* assign = expr.as_if<slang::ast::AssignmentExpression>();
            if (!assign) {
                return nullptr;
            }
            if (const slang::ast::ValueSymbol* sym = resolveAssignedSymbol(assign->left())) {
                if (sym->kind == slang::ast::SymbolKind::Net) {
                    return sym;
                }
            }
            return nullptr;
        };

        const auto recurse = [&](const auto& self, const slang::ast::Statement& stmtInfo)
                                 -> const slang::ast::ValueSymbol* {
            if (const auto* exprStmt = stmtInfo.as_if<slang::ast::ExpressionStatement>()) {
                if (const slang::ast::ValueSymbol* sym = checkExpr(exprStmt->expr)) {
                    return sym;
                }
            }
            if (const auto* procAssign = stmtInfo.as_if<slang::ast::ProceduralAssignStatement>()) {
                if (const slang::ast::ValueSymbol* sym = checkExpr(procAssign->assignment)) {
                    return sym;
                }
            }
            if (const auto* timed = stmtInfo.as_if<slang::ast::TimedStatement>()) {
                if (const slang::ast::ValueSymbol* sym = self(self, timed->stmt)) {
                    return sym;
                }
            }
            if (const auto* list = stmtInfo.as_if<slang::ast::StatementList>()) {
                for (const slang::ast::Statement* child : list->list) {
                    if (child) {
                        if (const slang::ast::ValueSymbol* sym = self(self, *child)) {
                            return sym;
                        }
                    }
                }
            }
            if (const auto* blockStmt = stmtInfo.as_if<slang::ast::BlockStatement>()) {
                return self(self, blockStmt->body);
            }
            if (const auto* conditional = stmtInfo.as_if<slang::ast::ConditionalStatement>()) {
                if (const slang::ast::ValueSymbol* sym = self(self, conditional->ifTrue)) {
                    return sym;
                }
                if (conditional->ifFalse) {
                    if (const slang::ast::ValueSymbol* sym = self(self, *conditional->ifFalse)) {
                        return sym;
                    }
                }
            }
            if (const auto* caseStmt = stmtInfo.as_if<slang::ast::CaseStatement>()) {
                for (const auto& item : caseStmt->items) {
                    if (item.stmt) {
                        if (const slang::ast::ValueSymbol* sym = self(self, *item.stmt)) {
                            return sym;
                        }
                    }
                }
                if (caseStmt->defaultCase) {
                    if (const slang::ast::ValueSymbol* sym = self(self, *caseStmt->defaultCase)) {
                        return sym;
                    }
                }
            }
            if (const auto* forLoop = stmtInfo.as_if<slang::ast::ForLoopStatement>()) {
                return self(self, forLoop->body);
            }
            if (const auto* repeatLoop = stmtInfo.as_if<slang::ast::RepeatLoopStatement>()) {
                return self(self, repeatLoop->body);
            }
            if (const auto* whileLoop = stmtInfo.as_if<slang::ast::WhileLoopStatement>()) {
                return self(self, whileLoop->body);
            }
            if (const auto* doWhileLoop = stmtInfo.as_if<slang::ast::DoWhileLoopStatement>()) {
                return self(self, doWhileLoop->body);
            }
            if (const auto* foreverLoop = stmtInfo.as_if<slang::ast::ForeverLoopStatement>()) {
                return self(self, foreverLoop->body);
            }
            if (const auto* foreachLoop = stmtInfo.as_if<slang::ast::ForeachLoopStatement>()) {
                return self(self, foreachLoop->body);
            }
            return nullptr;
        };

        return recurse(recurse, root);
    };

    auto findWireAssignmentInBlock = [&]() -> const slang::ast::ValueSymbol* {
        const slang::ast::ValueSymbol* offending = nullptr;
        AssignmentCollector collector([&](const slang::ast::Expression& lhs) {
            if (offending) {
                return;
            }
            if (const slang::ast::ValueSymbol* sym = resolveAssignedSymbol(lhs)) {
                if (sym->kind == slang::ast::SymbolKind::Net) {
                    offending = sym;
                }
            }
        });
        block_.getBody().visit(collector);
        return offending;
    };

    if (const auto* invalid = stmt.as_if<slang::ast::InvalidStatement>()) {
        if (const slang::ast::Statement* child = invalid->child) {
            if (const auto* exprStmt = child->as_if<slang::ast::ExpressionStatement>()) {
                if (reportWireAssignment(exprStmt->expr)) {
                    return;
                }
            }
            if (const slang::ast::ValueSymbol* netTarget = findWireAssignment(*child)) {
                emitWireAssignmentDiag(*netTarget);
                return;
            }
            if (const slang::ast::ValueSymbol* netTarget = findWireAssignment(stmt)) {
                emitWireAssignmentDiag(*netTarget);
                return;
            }
            if (&stmt != &block_.getBody()) {
                if (const slang::ast::ValueSymbol* netTarget = findWireAssignment(block_.getBody())) {
                    emitWireAssignmentDiag(*netTarget);
                    return;
                }
            }
            if (const slang::ast::ValueSymbol* netTarget = findWireAssignmentInBlock()) {
                emitWireAssignmentDiag(*netTarget);
                return;
            }
            std::string message = std::string(modeLabel());
            message.append(
                " contains an invalid statement; a common cause is procedural assignment to a "
                "wire (e.g., port not declared logic/reg)");
            diagnostics_->nyi(block_, std::move(message));
            return;
        }
    }

    std::string message = std::string(modeLabel());
    message.append(" contains an invalid statement (semantic analysis failed)");
    diagnostics_->nyi(block_, std::move(message));
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
        insertShadowSlice(state, slice, currentAssignmentIsNonBlocking_);
    }
    if (currentAssignmentIsNonBlocking_) {
        state.dirtyAll = true;
        state.composedAll = ValueId::invalid();
    }
    else {
        state.dirtyBlocking = true;
        state.dirtyAll = true;
        state.composedBlocking = ValueId::invalid();
        state.composedAll = ValueId::invalid();
    }
    currentFrame().touched.insert(&entry);
}

const SignalMemoEntry*
AlwaysConverter::findMemoEntryForSymbol(const slang::ast::ValueSymbol& symbol) const {
    auto findIn = [&](std::span<const SignalMemoEntry> memo) -> const SignalMemoEntry* {
        for (const SignalMemoEntry& entry : memo) {
            if (entry.symbol == &symbol) {
                return &entry;
            }
        }
        return nullptr;
    };
    if (const SignalMemoEntry* entry = findIn(netMemo_)) {
        return entry;
    }
    if (const SignalMemoEntry* entry = findIn(memMemo_)) {
        return entry;
    }
    return findIn(regMemo_);
}

ValueId AlwaysConverter::sliceExistingValue(const WriteBackMemo::Slice& existing,
                                                int64_t segMsb, int64_t segLsb) {
    if (!existing.value) {
        return ValueId::invalid();
    }
    if (segLsb == existing.lsb && segMsb == existing.msb) {
        return existing.value;
    }
    if (segLsb < existing.lsb || segMsb > existing.msb || segMsb < segLsb) {
        return ValueId::invalid();
    }

    const int64_t relStart = segLsb - existing.lsb;
    const int64_t relEnd = segMsb - existing.lsb;
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    OperationId op =
        createOperation(graph_, grh::ir::OperationKind::kSliceStatic, makeControlOpName("shadow_slice"));
    applyDebug(graph(), op, debugInfo);
    addOperand(graph(), op, existing.value);
    setAttr(graph(), op, "sliceStart", relStart);
    setAttr(graph(), op, "sliceEnd", relEnd);

    ValueId result = createValue(graph_, makeControlValueName("shadow_slice"),
                                 segMsb - segLsb + 1,
                                 graph().getValue(existing.value).isSigned());
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), op, result);
    return result;
}

namespace {

void insertShadowSliceList(std::vector<WriteBackMemo::Slice>& entries,
                           const WriteBackMemo::Slice& slice,
                           const std::function<ValueId(const WriteBackMemo::Slice&, int64_t, int64_t)>& sliceExisting) {
    WriteBackMemo::Slice copy = slice;
    std::vector<WriteBackMemo::Slice> preserved;
    preserved.reserve(entries.size() + 2);

    for (const WriteBackMemo::Slice& existing : entries) {
        const bool overlap = !(copy.msb < existing.lsb || copy.lsb > existing.msb);
        if (!overlap) {
            preserved.push_back(existing);
            continue;
        }

        // Preserve upper segment of existing slice if it sits above the new slice.
        if (existing.msb > copy.msb) {
            const int64_t segLsb = copy.msb + 1;
            WriteBackMemo::Slice upper = existing;
            upper.msb = existing.msb;
            upper.lsb = segLsb;
            upper.value = sliceExisting(existing, upper.msb, upper.lsb);
            if (upper.value) {
                preserved.push_back(std::move(upper));
            }
        }

        // Preserve lower segment of existing slice if it sits below the new slice.
        if (existing.lsb < copy.lsb) {
            const int64_t segMsb = copy.lsb - 1;
            WriteBackMemo::Slice lower = existing;
            lower.msb = segMsb;
            lower.lsb = existing.lsb;
            lower.value = sliceExisting(existing, lower.msb, lower.lsb);
            if (lower.value) {
                preserved.push_back(std::move(lower));
            }
        }
    }

    preserved.push_back(std::move(copy));
    entries = std::move(preserved);

    std::sort(entries.begin(), entries.end(), [](const WriteBackMemo::Slice& lhs,
                                                 const WriteBackMemo::Slice& rhs) {
        if (lhs.msb != rhs.msb) {
            return lhs.msb > rhs.msb;
        }
        return lhs.lsb > rhs.lsb;
    });
}

} // namespace

void AlwaysConverter::insertShadowSlice(ShadowState& state,
                                        const WriteBackMemo::Slice& slice, bool nonBlocking) {
    auto sliceExisting = [&](const WriteBackMemo::Slice& existing, int64_t msb, int64_t lsb) {
        return sliceExistingValue(existing, msb, lsb);
    };
    auto& entries = nonBlocking ? state.nbaSlices : state.slices;
    insertShadowSliceList(entries, slice, sliceExisting);
}

ValueId AlwaysConverter::lookupShadowValue(const SignalMemoEntry& entry) {
    ShadowFrame& frame = currentFrame();
    auto it = frame.map.find(&entry);
    if (it == frame.map.end()) {
        return ValueId::invalid();
    }
    ShadowState& state = it->second;
    if (!state.dirtyBlocking && state.composedBlocking) {
        return state.composedBlocking;
    }
    return rebuildShadowValue(entry, state, /*includeNonBlocking=*/false);
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

ValueId AlwaysConverter::rebuildShadowValue(const SignalMemoEntry& entry,
                                                    ShadowState& state) {
    return rebuildShadowValue(entry, state, /*includeNonBlocking=*/true);
}

ValueId AlwaysConverter::rebuildShadowValue(const SignalMemoEntry& entry,
                                                    ShadowState& state, bool includeNonBlocking) {
    auto collectSlices = [&]() {
        if (!includeNonBlocking || state.nbaSlices.empty()) {
            return state.slices;
        }
        std::vector<WriteBackMemo::Slice> merged = state.slices;
        auto sliceExisting = [&](const WriteBackMemo::Slice& existing, int64_t msb,
                                 int64_t lsb) { return sliceExistingValue(existing, msb, lsb); };
        for (const auto& nb : state.nbaSlices) {
            insertShadowSliceList(merged, nb, sliceExisting);
        }
        return merged;
    };

    const bool cachedAvailable = includeNonBlocking ? (!state.dirtyAll && state.composedAll)
                                                    : (!state.dirtyBlocking && state.composedBlocking);
    if (cachedAvailable) {
        return includeNonBlocking ? state.composedAll : state.composedBlocking;
    }

    std::vector<WriteBackMemo::Slice> mergedSlices = collectSlices();
    if (mergedSlices.empty()) {
        if (includeNonBlocking) {
            state.composedAll = ValueId::invalid();
            state.dirtyAll = false;
        }
        else {
            state.composedBlocking = ValueId::invalid();
            state.dirtyBlocking = false;
        }
        return ValueId::invalid();
    }

    const int64_t targetWidth = entry.width > 0 ? entry.width : 1;
    int64_t expectedMsb = targetWidth - 1;
    std::vector<ValueId> components;
    components.reserve(mergedSlices.size() + 2);

    auto appendHoldRange = [&](int64_t msb, int64_t lsb) -> bool {
        if (msb < lsb) {
            return true;
        }
        ValueId hold = ValueId::invalid();
        if (entry.value) {
            if (lsb == 0 && msb == graph().getValue(entry.value).width() - 1) {
                hold = entry.value;
            }
            else {
                const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
                OperationId sliceOp =
                    createOperation(graph_, grh::ir::OperationKind::kSliceStatic,
                                    makeShadowOpName(entry, "hold"));
                applyDebug(graph(), sliceOp, debugInfo);
                addOperand(graph(), sliceOp, entry.value);
                setAttr(graph(), sliceOp, "sliceStart", lsb);
                setAttr(graph(), sliceOp, "sliceEnd", msb);

                ValueId result = createValue(graph_,
                    makeShadowValueName(entry, "hold"), msb - lsb + 1, entry.isSigned);
                applyDebug(graph(), result, debugInfo);
                addResult(graph(), sliceOp, result);
                hold = result;
            }
        }
        else {
            hold = createZeroValue(msb - lsb + 1);
        }
        if (!hold) {
            if (includeNonBlocking) {
                state.composedAll = ValueId::invalid();
                state.dirtyAll = false;
            }
            else {
                state.composedBlocking = ValueId::invalid();
                state.dirtyBlocking = false;
            }
            return false;
        }
        components.push_back(hold);
        return true;
    };

    for (const WriteBackMemo::Slice& slice : mergedSlices) {
        const int64_t gapWidth = expectedMsb - slice.msb;
        if (gapWidth > 0) {
            if (!appendHoldRange(expectedMsb, slice.msb + 1)) {
                return ValueId::invalid();
            }
            expectedMsb -= gapWidth;
        }
        components.push_back(slice.value);
        expectedMsb = slice.lsb - 1;
    }

    if (expectedMsb >= 0) {
        if (!appendHoldRange(expectedMsb, 0)) {
            return ValueId::invalid();
        }
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
    ValueId composed = ValueId::invalid();
    if (components.size() == 1) {
        composed = components.front();
    }
    else {
        OperationId concat =
            createOperation(graph_, grh::ir::OperationKind::kConcat,
                                   makeShadowOpName(entry, "shadow_concat"));
        applyDebug(graph(), concat, debugInfo);
        for (ValueId operand : components) {
            addOperand(graph(), concat, operand);
        }
        ValueId value =
            createValue(graph_, makeShadowValueName(entry, "shadow"), targetWidth, entry.isSigned);
        applyDebug(graph(), value, debugInfo);
        addResult(graph(), concat, value);
        composed = value;
    }

    if (includeNonBlocking) {
        state.composedAll = composed;
        state.dirtyAll = false;
    }
    else {
        state.composedBlocking = composed;
        state.dirtyBlocking = false;
    }
    return composed;
}

ValueId AlwaysConverter::createZeroValue(int64_t width) {
    if (width <= 0) {
        return ValueId::invalid();
    }
    if (auto it = zeroCache_.find(width); it != zeroCache_.end()) {
        return it->second;
    }

    std::string opName = "_comb_zero_";
    opName.append(std::to_string(controlInstanceId_));
    opName.push_back('_');
    opName.append(std::to_string(shadowNameCounter_++));
    std::string valueName = "_comb_zero_val_";
    valueName.append(std::to_string(controlInstanceId_));
    valueName.push_back('_');
    valueName.append(std::to_string(shadowNameCounter_++));

    OperationId op = createOperation(graph_, grh::ir::OperationKind::kConstant, opName);
    applyDebug(graph(), op, makeDebugInfo(sourceManager_, &block_));
    ValueId value = createValue(graph_, valueName, width, /*isSigned=*/false);
    applyDebug(graph(), value, makeDebugInfo(sourceManager_, &block_));
    addResult(graph(), op, value);
    std::ostringstream oss;
    oss << width << "'h0";
    setAttr(graph(), op, "constValue", oss.str());
    zeroCache_[width] = value;
    return value;
}

ValueId AlwaysConverter::createOneValue(int64_t width) {
    if (width <= 0) {
        return ValueId::invalid();
    }
    if (auto it = oneCache_.find(width); it != oneCache_.end()) {
        return it->second;
    }
    std::string opName = "_comb_one_";
    opName.append(std::to_string(controlInstanceId_));
    opName.push_back('_');
    opName.append(std::to_string(shadowNameCounter_++));
    std::string valueName = "_comb_one_val_";
    valueName.append(std::to_string(controlInstanceId_));
    valueName.push_back('_');
    valueName.append(std::to_string(shadowNameCounter_++));
    OperationId op = createOperation(graph_, grh::ir::OperationKind::kConstant, opName);
    applyDebug(graph(), op, makeDebugInfo(sourceManager_, &block_));
    ValueId value = createValue(graph_, valueName, width, /*isSigned=*/false);
    applyDebug(graph(), value, makeDebugInfo(sourceManager_, &block_));
    addResult(graph(), op, value);
    std::ostringstream oss;
    // Fill with ones: e.g. 8'hff
    oss << width << "'h";
    const int64_t hexDigits = (width + 3) / 4;
    for (int64_t i = 0; i < hexDigits; ++i) {
        oss << 'f';
    }
    setAttr(graph(), op, "constValue", oss.str());
    oneCache_[width] = value;
    return value;
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
    base.append(std::to_string(controlInstanceId_));
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
    base.append(std::to_string(controlInstanceId_));
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
AlwaysConverter::mergeShadowFrames(ValueId condition, ShadowFrame&& trueFrame,
                                       ShadowFrame&& falseFrame,
                                       const slang::ast::Statement& /*originStmt*/,
                                       std::string_view label) {
    std::unordered_set<const SignalMemoEntry*> coverage;
    coverage.insert(trueFrame.touched.begin(), trueFrame.touched.end());
    coverage.insert(falseFrame.touched.begin(), falseFrame.touched.end());

    if (coverage.empty()) {
        return std::move(falseFrame);
    }

    ValueId condBit = coerceToCondition(condition);
    if (!condBit) {
        return std::nullopt;
    }

    for (const SignalMemoEntry* entry : coverage) {
        if (!entry) {
            reportLatchIssue("comb always branch references unknown target");
            return std::nullopt;
        }

        auto trueIt = trueFrame.map.find(entry);
        auto falseIt = falseFrame.map.find(entry);
        ValueId trueValue = ValueId::invalid();
        ValueId falseValue = ValueId::invalid();
        bool inferredLatch = false;

        if (!isSequential()) {
            ValueId holdValue = entry->value;
            if ((trueIt == trueFrame.map.end() || falseIt == falseFrame.map.end()) && !holdValue) {
                reportLatchIssue("comb always branch coverage incomplete but missing hold value",
                                 entry);
                return std::nullopt;
            }
            if (trueIt == trueFrame.map.end()) {
                trueValue = holdValue;
                inferredLatch = true;
            }
            if (falseIt == falseFrame.map.end()) {
                falseValue = holdValue;
                inferredLatch = true;
            }
        }

        if (trueIt != trueFrame.map.end()) {
            trueValue = rebuildShadowValue(*entry, trueIt->second);
        }
        if (falseIt != falseFrame.map.end()) {
            falseValue = rebuildShadowValue(*entry, falseIt->second);
        }

        // Sequential semantics: missing branch implies hold on that branch.
        if (isSequential()) {
            if (!trueValue) {
                // Use current entry value (Q) as hold.
                if (!entry->value) {
                    reportLatchIssue("seq always missing hold value for true branch", entry);
                    return std::nullopt;
                }
                trueValue = entry->value;
            }
            if (!falseValue) {
                if (!entry->value) {
                    reportLatchIssue("seq always missing hold value for false branch", entry);
                    return std::nullopt;
                }
                falseValue = entry->value;
            }
        }

        if (!trueValue || !falseValue) {
            return std::nullopt;
        }

        ValueId muxValue = createMuxForEntry(*entry, condBit, trueValue, falseValue, label);
        if (!muxValue) {
            return std::nullopt;
        }
        if (inferredLatch) {
            reportLatchIssue("comb always branch coverage incomplete; latch inferred", entry);
        }

        ShadowState mergedState;
        if (isSequential()) {
            std::vector<int64_t> cuts;
            const int64_t top = entry->width > 0 ? entry->width - 1 : 0;
            cuts.push_back(top);
            cuts.push_back(-1);
            auto collectCuts = [&](const ShadowState* state) {
                if (!state) {
                    return;
                }
                for (const auto& s : state->slices) {
                    cuts.push_back(s.msb);
                    cuts.push_back(s.lsb - 1);
                }
                for (const auto& s : state->nbaSlices) {
                    cuts.push_back(s.msb);
                    cuts.push_back(s.lsb - 1);
                }
            };
            collectCuts(trueIt != trueFrame.map.end() ? &trueIt->second : nullptr);
            collectCuts(falseIt != falseFrame.map.end() ? &falseIt->second : nullptr);
            std::sort(cuts.begin(), cuts.end(), std::greater<int64_t>());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

            WriteBackMemo::Slice muxSlice;
            if (entry->symbol && !entry->symbol->name.empty()) {
                muxSlice.path = std::string(entry->symbol->name);
            }
            muxSlice.msb = top;
            muxSlice.lsb = 0;
            muxSlice.value = muxValue;

            for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
                const int64_t segMsb = cuts[i];
                const int64_t segLsb = cuts[i + 1] + 1;
                if (segMsb < segLsb) {
                    continue;
                }
                WriteBackMemo::Slice seg = muxSlice;
                seg.msb = segMsb;
                seg.lsb = segLsb;
                seg.value = sliceExistingValue(muxSlice, segMsb, segLsb);
                if (seg.value) {
                    mergedState.slices.push_back(std::move(seg));
                }
            }
            if (mergedState.slices.empty()) {
                mergedState.slices.push_back(buildFullSlice(*entry, muxValue));
            }
        }
        else {
            if (entry->multiDriver) {
                struct Range {
                    int64_t msb = 0;
                    int64_t lsb = 0;
                };
                std::vector<Range> ranges;
                auto collectRanges = [&](const ShadowState* state) {
                    if (!state) {
                        return;
                    }
                    for (const auto& slice : state->slices) {
                        ranges.push_back(Range{slice.msb, slice.lsb});
                    }
                    for (const auto& slice : state->nbaSlices) {
                        ranges.push_back(Range{slice.msb, slice.lsb});
                    }
                };
                collectRanges(trueIt != trueFrame.map.end() ? &trueIt->second : nullptr);
                collectRanges(falseIt != falseFrame.map.end() ? &falseIt->second : nullptr);

                if (!ranges.empty()) {
                    std::sort(ranges.begin(), ranges.end(), [](const Range& lhs, const Range& rhs) {
                        if (lhs.lsb != rhs.lsb) {
                            return lhs.lsb < rhs.lsb;
                        }
                        return lhs.msb < rhs.msb;
                    });

                    std::vector<Range> mergedRanges;
                    mergedRanges.reserve(ranges.size());
                    for (const Range& range : ranges) {
                        if (mergedRanges.empty() || range.lsb > mergedRanges.back().msb + 1) {
                            mergedRanges.push_back(range);
                        }
                        else {
                            mergedRanges.back().msb = std::max(mergedRanges.back().msb, range.msb);
                        }
                    }

                    std::sort(mergedRanges.begin(), mergedRanges.end(),
                              [](const Range& lhs, const Range& rhs) {
                                  if (lhs.msb != rhs.msb) {
                                      return lhs.msb > rhs.msb;
                                  }
                                  return lhs.lsb > rhs.lsb;
                              });

                    WriteBackMemo::Slice muxSlice = buildFullSlice(*entry, muxValue);
                    for (const Range& range : mergedRanges) {
                        WriteBackMemo::Slice slice = muxSlice;
                        slice.msb = range.msb;
                        slice.lsb = range.lsb;
                        slice.value = sliceExistingValue(muxSlice, range.msb, range.lsb);
                        if (slice.value) {
                            mergedState.slices.push_back(std::move(slice));
                        }
                    }
                }
                else {
                    mergedState.slices.push_back(buildFullSlice(*entry, muxValue));
                }
            }
            else {
                mergedState.slices.push_back(buildFullSlice(*entry, muxValue));
            }
        }
        mergedState.composedBlocking = muxValue;
        mergedState.composedAll = muxValue;
        mergedState.dirtyBlocking = false;
        mergedState.dirtyAll = false;
        if (falseIt == falseFrame.map.end()) {
            falseFrame.map.emplace(entry, std::move(mergedState));
        }
        else {
            falseIt->second = std::move(mergedState);
        }
    }

    falseFrame.touched.insert(coverage.begin(), coverage.end());
    return std::move(falseFrame);
}

WriteBackMemo::Slice AlwaysConverter::buildFullSlice(const SignalMemoEntry& entry,
                                                         ValueId value) {
    WriteBackMemo::Slice slice;
    if (entry.symbol && !entry.symbol->name.empty()) {
        slice.path = std::string(entry.symbol->name);
    }
    const int64_t width = entry.width > 0 ? entry.width : 1;
    slice.msb = width - 1;
    slice.lsb = 0;
    slice.value = value;
    slice.originExpr = nullptr;
    return slice;
}

ValueId AlwaysConverter::createMuxForEntry(const SignalMemoEntry& entry,
                                                   ValueId condition, ValueId onTrue,
                                                   ValueId onFalse, std::string_view label) {
    const int64_t width = entry.width > 0 ? entry.width : 1;
    if (graph().getValue(onTrue).width() != width || graph().getValue(onFalse).width() != width) {
        reportLatchIssue("comb always mux width mismatch", &entry);
        return ValueId::invalid();
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
    OperationId op =
        createOperation(graph_, grh::ir::OperationKind::kMux, makeShadowOpName(entry, label));
    applyDebug(graph(), op, debugInfo);
    addOperand(graph(), op, condition);
    addOperand(graph(), op, onTrue);
    addOperand(graph(), op, onFalse);
    ValueId result =
        createValue(graph_, makeShadowValueName(entry, label), width, entry.isSigned);
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), op, result);
    return result;
}

ValueId AlwaysConverter::buildCaseMatch(const slang::ast::CaseStatement::ItemGroup& item,
                                                ValueId controlValue,
                                                slang::ast::CaseStatementCondition condition) {
    std::vector<ValueId> terms;
    terms.reserve(item.expressions.size());

    for (const slang::ast::Expression* expr : item.expressions) {
        if (!expr) {
            continue;
        }
        ValueId rhsVal = rhsConverter_->convert(*expr);
        if (!rhsVal) {
            return ValueId::invalid();
        }
        ValueId term = ValueId::invalid();
        if (condition == slang::ast::CaseStatementCondition::WildcardXOrZ ||
            condition == slang::ast::CaseStatementCondition::WildcardJustZ) {
            term = buildWildcardEquality(controlValue, rhsVal, *expr, condition);
        }
        if (!term) {
            term = buildEquality(controlValue, rhsVal, "case_eq");
        }
        if (!term) {
            return ValueId::invalid();
        }
        terms.push_back(term);
    }

    if (terms.empty()) {
        reportLatchIssue("comb always case item lacks expressions");
        return ValueId::invalid();
    }

    ValueId match = terms.front();
    for (std::size_t idx = 1; idx < terms.size(); ++idx) {
        ValueId combined = buildLogicOr(match, terms[idx]);
        if (!combined) {
            return ValueId::invalid();
        }
        match = combined;
    }
    return match;
}

ValueId AlwaysConverter::buildEquality(ValueId lhs, ValueId rhs,
                                               std::string_view hint) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    OperationId op =
        createOperation(graph_, grh::ir::OperationKind::kEq, makeControlOpName(hint));
    applyDebug(graph(), op, debugInfo);
    addOperand(graph(), op, lhs);
    addOperand(graph(), op, rhs);
    ValueId result = createValue(graph_, makeControlValueName(hint), 1, /*isSigned=*/false);
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), op, result);
    return result;
}

ValueId AlwaysConverter::buildLogicOr(ValueId lhs, ValueId rhs) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    OperationId op =
        createOperation(graph_, grh::ir::OperationKind::kOr, makeControlOpName("case_or"));
    applyDebug(graph(), op, debugInfo);
    addOperand(graph(), op, lhs);
    addOperand(graph(), op, rhs);
    ValueId result = createValue(graph_, makeControlValueName("case_or"), 1,
                                            /*isSigned=*/false);
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), op, result);
    return result;
}

ValueId AlwaysConverter::buildLogicAnd(ValueId lhs, ValueId rhs) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    OperationId op =
        createOperation(graph_, grh::ir::OperationKind::kAnd, makeControlOpName("and"));
    applyDebug(graph(), op, debugInfo);
    addOperand(graph(), op, lhs);
    addOperand(graph(), op, rhs);
    ValueId result =
        createValue(graph_, makeControlValueName("and"), 1, /*isSigned=*/false);
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), op, result);
    return result;
}

ValueId AlwaysConverter::currentGuardValue() const {
    return guardStack_.empty() ? ValueId::invalid() : guardStack_.back();
}

void AlwaysConverter::pushGuard(ValueId guard) {
    if (!guard) {
        return;
    }
    if (guardStack_.empty()) {
        guardStack_.push_back(guard);
        return;
    }
    ValueId prev = guardStack_.back();
    ValueId combined = buildLogicAnd(prev, guard);
    guardStack_.push_back(combined ? combined : guard);
}

void AlwaysConverter::popGuard() {
    if (!guardStack_.empty()) {
        guardStack_.pop_back();
    }
}

ValueId AlwaysConverter::buildLogicNot(ValueId v) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    OperationId op =
        createOperation(graph_, grh::ir::OperationKind::kLogicNot, makeControlOpName("not"));
    applyDebug(graph(), op, debugInfo);
    addOperand(graph(), op, v);
    ValueId result =
        createValue(graph_, makeControlValueName("not"), 1, /*isSigned=*/false);
    applyDebug(graph(), result, debugInfo);
    addResult(graph(), op, result);
    return result;
}

ValueId AlwaysConverter::coerceToCondition(ValueId v) {
    if (graph().getValue(v).width() == 1) {
        return v;
    }
    ValueId zero = createZeroValue(graph().getValue(v).width());
    if (!zero) {
        return ValueId::invalid();
    }
    ValueId eqZero = buildEquality(v, zero, "eq0");
    if (!eqZero) {
        return ValueId::invalid();
    }
    return buildLogicNot(eqZero);
}

ValueId AlwaysConverter::buildWildcardEquality(
    ValueId controlValue, ValueId rhsValue, const slang::ast::Expression& rhsExpr,
    slang::ast::CaseStatementCondition condition) {
    const int64_t width = graph().getValue(controlValue).width();
    if (width <= 0) {
        return ValueId::invalid();
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, &rhsExpr);
    std::optional<slang::SVInt> literalOpt = evaluateConstantInt(rhsExpr, /*allowUnknown=*/true);
    if (!literalOpt) {
        return ValueId::invalid();
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
        return ValueId::invalid();
    }

    slang::SVInt maskValue = slang::SVInt::fromString(maskLiteral);
    OperationId xorOp =
        createOperation(graph_, grh::ir::OperationKind::kXor, makeControlOpName("case_wild_xor"));
    applyDebug(graph(), xorOp, debugInfo);
    addOperand(graph(), xorOp, controlValue);
    addOperand(graph(), xorOp, rhsValue);
    ValueId xorResult =
        createValue(graph_, makeControlValueName("case_wild_xor"), width, /*isSigned=*/false);
    applyDebug(graph(), xorResult, debugInfo);
    addResult(graph(), xorOp, xorResult);

    ValueId maskConst = createLiteralValue(maskValue, /*isSigned=*/false, "case_wild_mask");
    if (!maskConst) {
        return ValueId::invalid();
    }

    OperationId andOp =
        createOperation(graph_, grh::ir::OperationKind::kAnd, makeControlOpName("case_wild_and"));
    applyDebug(graph(), andOp, debugInfo);
    addOperand(graph(), andOp, xorResult);
    addOperand(graph(), andOp, maskConst);
    ValueId masked =
        createValue(graph_, makeControlValueName("case_wild_and"), width, /*isSigned=*/false);
    applyDebug(graph(), masked, debugInfo);
    addResult(graph(), andOp, masked);

    ValueId zero = createZeroValue(width);
    if (!zero) {
        return ValueId::invalid();
    }
    return buildEquality(masked, zero, "case_wild_eq0");
}

ValueId AlwaysConverter::createLiteralValue(const slang::SVInt& literal, bool isSigned,
                                                    std::string_view hint) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    OperationId op =
        createOperation(graph_, grh::ir::OperationKind::kConstant, makeControlOpName(hint));
    applyDebug(graph(), op, debugInfo);
    ValueId value = createValue(graph_, makeControlValueName(hint),
                                           static_cast<int64_t>(literal.getBitWidth()),
                                           isSigned);
    applyDebug(graph(), value, debugInfo);
    addResult(graph(), op, value);
    setAttr(graph(), op, "constValue",
                    literal.toString(slang::LiteralBase::Hex, true, literal.getBitWidth()));
    return value;
}

std::string AlwaysConverter::makeControlOpName(std::string_view suffix) {
    std::string base = "_comb_ctrl_op_";
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(controlInstanceId_));
    base.push_back('_');
    base.append(std::to_string(controlNameCounter_++));
    return base;
}

std::string AlwaysConverter::makeControlValueName(std::string_view suffix) {
    std::string base = "_comb_ctrl_val_";
    base.append(suffix);
    base.push_back('_');
    base.append(std::to_string(controlInstanceId_));
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
    diagnostics_->warn(block_, std::move(message));
}

bool AlwaysConverter::isCombinationalFullCase(const slang::ast::CaseStatement& stmt) {
    using slang::ast::CaseStatementCondition;
    if (isSequential() || stmt.defaultCase) {
        return false;
    }
    if (stmt.condition != CaseStatementCondition::Normal) {
        return false;
    }

    const slang::ast::Type* type = stmt.expr.unwrapImplicitConversions().type;
    if (!type || !type->isIntegral()) {
        return false;
    }
    const int64_t bitWidth = type->getBitWidth();
    if (bitWidth <= 0 || bitWidth >= 32) {
        return false;
    }

    const uint64_t required = 1ull << bitWidth;
    std::unordered_set<uint64_t> seen;
    seen.reserve(stmt.items.size());

    for (const auto& item : stmt.items) {
        for (const slang::ast::Expression* expr : item.expressions) {
            if (!expr) {
                continue;
            }
            std::optional<slang::SVInt> constant =
                evaluateConstantInt(*expr, /*allowUnknown=*/false);
            if (!constant) {
                return false;
            }
            slang::SVInt normalized =
                constant->trunc(static_cast<slang::bitwidth_t>(bitWidth));
            normalized.setSigned(false);
            std::optional<uint64_t> valueOpt = normalized.as<uint64_t>();
            if (!valueOpt) {
                return false;
            }
            seen.insert(*valueOpt);
            if (seen.size() >= required) {
                return true;
            }
        }
    }

    return seen.size() >= required;
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
        if (const auto* assign = step->as_if<slang::ast::AssignmentExpression>()) {
            if (const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(assign->left())) {
                slang::ConstantValue value = assign->right().eval(ctx);
                if (!value || !value.isInteger() || value.hasUnknown()) {
                    if (diagnostics_) {
                        std::string message = std::string(modeLabel()) +
                                              " for-loop step expression must produce an integer";
                        diagnostics_->nyi(*symbol, std::move(message));
                    }
                    return false;
                }

                const slang::ast::Type& type = symbol->getType();
                const int64_t rawWidth = static_cast<int64_t>(type.getBitstreamWidth());
                const slang::bitwidth_t width =
                    static_cast<slang::bitwidth_t>(rawWidth > 0 ? rawWidth : 1);
                slang::SVInt nextValue = value.integer().resize(width);
                nextValue.setSigned(type.isSigned());
                slang::ConstantValue nextConst(nextValue);
                if (slang::ConstantValue* storage = ctx.findLocal(symbol)) {
                    *storage = std::move(nextConst);
                }
                else if (!ctx.createLocal(symbol, std::move(nextConst))) {
                    return false;
                }
                continue;
            }
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
    ValueId literal = createLiteralValue(resized, type.isSigned(), hint);
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
        if (rhsConverter_) {
            rhsConverter_->clearCache();
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

ValueId AlwaysConverter::lookupLoopValue(const slang::ast::ValueSymbol& symbol) const {
    auto it = loopValueMap_.find(&symbol);
    if (it == loopValueMap_.end()) {
        return ValueId::invalid();
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

void WriteBackMemo::recordMultiDriverPart(const SignalMemoEntry& target, MultiDriverPart part) {
    if (!target.value) {
        return;
    }
    MultiDriverBucket& bucket = multiDriverParts_[target.value];
    if (!bucket.target) {
        bucket.target = &target;
    }
    bucket.parts.push_back(std::move(part));
}

void WriteBackMemo::clear() {
    entries_.clear();
    multiDriverParts_.clear();
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

std::optional<grh::ir::SrcLoc> WriteBackMemo::srcLocForEntry(const Entry& entry) const {
    for (const auto& slice : entry.slices) {
        if (slice.originExpr) {
            if (auto info = makeDebugInfo(sourceManager_, slice.originExpr)) {
                return info;
            }
        }
    }
    if (entry.originSymbol) {
        if (auto info = makeDebugInfo(sourceManager_, entry.originSymbol)) {
            return info;
        }
    }
    if (entry.target && entry.target->symbol) {
        return makeDebugInfo(sourceManager_, entry.target->symbol);
    }
    return std::nullopt;
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

ValueId WriteBackMemo::composeSlices(Entry& entry, grh::ir::Graph& graph,
                                         ElaborateDiagnostics* diagnostics) {
    if (!entry.target) {
        reportIssue(entry, "Write-back target is missing memo metadata", diagnostics);
        return ValueId::invalid();
    }
    if (entry.slices.empty()) {
        reportIssue(entry, "Write-back entry has no slices to compose", diagnostics);
        return ValueId::invalid();
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
    const auto debugInfo = srcLocForEntry(entry);
    std::vector<ValueId> components;
    components.reserve(entry.slices.size() + 2);

    for (const Slice& slice : entry.slices) {
        if (!slice.value) {
            reportIssue(entry, "Write-back slice is missing RHS value", diagnostics);
            return ValueId::invalid();
        }
        if (slice.msb < slice.lsb) {
            reportIssue(entry, "Write-back slice has invalid bit range", diagnostics);
            return ValueId::invalid();
        }

        if (slice.msb > expectedMsb) {
            std::ostringstream oss;
            oss << "Write-back slice exceeds target width; slice msb=" << slice.msb
                << " expected at most " << expectedMsb;
            reportIssue(entry, oss.str(), diagnostics);
            return ValueId::invalid();
        }

        const int64_t gapWidth = expectedMsb - slice.msb;
        if (gapWidth > 0) {
            ValueId zero = createZeroValue(entry, gapWidth, graph);
            if (!zero) {
                reportIssue(entry, "Failed to create zero-fill value for write-back gap",
                            diagnostics);
                return ValueId::invalid();
            }
            components.push_back(zero);
            expectedMsb -= gapWidth;
        }

        if (slice.msb != expectedMsb) {
            std::ostringstream oss;
            oss << "Write-back bookkeeping error; slice msb=" << slice.msb
                << " but expected " << expectedMsb;
            reportIssue(entry, oss.str(), diagnostics);
            return ValueId::invalid();
        }

        const int64_t sliceWidth = slice.msb - slice.lsb + 1;
        if (graph.getValue(slice.value).width() != sliceWidth) {
            std::ostringstream oss;
            oss << "Write-back slice width mismatch; slice covers " << sliceWidth
                << " bits but RHS value width is " << graph.getValue(slice.value).width();
            reportIssue(entry, oss.str(), diagnostics);
            return ValueId::invalid();
        }

        components.push_back(slice.value);
        expectedMsb = slice.lsb - 1;
    }

    if (expectedMsb >= 0) {
        ValueId zero = createZeroValue(entry, expectedMsb + 1, graph);
        if (!zero) {
            reportIssue(entry, "Failed to create zero-fill value for trailing gap", diagnostics);
            return ValueId::invalid();
        }
        components.push_back(zero);
        expectedMsb = -1;
    }

    if (components.empty()) {
        reportIssue(entry, "Write-back entry produced no value components", diagnostics);
        return ValueId::invalid();
    }

    if (components.size() == 1) {
        return components.front();
    }

    OperationId concat =
        createOperation(graph, grh::ir::OperationKind::kConcat, makeOperationName(entry, "concat"));
    applyDebug(graph, concat, debugInfo);
    for (ValueId component : components) {
        addOperand(graph, concat, component);
    }

    ValueId composed =
        createValue(graph, makeValueName(entry, "concat"), targetWidth, entry.target->isSigned);
    applyDebug(graph, composed, debugInfo);
    addResult(graph, concat, composed);
    return composed;
}

void WriteBackMemo::attachToTarget(const Entry& entry, ValueId composedValue,
                                   grh::ir::Graph& graph, ElaborateDiagnostics* diagnostics) {
    if (!entry.target) {
        reportIssue(entry, "Missing target when attaching write-back value", diagnostics);
        return;
    }

    if (!entry.target->stateOp) {
        ValueId targetValue = entry.target->value;
        if (!targetValue) {
            reportIssue(entry, "Net write-back lacks GRH value handle", diagnostics);
            return;
        }
        if (graph.getValue(targetValue).width() != graph.getValue(composedValue).width()) {
            std::ostringstream oss;
            oss << "Net write-back width mismatch; target width=" << graph.getValue(targetValue).width()
                << " source width=" << graph.getValue(composedValue).width();
            reportIssue(entry, oss.str(), diagnostics);
            return;
        }
        OperationId assign =
            createOperation(graph, grh::ir::OperationKind::kAssign, makeOperationName(entry, "assign"));
        applyDebug(graph, assign, srcLocForEntry(entry));
        addOperand(graph, assign, composedValue);
        addResult(graph, assign, targetValue);
        return;
    }

    OperationId stateOp = entry.target->stateOp;
    if (!stateOp) {
        reportIssue(entry, "Sequential write-back missing state operation", diagnostics);
        return;
    }

    if (graph.getOperation(stateOp).kind() == grh::ir::OperationKind::kMemory) {
        reportIssue(entry, "Memory write-back is not implemented yet", diagnostics);
        return;
    }

    if (!graph.getOperation(stateOp).operands().empty()) {
        reportIssue(entry, "State operation already has a data operand", diagnostics);
        return;
    }

    if (!graph.getOperation(stateOp).results().empty()) {
        ValueId stateValue = graph.getOperation(stateOp).results().front();
        if (stateValue && graph.getValue(stateValue).width() != graph.getValue(composedValue).width()) {
            std::ostringstream oss;
            oss << "Register write-back width mismatch; state width=" << graph.getValue(stateValue).width()
                << " source width=" << graph.getValue(composedValue).width();
            reportIssue(entry, oss.str(), diagnostics);
            return;
        }
    }

    addOperand(graph, stateOp, composedValue);
}

ValueId WriteBackMemo::createZeroValue(const Entry& entry, int64_t width, grh::ir::Graph& graph) {
    if (width <= 0) {
        return ValueId::invalid();
    }

    const auto info = srcLocForEntry(entry);
    OperationId op =
        createOperation(graph, grh::ir::OperationKind::kConstant, makeOperationName(entry, "zero"));
    applyDebug(graph, op, info);
    ValueId value = createValue(graph, makeValueName(entry, "zero"), width, /*isSigned=*/false);
    applyDebug(graph, value, info);
    addResult(graph, op, value);
    std::ostringstream oss;
    oss << width << "'h0";
    setAttr(graph, op, "constValue", oss.str());
    return value;
}

bool WriteBackMemo::tryLowerLatch(Entry& entry, ValueId dataValue, grh::ir::Graph& graph,
                                  ElaborateDiagnostics* diagnostics) {
    const auto* block = entry.originSymbol ? entry.originSymbol->as_if<slang::ast::ProceduralBlockSymbol>()
                                           : nullptr;
    if (!block) {
        return false;
    }
    if (entry.kind != AssignmentKind::Procedural) {
        return false;
    }
    using slang::ast::ProceduralBlockKind;
    if (block->procedureKind != ProceduralBlockKind::AlwaysComb &&
        block->procedureKind != ProceduralBlockKind::Always &&
        block->procedureKind != ProceduralBlockKind::AlwaysLatch) {
        return false;
    }
    if (!entry.target || !entry.target->value) {
        reportIssue(entry, "Latch lowering missing target value", diagnostics);
        return false;
    }
    if (entry.target->stateOp) {
        return false;
    }

    ValueId q = entry.target->value;
    const int64_t targetWidth = graph.getValue(q).width();
    auto ensureOneBit = [&](ValueId cond, std::string_view label) -> ValueId {
        if (!cond) {
            return ValueId::invalid();
        }
        if (graph.getValue(cond).width() != 1) {
            std::ostringstream oss;
            oss << "Latch " << label << " must be 1 bit (got " << graph.getValue(cond).width() << ")";
            reportIssue(entry, oss.str(), diagnostics);
            return ValueId::invalid();
        }
        return cond;
    };

    struct LatchInfo {
        ValueId enable = ValueId::invalid();
        bool enableActiveLow = false;
        ValueId data = ValueId::invalid();
        ValueId resetSignal = ValueId::invalid();
        bool resetActiveHigh = true;
        ValueId resetValue = ValueId::invalid();
        std::vector<ValueId> muxValues;
    };

    auto parseEnableMux = [&](ValueId candidate) -> std::optional<LatchInfo> {
        OperationId op = graph.getValue(candidate).definingOp();
        if (!op || graph.getOperation(op).kind() != grh::ir::OperationKind::kMux || graph.getOperation(op).operands().size() != 3) {
            return std::nullopt;
        }
        ValueId cond = ensureOneBit(graph.getOperation(op).operands()[0], "enable condition");
        if (!cond) {
            return std::nullopt;
        }
        ValueId tVal = graph.getOperation(op).operands()[1];
        ValueId fVal = graph.getOperation(op).operands()[2];

        if (tVal && fVal && tVal == q && graph.getValue(fVal).width() == targetWidth) {
            return LatchInfo{.enable = cond,
                             .enableActiveLow = true,
                             .data = fVal,
                             .muxValues = {candidate}};
        }
        if (fVal && tVal && fVal == q && graph.getValue(tVal).width() == targetWidth) {
            return LatchInfo{.enable = cond,
                             .enableActiveLow = false,
                             .data = tVal,
                             .muxValues = {candidate}};
        }
        return std::nullopt;
    };

    auto makeLogicNot = [&](ValueId input, std::string_view label) -> ValueId {
        const auto info = srcLocForEntry(entry);
        OperationId op = createOperation(graph, grh::ir::OperationKind::kLogicNot,
                                                   makeOperationName(entry, label));
        applyDebug(graph, op, info);
        addOperand(graph, op, input);
        ValueId result =
            createValue(graph, makeValueName(entry, label), 1, /*isSigned=*/false);
        applyDebug(graph, result, info);
        addResult(graph, op, result);
        return result;
    };

    auto makeLogicOr = [&](ValueId lhs, ValueId rhs, std::string_view label)
                           -> ValueId {
        const auto info = srcLocForEntry(entry);
        OperationId op = createOperation(graph, grh::ir::OperationKind::kLogicOr,
                                                   makeOperationName(entry, label));
        applyDebug(graph, op, info);
        addOperand(graph, op, lhs);
        addOperand(graph, op, rhs);
        ValueId result =
            createValue(graph, makeValueName(entry, label), 1, /*isSigned=*/false);
        applyDebug(graph, result, info);
        addResult(graph, op, result);
        return result;
    };

    auto parseResetEnableMux = [&](ValueId candidate) -> std::optional<LatchInfo> {
        OperationId op = graph.getValue(candidate).definingOp();
        if (!op || graph.getOperation(op).kind() != grh::ir::OperationKind::kMux || graph.getOperation(op).operands().size() != 3) {
            return std::nullopt;
        }
        ValueId cond = ensureOneBit(graph.getOperation(op).operands()[0], "reset condition");
        if (!cond) {
            return std::nullopt;
        }
        ValueId tVal = graph.getOperation(op).operands()[1];
        ValueId fVal = graph.getOperation(op).operands()[2];

        auto tryBranch = [&](ValueId resetBranch, ValueId dataBranch,
                             bool resetActiveHigh) -> std::optional<LatchInfo> {
            if (!resetBranch || graph.getValue(resetBranch).width() != targetWidth) {
                return std::nullopt;
            }
            if (!dataBranch) {
                return std::nullopt;
            }
            if (auto info = parseEnableMux(dataBranch)) {
                info->resetSignal = cond;
                info->resetActiveHigh = resetActiveHigh;
                info->resetValue = resetBranch;
                info->muxValues.push_back(candidate);
                return info;
            }
            return std::nullopt;
        };

        if (auto info = tryBranch(tVal, fVal, /*resetActiveHigh=*/true)) {
            return info;
        }
        if (auto info = tryBranch(fVal, tVal, /*resetActiveHigh=*/false)) {
            return info;
        }
        return std::nullopt;
    };

    auto parseMuxChainLatch = [&](ValueId candidate) -> std::optional<LatchInfo> {
        if (!entry.target || !entry.target->value) {
            return std::nullopt;
        }
        struct ChainStep {
            ValueId muxValue = ValueId::invalid();
            ValueId cond = ValueId::invalid();
            ValueId dataBranch = ValueId::invalid();
            bool holdOnTrue = false;
        };

        ValueId q = entry.target->value;
        auto reachesHold = [&](ValueId value, auto&& self,
                               std::unordered_set<ValueId, grh::ir::ValueIdHash>& visited) -> bool {
            if (!value) {
                return false;
            }
            if (!visited.insert(value).second) {
                return false;
            }
            if (value == q) {
                return true;
            }
            OperationId op = graph.getValue(value).definingOp();
            if (!op || graph.getOperation(op).kind() != grh::ir::OperationKind::kMux || graph.getOperation(op).operands().size() != 3) {
                return false;
            }
            return self(graph.getOperation(op).operands()[1], self, visited) ||
                   self(graph.getOperation(op).operands()[2], self, visited);
        };

        std::vector<ChainStep> chain;
        std::unordered_set<ValueId, grh::ir::ValueIdHash> visited;
        ValueId cursor = candidate;
        while (true) {
            if (cursor == q) {
                break;
            }
            if (!visited.insert(cursor).second) {
                return std::nullopt;
            }
            OperationId op = graph.getValue(cursor).definingOp();
            if (!op || graph.getOperation(op).kind() != grh::ir::OperationKind::kMux || graph.getOperation(op).operands().size() != 3) {
                return std::nullopt;
            }
            ValueId cond = ensureOneBit(graph.getOperation(op).operands()[0], "mux condition");
            if (!cond) {
                return std::nullopt;
            }
            ValueId tVal = graph.getOperation(op).operands()[1];
            ValueId fVal = graph.getOperation(op).operands()[2];
            std::unordered_set<ValueId, grh::ir::ValueIdHash> seenTrue;
            const bool trueHolds = reachesHold(tVal, reachesHold, seenTrue);
            std::unordered_set<ValueId, grh::ir::ValueIdHash> seenFalse;
            const bool falseHolds = reachesHold(fVal, reachesHold, seenFalse);
            if (trueHolds == falseHolds) {
                return std::nullopt;
            }
            ChainStep step;
            step.muxValue = cursor;
            step.cond = cond;
            step.dataBranch = trueHolds ? fVal : tVal;
            step.holdOnTrue = trueHolds;
            chain.push_back(step);
            cursor = trueHolds ? tVal : fVal;
        }

        if (chain.empty()) {
            return std::nullopt;
        }

        ValueId enable = ValueId::invalid();
        for (const ChainStep& step : chain) {
            ValueId clause =
                step.holdOnTrue ? makeLogicNot(step.cond, "latch_hold_not") : step.cond;
            if (!clause) {
                return std::nullopt;
            }
            if (!enable) {
                enable = clause;
            }
            else {
                ValueId combined = makeLogicOr(enable, clause, "latch_hold_or");
                if (!combined) {
                    return std::nullopt;
                }
                enable = combined;
            }
        }

        ValueId replacement = createZeroValue(entry, targetWidth, graph);
        if (!replacement) {
            reportIssue(entry, "Latch reconstruction failed to create hold replacement", diagnostics);
            return std::nullopt;
        }
        ValueId dataExpr = replacement;
        const auto info = srcLocForEntry(entry);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (!it->dataBranch || graph.getValue(it->dataBranch).width() != targetWidth) {
                reportIssue(entry, "Latch mux data width mismatch", diagnostics);
                return std::nullopt;
            }
            ValueId trueVal = it->holdOnTrue ? dataExpr : it->dataBranch;
            ValueId falseVal = it->holdOnTrue ? it->dataBranch : dataExpr;
            OperationId mux =
                createOperation(graph, grh::ir::OperationKind::kMux, makeOperationName(entry, "latch_mux"));
            applyDebug(graph, mux, info);
            addOperand(graph, mux, it->cond);
            addOperand(graph, mux, trueVal);
            addOperand(graph, mux, falseVal);
            ValueId muxResult =
                createValue(graph, makeValueName(entry, "latch_mux"), targetWidth, entry.target->isSigned);
            applyDebug(graph, muxResult, info);
            addResult(graph, mux, muxResult);
            dataExpr = muxResult;
        }

        LatchInfo infoStruct;
        infoStruct.enable = enable;
        infoStruct.enableActiveLow = false;
        infoStruct.data = dataExpr;
        for (const ChainStep& step : chain) {
            infoStruct.muxValues.push_back(step.muxValue);
        }
        return infoStruct;
    };

    std::optional<LatchInfo> latch = parseResetEnableMux(dataValue);
    if (!latch) {
        latch = parseEnableMux(dataValue);
    }
    if (!latch) {
        latch = parseMuxChainLatch(dataValue);
    }
    if (!latch) {
        return false;
    }

    const auto debugInfo = srcLocForEntry(entry);
    grh::ir::OperationKind opKind =
        latch->resetSignal ? grh::ir::OperationKind::kLatchArst : grh::ir::OperationKind::kLatch;
    OperationId op = createOperation(graph, opKind, makeOperationName(entry, "latch"));
    applyDebug(graph, op, debugInfo);
    addOperand(graph, op, latch->enable);
    if (latch->resetSignal && latch->resetValue) {
        addOperand(graph, op, latch->resetSignal);
        addOperand(graph, op, latch->resetValue);
    }
    if (latch->data) {
        addOperand(graph, op, latch->data);
    }
    addResult(graph, op, q);
    setAttr(graph, op, "enLevel", latch->enableActiveLow ? std::string("low")
                                                      : std::string("high"));
    if (latch->resetSignal) {
        setAttr(graph, op, "rstPolarity",
                        latch->resetActiveHigh ? std::string("high") : std::string("low"));
    }

    auto pruneMuxValue = [&](ValueId value) {
        if (!value) {
            return;
        }
        if (!graph.getValue(value).users().empty()) {
            return;
        }
        OperationId op = graph.getValue(value).definingOp();
        if (!op || graph.getOperation(op).kind() != grh::ir::OperationKind::kMux) {
            return;
        }
        if (graph.getOperation(op).results().size() != 1 || graph.getOperation(op).results().front() != value) {
            return;
        }
        graph.eraseOp(op);
    };
    for (ValueId muxVal : latch->muxValues) {
        pruneMuxValue(muxVal);
    }

    if (diagnostics) {
        std::string msg = "Latch inferred for procedural block";
        if (entry.target && entry.target->symbol && !entry.target->symbol->name.empty()) {
            msg.append(" (signal=");
            msg.append(entry.target->symbol->name);
            msg.push_back(')');
        }
        diagnostics->warn(*block, std::move(msg));
    }
    entry.consumed = true;
    return true;
}

void WriteBackMemo::finalize(grh::ir::Graph& graph, ElaborateDiagnostics* diagnostics) {
    for (auto& [valueHandle, bucket] : multiDriverParts_) {
        const SignalMemoEntry* target = bucket.target;
        if (!target || !target->value || bucket.parts.empty()) {
            continue;
        }
        if (graph.getValue(valueHandle).definingOp()) {
            continue;
        }
        auto& parts = bucket.parts;
        std::sort(parts.begin(), parts.end(), [](const MultiDriverPart& lhs,
                                                 const MultiDriverPart& rhs) {
            return lhs.msb > rhs.msb;
        });
        const int64_t targetWidth = target->width > 0 ? target->width : 1;
        int64_t expectedMsb = targetWidth - 1;
        std::vector<ValueId> components;
        components.reserve(parts.size() + 2);
        Entry temp;
        temp.target = target;
        auto appendPad = [&](int64_t msb, int64_t lsb) -> bool {
            if (msb < lsb) {
                return true;
            }
            ValueId zero = createZeroValue(temp, msb - lsb + 1, graph);
            if (!zero) {
                return false;
            }
            components.push_back(zero);
            return true;
        };
        for (const MultiDriverPart& part : parts) {
            const int64_t gapWidth = expectedMsb - part.msb;
            if (gapWidth > 0) {
                if (!appendPad(expectedMsb, part.msb + 1)) {
                    break;
                }
                expectedMsb -= gapWidth;
            }
            components.push_back(part.value);
            expectedMsb = part.lsb - 1;
        }
        if (expectedMsb >= 0) {
            appendPad(expectedMsb, 0);
        }
        if (components.empty()) {
            continue;
        }
        if (components.size() == 1) {
            OperationId assign =
                createOperation(graph, grh::ir::OperationKind::kAssign,
                                      makeOperationName(temp, "split_assign"));
            applyDebug(graph, assign, makeDebugInfo(sourceManager_, target->symbol));
            addOperand(graph, assign, components.front());
            addResult(graph, assign, target->value);
            continue;
        }
        OperationId concat =
            createOperation(graph, grh::ir::OperationKind::kConcat,
                                  makeOperationName(temp, "split_concat"));
        applyDebug(graph, concat, makeDebugInfo(sourceManager_, target->symbol));
        for (ValueId component : components) {
            addOperand(graph, concat, component);
        }
        addResult(graph, concat, target->value);
    }

    for (Entry& entry : entries_) {
        if (entry.consumed) {
            continue;
        }
        ValueId composed = composeSlices(entry, graph, diagnostics);
        if (!composed) {
            continue;
        }
        if (tryLowerLatch(entry, composed, graph, diagnostics)) {
            continue;
        }
        attachToTarget(entry, composed, graph, diagnostics);
    }
    entries_.clear();
    multiDriverParts_.clear();
}

//===---------------------------------------------------------------------===//
// RHSConverter implementation
//===---------------------------------------------------------------------===//

RHSConverter::RHSConverter(Context context) : graph_(context.graph), origin_(context.origin),
                                              diagnostics_(context.diagnostics),
                                              sourceManager_(context.sourceManager),
                                              netMemo_(context.netMemo), regMemo_(context.regMemo),
                                              memMemo_(context.memMemo),
                                              preferredBlock_(context.preferredBlock) {
    instanceId_ = nextConverterInstanceId();
}

ValueId RHSConverter::convert(const slang::ast::Expression& expr) {
    if (!graph_) {
        return ValueId::invalid();
    }

    if (auto it = cache_.find(&expr); it != cache_.end()) {
        return it->second;
    }

    const slang::ast::Expression* previous = currentExpr_;
    currentExpr_ = &expr;
    ValueId value = convertExpression(expr);
    currentExpr_ = previous;
    if (value && !suppressCache_) {
        cache_[&expr] = value;
    }
    suppressCache_ = false;
    return value;
}

void RHSConverter::clearCache() {
    cache_.clear();
    suppressCache_ = false;
}

std::string RHSConverter::makeValueName(std::string_view hint, std::size_t index) const {
    std::string base = hint.empty() ? std::string("value")
                                    : sanitizeForGraphName(hint, /* allowLeadingDigit */ false);
    if (base.empty()) {
        base = "value";
    }
    std::string name = "_rhs_val_";
    name.append(base);
    name.push_back('_');
    name.append(std::to_string(instanceId_));
    name.push_back('_');
    name.append(std::to_string(index));
    return name;
}

std::string RHSConverter::makeOperationName(std::string_view hint, std::size_t index) const {
    std::string base = hint.empty() ? std::string("op")
                                    : sanitizeForGraphName(hint, /* allowLeadingDigit */ false);
    if (base.empty()) {
        base = "op";
    }
    std::string name = "_rhs_op_";
    name.append(base);
    name.push_back('_');
    name.append(std::to_string(instanceId_));
    name.push_back('_');
    name.append(std::to_string(index));
    return name;
}

ValueId RHSConverter::convertExpression(const slang::ast::Expression& expr) {
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
    case ExpressionKind::Call:
        return convertCall(expr.as<slang::ast::CallExpression>());
    default:
        reportUnsupported("expression kind", expr);
        return ValueId::invalid();
    }
}

ValueId RHSConverter::convertNamedValue(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        const auto& named = expr.as<slang::ast::NamedValueExpression>();

        if (const SignalMemoEntry* entry = findMemoEntry(named.symbol)) {
            if (ValueId memoHandler = handleMemoEntry(named, *entry)) {
                return memoHandler;
            }
            if (ValueId value = resolveMemoValue(*entry)) {
                return value;
            }
        }

        if (ValueId custom = handleCustomNamedValue(named)) {
            suppressCache_ = true;
            return custom;
        }

        if (ValueId fallback = resolveGraphValue(named.symbol)) {
            return fallback;
        }

        if (ValueId paramValue = materializeParameterValue(named)) {
            return paramValue;
        }
    }

    reportUnsupported("named value", expr);
    return ValueId::invalid();
}

ValueId RHSConverter::materializeParameterValue(const slang::ast::NamedValueExpression& expr) {
    const auto* param = expr.symbol.as_if<slang::ast::ParameterSymbol>();
    if (!param) {
        return ValueId::invalid();
    }

    const slang::ConstantValue& constValue = param->getValue(expr.sourceRange);
    if (!constValue || !constValue.isInteger()) {
        if (diagnostics_ && origin_) {
            std::string message = "Parameter ";
            message.append(std::string(param->name));
            message.append(" has unsupported value type for RHS conversion");
            diagnostics_->nyi(*origin_, std::move(message));
        }
        return ValueId::invalid();
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
        return ValueId::invalid();
    }

    const slang::SVInt& literal = constValue.integer();
    std::string_view hint = param->name.empty() ? "param" : param->name;
    return createConstantValue(literal, *type, hint);
}

ValueId RHSConverter::convertLiteral(const slang::ast::Expression& expr) {
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
        return ValueId::invalid();
    }
}

ValueId RHSConverter::convertUnary(const slang::ast::UnaryExpression& expr) {
    ValueId operand = convert(expr.operand());
    if (!operand) {
        return ValueId::invalid();
    }

    using slang::ast::UnaryOperator;
    switch (expr.op) {
    case UnaryOperator::Plus:
        return operand;
    case UnaryOperator::Minus: {
        ValueId zero = createZeroValue(*expr.type, "neg_zero");
        if (!zero) {
            return ValueId::invalid();
        }
        return buildBinaryOp(grh::ir::OperationKind::kSub, zero, operand, expr, "neg");
    }
    case UnaryOperator::BitwiseNot:
        return buildUnaryOp(grh::ir::OperationKind::kNot, operand, expr, "not");
    case UnaryOperator::LogicalNot: {
        ValueId logicOperand = reduceToLogicValue(operand, expr);
        return logicOperand ? buildUnaryOp(grh::ir::OperationKind::kLogicNot, logicOperand, expr,
                                           "lnot")
                            : ValueId::invalid();
    }
    case UnaryOperator::BitwiseAnd:
        return buildUnaryOp(grh::ir::OperationKind::kReduceAnd, operand, expr, "red_and");
    case UnaryOperator::BitwiseOr:
        return buildUnaryOp(grh::ir::OperationKind::kReduceOr, operand, expr, "red_or");
    case UnaryOperator::BitwiseXor:
        return buildUnaryOp(grh::ir::OperationKind::kReduceXor, operand, expr, "red_xor");
    case UnaryOperator::BitwiseNand:
        return buildUnaryOp(grh::ir::OperationKind::kReduceNand, operand, expr, "red_nand");
    case UnaryOperator::BitwiseNor:
        return buildUnaryOp(grh::ir::OperationKind::kReduceNor, operand, expr, "red_nor");
    case UnaryOperator::BitwiseXnor:
        return buildUnaryOp(grh::ir::OperationKind::kReduceXnor, operand, expr, "red_xnor");
    default:
        reportUnsupported("unary operator", expr);
        return ValueId::invalid();
    }
}

ValueId RHSConverter::convertBinary(const slang::ast::BinaryExpression& expr) {
    ValueId lhs = convert(expr.left());
    ValueId rhs = convert(expr.right());
    if (!lhs || !rhs) {
        return ValueId::invalid();
    }

    using slang::ast::BinaryOperator;
    auto opKind = grh::ir::OperationKind::kAssign;
    bool supported = true;
    switch (expr.op) {
    case BinaryOperator::Add:
        opKind = grh::ir::OperationKind::kAdd;
        break;
    case BinaryOperator::Subtract:
        opKind = grh::ir::OperationKind::kSub;
        break;
    case BinaryOperator::Multiply:
        opKind = grh::ir::OperationKind::kMul;
        break;
    case BinaryOperator::Divide:
        opKind = grh::ir::OperationKind::kDiv;
        break;
    case BinaryOperator::Mod:
        opKind = grh::ir::OperationKind::kMod;
        break;
    case BinaryOperator::BinaryAnd:
        opKind = grh::ir::OperationKind::kAnd;
        break;
    case BinaryOperator::BinaryOr:
        opKind = grh::ir::OperationKind::kOr;
        break;
    case BinaryOperator::BinaryXor:
        opKind = grh::ir::OperationKind::kXor;
        break;
    case BinaryOperator::BinaryXnor:
        opKind = grh::ir::OperationKind::kXnor;
        break;
    case BinaryOperator::Equality:
    case BinaryOperator::CaseEquality:
        opKind = grh::ir::OperationKind::kEq;
        break;
    case BinaryOperator::Inequality:
    case BinaryOperator::CaseInequality:
        opKind = grh::ir::OperationKind::kNe;
        break;
    case BinaryOperator::GreaterThan:
        opKind = grh::ir::OperationKind::kGt;
        break;
    case BinaryOperator::GreaterThanEqual:
        opKind = grh::ir::OperationKind::kGe;
        break;
    case BinaryOperator::LessThan:
        opKind = grh::ir::OperationKind::kLt;
        break;
    case BinaryOperator::LessThanEqual:
        opKind = grh::ir::OperationKind::kLe;
        break;
    case BinaryOperator::LogicalAnd:
        opKind = grh::ir::OperationKind::kLogicAnd;
        break;
    case BinaryOperator::LogicalOr:
        opKind = grh::ir::OperationKind::kLogicOr;
        break;
    case BinaryOperator::LogicalShiftLeft:
    case BinaryOperator::ArithmeticShiftLeft:
        opKind = grh::ir::OperationKind::kShl;
        break;
    case BinaryOperator::LogicalShiftRight:
        opKind = grh::ir::OperationKind::kLShr;
        break;
    case BinaryOperator::ArithmeticShiftRight:
        opKind = grh::ir::OperationKind::kAShr;
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
        return ValueId::invalid();
    }

    if (opKind == grh::ir::OperationKind::kLogicAnd || opKind == grh::ir::OperationKind::kLogicOr) {
        lhs = reduceToLogicValue(lhs, expr);
        rhs = reduceToLogicValue(rhs, expr);
        if (!lhs || !rhs) {
            return ValueId::invalid();
        }
    }

    return buildBinaryOp(opKind, lhs, rhs, expr, "bin");
}

ValueId RHSConverter::reduceToLogicValue(ValueId input,
                                             const slang::ast::Expression& originExpr) {
    if (graph().getValue(input).width() <= 1) {
        return input;
    }
    OperationId op = createOperation(grh::ir::OperationKind::kReduceOr, "logic_truth");
    addOperand(graph(), op, input);
    ValueId reduced = createTemporaryValue(*originExpr.type, "logic_truth");
    addResult(graph(), op, reduced);
    return reduced;
}

ValueId RHSConverter::convertConditional(const slang::ast::ConditionalExpression& expr) {
    if (expr.conditions.empty()) {
        reportUnsupported("conditional (missing condition)", expr);
        return ValueId::invalid();
    }

    const auto& condition = expr.conditions.front();
    if (condition.pattern) {
        reportUnsupported("patterned conditional", expr);
        return ValueId::invalid();
    }

    ValueId condValue = convert(*condition.expr);
    ValueId trueValue = convert(expr.left());
    ValueId falseValue = convert(expr.right());
    if (!condValue || !trueValue || !falseValue) {
        return ValueId::invalid();
    }

    return buildMux(condValue, trueValue, falseValue, expr);
}

ValueId RHSConverter::convertConcatenation(
    const slang::ast::ConcatenationExpression& expr) {
    std::vector<ValueId> operands;
    for (const slang::ast::Expression* operandExpr : expr.operands()) {
        if (!operandExpr) {
            continue;
        }
        ValueId operandValue = convert(*operandExpr);
        if (!operandValue) {
            return ValueId::invalid();
        }
        operands.push_back(operandValue);
    }

    if (operands.empty()) {
        return ValueId::invalid();
    }

    if (operands.size() == 1) {
        const TypeInfo info = deriveTypeInfo(*expr.type);
        return resizeValue(operands.front(), *expr.type, info, expr, "concat");
    }

    OperationId op = createOperation(grh::ir::OperationKind::kConcat, "concat");
    for (ValueId operand : operands) {
        addOperand(graph(), op, operand);
    }
    ValueId result = createTemporaryValue(*expr.type, "concat");
    addResult(graph(), op, result);
    return result;
}

ValueId RHSConverter::convertReplication(const slang::ast::ReplicationExpression& expr) {
    std::optional<int64_t> replicateCount = evaluateConstantInt(expr.count());
    if (!replicateCount || *replicateCount <= 0) {
        reportUnsupported("replication count", expr);
        return ValueId::invalid();
    }

    ValueId operand = convert(expr.concat());
    if (!operand) {
        return ValueId::invalid();
    }

    OperationId op = createOperation(grh::ir::OperationKind::kReplicate, "replicate");
    addOperand(graph(), op, operand);
    setAttr(graph(), op, "rep", static_cast<int64_t>(*replicateCount));
    ValueId result = createTemporaryValue(*expr.type, "replicate");
    addResult(graph(), op, result);
    return result;
}

ValueId RHSConverter::convertConversion(const slang::ast::ConversionExpression& expr) {
    const TypeInfo info = deriveTypeInfo(*expr.type);
    if (std::optional<slang::SVInt> constant = evaluateConstantSvInt(expr)) {
        return createConstantValue(*constant, *expr.type, "convert");
    }

    ValueId operand = convert(expr.operand());
    if (!operand) {
        return ValueId::invalid();
    }

    return resizeValue(operand, *expr.type, info, expr, "convert");
}

ValueId RHSConverter::convertCall(const slang::ast::CallExpression& expr) {
    if (std::optional<slang::SVInt> constant = evaluateConstantSvInt(expr)) {
        std::string hint =
            sanitizeForGraphName(expr.getSubroutineName(), /* allowLeadingDigit */ false);
        if (!hint.empty() && hint.front() == '$') {
            hint.erase(hint.begin());
        }
        if (hint.empty()) {
            hint = "call";
        }
        return createConstantValue(*constant, *expr.type, hint);
    }

    if (expr.isSystemCall()) {
        std::string_view name = expr.getSubroutineName();
        if (name == "$signed" || name == "$unsigned") {
            auto args = expr.arguments();
            if (args.empty() || !args.front()) {
                reportUnsupported("call expression", expr);
                return ValueId::invalid();
            }
            ValueId operand = convert(*args.front());
            if (!operand) {
                return ValueId::invalid();
            }
            const TypeInfo info = deriveTypeInfo(*expr.type);
            return resizeValue(operand, *expr.type, info, expr,
                               name == "$signed" ? "signed" : "unsigned");
        }
    }

    reportUnsupported("call expression", expr);
    return ValueId::invalid();
}

ValueId RHSConverter::convertElementSelect(const slang::ast::ElementSelectExpression& expr) {
    reportUnsupported("array select", expr);
    return ValueId::invalid();
}

ValueId RHSConverter::convertRangeSelect(const slang::ast::RangeSelectExpression& expr) {
    reportUnsupported("range select", expr);
    return ValueId::invalid();
}

ValueId RHSConverter::convertMemberAccess(const slang::ast::MemberAccessExpression& expr) {
    reportUnsupported("member access", expr);
    return ValueId::invalid();
}

ValueId RHSConverter::handleMemoEntry(const slang::ast::NamedValueExpression&,
                                          const SignalMemoEntry&) {
    return ValueId::invalid();
}

ValueId RHSConverter::handleCustomNamedValue(const slang::ast::NamedValueExpression&) {
    return ValueId::invalid();
}

ValueId RHSConverter::createTemporaryValue(const slang::ast::Type& type,
                                               std::string_view hint) {
    const TypeInfo info = deriveTypeInfo(type);
    std::string name = makeValueName(hint, valueCounter_++);
    ValueId value =
        createValue(graph(), name, info.width > 0 ? info.width : 1, info.isSigned);
    applyDebug(graph(), value, makeDebugInfo(sourceManager_, currentExpr_));
    return value;
}

OperationId RHSConverter::createOperation(grh::ir::OperationKind kind, std::string_view hint) {
    std::string name = makeOperationName(hint, operationCounter_++);
    OperationId op = ::wolf_sv_parser::createOperation(graph(), kind, name);
    applyDebug(graph(), op, makeDebugInfo(sourceManager_, currentExpr_));
    return op;
}

ValueId RHSConverter::createConstantValue(const slang::SVInt& value,
                                              const slang::ast::Type& type,
                                              std::string_view hint) {
    OperationId op = createOperation(grh::ir::OperationKind::kConstant, hint);
    ValueId result = createTemporaryValue(type, hint);
    addResult(graph(), op, result);
    setAttr(graph(), op, "constValue", formatConstantLiteral(value, type));
    return result;
}

ValueId RHSConverter::createZeroValue(const slang::ast::Type& type,
                                          std::string_view hint) {
    const TypeInfo info = deriveTypeInfo(type);
    slang::SVInt literal(static_cast<slang::bitwidth_t>(info.width), uint64_t(0), info.isSigned);
    return createConstantValue(literal, type, hint);
}

ValueId RHSConverter::buildUnaryOp(grh::ir::OperationKind kind, ValueId operand,
                                       const slang::ast::Expression& originExpr,
                                       std::string_view hint) {
    OperationId op = createOperation(kind, hint);
    addOperand(graph(), op, operand);
    ValueId result = createTemporaryValue(*originExpr.type, hint);
    addResult(graph(), op, result);
    return result;
}

ValueId RHSConverter::buildBinaryOp(grh::ir::OperationKind kind, ValueId lhs, ValueId rhs,
                                        const slang::ast::Expression& originExpr,
                                        std::string_view hint) {
    OperationId op = createOperation(kind, hint);
    addOperand(graph(), op, lhs);
    addOperand(graph(), op, rhs);
    ValueId result = createTemporaryValue(*originExpr.type, hint);
    addResult(graph(), op, result);
    return result;
}

ValueId RHSConverter::buildMux(ValueId cond, ValueId onTrue, ValueId onFalse,
                                   const slang::ast::Expression& originExpr) {
    OperationId op = createOperation(grh::ir::OperationKind::kMux, "mux");
    addOperand(graph(), op, cond);
    addOperand(graph(), op, onTrue);
    addOperand(graph(), op, onFalse);
    ValueId result = createTemporaryValue(*originExpr.type, "mux");
    addResult(graph(), op, result);
    return result;
}

ValueId RHSConverter::buildAssign(ValueId input, const slang::ast::Expression& originExpr,
                                      std::string_view hint) {
    OperationId op = createOperation(grh::ir::OperationKind::kAssign, hint);
    addOperand(graph(), op, input);
    ValueId result = createTemporaryValue(*originExpr.type, hint);
    addResult(graph(), op, result);
    return result;
}

ValueId RHSConverter::resizeValue(ValueId input, const slang::ast::Type& targetType,
                                      const TypeInfo& targetInfo,
                                      const slang::ast::Expression& originExpr,
                                      std::string_view hint) {
    if (!graph_) {
        return ValueId::invalid();
    }

    const int64_t targetWidth = targetInfo.width > 0 ? targetInfo.width : 1;
    const int64_t inputWidth = graph().getValue(input).width() > 0 ? graph().getValue(input).width() : 1;

    if (inputWidth == targetWidth &&
        graph().getValue(input).isSigned() == targetInfo.isSigned) {
        return input;
    }

    if (inputWidth == targetWidth) {
        return buildAssign(input, originExpr, hint);
    }

    if (inputWidth > targetWidth) {
        OperationId slice = createOperation(grh::ir::OperationKind::kSliceStatic, hint);
        addOperand(graph(), slice, input);
        setAttr(graph(), slice, "sliceStart", int64_t(0));
        setAttr(graph(), slice, "sliceEnd", targetWidth - 1);
        ValueId result = createTemporaryValue(targetType, hint);
        addResult(graph(), slice, result);
        return result;
    }

    const int64_t extendWidth = targetWidth - inputWidth;
    OperationId concat = createOperation(grh::ir::OperationKind::kConcat, hint);

    ValueId extendValue = ValueId::invalid();
    if (graph().getValue(input).isSigned()) {
        // Sign extend using the operand's MSB.
        OperationId signSlice = createOperation(grh::ir::OperationKind::kSliceStatic, "sign");
        addOperand(graph(), signSlice, input);
        setAttr(graph(), signSlice, "sliceStart", inputWidth - 1);
        setAttr(graph(), signSlice, "sliceEnd", inputWidth - 1);

        std::string signName = makeValueName("sign", valueCounter_++);
        ValueId signBit = createValue(graph(), signName, 1, graph().getValue(input).isSigned());
        applyDebug(graph(), signBit, makeDebugInfo(sourceManager_, currentExpr_));
        addResult(graph(), signSlice, signBit);

        OperationId rep = createOperation(grh::ir::OperationKind::kReplicate, "signext");
        addOperand(graph(), rep, signBit);
        setAttr(graph(), rep, "rep", extendWidth);
        std::string repName = makeValueName("signext", valueCounter_++);
        ValueId extBits = createValue(graph(), repName, extendWidth, targetInfo.isSigned);
        applyDebug(graph(), extBits, makeDebugInfo(sourceManager_, currentExpr_));
        addResult(graph(), rep, extBits);
        extendValue = extBits;
    }
    else {
        OperationId zeroOp = createOperation(grh::ir::OperationKind::kConstant, "zext");
        std::string valName = makeValueName("zext", valueCounter_++);
        ValueId zeros = createValue(graph(), valName, extendWidth, /*isSigned=*/false);
        applyDebug(graph(), zeros, makeDebugInfo(sourceManager_, currentExpr_));
        addResult(graph(), zeroOp, zeros);
        std::ostringstream oss;
        oss << extendWidth << "'h0";
        setAttr(graph(), zeroOp, "constValue", oss.str());
        extendValue = zeros;
    }

    addOperand(graph(), concat, extendValue);
    addOperand(graph(), concat, input);
    ValueId result = createTemporaryValue(targetType, hint);
    addResult(graph(), concat, result);
    return result;
}

const SignalMemoEntry* RHSConverter::findMemoEntry(const slang::ast::ValueSymbol& symbol) const {
    auto finder = [&](std::span<const SignalMemoEntry> memo) -> const SignalMemoEntry* {
        const SignalMemoEntry* fallback = nullptr;
        for (const SignalMemoEntry& entry : memo) {
            if (entry.symbol == &symbol) {
                if (preferredBlock_ && entry.drivingBlock && entry.drivingBlock != preferredBlock_) {
                    if (!fallback) {
                        fallback = &entry;
                    }
                    continue;
                }
                return &entry;
            }
        }
        return fallback;
    };

    if (const SignalMemoEntry* entry = finder(netMemo_)) {
        return entry;
    }
    // Prefer memory classification over reg when both views contain the symbol.
    if (const SignalMemoEntry* entry = finder(memMemo_)) {
        return entry;
    }
    return finder(regMemo_);
}

ValueId RHSConverter::resolveMemoValue(const SignalMemoEntry& entry) {
    if (entry.value) {
        return entry.value;
    }

    if (entry.stateOp) {
        const grh::ir::Operation opView = graph().getOperation(entry.stateOp);
        const grh::ir::OperationKind kind = opView.kind();
        if (kind == grh::ir::OperationKind::kRegister || kind == grh::ir::OperationKind::kRegisterRst ||
            kind == grh::ir::OperationKind::kRegisterArst) {
            auto results = opView.results();
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
    return ValueId::invalid();
}

ValueId RHSConverter::resolveGraphValue(const slang::ast::ValueSymbol& symbol) {
    if (!graph_) {
        return ValueId::invalid();
    }

    const std::string_view symbolName = symbol.name;
    if (symbolName.empty()) {
        return ValueId::invalid();
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
    const bool hasUnknown = value.hasUnknown();
    const auto base =
        hasUnknown ? slang::LiteralBase::Binary : slang::LiteralBase::Hex;
    return value.toString(base, /* includeBase */ true, slang::SVInt::MAX_BITS);
}

void RHSConverter::reportUnsupported(std::string_view what,
                                     const slang::ast::Expression& expr) {
    if (!diagnostics_ || !origin_) {
        return;
    }

    std::string message = "Unsupported RHS " + std::string(what);
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        if (const auto* named = expr.as_if<slang::ast::NamedValueExpression>()) {
            if (!named->symbol.name.empty()) {
                message.append(" (symbol=");
                message.append(std::string(named->symbol.name));
                message.push_back(')');
            }
        }
    }
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

std::optional<slang::SVInt> RHSConverter::evaluateConstantSvInt(
    const slang::ast::Expression& expr) {
    if (!origin_) {
        return std::nullopt;
    }

    slang::ast::EvalContext& ctx = ensureEvalContext();
    ctx.reset();
    slang::ConstantValue value = expr.eval(ctx);
    if (!value || !value.isInteger() || value.hasUnknown()) {
        return std::nullopt;
    }

    return value.integer();
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

ValueId CombRHSConverter::buildStaticSlice(ValueId input, int64_t sliceStart,
                                               int64_t sliceEnd,
                                               const slang::ast::Expression& originExpr,
                                               std::string_view hint) {
    if (sliceStart < 0 || sliceEnd < sliceStart) {
        reportUnsupported("static slice range", originExpr);
        return ValueId::invalid();
    }

    OperationId op = createOperation(grh::ir::OperationKind::kSliceStatic, hint);
    addOperand(graph(), op, input);
    setAttr(graph(), op, "sliceStart", sliceStart);
    setAttr(graph(), op, "sliceEnd", sliceEnd);
    ValueId result = createTemporaryValue(*originExpr.type, hint);
    addResult(graph(), op, result);
    return result;
}

ValueId CombRHSConverter::buildDynamicSlice(ValueId input, ValueId offset,
                                                int64_t sliceWidth,
                                                const slang::ast::Expression& originExpr,
                                                std::string_view hint) {
    if (sliceWidth <= 0) {
        reportUnsupported("dynamic slice width", originExpr);
        return ValueId::invalid();
    }
    OperationId op = createOperation(grh::ir::OperationKind::kSliceDynamic, hint);
    addOperand(graph(), op, input);
    addOperand(graph(), op, offset);
    setAttr(graph(), op, "sliceWidth", sliceWidth);
    ValueId result = createTemporaryValue(*originExpr.type, hint);
    addResult(graph(), op, result);
    return result;
}

ValueId CombRHSConverter::buildArraySlice(ValueId input, ValueId index,
                                              int64_t sliceWidth,
                                              const slang::ast::Expression& originExpr) {
    if (sliceWidth <= 0) {
        reportUnsupported("array slice width", originExpr);
        return ValueId::invalid();
    }
    OperationId op = createOperation(grh::ir::OperationKind::kSliceArray, "array_slice");
    addOperand(graph(), op, input);
    addOperand(graph(), op, index);
    setAttr(graph(), op, "sliceWidth", sliceWidth);
    ValueId result = createTemporaryValue(*originExpr.type, "array_slice");
    addResult(graph(), op, result);
    return result;
}

ValueId CombRHSConverter::buildMemoryRead(const SignalMemoEntry& entry,
                                          const slang::ast::ElementSelectExpression& expr) {
    if (!entry.stateOp || graph().getOperation(entry.stateOp).kind() != grh::ir::OperationKind::kMemory) {
        reportUnsupported("memory read target", expr);
        return ValueId::invalid();
    }

    ValueId addr = convert(expr.selector());
    if (!addr) {
        return ValueId::invalid();
    }

    OperationId op = createOperation(grh::ir::OperationKind::kMemoryAsyncReadPort, "mem_read");
    addOperand(graph(), op, addr);
    setAttr(graph(), op, "memSymbol",
            std::string(graph().getOperation(entry.stateOp).symbolText()));
    ValueId result = createTemporaryValue(*expr.type, "mem_read");
    addResult(graph(), op, result);
    return result;
}

ValueId CombRHSConverter::createIntConstant(int64_t value, const slang::ast::Type& type,
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

std::optional<int64_t>
CombRHSConverter::translateStaticIndex(const slang::ast::Expression& valueExpr,
                                       int64_t rawIndex) const {
    const SignalMemoEntry* entry = findMemoEntryFromExpression(valueExpr);
    if (entry && entry->symbol) {
        const std::string suffix = "[" + std::to_string(rawIndex) + "]";
        for (const auto& field : entry->fields) {
            if (field.path.ends_with(suffix)) {
                return field.lsb;
            }
        }
    }

    if (!valueExpr.type || !valueExpr.type->isFixedSize()) {
        return std::nullopt;
    }
    const slang::ConstantRange range = valueExpr.type->getFixedRange();
    return static_cast<int64_t>(range.translateIndex(static_cast<int32_t>(rawIndex)));
}

ValueId CombRHSConverter::translateDynamicIndex(const slang::ast::Expression& valueExpr,
                                                    ValueId rawIndex,
                                                    const slang::ast::Expression& originExpr,
                                                    std::string_view hint) {
    if (valueExpr.type && valueExpr.type->isUnpackedArray()) {
        return rawIndex;
    }
    if (!valueExpr.type || !valueExpr.type->isFixedSize()) {
        return rawIndex;
    }
    const slang::ConstantRange range = valueExpr.type->getFixedRange();
    if (range.isLittleEndian()) {
        const int64_t lower = range.lower();
        if (lower == 0) {
            return rawIndex;
        }
        ValueId lowerConst = createIntConstant(lower, *originExpr.type, hint);
        if (!lowerConst) {
            return ValueId::invalid();
        }
        return buildBinaryOp(grh::ir::OperationKind::kSub, rawIndex, lowerConst, originExpr,
                             hint);
    }

    ValueId upperConst = createIntConstant(range.upper(), *originExpr.type, hint);
    if (!upperConst) {
        return ValueId::invalid();
    }
    return buildBinaryOp(grh::ir::OperationKind::kSub, upperConst, rawIndex, originExpr, hint);
}

ValueId
CombRHSConverter::convertElementSelect(const slang::ast::ElementSelectExpression& expr) {
    if (const SignalMemoEntry* entry = findMemoEntryFromExpression(expr.value())) {
        if (entry->stateOp &&
            graph().getOperation(entry->stateOp).kind() == grh::ir::OperationKind::kMemory) {
            return buildMemoryRead(*entry, expr);
        }
    }

    ValueId input = convert(expr.value());
    if (!input) {
        return ValueId::invalid();
    }

    const TypeInfo info = deriveTypeInfo(*expr.type);
    // 尽可能在 elaboration 期把元素选择降级成静态切片，避免生成 kSliceArray。
    // 仅在 selector 真正为常量选择时才这么做，避免对普通变量的默认值做静态折叠。
    slang::ast::EvalContext& ctx = ensureEvalContext();
    bool selectorRuntime = false;
    if (const auto* namedSel = expr.selector().as_if<slang::ast::NamedValueExpression>()) {
        if (const auto* sym = namedSel->symbol.as_if<slang::ast::ValueSymbol>()) {
            const auto kind = sym->kind;
            if (kind != slang::ast::SymbolKind::Parameter &&
                kind != slang::ast::SymbolKind::EnumValue) {
                selectorRuntime = true;
            }
        }
    }

    if (!selectorRuntime && expr.isConstantSelect(ctx)) {
        if (std::optional<int64_t> indexConst = evaluateConstantInt(expr.selector())) {
            if (info.width > 0) {
                const int64_t baseIndex =
                    translateStaticIndex(expr.value(), *indexConst).value_or(*indexConst);
                const int64_t sliceStart = baseIndex * info.width;
                const int64_t sliceEnd = sliceStart + info.width - 1;
                if (sliceStart >= 0 && sliceEnd >= sliceStart) {
                    return buildStaticSlice(input, sliceStart, sliceEnd, expr, "array_static");
                }
            }
        }
    }

    ValueId index = convert(expr.selector());
    if (!index) {
        return ValueId::invalid();
    }

    ValueId normalizedIndex =
        translateDynamicIndex(expr.value(), index, expr.selector(), "array_index");
    if (!normalizedIndex) {
        return ValueId::invalid();
    }

    return buildArraySlice(input, normalizedIndex, info.width, expr);
}

ValueId
CombRHSConverter::convertRangeSelect(const slang::ast::RangeSelectExpression& expr) {
    ValueId input = convert(expr.value());
    if (!input) {
        return ValueId::invalid();
    }

    std::optional<slang::ConstantRange> valueRange;
    if (expr.value().type && expr.value().type->isFixedSize()) {
        valueRange = expr.value().type->getFixedRange();
    }

    using slang::ast::RangeSelectionKind;
    switch (expr.getSelectionKind()) {
    case RangeSelectionKind::Simple: {
        std::optional<int64_t> left = evaluateConstantInt(expr.left());
        std::optional<int64_t> right = evaluateConstantInt(expr.right());
        if (!left || !right) {
            reportUnsupported("static range bounds", expr);
            return ValueId::invalid();
        }
        const int64_t normLeft =
            translateStaticIndex(expr.value(), *left).value_or(*left);
        const int64_t normRight =
            translateStaticIndex(expr.value(), *right).value_or(*right);
        const int64_t sliceStart = std::min(normLeft, normRight);
        const int64_t sliceEnd = std::max(normLeft, normRight);
        return buildStaticSlice(input, sliceStart, sliceEnd, expr, "range_slice");
    }
    case RangeSelectionKind::IndexedUp: {
        std::optional<int64_t> width = evaluateConstantInt(expr.right());
        if (!width || *width <= 0) {
            reportUnsupported("indexed range width", expr);
            return ValueId::invalid();
        }

        if (std::optional<int64_t> baseConst = evaluateConstantInt(expr.left())) {
            const int64_t msbIndex = *baseConst + *width - 1;
            const int64_t lsbIndex = *baseConst;
            const int64_t normMsb =
                translateStaticIndex(expr.value(), msbIndex).value_or(msbIndex);
            const int64_t normLsb =
                translateStaticIndex(expr.value(), lsbIndex).value_or(lsbIndex);
            const int64_t sliceStart = std::min(normLsb, normMsb);
            const int64_t sliceEnd = std::max(normLsb, normMsb);
            return buildStaticSlice(input, sliceStart, sliceEnd, expr, "range_up");
        }

        if (!expr.left().type) {
            reportUnsupported("indexed range base type", expr);
            return ValueId::invalid();
        }

        ValueId base = convert(expr.left());
        if (!base) {
            return ValueId::invalid();
        }

        ValueId lsbIndex = base;
        if (*width > 1 && valueRange && !valueRange->isLittleEndian()) {
            ValueId widthValue =
                createIntConstant(*width - 1, *expr.left().type, "range_up_width");
            if (!widthValue) {
                return ValueId::invalid();
            }
            lsbIndex =
                buildBinaryOp(grh::ir::OperationKind::kAdd, base, widthValue, expr.left(),
                              "range_up_base");
            if (!lsbIndex) {
                return ValueId::invalid();
            }
        }

        ValueId offset =
            translateDynamicIndex(expr.value(), lsbIndex, expr.left(), "range_up_index");
        if (!offset) {
            return ValueId::invalid();
        }

        return buildDynamicSlice(input, offset, *width, expr, "range_up");
    }
    case RangeSelectionKind::IndexedDown: {
        std::optional<int64_t> width = evaluateConstantInt(expr.right());
        if (!width || *width <= 0) {
            reportUnsupported("indexed range width", expr);
            return ValueId::invalid();
        }

        if (std::optional<int64_t> baseConst = evaluateConstantInt(expr.left())) {
            const int64_t msbIndex = *baseConst;
            const int64_t lsbIndex = *baseConst - *width + 1;
            const int64_t normMsb =
                translateStaticIndex(expr.value(), msbIndex).value_or(msbIndex);
            const int64_t normLsb =
                translateStaticIndex(expr.value(), lsbIndex).value_or(lsbIndex);
            const int64_t sliceStart = std::min(normLsb, normMsb);
            const int64_t sliceEnd = std::max(normLsb, normMsb);
            return buildStaticSlice(input, sliceStart, sliceEnd, expr, "range_down");
        }

        if (!expr.left().type) {
            reportUnsupported("indexed range base type", expr);
            return ValueId::invalid();
        }

        ValueId base = convert(expr.left());
        if (!base) {
            return ValueId::invalid();
        }

        ValueId lsbIndex = base;
        if (*width > 1) {
            ValueId widthValue =
                createIntConstant(*width - 1, *expr.left().type, "range_down_width");
            if (!widthValue) {
                return ValueId::invalid();
            }
            lsbIndex = buildBinaryOp(grh::ir::OperationKind::kSub, base, widthValue, expr.left(),
                                     "range_down_base");
            if (!lsbIndex) {
                return ValueId::invalid();
            }
        }

        ValueId offset =
            translateDynamicIndex(expr.value(), lsbIndex, expr.left(), "range_down_index");
        if (!offset) {
            return ValueId::invalid();
        }

        return buildDynamicSlice(input, offset, *width, expr, "range_down");
    }
    default:
        reportUnsupported("range select kind", expr);
        return ValueId::invalid();
    }
}

ValueId
CombRHSConverter::convertMemberAccess(const slang::ast::MemberAccessExpression& expr) {
    std::optional<SliceRange> slice = deriveStructFieldSlice(expr);
    if (!slice) {
        reportUnsupported("struct member access", expr);
        return ValueId::invalid();
    }

    ValueId input = convert(expr.value());
    if (!input) {
        return ValueId::invalid();
    }

    const int64_t sliceStart = std::min(slice->lsb, slice->msb);
    const int64_t sliceEnd = std::max(slice->lsb, slice->msb);
    return buildStaticSlice(input, sliceStart, sliceEnd, expr, "member_slice");
}

void ElaborateDiagnostics::todo(const slang::ast::Symbol& symbol, std::string message) {
    add(ElaborateDiagnosticKind::Todo, symbol, std::move(message));
}

void ElaborateDiagnostics::nyi(const slang::ast::Symbol& symbol, std::string message) {
    add(ElaborateDiagnosticKind::NotYetImplemented, symbol, std::move(message));
}

void ElaborateDiagnostics::warn(const slang::ast::Symbol& symbol, std::string message) {
    add(ElaborateDiagnosticKind::Warning, symbol, std::move(message));
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

grh::ir::Netlist Elaborate::convert(const slang::ast::RootSymbol& root) {
    sourceManager_ = root.getCompilation().getSourceManager();
    grh::ir::Netlist netlist;

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
        grh::ir::Graph* graph = materializeGraph(*topInstance, netlist, newlyCreated);
        if (!graph) {
            continue;
        }

        convertInstanceBody(*topInstance, *graph, netlist);
        netlist.markAsTop(graph->symbol());
        if (!topInstance->name.empty()) {
            netlist.registerGraphAlias(std::string(topInstance->name), *graph);
        }
        else if (!topInstance->getDefinition().name.empty()) {
            netlist.registerGraphAlias(std::string(topInstance->getDefinition().name), *graph);
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

std::span<const SignalMemoEntry>
Elaborate::peekMemMemo(const slang::ast::InstanceBodySymbol& body) const {
    if (auto it = memMemo_.find(&body); it != memMemo_.end()) {
        return it->second;
    }
    return {};
}

std::span<const DpiImportEntry>
Elaborate::peekDpiImports(const slang::ast::InstanceBodySymbol& body) const {
    if (auto it = dpiImports_.find(&body); it != dpiImports_.end()) {
        return it->second;
    }
    return {};
}

const InoutPortMemo* Elaborate::findInoutMemo(const slang::ast::InstanceBodySymbol& body,
                                              const slang::ast::ValueSymbol& symbol) const {
    auto it = inoutMemo_.find(&body);
    if (it == inoutMemo_.end()) {
        return nullptr;
    }
    auto memoIt = it->second.find(&symbol);
    if (memoIt == it->second.end()) {
        return nullptr;
    }
    return &memoIt->second;
}

const BlackboxMemoEntry*
Elaborate::peekBlackboxMemo(const slang::ast::InstanceBodySymbol& body) const {
    if (auto it = blackboxMemo_.find(&body); it != blackboxMemo_.end()) {
        return &it->second;
    }

    if (body.parentInstance) {
        if (const auto* canonical = body.parentInstance->getCanonicalBody()) {
            if (auto it = blackboxMemo_.find(canonical); it != blackboxMemo_.end()) {
                return &it->second;
            }
        }
    }
    return nullptr;
}

grh::ir::Graph* Elaborate::materializeGraph(const slang::ast::InstanceSymbol& instance,
                                        grh::ir::Netlist& netlist, bool& wasCreated) {
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

    grh::ir::Graph& graph = netlist.createGraph(graphName);
    graphByBody_[keyBody] = &graph;
    graphByBody_[&instance.body] = &graph;
    wasCreated = true;
    return &graph;
}

void Elaborate::populatePorts(const slang::ast::InstanceSymbol& instance,
                              const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph) {

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
            TypeHelper::Info typeInfo = TypeHelper::analyze(type, *port, diagnostics_);
            const int64_t width = typeInfo.width > 0 ? typeInfo.width : 1;
            const bool isSigned = typeInfo.isSigned;

            std::string portName(port->name);
            auto createPortValue = [&](std::string_view suffix, bool signedValue) -> ValueId {
                std::string name = portName;
                name.append(suffix);
                ValueId value = createValue(graph, name, width, signedValue);
                applyDebug(graph, value, makeDebugInfo(sourceManager_, port));
                return value;
            };

            ValueId value = ValueId::invalid();
            ValueId inValue = ValueId::invalid();
            ValueId outValue = ValueId::invalid();
            ValueId oeValue = ValueId::invalid();
            if (port->direction == slang::ast::ArgumentDirection::InOut) {
                inValue = createPortValue("__in", isSigned);
                outValue = createPortValue("__out", isSigned);
                oeValue = createPortValue("__oe", /*signedValue=*/false);
                value = inValue;
            }
            else {
                value = createPortValue("", isSigned);
            }

            registerValueForSymbol(*port, value);
            if (const auto* internal = port->internalSymbol ?
                                           port->internalSymbol->as_if<slang::ast::ValueSymbol>()
                                           : nullptr) {
                registerValueForSymbol(*internal, value);
                if (port->direction == slang::ast::ArgumentDirection::InOut) {
                    InoutPortMemo memo;
                    memo.symbol = internal;
                    memo.in = inValue;
                    memo.out = outValue;
                    memo.oe = oeValue;
                    memo.outEntry.symbol = internal;
                    memo.outEntry.type = &type;
                    memo.outEntry.width = width;
                    memo.outEntry.isSigned = isSigned;
                    memo.outEntry.value = outValue;
                    memo.outEntry.fields.reserve(typeInfo.fields.size());
                    for (const auto& field : typeInfo.fields) {
                        memo.outEntry.fields.push_back(
                            SignalMemoField{field.path, field.msb, field.lsb, field.isSigned});
                    }
                    memo.oeEntry = memo.outEntry;
                    memo.oeEntry.isSigned = false;
                    memo.oeEntry.value = oeValue;
                    inoutMemo_[&body][internal] = std::move(memo);
                }
            }

            grh::ir::SymbolId portSym = graph.internSymbol(portName);
            switch (port->direction) {
            case slang::ast::ArgumentDirection::In:
                graph.bindInputPort(portSym, value);
                break;
            case slang::ast::ArgumentDirection::Out:
                graph.bindOutputPort(portSym, value);
                break;
            case slang::ast::ArgumentDirection::InOut:
                if (!inValue || !outValue || !oeValue) {
                    handleUnsupportedPort(*port, "InOut port lacks values", diagnostics_);
                    break;
                }
                graph.bindInoutPort(portSym, inValue, outValue, oeValue);
                break;
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
                                      grh::ir::Graph& graph) {
    if (!options_.emitPlaceholders) {
        return;
    }

    std::string opName = "_module_placeholder";
    if (placeholderCounter_ > 0) {
        opName.append("_");
        opName.append(std::to_string(placeholderCounter_));
    }
    ++placeholderCounter_;

    OperationId op = createOperation(graph, grh::ir::OperationKind::kBlackbox, opName);
    applyDebug(graph, op, makeDebugInfo(sourceManager_, &instance));

    const auto& definition = instance.body.getDefinition();
    std::string moduleName = !definition.name.empty() ? std::string(definition.name)
                                                      : std::string(instance.name);
    if (moduleName.empty()) {
        moduleName = "anonymous_module";
    }

    setAttr(graph, op, "module_name", moduleName);
    setAttr(graph, op, "status", std::string("TODO: module body elaboration pending"));

    if (diagnostics_) {
        diagnostics_->todo(instance, "Module body elaboration pending");
    }
}

void Elaborate::convertInstanceBody(const slang::ast::InstanceSymbol& instance,
                                    grh::ir::Graph& graph, grh::ir::Netlist& netlist) {
    const slang::ast::InstanceBodySymbol* canonicalBody = instance.getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body =
        canonicalBody ? *canonicalBody : instance.body;

    if (!processedBodies_.insert(&body).second) {
        return;
    }

    const BlackboxMemoEntry* blackbox = ensureBlackboxMemo(body);
    const bool isBlackbox = blackbox && blackbox->isBlackbox;

    populatePorts(instance, body, graph);
    if (isBlackbox) {
        return;
    }
    emitModulePlaceholder(instance, graph);
    collectDpiImports(body);
    collectSignalMemos(body);
    materializeSignalMemos(body, graph);
    materializeDpiImports(body, graph);
    ensureWriteBackMemo(body);
    processNetInitializers(body, graph);

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
            using slang::ast::ProceduralBlockKind;
            if (block->procedureKind == ProceduralBlockKind::Initial) {
                if (diagnostics_) {
                    diagnostics_->warn(*block, "initial blocks are ignored (non-synthesizable)");
                }
                continue;
            }
            if (block->procedureKind == ProceduralBlockKind::AlwaysLatch || isCombProceduralBlock(*block)) {
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
                                     grh::ir::Graph& graph, grh::ir::Netlist& netlist) {
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
                                     grh::ir::Graph& graph, grh::ir::Netlist& netlist) {
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
                                          grh::ir::Graph& graph, grh::ir::Netlist& netlist) {
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

void Elaborate::processNetInitializers(const slang::ast::InstanceBodySymbol& body,
                                       grh::ir::Graph& graph) {
    std::span<const SignalMemoEntry> netMemo = peekNetMemo(body);
    if (netMemo.empty()) {
        return;
    }

    auto findEntry = [&](const slang::ast::ValueSymbol& symbol) -> const SignalMemoEntry* {
        for (const SignalMemoEntry& entry : netMemo) {
            if (entry.symbol == &symbol) {
                return &entry;
            }
        }
        return nullptr;
    };

    const std::span<const SignalMemoEntry> regMemo = peekRegMemo(body);
    const std::span<const SignalMemoEntry> memMemo = peekMemMemo(body);

    for (const slang::ast::Symbol& member : body.members()) {
        const auto* net = member.as_if<slang::ast::NetSymbol>();
        if (!net) {
            continue;
        }
        const slang::ast::Expression* init = net->getInitializer();
        if (!init) {
            continue;
        }
        const SignalMemoEntry* entry = findEntry(*net);
        if (!entry) {
            continue;
        }

        RHSConverter::Context context{
            .graph = &graph,
            .netMemo = netMemo,
            .regMemo = regMemo,
            .memMemo = memMemo,
            .origin = net,
            .diagnostics = diagnostics_,
            .sourceManager = sourceManager_,
        };
        CombRHSConverter converter(context);
        ValueId rhsValue = converter.convert(*init);
        if (!rhsValue) {
            continue;
        }

        WriteBackMemo::Slice slice;
        slice.path = net->name.empty() ? std::string() : std::string(net->name);
        const int64_t width = entry->width > 0 ? entry->width : 1;
        slice.msb = width - 1;
        slice.lsb = 0;
        slice.value = rhsValue;
        slice.originExpr = init;

        WriteBackMemo& memo = ensureWriteBackMemo(body);
        memo.recordWrite(*entry, WriteBackMemo::AssignmentKind::Continuous, net,
                         std::vector<WriteBackMemo::Slice>{std::move(slice)});
    }
}

void Elaborate::processContinuousAssign(const slang::ast::ContinuousAssignSymbol& assign,
                                        const slang::ast::InstanceBodySymbol& body,
                                        grh::ir::Graph& graph) {
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
        .memMemo = peekMemMemo(body),
        .origin = &assign,
        .diagnostics = diagnostics_,
        .sourceManager = sourceManager_};
    CombRHSConverter converter(context);

    const slang::ast::ValueSymbol* lhsSymbol = resolveAssignedSymbol(assignment->left());
    const InoutPortMemo* inoutMemo =
        lhsSymbol ? findInoutMemo(body, *lhsSymbol) : nullptr;
    if (inoutMemo) {
        auto isAllZLiteral = [&](const slang::ast::Expression& valueExpr) -> bool {
            slang::ast::EvalContext eval(assign);
            eval.reset();
            slang::ConstantValue value = valueExpr.eval(eval);
            if (!value || !value.isInteger()) {
                return false;
            }
            const slang::SVInt& literal = value.integer();
            const auto width = static_cast<int64_t>(literal.getBitWidth());
            for (int64_t bit = 0; bit < width; ++bit) {
                slang::logic_t bitVal = literal[static_cast<int32_t>(bit)];
                if (bitVal.value != slang::logic_t::Z_VALUE) {
                    return false;
                }
            }
            return true;
        };

        const auto* condExpr = assignment->right().as_if<slang::ast::ConditionalExpression>();
        if (!condExpr || condExpr->conditions.empty() || condExpr->conditions.front().pattern) {
            if (diagnostics_) {
                diagnostics_->nyi(assign, "Inout assign must use a simple ternary with 'z'");
            }
            return;
        }
        const auto& condition = condExpr->conditions.front();
        const slang::ast::Expression& trueExpr = condExpr->left();
        const slang::ast::Expression& falseExpr = condExpr->right();
        const bool trueIsZ = isAllZLiteral(trueExpr);
        const bool falseIsZ = isAllZLiteral(falseExpr);
        if (trueIsZ == falseIsZ) {
            if (diagnostics_) {
                diagnostics_->nyi(assign, "Inout ternary must have exactly one 'z' branch");
            }
            return;
        }

        const slang::ast::Expression& dataExpr = trueIsZ ? falseExpr : trueExpr;
        ValueId dataValue = converter.convert(dataExpr);
        if (!dataValue) {
            return;
        }

        ValueId condValue = converter.convert(*condition.expr);
        if (!condValue) {
            return;
        }
        if (graph.getValue(condValue).width() > 1) {
            OperationId reduce =
                createOperation(graph, grh::ir::OperationKind::kReduceOr,
                                makeUniqueOperationName(graph, "inout_cond_reduce"));
            addOperand(graph, reduce, condValue);
            ValueId reduced =
                createValue(graph, makeUniqueOperationName(graph, "inout_cond"), 1, false);
            addResult(graph, reduce, reduced);
            condValue = reduced;
        }
        if (graph.getValue(condValue).width() != 1) {
            if (diagnostics_) {
                diagnostics_->nyi(assign, "Inout ternary condition must be 1-bit");
            }
            return;
        }

        ValueId oeValue = condValue;
        if (trueIsZ) {
            OperationId invOp =
                createOperation(graph, grh::ir::OperationKind::kLogicNot,
                                makeUniqueOperationName(graph, "inout_oe_not"));
            addOperand(graph, invOp, condValue);
            ValueId invVal =
                createValue(graph, makeUniqueOperationName(graph, "inout_oe"), 1, false);
            addResult(graph, invOp, invVal);
            oeValue = invVal;
        }

        const int64_t targetWidth = inoutMemo->outEntry.width;
        const int64_t oeWidth = graph.getValue(oeValue).width();
        if (oeWidth != targetWidth) {
            if (oeWidth == 1 && targetWidth > 1) {
                OperationId repOp =
                    createOperation(graph, grh::ir::OperationKind::kReplicate,
                                    makeUniqueOperationName(graph, "inout_oe_rep"));
                setAttr(graph, repOp, "rep", targetWidth);
                addOperand(graph, repOp, oeValue);
                ValueId repVal =
                    createValue(graph, makeUniqueOperationName(graph, "inout_oe"), targetWidth,
                                false);
                addResult(graph, repOp, repVal);
                oeValue = repVal;
            }
            else {
                if (diagnostics_) {
                    diagnostics_->nyi(assign, "Inout oe width mismatch");
                }
                return;
            }
        }

        WriteBackMemo& memo = ensureWriteBackMemo(body);
        std::unordered_map<const slang::ast::ValueSymbol*, const SignalMemoEntry*> outOverride;
        std::unordered_map<const slang::ast::ValueSymbol*, const SignalMemoEntry*> oeOverride;
        outOverride.emplace(inoutMemo->symbol, &inoutMemo->outEntry);
        oeOverride.emplace(inoutMemo->symbol, &inoutMemo->oeEntry);

        LHSConverter::Context lhsContext{
            .graph = &graph,
            .netMemo = netMemo,
            .regMemo = {},
            .memMemo = {},
            .inoutOverrides = &outOverride,
            .origin = &assign,
            .diagnostics = diagnostics_,
            .sourceManager = sourceManager_};
        ContinuousAssignLHSConverter lhsOut(lhsContext, memo);
        lhsOut.convert(*assignment, dataValue);

        LHSConverter::Context oeContext = lhsContext;
        oeContext.inoutOverrides = &oeOverride;
        ContinuousAssignLHSConverter lhsOe(oeContext, memo);
        lhsOe.convert(*assignment, oeValue);
        return;
    }

    ValueId rhsValue = converter.convert(assignment->right());
    if (!rhsValue) {
        return;
    }

    WriteBackMemo& memo = ensureWriteBackMemo(body);
    LHSConverter::Context lhsContext{
        .graph = &graph,
        .netMemo = netMemo,
        .regMemo = {},
        .memMemo = {},
        .origin = &assign,
        .diagnostics = diagnostics_,
        .sourceManager = sourceManager_};
    ContinuousAssignLHSConverter lhsConverter(lhsContext, memo);
    lhsConverter.convert(*assignment, rhsValue);
}

void Elaborate::processCombAlways(const slang::ast::ProceduralBlockSymbol& block,
                                  const slang::ast::InstanceBodySymbol& body,
                                  grh::ir::Graph& graph) {
    std::span<const SignalMemoEntry> netMemo = peekNetMemo(body);
    std::span<const SignalMemoEntry> regMemo = peekRegMemo(body);
    std::span<const SignalMemoEntry> memMemo = peekMemMemo(body);
    std::span<const DpiImportEntry> dpiImports = peekDpiImports(body);
    WriteBackMemo& memo = ensureWriteBackMemo(body);
    CombAlwaysConverter converter(graph, netMemo, regMemo, memMemo, dpiImports, memo, block,
                                  diagnostics_, sourceManager_);
    converter.run();
}

void Elaborate::processSeqAlways(const slang::ast::ProceduralBlockSymbol& block,
                                 const slang::ast::InstanceBodySymbol& body,
                                 grh::ir::Graph& graph) {
    std::span<const SignalMemoEntry> netMemo = peekNetMemo(body);
    std::span<const SignalMemoEntry> regMemo = peekRegMemo(body);
    std::span<const SignalMemoEntry> memMemo = peekMemMemo(body);
    std::span<const DpiImportEntry> dpiImports = peekDpiImports(body);
    WriteBackMemo& memo = ensureWriteBackMemo(body);
    SeqAlwaysConverter converter(graph, netMemo, regMemo, memMemo, dpiImports, memo, block,
                                 diagnostics_, sourceManager_);
    converter.run();
}

void Elaborate::processInstance(const slang::ast::InstanceSymbol& childInstance,
                                grh::ir::Graph& parentGraph, grh::ir::Netlist& netlist) {
    if (!childInstance.isModule()) {
        if (diagnostics_) {
            diagnostics_->nyi(childInstance, "Only module instances are supported");
        }
        return;
    }

    const slang::ast::InstanceBodySymbol* canonicalBody = childInstance.getCanonicalBody();
    const slang::ast::InstanceBodySymbol& memoBody =
        canonicalBody ? *canonicalBody : childInstance.body;
    const BlackboxMemoEntry* blackbox = ensureBlackboxMemo(memoBody);

    bool childCreated = false;
    grh::ir::Graph* childGraph = materializeGraph(childInstance, netlist, childCreated);
    if (!childGraph) {
        return;
    }

    convertInstanceBody(childInstance, *childGraph, netlist);
    if (blackbox && blackbox->isBlackbox) {
        createBlackboxOperation(childInstance, parentGraph, *blackbox);
        return;
    }

    createInstanceOperation(childInstance, parentGraph, *childGraph);
}

void Elaborate::createInstanceOperation(const slang::ast::InstanceSymbol& childInstance,
                                        grh::ir::Graph& parentGraph, grh::ir::Graph& targetGraph) {
    std::string baseName =
        childInstance.name.empty() ? std::string("inst") : std::string(childInstance.name);
    std::string opName = makeUniqueOperationName(parentGraph, baseName);
    OperationId op = createOperation(parentGraph, grh::ir::OperationKind::kInstance, opName);
    applyDebug(parentGraph, op, makeDebugInfo(sourceManager_, &childInstance));

    // Prefer本地实例名，若缺失再回退层级路径，避免在 emit 时打印带前缀的层次化名字。
    std::string instanceName =
        childInstance.name.empty() ? std::string() : sanitizeForGraphName(childInstance.name);
    if (instanceName.empty()) {
        instanceName = sanitizeForGraphName(deriveSymbolPath(childInstance));
    }
    if (instanceName.empty()) {
        instanceName = childInstance.name.empty() ? std::string("_inst_") + std::to_string(instanceCounter_++)
                                                  : sanitizeForGraphName(childInstance.name);
    }
    if (instanceName.empty()) {
        instanceName = "_inst_" + std::to_string(instanceCounter_++);
    }

    const slang::ast::InstanceBodySymbol* contextBody = nullptr;
    for (const auto& [body, mappedGraph] : graphByBody_) {
        if (mappedGraph == &parentGraph) {
            contextBody = body;
            break;
        }
    }

    std::span<const SignalMemoEntry> netMemo;
    std::span<const SignalMemoEntry> regMemo;
    std::span<const SignalMemoEntry> memMemo;
    WriteBackMemo* portWriteMemo = nullptr;
    if (contextBody) {
        netMemo = peekNetMemo(*contextBody);
        regMemo = peekRegMemo(*contextBody);
        memMemo = peekMemMemo(*contextBody);
        portWriteMemo = &ensureWriteBackMemo(*contextBody);
    }

    class PortLHSConverter : public LHSConverter {
    public:
        using LHSConverter::LHSConverter;
        bool convert(const slang::ast::Expression& expr, ValueId rhsValue,
                     std::vector<WriteResult>& outResults) {
            return lowerExpression(expr, rhsValue, outResults);
        }
    };

    auto makePortValue = [&](const slang::ast::PortSymbol& port) -> ValueId {
        TypeHelper::Info info = TypeHelper::analyze(port.getType(), port, diagnostics_);
        std::string base =
            instanceName.empty() || port.name.empty()
                ? std::string(port.name.empty() ? "_port" : port.name)
                : sanitizeForGraphName(instanceName + "_" + std::string(port.name));
        if (base.empty()) {
            base = "_port";
        }
        std::string candidate = base;
        std::size_t suffix = 0;
        while (parentGraph.findValue(candidate) || parentGraph.findOperation(candidate)) {
            candidate = base;
            candidate.push_back('_');
            candidate.append(std::to_string(++suffix));
        }
        const int64_t width = info.width > 0 ? info.width : 1;
        ValueId value = createValue(parentGraph, candidate, width, info.isSigned);
        applyDebug(parentGraph, value, makeDebugInfo(sourceManager_, &port));
        return value;
    };

    std::vector<std::string> inputPortNames;
    std::vector<std::string> outputPortNames;
    std::vector<std::string> inoutPortNames;
    std::vector<ValueId> inputOperands;
    std::vector<ValueId> outputResults;
    std::vector<ValueId> inoutOutOperands;
    std::vector<ValueId> inoutOeOperands;
    std::vector<ValueId> inoutInResults;

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

            switch (port->direction) {
            case slang::ast::ArgumentDirection::In: {
                if (!expr) {
                    break;
                }
                ValueId value = resolveConnectionValue(*expr, parentGraph, port);
                if (!value) {
                    break;
                }
                inputOperands.push_back(value);
                inputPortNames.emplace_back(port->name);
                break;
            }
            case slang::ast::ArgumentDirection::Out: {
                ValueId resolved = expr ? resolveConnectionValue(*expr, parentGraph, port)
                                            : ValueId::invalid();
                const bool useDirect = resolved && !parentGraph.getValue(resolved).definingOp();
                ValueId resultValue = useDirect ? resolved : makePortValue(*port);
                if (!resultValue) {
                    break;
                }
                outputResults.push_back(resultValue);
                outputPortNames.emplace_back(port->name);

                const slang::ast::Expression* targetExpr = expr;
                if (targetExpr && targetExpr->kind == slang::ast::ExpressionKind::Assignment) {
                    const auto& assign = targetExpr->as<slang::ast::AssignmentExpression>();
                    if (assign.isLValueArg()) {
                        targetExpr = &assign.left();
                    }
                }
                if (!useDirect && targetExpr && portWriteMemo) {
                    LHSConverter::Context lhsContext{
                        .graph = &parentGraph,
                        .netMemo = netMemo,
                        .regMemo = regMemo,
                        .memMemo = memMemo,
                        .origin = port,
                        .diagnostics = diagnostics_,
                        .sourceManager = sourceManager_};
                    PortLHSConverter lhsConverter(lhsContext);
                    std::vector<LHSConverter::WriteResult> writeResults;
                    if (lhsConverter.convert(*targetExpr, resultValue, writeResults)) {
                        for (LHSConverter::WriteResult& result : writeResults) {
                            if (!result.target) {
                                continue;
                            }
                            portWriteMemo->recordWrite(*result.target,
                                                       WriteBackMemo::AssignmentKind::Continuous,
                                                       port, std::move(result.slices));
                        }
                    }
                }
                break;
            }
            case slang::ast::ArgumentDirection::InOut:
            {
                if (!expr) {
                    if (diagnostics_) {
                        diagnostics_->nyi(*port, "Missing inout port connection during hierarchy elaboration");
                    }
                    break;
                }
                const slang::ast::Expression* targetExpr = expr;
                if (targetExpr->kind == slang::ast::ExpressionKind::Assignment) {
                    const auto& assign = targetExpr->as<slang::ast::AssignmentExpression>();
                    if (assign.isLValueArg()) {
                        targetExpr = &assign.left();
                    }
                }
                if (targetExpr->kind == slang::ast::ExpressionKind::HierarchicalValue) {
                    if (diagnostics_) {
                        diagnostics_->nyi(*port, "Hierarchical inout port connections are not supported");
                    }
                    break;
                }
                if (!targetExpr->as_if<slang::ast::NamedValueExpression>()) {
                    if (diagnostics_) {
                        diagnostics_->nyi(*port, "Inout port connections must be simple named values");
                    }
                    break;
                }
                const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(*targetExpr);
                const InoutPortMemo* inoutMemo =
                    (symbol && contextBody) ? findInoutMemo(*contextBody, *symbol) : nullptr;
                if (!inoutMemo) {
                    if (diagnostics_) {
                        diagnostics_->nyi(*port, "Inout port connection lacks inout value triple");
                    }
                    break;
                }
                inoutOutOperands.push_back(inoutMemo->out);
                inoutOeOperands.push_back(inoutMemo->oe);
                inoutInResults.push_back(inoutMemo->in);
                inoutPortNames.emplace_back(port->name);
                break;
            }
            case slang::ast::ArgumentDirection::Ref:
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Ref port directions are not supported yet");
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

    for (ValueId operand : inputOperands) {
        addOperand(parentGraph, op, operand);
    }
    for (ValueId operand : inoutOutOperands) {
        addOperand(parentGraph, op, operand);
    }
    for (ValueId operand : inoutOeOperands) {
        addOperand(parentGraph, op, operand);
    }
    for (ValueId result : outputResults) {
        addResult(parentGraph, op, result);
    }
    for (ValueId result : inoutInResults) {
        addResult(parentGraph, op, result);
    }

    setAttr(parentGraph, op, "moduleName", targetGraph.symbol());
    setAttr(parentGraph, op, "instanceName", instanceName);
    setAttr(parentGraph, op, "inputPortName", inputPortNames);
    setAttr(parentGraph, op, "outputPortName", outputPortNames);
    setAttr(parentGraph, op, "inoutPortName", inoutPortNames);
}

void Elaborate::createBlackboxOperation(const slang::ast::InstanceSymbol& childInstance,
                                        grh::ir::Graph& parentGraph, const BlackboxMemoEntry& memo) {
    std::string baseName =
        childInstance.name.empty() ? std::string("inst") : std::string(childInstance.name);
    std::string opName = makeUniqueOperationName(parentGraph, baseName);
    OperationId op = createOperation(parentGraph, grh::ir::OperationKind::kBlackbox, opName);
    applyDebug(parentGraph, op, makeDebugInfo(sourceManager_, &childInstance));

    std::string instanceName =
        childInstance.name.empty() ? std::string() : sanitizeForGraphName(childInstance.name);
    if (instanceName.empty()) {
        instanceName = sanitizeForGraphName(deriveSymbolPath(childInstance));
    }
    if (instanceName.empty()) {
        instanceName = childInstance.name.empty() ? std::string("_inst_") + std::to_string(instanceCounter_++)
                                                  : sanitizeForGraphName(childInstance.name);
    }
    if (instanceName.empty()) {
        instanceName = "_inst_" + std::to_string(instanceCounter_++);
    }

    const slang::ast::InstanceBodySymbol* contextBody = nullptr;
    for (const auto& [body, mappedGraph] : graphByBody_) {
        if (mappedGraph == &parentGraph) {
            contextBody = body;
            break;
        }
    }

    std::span<const SignalMemoEntry> netMemo;
    std::span<const SignalMemoEntry> regMemo;
    std::span<const SignalMemoEntry> memMemo;
    WriteBackMemo* portWriteMemo = nullptr;
    if (contextBody) {
        netMemo = peekNetMemo(*contextBody);
        regMemo = peekRegMemo(*contextBody);
        memMemo = peekMemMemo(*contextBody);
        portWriteMemo = &ensureWriteBackMemo(*contextBody);
    }

    class PortLHSConverter : public LHSConverter {
    public:
        using LHSConverter::LHSConverter;
        bool convert(const slang::ast::Expression& expr, ValueId rhsValue,
                     std::vector<WriteResult>& outResults) {
            return lowerExpression(expr, rhsValue, outResults);
        }
    };

    auto makePortValue = [&](const slang::ast::PortSymbol& port) -> ValueId {
        TypeHelper::Info info = TypeHelper::analyze(port.getType(), port, diagnostics_);
        std::string base =
            instanceName.empty() || port.name.empty()
                ? std::string(port.name.empty() ? "_port" : port.name)
                : sanitizeForGraphName(instanceName + "_" + std::string(port.name));
        if (base.empty()) {
            base = "_port";
        }
        std::string candidate = base;
        std::size_t suffix = 0;
        while (parentGraph.findValue(candidate) || parentGraph.findOperation(candidate)) {
            candidate = base;
            candidate.push_back('_');
            candidate.append(std::to_string(++suffix));
        }
        const int64_t width = info.width > 0 ? info.width : 1;
        ValueId value = createValue(parentGraph, candidate, width, info.isSigned);
        applyDebug(parentGraph, value, makeDebugInfo(sourceManager_, &port));
        return value;
    };

    std::vector<std::string> inputPortNames;
    std::vector<std::string> outputPortNames;
    std::vector<std::string> inoutPortNames;
    std::vector<ValueId> inputOperands;
    std::vector<ValueId> outputResults;
    std::vector<ValueId> inoutOutOperands;
    std::vector<ValueId> inoutOeOperands;
    std::vector<ValueId> inoutInResults;
    inputPortNames.reserve(memo.ports.size());
    outputPortNames.reserve(memo.ports.size());

    for (const BlackboxPort& portMeta : memo.ports) {
        const slang::ast::Symbol* symbol = childInstance.body.findPort(portMeta.name);
        const auto* port = symbol ? symbol->as_if<slang::ast::PortSymbol>() : nullptr;
        if (!port) {
            if (diagnostics_) {
                diagnostics_->nyi(childInstance, "Port lookup failed for blackbox connection");
            }
            continue;
        }

        const auto* connection = childInstance.getPortConnection(*port);
        if (!connection) {
            if (diagnostics_) {
                diagnostics_->nyi(*port, "Missing port connection during blackbox elaboration");
            }
            continue;
        }

        const slang::ast::Expression* expr = connection->getExpression();

        switch (portMeta.direction) {
        case slang::ast::ArgumentDirection::In: {
            if (!expr) {
                break;
            }
            ValueId value = resolveConnectionValue(*expr, parentGraph, port);
            if (!value) {
                break;
            }
            if (diagnostics_ && portMeta.width > 0 &&
                parentGraph.getValue(value).width() != portMeta.width) {
                std::ostringstream oss;
                oss << "Port width mismatch for " << portMeta.name << " (expected "
                    << portMeta.width << ", got " << parentGraph.getValue(value).width() << ")";
                diagnostics_->nyi(*port, oss.str());
            }
            inputOperands.push_back(value);
            inputPortNames.emplace_back(portMeta.name);
            break;
        }
        case slang::ast::ArgumentDirection::Out: {
            ValueId resolved = expr ? resolveConnectionValue(*expr, parentGraph, port)
                                    : ValueId::invalid();
            if (diagnostics_ && resolved && portMeta.width > 0 &&
                parentGraph.getValue(resolved).width() != portMeta.width) {
                std::ostringstream oss;
                oss << "Port width mismatch for " << portMeta.name << " (expected "
                    << portMeta.width << ", got " << parentGraph.getValue(resolved).width() << ")";
                diagnostics_->nyi(*port, oss.str());
            }
            const bool useDirect = resolved && !parentGraph.getValue(resolved).definingOp();
            ValueId resultValue = useDirect ? resolved : makePortValue(*port);
            if (!resultValue) {
                break;
            }
            outputResults.push_back(resultValue);
            outputPortNames.emplace_back(portMeta.name);

            if (!useDirect && expr && portWriteMemo) {
                const slang::ast::Expression* targetExpr = expr;
                if (targetExpr->kind == slang::ast::ExpressionKind::Assignment) {
                    const auto& assign = targetExpr->as<slang::ast::AssignmentExpression>();
                    if (assign.isLValueArg()) {
                        targetExpr = &assign.left();
                    }
                }
                if (targetExpr) {
                    LHSConverter::Context lhsContext{
                        .graph = &parentGraph,
                        .netMemo = netMemo,
                        .regMemo = regMemo,
                        .memMemo = memMemo,
                        .origin = port,
                        .diagnostics = diagnostics_,
                        .sourceManager = sourceManager_};
                    PortLHSConverter lhsConverter(lhsContext);
                    std::vector<LHSConverter::WriteResult> writeResults;
                    if (lhsConverter.convert(*targetExpr, resultValue, writeResults)) {
                        for (LHSConverter::WriteResult& result : writeResults) {
                            if (!result.target) {
                                continue;
                            }
                            portWriteMemo->recordWrite(*result.target,
                                                       WriteBackMemo::AssignmentKind::Continuous,
                                                       port, std::move(result.slices));
                        }
                    }
                }
            }
            break;
        }
        case slang::ast::ArgumentDirection::InOut: {
            if (!expr) {
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Missing inout port connection during blackbox elaboration");
                }
                break;
            }
            const slang::ast::Expression* targetExpr = expr;
            if (targetExpr->kind == slang::ast::ExpressionKind::Assignment) {
                const auto& assign = targetExpr->as<slang::ast::AssignmentExpression>();
                if (assign.isLValueArg()) {
                    targetExpr = &assign.left();
                }
            }
            if (targetExpr->kind == slang::ast::ExpressionKind::HierarchicalValue) {
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Hierarchical inout port connections are not supported");
                }
                break;
            }
            if (!targetExpr->as_if<slang::ast::NamedValueExpression>()) {
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Inout port connections must be simple named values");
                }
                break;
            }
            const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(*targetExpr);
            const InoutPortMemo* inoutMemo =
                (symbol && contextBody) ? findInoutMemo(*contextBody, *symbol) : nullptr;
            if (!inoutMemo) {
                if (diagnostics_) {
                    diagnostics_->nyi(*port, "Inout port connection lacks inout value triple");
                }
                break;
            }
            if (diagnostics_ && portMeta.width > 0 &&
                parentGraph.getValue(inoutMemo->out).width() != portMeta.width) {
                std::ostringstream oss;
                oss << "Port width mismatch for " << portMeta.name << " (expected "
                    << portMeta.width << ", got " << parentGraph.getValue(inoutMemo->out).width()
                    << ")";
                diagnostics_->nyi(*port, oss.str());
            }
            inoutOutOperands.push_back(inoutMemo->out);
            inoutOeOperands.push_back(inoutMemo->oe);
            inoutInResults.push_back(inoutMemo->in);
            inoutPortNames.emplace_back(portMeta.name);
            break;
        }
        default:
            if (diagnostics_) {
                diagnostics_->nyi(*port,
                                  "Ref port directions are not supported for blackbox");
            }
            break;
        }
    }

    for (ValueId operand : inputOperands) {
        addOperand(parentGraph, op, operand);
    }
    for (ValueId operand : inoutOutOperands) {
        addOperand(parentGraph, op, operand);
    }
    for (ValueId operand : inoutOeOperands) {
        addOperand(parentGraph, op, operand);
    }
    for (ValueId result : outputResults) {
        addResult(parentGraph, op, result);
    }
    for (ValueId result : inoutInResults) {
        addResult(parentGraph, op, result);
    }

    std::vector<std::string> parameterNames;
    std::vector<std::string> parameterValues;
    parameterNames.reserve(memo.parameters.size());
    parameterValues.reserve(memo.parameters.size());
    for (const BlackboxParameter& param : memo.parameters) {
        parameterNames.push_back(param.name);
        parameterValues.push_back(param.value);
    }

    setAttr(parentGraph, op, "moduleName", memo.moduleName);
    setAttr(parentGraph, op, "instanceName", instanceName);
    setAttr(parentGraph, op, "inputPortName", inputPortNames);
    setAttr(parentGraph, op, "outputPortName", outputPortNames);
    setAttr(parentGraph, op, "inoutPortName", inoutPortNames);
    setAttr(parentGraph, op, "parameterNames", parameterNames);
    setAttr(parentGraph, op, "parameterValues", parameterValues);
}

ValueId Elaborate::ensureValueForSymbol(const slang::ast::ValueSymbol& symbol,
                                            grh::ir::Graph& graph) {
    if (auto it = valueCache_.find(&symbol); it != valueCache_.end()) {
        const grh::ir::GraphId graphId = graph.id();
        for (const ValueId& cached : it->second) {
            if (cached.graph == graphId) {
                return cached;
            }
        }
    }

    const slang::ast::Type& type = symbol.getType();
    TypeHelper::Info info = TypeHelper::analyze(type, symbol, diagnostics_);
    std::string baseName = symbol.name.empty() ? std::string("_value") : std::string(symbol.name);
    std::string candidate = baseName;
    std::size_t attempt = 0;
    while (graph.findValue(candidate) || graph.findOperation(candidate)) {
        candidate = baseName;
        candidate.push_back('_');
        candidate.append(std::to_string(++attempt));
    }

    ValueId value = createValue(graph, candidate, info.width > 0 ? info.width : 1,
                                          info.isSigned);
    applyDebug(graph, value, makeDebugInfo(sourceManager_, &symbol));
    registerValueForSymbol(symbol, value);
    return value;
}

ValueId Elaborate::resolveConnectionValue(const slang::ast::Expression& expr,
                                              grh::ir::Graph& graph,
                                              const slang::ast::Symbol* origin) {
    const slang::ast::Expression* targetExpr = &expr;
    if (expr.kind == slang::ast::ExpressionKind::Assignment) {
        const auto& assign = expr.as<slang::ast::AssignmentExpression>();
        if (assign.isLValueArg()) {
            targetExpr = &assign.left();
        }
        else {
            if (diagnostics_ && origin) {
                diagnostics_->nyi(*origin, "Assignment port connections are not supported yet");
            }
            return ValueId::invalid();
        }
    }

    if (targetExpr->kind == slang::ast::ExpressionKind::HierarchicalValue) {
        if (diagnostics_ && origin) {
            diagnostics_->nyi(*origin, "Hierarchical port connections are not supported yet");
        }
        return ValueId::invalid();
    }

    const slang::ast::InstanceBodySymbol* contextBody = nullptr;
    for (const auto& [body, mappedGraph] : graphByBody_) {
        if (mappedGraph == &graph) {
            contextBody = body;
            break;
        }
    }

    std::span<const SignalMemoEntry> netMemo;
    std::span<const SignalMemoEntry> regMemo;
    std::span<const SignalMemoEntry> memMemo;
    if (contextBody) {
        netMemo = peekNetMemo(*contextBody);
        regMemo = peekRegMemo(*contextBody);
        memMemo = peekMemMemo(*contextBody);
    }

    RHSConverter::Context context{
        .graph = &graph,
        .netMemo = netMemo,
        .regMemo = regMemo,
        .memMemo = memMemo,
        .origin = origin,
        .diagnostics = diagnostics_,
        .sourceManager = sourceManager_};
    CombRHSConverter converter(context);
    if (ValueId value = converter.convert(*targetExpr)) {
        return value;
    }

    if (targetExpr->kind == slang::ast::ExpressionKind::NamedValue) {
        const auto& named = targetExpr->as<slang::ast::NamedValueExpression>();
        return ensureValueForSymbol(named.symbol, graph);
    }

    return ValueId::invalid();
}

std::string Elaborate::makeUniqueOperationName(grh::ir::Graph& graph, std::string baseName) {
    if (baseName.empty()) {
        baseName = "_inst";
    }

    std::string candidate = baseName;
    std::size_t suffix = 0;
    while (graph.findOperation(candidate) || graph.findValue(candidate)) {
        candidate = baseName;
        candidate.push_back('_');
        candidate.append(std::to_string(++suffix));
    }
    return candidate;
}

void Elaborate::registerValueForSymbol(const slang::ast::Symbol& symbol, ValueId value) {
    auto& bucket = valueCache_[&symbol];
    for (ValueId& cached : bucket) {
        if (cached.graph == value.graph) {
            cached = value;
            return;
        }
    }
    bucket.push_back(value);
}

void Elaborate::materializeSignalMemos(const slang::ast::InstanceBodySymbol& body,
                                       grh::ir::Graph& graph) {
    ensureNetValues(body, graph);
    ensureRegState(body, graph);
    ensureMemState(body, graph);
}

void Elaborate::ensureNetValues(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph) {
    auto it = netMemo_.find(&body);
    if (it == netMemo_.end()) {
        return;
    }

    for (SignalMemoEntry& entry : it->second) {
        if (!entry.symbol) {
            continue;
        }
        ValueId value = ensureValueForSymbol(*entry.symbol, graph);
        entry.value = value;
    }
}

void Elaborate::ensureRegState(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph) {
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
        // 跳过 memory（非标量寄存器）的 reg 视图，在 ensureMemState 中生成其 kMemory 占位符。
        if (deriveMemoryLayout(*entry.type, *entry.symbol, diagnostics_)) {
            continue;
        }

        ValueId value = ensureValueForSymbol(*entry.symbol, graph);
        entry.value = value;
        if (!value) {
            continue;
        }

        if (entry.drivingBlock &&
            entry.drivingBlock->procedureKind == slang::ast::ProceduralBlockKind::AlwaysLatch) {
            // latch 在组合流程中处理，这里不预先生成寄存器占位
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

        auto makeRstPolarity = [](bool activeHigh) {
            return activeHigh ? std::string("high") : std::string("low");
        };

        grh::ir::OperationKind opKind = grh::ir::OperationKind::kRegister;
        std::optional<std::string> rstPolarity;
        if (asyncInfo) {
            entry.asyncResetExpr = asyncInfo->expr;
            entry.asyncResetEdge = asyncInfo->edge;
            if (entry.asyncResetExpr && entry.asyncResetEdge != slang::ast::EdgeKind::None &&
                entry.asyncResetEdge != slang::ast::EdgeKind::BothEdges) {
                const bool activeHigh = entry.asyncResetEdge == slang::ast::EdgeKind::PosEdge;
                rstPolarity = makeRstPolarity(activeHigh);
                opKind = grh::ir::OperationKind::kRegisterArst;
            }
            else if (diagnostics_) {
                diagnostics_->nyi(*entry.symbol,
                                  "Async reset edge kind is not supported for this register");
            }
        }
        else if (syncInfo && syncInfo->symbol) {
            entry.syncResetSymbol = syncInfo->symbol;
            entry.syncResetActiveHigh = syncInfo->activeHigh;
            rstPolarity = makeRstPolarity(syncInfo->activeHigh);
            opKind = grh::ir::OperationKind::kRegisterRst;
        }

        if (entry.multiDriver) {
            // Leave stateOp unbound; multi-driver signals will be split per driving block later.
            continue;
        }

        std::string opName = makeOperationNameForSymbol(*entry.symbol, "register", graph);
        OperationId op = createOperation(graph, opKind, opName);
        applyDebug(graph, op, makeDebugInfo(sourceManager_, entry.symbol));
        addResult(graph, op, value);
        setAttr(graph, op, "clkPolarity", *clkPolarity);
        if (rstPolarity) {
            setAttr(graph, op, "rstPolarity", *rstPolarity);
        }
        entry.stateOp = op;
    }
}

void Elaborate::ensureMemState(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph) {
    auto it = memMemo_.find(&body);
    if (it == memMemo_.end()) {
        return;
    }
    for (SignalMemoEntry& entry : it->second) {
        if (!entry.symbol || !entry.type) {
            continue;
        }
        if (entry.stateOp) {
            continue;
        }
        if (const auto layout = deriveMemoryLayout(*entry.type, *entry.symbol, diagnostics_)) {
            std::string opName = makeOperationNameForSymbol(*entry.symbol, "memory", graph);
            OperationId op = createOperation(graph, grh::ir::OperationKind::kMemory, opName);
            applyDebug(graph, op, makeDebugInfo(sourceManager_, entry.symbol));
            setAttr(graph, op, "width", layout->rowWidth);
            setAttr(graph, op, "row", layout->rowCount);
            setAttr(graph, op, "isSigned", layout->isSigned);
            entry.stateOp = op;
            // 将同名 symbol 在 regMemo 里的视图一并更新 stateOp，方便测试从 regMemo 访问到 memory。
            if (auto regIt = regMemo_.find(&body); regIt != regMemo_.end()) {
                for (SignalMemoEntry& regEntry : regIt->second) {
                    if (regEntry.symbol == entry.symbol) {
                        regEntry.stateOp = op;
                    }
                }
            }
            continue;
        }
        // Not a memory after all; keep as is.
    }
}
WriteBackMemo& Elaborate::ensureWriteBackMemo(const slang::ast::InstanceBodySymbol& body) {
    auto [it, inserted] = writeBackMemo_.try_emplace(&body);
    it->second.setSourceManager(sourceManager_);
    return it->second;
}

void Elaborate::finalizeWriteBackMemo(const slang::ast::InstanceBodySymbol& body,
                                      grh::ir::Graph& graph) {
    auto it = writeBackMemo_.find(&body);
    if (it == writeBackMemo_.end()) {
        return;
    }
    it->second.finalize(graph, diagnostics_);
}

const BlackboxMemoEntry*
Elaborate::ensureBlackboxMemo(const slang::ast::InstanceBodySymbol& body) {
    if (auto it = blackboxMemo_.find(&body); it != blackboxMemo_.end()) {
        return &it->second;
    }

    const slang::ast::InstanceBodySymbol* keyBody = &body;
    if (body.parentInstance) {
        if (const auto* canonical = body.parentInstance->getCanonicalBody()) {
            keyBody = canonical;
            if (auto it = blackboxMemo_.find(keyBody); it != blackboxMemo_.end()) {
                auto [aliasIt, _] = blackboxMemo_.emplace(&body, it->second);
                return &aliasIt->second;
            }
        }
    }

    BlackboxMemoEntry entry;
    entry.body = keyBody;
    entry.moduleName = body.getDefinition().name.empty()
                           ? deriveSymbolPath(body.getDefinition())
                           : std::string(body.getDefinition().name);
    if (entry.moduleName.empty()) {
        entry.moduleName = "_anonymous_module";
    }

    entry.hasExplicitAttribute = hasBlackboxAttribute(body);
    entry.hasImplementation = hasBlackboxImplementation(body);
    if (entry.hasExplicitAttribute && entry.hasImplementation && diagnostics_) {
        diagnostics_->nyi(body.getDefinition(),
                          "Module marked as blackbox but contains implementation; treating as "
                          "normal module body");
    }
    entry.isBlackbox = (entry.hasExplicitAttribute || !entry.hasImplementation) &&
                       !entry.hasImplementation;

    if (entry.isBlackbox) {
        entry.ports.reserve(body.getPortList().size());
        for (const slang::ast::Symbol* portSymbol : body.getPortList()) {
            if (!portSymbol) {
                continue;
            }

            if (const auto* port = portSymbol->as_if<slang::ast::PortSymbol>()) {
                if (port->isNullPort || port->name.empty()) {
                    handleUnsupportedPort(*port,
                                          port->isNullPort ? "null ports are not supported"
                                                           : "anonymous ports are not supported",
                                          diagnostics_);
                    continue;
                }

                const slang::ast::Type& type = port->getType();
                TypeHelper::Info info = TypeHelper::analyze(type, *port, diagnostics_);
                BlackboxPort memoPort;
                memoPort.symbol = port;
                memoPort.name = port->name;
                memoPort.direction = port->direction;
                memoPort.width = info.width > 0 ? info.width : 1;
                memoPort.isSigned = info.isSigned;

                switch (port->direction) {
                case slang::ast::ArgumentDirection::In:
                case slang::ast::ArgumentDirection::Out:
                case slang::ast::ArgumentDirection::InOut:
                    entry.ports.push_back(std::move(memoPort));
                    break;
                case slang::ast::ArgumentDirection::Ref:
                    handleUnsupportedPort(
                        *port,
                        std::string("direction ") + std::string(slang::ast::toString(port->direction)),
                        diagnostics_);
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

        for (const slang::ast::ParameterSymbolBase* paramBase : body.getParameters()) {
            if (!paramBase) {
                continue;
            }

            BlackboxParameter param;
            param.name = paramBase->symbol.name.empty() ? std::string() : std::string(paramBase->symbol.name);
            if (param.name.empty()) {
                continue;
            }

            if (const auto* valueParam = paramBase->symbol.as_if<slang::ast::ParameterSymbol>()) {
                param.value = parameterValueToString(valueParam->getValue());
            }
            else if (const auto* typeParam =
                         paramBase->symbol.as_if<slang::ast::TypeParameterSymbol>()) {
                param.value = typeParameterToString(*typeParam);
            }
            else {
                param.value = "unsupported_param";
            }

            entry.parameters.push_back(std::move(param));
        }
    }

    auto [it, inserted] = blackboxMemo_.emplace(keyBody, std::move(entry));
    if (keyBody != &body) {
        blackboxMemo_.emplace(&body, it->second);
        auto aliasIt = blackboxMemo_.find(&body);
        return aliasIt != blackboxMemo_.end() ? &aliasIt->second : nullptr;
    }
    return &it->second;
}

std::string Elaborate::makeOperationNameForSymbol(const slang::ast::ValueSymbol& symbol,
                                                  std::string_view fallback, grh::ir::Graph& graph) {
    if (!symbol.name.empty()) {
        std::string symbolName(symbol.name);
        if (!graph.findOperation(symbolName) && !graph.findValue(symbolName)) {
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
    std::unordered_map<const slang::ast::SubroutineSymbol*, const DpiImportEntry*> dpiLookup;
    if (auto it = dpiImports_.find(&body); it != dpiImports_.end()) {
        for (const DpiImportEntry& entry : it->second) {
            if (entry.symbol) {
                dpiLookup.emplace(entry.symbol, &entry);
            }
        }
    }

    auto registerCandidate = [&](const slang::ast::ValueSymbol& symbol) {
        const slang::ast::Type& type = symbol.getType();
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
        memMemo_[&body].clear();
        return;
    }

    std::unordered_map<const slang::ast::ValueSymbol*, MemoDriverKind> driverKinds;
    driverKinds.reserve(candidates.size());
    std::unordered_map<const slang::ast::ValueSymbol*,
                       std::vector<const slang::ast::ProceduralBlockSymbol*>>
        regDriverBlocks;
    regDriverBlocks.reserve(candidates.size());
    std::unordered_map<const slang::ast::ValueSymbol*,
                       std::vector<const slang::ast::ProceduralBlockSymbol*>>
        netDriverBlocks;
    netDriverBlocks.reserve(candidates.size());

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
        if (block) {
            auto& owners = driver == MemoDriverKind::Reg
                               ? regDriverBlocks[&symbol]
                               : netDriverBlocks[&symbol];
            if (std::find(owners.begin(), owners.end(), block) == owners.end()) {
                owners.push_back(block);
            }
        }
        if (!hasDriver(state, driver)) {
            state |= driver;
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
            if (block->procedureKind == slang::ast::ProceduralBlockKind::Initial) {
                continue;
            }
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

        if (!dpiLookup.empty()) {
            auto onDpiCall = [&](const slang::ast::CallExpression& call) {
                const auto* subroutine =
                    std::get_if<const slang::ast::SubroutineSymbol*>(&call.subroutine);
                if (!subroutine) {
                    return;
                }
                auto mapIt = dpiLookup.find(*subroutine);
                if (mapIt == dpiLookup.end()) {
                    return;
                }
                const DpiImportEntry& entry = *mapIt->second;
                auto args = call.arguments();
                for (std::size_t idx = 0; idx < entry.args.size(); ++idx) {
                    const DpiImportArg& argInfo = entry.args[idx];
                    if (argInfo.direction != slang::ast::ArgumentDirection::Out &&
                        argInfo.direction != slang::ast::ArgumentDirection::InOut) {
                        continue;
                    }
                    if (idx >= args.size() || !args[idx]) {
                        continue;
                    }
                    collectAssignedSymbols(
                        *args[idx], [&](const slang::ast::ValueSymbol& symbol) {
                            markDriver(symbol, MemoDriverKind::Reg, block);
                        });
                }
            };
            DpiCallCollector dpiCollector(dpiLookup, onDpiCall);
            block->getBody().visit(dpiCollector);
        }
        continue;
    }
    }

    std::vector<SignalMemoEntry> nets;
    std::vector<SignalMemoEntry> regs;
    std::vector<SignalMemoEntry> mems;
    nets.reserve(candidates.size());
    regs.reserve(candidates.size());
    mems.reserve(candidates.size());

    for (auto& [symbol, entry] : candidates) {
        MemoDriverKind driver = MemoDriverKind::None;
        if (auto it = driverKinds.find(symbol); it != driverKinds.end()) {
            driver = it->second;
        }

        const bool isNetSymbol = symbol && symbol->kind == slang::ast::SymbolKind::Net;
        const bool isVarSymbol = symbol && symbol->kind == slang::ast::SymbolKind::Variable;
        const bool netOnly = hasDriver(driver, MemoDriverKind::Net) &&
                             !hasDriver(driver, MemoDriverKind::Reg);
        const bool regOnly = hasDriver(driver, MemoDriverKind::Reg) &&
                             !hasDriver(driver, MemoDriverKind::Net);

        // Unpacked arrays recognized as memory仅在“纯时序驱动”的情况下进入 memMemo；
        // 组合驱动的 unpacked array 仍按 net 扁平化处理。
        if (regOnly) {
            if (deriveMemoryLayout(*entry.type, *entry.symbol, diagnostics_)) {
                mems.push_back(entry);
                continue;
            }
        }

        // Wires with an initializer may not be recorded as having drivers; still treat them as nets
        // so RHS conversion can resolve the NamedValue.
        const bool treatAsDriverlessNet = isNetSymbol && !hasDriver(driver, MemoDriverKind::Reg);
        const bool treatAsDriverlessVar = isVarSymbol && driver == MemoDriverKind::None;

        if (netOnly || treatAsDriverlessNet || treatAsDriverlessVar) {
            if (auto it = netDriverBlocks.find(symbol); it != netDriverBlocks.end()) {
                entry.multiDriver = it->second.size() > 1;
            }
            nets.push_back(entry);
        }
        else if (regOnly) {
            if (auto blockIt = regDriverBlocks.find(symbol); blockIt != regDriverBlocks.end() &&
                !blockIt->second.empty()) {
                const bool multi = blockIt->second.size() > 1;
                for (const auto* driverBlock : blockIt->second) {
                    SignalMemoEntry copy = entry;
                    copy.drivingBlock = driverBlock;
                    copy.multiDriver = multi;
                    regs.push_back(std::move(copy));
                }
            }
            else {
                regs.push_back(entry);
            }
        }
    }

    auto byName = [](const SignalMemoEntry& lhs, const SignalMemoEntry& rhs) {
        return lhs.symbol->name < rhs.symbol->name;
    };
    std::sort(nets.begin(), nets.end(), byName);
    std::sort(regs.begin(), regs.end(), byName);
    std::sort(mems.begin(), mems.end(), byName);
    // 让 memory 也出现在 regMemo 视图中（stateOp 为 kMemory，由 ensureMemState 填充）。
    // 这样下游测试可以在 regMemo 中查到 "mem" 实体，同时仍保留 memMemo 供 memory 专用处理。
    regs.insert(regs.end(), mems.begin(), mems.end());
    netMemo_[&body] = std::move(nets);
    regMemo_[&body] = std::move(regs);
    memMemo_[&body] = std::move(mems);
}

void Elaborate::collectDpiImports(const slang::ast::InstanceBodySymbol& body) {
    std::vector<DpiImportEntry> imports;
    imports.reserve(4);

    auto report = [&](const slang::ast::Symbol& symbol, std::string_view message) {
        if (diagnostics_) {
            diagnostics_->nyi(symbol, std::string(message));
        }
    };

    for (const slang::ast::Symbol& member : body.members()) {
        const auto* subroutine = member.as_if<slang::ast::SubroutineSymbol>();
        if (!subroutine || !subroutine->flags.has(slang::ast::MethodFlags::DPIImport)) {
            continue;
        }

        bool valid = true;
        if (subroutine->subroutineKind != slang::ast::SubroutineKind::Function) {
            report(*subroutine, "仅支持 import \"DPI-C\" function");
            valid = false;
        }

        if (!subroutine->getReturnType().isVoid()) {
            report(*subroutine, "DPI import 必须返回 void");
            valid = false;
        }

        if (subroutine->flags.has(slang::ast::MethodFlags::DPIContext) ||
            subroutine->flags.has(slang::ast::MethodFlags::Pure)) {
            report(*subroutine, "DPI import context/pure 属性暂不支持");
            valid = false;
        }

        DpiImportEntry entry;
        entry.symbol = subroutine;

        if (valid) {
            auto args = subroutine->getArguments();
            entry.args.reserve(args.size());
            std::size_t index = 0;
            for (const slang::ast::FormalArgumentSymbol* arg : args) {
                if (!arg) {
                    ++index;
                    continue;
                }
                if (arg->direction != slang::ast::ArgumentDirection::In &&
                    arg->direction != slang::ast::ArgumentDirection::Out &&
                    arg->direction != slang::ast::ArgumentDirection::InOut) {
                    report(*arg, "DPI import 仅支持 input/output/inout 方向参数");
                    valid = false;
                    break;
                }
                const slang::ast::Type& type = arg->getType();
                TypeHelper::Info info = TypeHelper::analyze(type, *arg, diagnostics_);
                if (!info.widthKnown) {
                    report(*arg, "DPI 参数必须是固定宽度类型");
                    valid = false;
                    break;
                }
                DpiImportArg argInfo;
                argInfo.name =
                    arg->name.empty() ? (std::string("arg") + std::to_string(index))
                                      : std::string(arg->name);
                argInfo.direction = arg->direction;
                argInfo.width = info.width > 0 ? info.width : 1;
                argInfo.isSigned = info.isSigned;
                if (info.fields.empty()) {
                    argInfo.fields.push_back(SignalMemoField{
                        argInfo.name, argInfo.width > 0 ? argInfo.width - 1 : 0, 0,
                        argInfo.isSigned});
                }
                else {
                    for (const auto& field : info.fields) {
                        argInfo.fields.push_back(SignalMemoField{field.path, field.msb, field.lsb,
                                                                 field.isSigned});
                    }
                }
                entry.args.push_back(std::move(argInfo));
                ++index;
            }
        }

        if (valid) {
            imports.push_back(std::move(entry));
        }
    }

    auto byName = [](const DpiImportEntry& lhs, const DpiImportEntry& rhs) {
        std::string_view l = lhs.symbol ? lhs.symbol->name : std::string_view{};
        std::string_view r = rhs.symbol ? rhs.symbol->name : std::string_view{};
        return l < r;
    };

    std::sort(imports.begin(), imports.end(), byName);
    dpiImports_[&body] = std::move(imports);
}

void Elaborate::materializeDpiImports(const slang::ast::InstanceBodySymbol& body,
                                      grh::ir::Graph& graph) {
    auto it = dpiImports_.find(&body);
    if (it == dpiImports_.end()) {
        return;
    }

    for (DpiImportEntry& entry : it->second) {
        if (!entry.symbol || entry.importOp) {
            continue;
        }
        std::string baseName;
        if (entry.symbol && !entry.symbol->name.empty()) {
            baseName = sanitizeForGraphName(entry.symbol->name);
        }
        if (baseName.empty()) {
            baseName = "dpic_import";
        }
        std::string opName = makeUniqueOperationName(graph, baseName);
        OperationId op = createOperation(graph, grh::ir::OperationKind::kDpicImport, opName);
        applyDebug(graph, op, makeDebugInfo(sourceManager_, entry.symbol));
        entry.importOp = op;

        std::vector<std::string> directions;
        std::vector<int64_t> widths;
        std::vector<std::string> names;
        directions.reserve(entry.args.size());
        widths.reserve(entry.args.size());
        names.reserve(entry.args.size());

        for (const DpiImportArg& arg : entry.args) {
            if (arg.direction == slang::ast::ArgumentDirection::In) {
                directions.emplace_back("input");
            }
            else if (arg.direction == slang::ast::ArgumentDirection::Out) {
                directions.emplace_back("output");
            }
            else {
                directions.emplace_back("inout");
            }
            widths.push_back(arg.width);
            names.push_back(arg.name);
        }

        setAttr(graph, op, "argsDirection", std::move(directions));
        setAttr(graph, op, "argsWidth", std::move(widths));
        setAttr(graph, op, "argsName", std::move(names));
        if (!entry.cIdentifier.empty()) {
            setAttr(graph, op, "cIdentifier", entry.cIdentifier);
        }
    }
}

} // namespace wolf_sv_parser
