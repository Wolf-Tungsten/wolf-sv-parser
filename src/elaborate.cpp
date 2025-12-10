#include "elaborate.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
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

std::optional<grh::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      slang::SourceLocation start,
                                      slang::SourceLocation end = {}) {
    if (!sourceManager || !start.valid()) {
        return std::nullopt;
    }
    const auto original = sourceManager->getFullyOriginalLoc(start);
    if (!original.valid() || !sourceManager->isFileLoc(original)) {
        return std::nullopt;
    }
    grh::SrcLoc info;
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

std::optional<grh::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      const slang::ast::Symbol* symbol) {
    if (!symbol) {
        return std::nullopt;
    }
    return makeSrcLoc(sourceManager, symbol->location, symbol->location);
}

std::optional<grh::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      const slang::ast::Expression* expr) {
    if (!expr) {
        return std::nullopt;
    }
    return makeSrcLoc(sourceManager, expr->sourceRange.start(), expr->sourceRange.end());
}

std::optional<grh::SrcLoc> makeSrcLoc(const slang::SourceManager* sourceManager,
                                      const slang::ast::Statement* stmt) {
    if (!stmt) {
        return std::nullopt;
    }
    return makeSrcLoc(sourceManager, stmt->sourceRange.start(), stmt->sourceRange.end());
}

void applySrcLoc(grh::Value& value, const std::optional<grh::SrcLoc>& info) {
    if (info) {
        value.setSrcLoc(*info);
    }
}

void applySrcLoc(grh::Operation& op, const std::optional<grh::SrcLoc>& info) {
    if (info) {
        op.setSrcLoc(*info);
    }
}

// Backward-compat helper names.
inline std::optional<grh::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                slang::SourceLocation start,
                                                slang::SourceLocation end = {}) {
    return makeSrcLoc(sm, start, end);
}
inline std::optional<grh::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                const slang::ast::Symbol* sym) {
    return makeSrcLoc(sm, sym);
}
inline std::optional<grh::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                const slang::ast::Expression* expr) {
    return makeSrcLoc(sm, expr);
}
inline std::optional<grh::SrcLoc> makeDebugInfo(const slang::SourceManager* sm,
                                                const slang::ast::Statement* stmt) {
    return makeSrcLoc(sm, stmt);
}
inline void applyDebug(grh::Value& value, const std::optional<grh::SrcLoc>& info) {
    applySrcLoc(value, info);
}
inline void applyDebug(grh::Operation& op, const std::optional<grh::SrcLoc>& info) {
    applySrcLoc(op, info);
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
    memMemo_(context.memMemo),
    origin_(context.origin),
    diagnostics_(context.diagnostics),
    sourceManager_(context.sourceManager),
    preferredBlock_(context.preferredBlock) {
    instanceId_ = nextConverterInstanceId();
}

bool LHSConverter::lower(const slang::ast::AssignmentExpression& assignment, grh::Value& rhsValue,
                         std::vector<LHSConverter::WriteResult>& outResults) {
    return lowerExpression(assignment.left(), rhsValue, outResults);
}

bool LHSConverter::lowerExpression(const slang::ast::Expression& expr, grh::Value& rhsValue,
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
    if (rhsValue.width() != lhsWidth) {
        std::ostringstream oss;
        oss << "Assign width mismatch; lhs=" << lhsWidth << " rhs=" << rhsValue.width();
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

grh::Value* LHSConverter::createSliceValue(grh::Value& source, int64_t lsb, int64_t msb,
                                           const slang::ast::Expression& originExpr) {
    if (lsb == 0 && msb == source.width() - 1) {
        return &source;
    }
    if (lsb < 0 || msb < lsb || msb >= source.width()) {
        report("Assign RHS slice range is out of bounds");
        return nullptr;
    }

    std::string opName = "_assign_slice_op_" + std::to_string(instanceId_) + "_" +
                         std::to_string(sliceCounter_);
    std::string valueName = "_assign_slice_val_" + std::to_string(instanceId_) + "_" +
                            std::to_string(sliceCounter_);
    ++sliceCounter_;

    grh::Operation& op = graph().createOperation(grh::OperationKind::kSliceStatic, opName);
    applyDebug(op, makeDebugInfo(sourceManager_, &originExpr));
    op.addOperand(source);
    op.setAttribute("sliceStart", lsb);
    op.setAttribute("sliceEnd", msb);

    const int64_t width = msb - lsb + 1;
    grh::Value& value = graph().createValue(
        valueName, width, originExpr.type ? originExpr.type->isSigned() : false);
    applyDebug(value, makeDebugInfo(sourceManager_, &originExpr));
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
    if (const auto* symbol = expr.symbol.as_if<slang::ast::ValueSymbol>()) {
        if (grh::Value* loop = owner_.lookupLoopValue(*symbol)) {
            return loop;
        }
    }
    return CombRHSConverter::handleMemoEntry(expr, entry);
}

void AlwaysBlockLHSConverter::seedEvalContextForLHS(slang::ast::EvalContext& ctx) {
    owner_.seedEvalContextWithLoopValues(ctx);
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
    auto* seqOwner = static_cast<SeqAlwaysConverter*>(&owner_);
    if (seqOwner && seqOwner->useSeqShadowValues()) {
        if (grh::Value* shadow = owner_.lookupShadowValue(entry)) {
            return shadow;
        }
    }
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
                // Use current guard as enable for sync read if available.
                grh::Value* en = owner_.currentGuardValue();
                return seqOwner->buildMemorySyncRead(*entry, *addrValue, expr, en);
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

bool AlwaysBlockLHSConverter::convertExpression(const slang::ast::Expression& expr,
                                                grh::Value& rhsValue) {
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
        grh::Value* currentEn = owner_.currentGuardValue();
        const int64_t entryWidth = seqOwner->memoryRowWidth(*memoryEntry);

        if (rootIsBase) {
            if (rhsValue.width() != entryWidth) {
                emitUnsupported("Memory word write width mismatch");
                return true;
            }
            seqOwner->recordMemoryWordWrite(*memoryEntry, assignment, *addrValue, rhsValue,
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

        if (rhsValue.width() != 1) {
            emitUnsupported("Memory single-bit write expects 1-bit RHS");
            return true;
        }

        grh::Value* bitIndexValue = owner_.rhsConverter_->convert(bitSelect->selector());
        if (!bitIndexValue) {
            return true;
        }

        seqOwner->recordMemoryBitWrite(*memoryEntry, assignment, *addrValue, *bitIndexValue,
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
                                              grh::Value& rhsValue) {
    const slang::ast::Expression* root = skipImplicitConversions(expr);
    const slang::ast::Expression* cursor = root;
    while (cursor) {
        if (const auto* element = cursor->as_if<slang::ast::ElementSelectExpression>()) {
            const slang::ast::Expression* inner = skipImplicitConversions(element->value());
            if (const auto* named = inner->as_if<slang::ast::NamedValueExpression>()) {
                if (const auto* symbol = named->symbol.as_if<slang::ast::ValueSymbol>()) {
                    const SignalMemoEntry* candidate = findMemoEntry(*symbol);
                    if (candidate && candidate->stateOp &&
                        candidate->stateOp->kind() == grh::OperationKind::kMemory) {
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
    const slang::ast::ElementSelectExpression& element, grh::Value& rhsValue) {
    auto* seqOwner = static_cast<SeqAlwaysConverter*>(&owner_);
    if (!seqOwner) {
        return false;
    }

    const slang::ast::ValueSymbol* symbol = resolveAssignedSymbol(element);
    const SignalMemoEntry* entry = symbol ? findMemoEntry(*symbol) : nullptr;
    if (!entry || (entry->stateOp && entry->stateOp->kind() == grh::OperationKind::kMemory)) {
        return false;
    }

    if (!owner_.rhsConverter_) {
        if (diagnostics()) {
            diagnostics()->nyi(owner_.block(), "Dynamic LHS index requires RHS converter context");
        }
        return true;
    }

    grh::Value* indexValue = owner_.rhsConverter_->convert(element.selector());
    if (!indexValue) {
        return true;
    }

    const int64_t targetWidth = entry->width > 0 ? entry->width : 1;
    if (rhsValue.width() <= 0 || rhsValue.width() > targetWidth) {
        if (diagnostics()) {
            diagnostics()->nyi(owner_.block(), "Dynamic bit select RHS width mismatch");
        }
        return true;
    }

    grh::Value* baseValue = owner_.lookupShadowValue(*entry);
    if (!baseValue) {
        baseValue = entry->value;
    }
    if (!baseValue) {
        baseValue = owner_.createZeroValue(targetWidth);
    }
    if (!baseValue) {
        return true;
    }

    grh::Value* maskValue =
        seqOwner->buildShiftedMask(*indexValue, targetWidth, "lhs_dyn_mask");
    if (!maskValue) {
        return true;
    }

    const auto debugInfo = makeDebugInfo(owner_.sourceManager_, &element);
    grh::Operation& invMaskOp = owner_.graph_.createOperation(
        grh::OperationKind::kNot, owner_.makeControlOpName("lhs_dyn_inv_mask"));
    applyDebug(invMaskOp, debugInfo);
    invMaskOp.addOperand(*maskValue);
    grh::Value& invMaskVal = owner_.graph_.createValue(
        owner_.makeControlValueName("lhs_dyn_inv_mask"), targetWidth, /*isSigned=*/false);
    applyDebug(invMaskVal, debugInfo);
    invMaskOp.addResult(invMaskVal);

    grh::Operation& holdOp = owner_.graph_.createOperation(
        grh::OperationKind::kAnd, owner_.makeControlOpName("lhs_dyn_hold"));
    applyDebug(holdOp, debugInfo);
    holdOp.addOperand(*baseValue);
    holdOp.addOperand(invMaskVal);
    grh::Value& holdVal = owner_.graph_.createValue(
        owner_.makeControlValueName("lhs_dyn_hold"), targetWidth, entry->isSigned);
    applyDebug(holdVal, debugInfo);
    holdOp.addResult(holdVal);

    const int64_t padWidth = targetWidth - rhsValue.width();
    grh::Value* paddedRhs = rhsValue.width() == targetWidth
                                ? &rhsValue
                                : seqOwner->createConcatWithZeroPadding(rhsValue, padWidth,
                                                                        "lhs_dyn_rhs_pad");
    if (!paddedRhs) {
        return true;
    }

    grh::Value* shiftedData =
        seqOwner->buildShiftedBitValue(*paddedRhs, *indexValue, targetWidth, "lhs_dyn_data");
    if (!shiftedData) {
        return true;
    }

    grh::Operation& mergeOp = owner_.graph_.createOperation(
        grh::OperationKind::kOr, owner_.makeControlOpName("lhs_dyn_merge"));
    applyDebug(mergeOp, debugInfo);
    mergeOp.addOperand(holdVal);
    mergeOp.addOperand(*shiftedData);
    grh::Value& mergedVal = owner_.graph_.createValue(
        owner_.makeControlValueName("lhs_dyn_merge"), targetWidth, entry->isSigned);
    applyDebug(mergedVal, debugInfo);
    mergeOp.addResult(mergedVal);

    grh::Value* finalVal = &mergedVal;
    if (grh::Value* guard = owner_.currentGuardValue()) {
        grh::Value* guardBit = owner_.coerceToCondition(*guard);
        if (guardBit) {
            grh::Operation& muxOp = owner_.graph_.createOperation(
                grh::OperationKind::kMux, owner_.makeControlOpName("lhs_dyn_guard"));
            applyDebug(muxOp, debugInfo);
            muxOp.addOperand(*guardBit);
            muxOp.addOperand(mergedVal);
            muxOp.addOperand(*baseValue);
            grh::Value& muxVal = owner_.graph_.createValue(
                owner_.makeControlValueName("lhs_dyn_guard"), targetWidth, entry->isSigned);
            applyDebug(muxVal, debugInfo);
            muxOp.addResult(muxVal);
            finalVal = &muxVal;
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

AlwaysConverter::AlwaysConverter(grh::Graph& graph, std::span<const SignalMemoEntry> netMemo,
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

CombAlwaysConverter::CombAlwaysConverter(grh::Graph& graph,
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

SeqAlwaysConverter::SeqAlwaysConverter(grh::Graph& graph,
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
    return seenBlockingAssignments_ && !seenNonBlockingAssignments_;
}

bool SeqAlwaysConverter::handleDisplaySystemTask(const slang::ast::CallExpression& call,
                                                 const slang::ast::ExpressionStatement&) {
    grh::Value* clkValue = ensureClockValue();
    if (!clkValue) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential display lacks resolved clock");
        }
        return true;
    }

    grh::Value* enableValue = nullptr;
    if (grh::Value* guard = currentGuardValue()) {
        enableValue = coerceToCondition(*guard);
    }
    else {
        enableValue = createOneValue(1);
    }
    if (!enableValue) {
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

    std::vector<grh::Value*> valueOperands;
    valueOperands.reserve(valueExprs.size());
    for (const slang::ast::Expression* expr : valueExprs) {
        grh::Value* value = rhsConverter_ ? rhsConverter_->convert(*expr) : nullptr;
        if (!value) {
            return true;
        }
        valueOperands.push_back(value);
    }

    grh::Operation& op =
        graph().createOperation(grh::OperationKind::kDisplay, makeControlOpName("display"));
    applyDebug(op, makeDebugInfo(sourceManager_, &call));
    op.addOperand(*clkValue);
    op.addOperand(*enableValue);
    for (grh::Value* operand : valueOperands) {
        op.addOperand(*operand);
    }
    if (clockPolarityAttr_) {
        op.setAttribute("clkPolarity", *clockPolarityAttr_);
    }
    op.setAttribute("formatString", formatString);
    op.setAttribute("displayKind", normalizeDisplayKind(call.getSubroutineName()));
    return true;
}

bool SeqAlwaysConverter::handleDpiCall(const slang::ast::CallExpression& call,
                                       const DpiImportEntry& entry,
                                       const slang::ast::ExpressionStatement&) {
    grh::Value* clkValue = ensureClockValue();
    if (!clkValue) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential DPI call lacks resolved clock");
        }
        return true;
    }

    grh::Value* enableValue = nullptr;
    if (grh::Value* guard = currentGuardValue()) {
        enableValue = coerceToCondition(*guard);
    }
    else {
        enableValue = createOneValue(1);
    }
    if (!enableValue) {
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

    std::vector<grh::Value*> inputOperands;
    std::vector<std::string> inputNames;
    std::vector<grh::Value*> outputValues;
    std::vector<std::string> outputNames;
    inputOperands.reserve(entry.args.size());
    inputNames.reserve(entry.args.size());
    outputValues.reserve(entry.args.size());
    outputNames.reserve(entry.args.size());

    for (std::size_t idx = 0; idx < entry.args.size(); ++idx) {
        const DpiImportArg& argInfo = entry.args[idx];
        const slang::ast::Expression* actual = args[idx];
        if (!actual) {
            if (diagnostics()) {
                diagnostics()->nyi(block(), "DPI 调用参数缺失表达式");
            }
            return true;
        }
        if (argInfo.direction == slang::ast::ArgumentDirection::In) {
            grh::Value* value = rhsConverter_->convert(*actual);
            if (!value) {
                return true;
            }
            if (value->width() != argInfo.width) {
                if (diagnostics()) {
                    std::ostringstream oss;
                    oss << "DPI input arg width mismatch: expected " << argInfo.width
                        << " actual " << value->width();
                    diagnostics()->nyi(block(), oss.str());
                }
                return true;
            }
            inputOperands.push_back(value);
            inputNames.push_back(argInfo.name);
        }
        else {
            std::string valueName = makeControlValueName("dpic_out");
            grh::Value& resultValue =
                graph().createValue(valueName, argInfo.width > 0 ? argInfo.width : 1,
                                    argInfo.isSigned);
            applyDebug(resultValue, makeDebugInfo(sourceManager_, actual));
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
                    return true;
                }
                WriteBackMemo::Slice slice = buildFullSlice(*entry, resultValue);
                slice.originExpr = actual;
                std::vector<WriteBackMemo::Slice> slices;
                slices.push_back(std::move(slice));
                handleEntryWrite(*entry, std::move(slices));
            }
            outputValues.push_back(&resultValue);
            outputNames.push_back(argInfo.name);
        }
    }

    grh::Operation& op =
        graph().createOperation(grh::OperationKind::kDpicCall, makeControlOpName("dpic_call"));
    applyDebug(op, makeDebugInfo(sourceManager_, &call));
    op.addOperand(*clkValue);
    op.addOperand(*enableValue);
    for (grh::Value* operand : inputOperands) {
        op.addOperand(*operand);
    }
    for (grh::Value* result : outputValues) {
        op.addResult(*result);
    }
    if (clockPolarityAttr_) {
        op.setAttribute("clkPolarity", *clockPolarityAttr_);
    }
    if (entry.importOp) {
        op.setAttribute("targetImportSymbol", entry.importOp->symbol());
    }
    else if (entry.symbol) {
        op.setAttribute("targetImportSymbol", std::string(entry.symbol->name));
        if (diagnostics()) {
            diagnostics()->nyi(block(), "DPI import operation 未在当前 graph 中创建");
        }
    }
    else if (diagnostics()) {
        diagnostics()->nyi(block(), "DPI import operation metadata缺失");
    }
    op.setAttribute("inArgName", inputNames);
    op.setAttribute("outArgName", outputNames);
    return true;
}

bool SeqAlwaysConverter::handleAssertionIntent(const slang::ast::Expression* condition,
                                               const slang::ast::ExpressionStatement* origin,
                                               std::string_view message,
                                               std::string_view severity) {
    grh::Value* clkValue = ensureClockValue();
    if (!clkValue) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Sequential assert lacks resolved clock");
        }
        return true;
    }

    grh::Value* guard = currentGuardValue();
    grh::Value* condBit = nullptr;
    if (condition && rhsConverter_) {
        grh::Value* condValue = rhsConverter_->convert(*condition);
        if (!condValue) {
            if (diagnostics() && origin) {
                diagnostics()->nyi(block(), "Failed to lower assert condition");
            }
            return true;
        }
        condBit = coerceToCondition(*condValue);
    }
    if (!condBit) {
        return true;
    }
    if (!condBit) {
        return true;
    }

    grh::Value* finalCond = condBit;
    if (guard) {
        grh::Value* guardBit = coerceToCondition(*guard);
        if (!guardBit) {
            return true;
        }
        // guard -> cond  === !guard || cond
        grh::Value* notGuard = buildLogicNot(*guardBit);
        if (!notGuard) {
            return true;
        }
        finalCond = buildLogicOr(*notGuard, *condBit);
        if (!finalCond) {
            return true;
        }
    }

    grh::Operation& op =
        graph().createOperation(grh::OperationKind::kAssert, makeControlOpName("assert"));
    applyDebug(op, origin ? makeDebugInfo(sourceManager_, origin)
                          : makeDebugInfo(sourceManager_, condition));
    op.addOperand(*clkValue);
    op.addOperand(*finalCond);
    if (clockPolarityAttr_) {
        op.setAttribute("clkPolarity", *clockPolarityAttr_);
    }
    if (!message.empty()) {
        op.setAttribute("message", std::string(message));
    }
    if (!severity.empty()) {
        op.setAttribute("severity", std::string(severity));
    }
    return true;
}

void SeqAlwaysConverter::planSequentialFinalize() {
    grh::Value* clockValue = ensureClockValue();
    if (!clockValue) {
        return;
    }

    const bool registerWrites = finalizeRegisterWrites(*clockValue);
    const bool memoryWrites = finalizeMemoryWrites(*clockValue);

    // If nothing to finalize, stay silent (some blocks may be empty after static pruning).
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
        if (!entry.target) {
            reportFinalizeIssue(entry, "Sequential write target lacks register metadata");
            entry.consumed = true;
            continue;
        }

        std::fprintf(stderr, "[seq] entry symbol=%s slices=%zu\n",
                     entry.target->symbol ? std::string(entry.target->symbol->name).c_str()
                                          : "<null>",
                     entry.slices.size());
        for (const auto& s : entry.slices) {
            std::fprintf(stderr, "  slice msb=%lld lsb=%lld width=%lld valw=%lld\n",
                         static_cast<long long>(s.msb),
                         static_cast<long long>(s.lsb),
                         static_cast<long long>(s.msb - s.lsb + 1),
                         s.value ? static_cast<long long>(s.value->width()) : -1LL);
        }

        if (entry.target->multiDriver) {
            const auto debugInfo =
                makeDebugInfo(sourceManager_, entry.target ? entry.target->symbol : nullptr);
            for (const auto& slice : entry.slices) {
                if (!slice.value) {
                    reportFinalizeIssue(entry, "Multi-driver register slice missing RHS value");
                    continue;
                }
                const int64_t sliceWidth = slice.msb - slice.lsb + 1;
                grh::Operation& split =
                    graph().createOperation(grh::OperationKind::kRegister,
                                            makeFinalizeOpName(*entry.target, "split"));
                applyDebug(split, debugInfo);
                if (clockPolarityAttr_) {
                    split.setAttribute("clkPolarity", *clockPolarityAttr_);
                }
                split.addOperand(clockValue);
                grh::Value& regVal = graph().createValue(
                    makeFinalizeValueName(*entry.target, "split"), sliceWidth, entry.target->isSigned);
                applyDebug(regVal, debugInfo);
                split.addResult(regVal);
                if (!attachDataOperand(split, *slice.value, entry)) {
                    continue;
                }
                memo().recordMultiDriverPart(*entry.target,
                                             WriteBackMemo::MultiDriverPart{slice.msb, slice.lsb,
                                                                            &regVal});
            }
            entry.consumed = true;
            consumedAny = true;
            continue;
        }

        grh::Operation* stateOp = entry.target->stateOp;
        if (!stateOp) {
            reportFinalizeIssue(entry, "Sequential write target lacks register metadata");
            entry.consumed = true;
            continue;
        }
        if (stateOp->kind() == grh::OperationKind::kMemory) {
            continue;
        }

        grh::Value* dataValue = buildDataOperand(entry);
        if (!dataValue) {
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
            if (!valueDependsOnSignal(*dataValue, *resetContext->signal)) {
                resetActive = false;
                resetContext.reset();
                switch (stateOp->kind()) {
                case grh::OperationKind::kRegisterRst:
                case grh::OperationKind::kRegisterArst:
                    stateOp->setKind(grh::OperationKind::kRegister);
                    break;
                case grh::OperationKind::kRegisterEnRst:
                case grh::OperationKind::kRegisterEnArst:
                    stateOp->setKind(grh::OperationKind::kRegisterEn);
                    break;
                default:
                    break;
                }
                stateOp->clearAttribute("rstPolarity");
            }
            else {
                resetExtraction =
                    extractResetBranches(*dataValue, *resetContext->signal, resetContext->activeHigh,
                                         entry);
                if (!resetExtraction) {
                    continue;
                }
            }
        }

        if (!attachClockOperand(*stateOp, clockValue, entry)) {
            continue;
        }
        if (resetActive) {
            if (!resetExtraction ||
                !attachResetOperands(*stateOp, *resetContext->signal,
                                     *resetExtraction->resetValue, entry)) {
                continue;
            }
        }
        // Attempt to extract enable pattern for both plain and reset registers, using peeled data if available.
        if (entry.target && entry.target->value) {
            grh::Value* analysisInput =
                (resetExtraction && resetExtraction->dataWithoutReset)
                    ? resetExtraction->dataWithoutReset
                    : dataValue;
            if (grh::Operation* mux = analysisInput->definingOp();
                mux && mux->kind() == grh::OperationKind::kMux &&
                mux->operands().size() == 3) {
                grh::Value* cond = mux->operands()[0];
                grh::Value* tVal = mux->operands()[1];
                grh::Value* fVal = mux->operands()[2];

                grh::Value* q = entry.target->value;
                grh::Value* enRaw = nullptr;
                grh::Value* newData = nullptr;
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
                if (enRaw && newData && newData->width() == q->width()) {
                    grh::Value* enBit = coerceToCondition(*enRaw);
                    if (enBit) {
                        const std::string enLevel = activeLow ? "low" : "high";
                        switch (stateOp->kind()) {
                        case grh::OperationKind::kRegister:
                            stateOp->setKind(grh::OperationKind::kRegisterEn);
                            stateOp->addOperand(*enBit); // [clk, en]
                            stateOp->setAttribute("enLevel", enLevel);
                            break;
                        case grh::OperationKind::kRegisterRst:
                            stateOp->setKind(grh::OperationKind::kRegisterEnRst);
                            stateOp->addOperand(*enBit); // [clk, rst, resetValue, en]
                            if (stateOp->operands().size() == 4) {
                                grh::Value* rstVal = stateOp->operands()[2];
                                if (rstVal) {
                                    stateOp->replaceOperand(2, *enBit);
                                    stateOp->replaceOperand(3, *rstVal);
                                }
                            }
                            stateOp->setAttribute("enLevel", enLevel);
                            break;
                        case grh::OperationKind::kRegisterArst:
                            stateOp->setKind(grh::OperationKind::kRegisterEnArst);
                            stateOp->addOperand(*enBit);
                            if (stateOp->operands().size() == 4) {
                                grh::Value* rstVal = stateOp->operands()[2];
                                if (rstVal) {
                                    stateOp->replaceOperand(2, *enBit);
                                    stateOp->replaceOperand(3, *rstVal);
                                }
                            }
                            stateOp->setAttribute("enLevel", enLevel);
                            break;
                        default:
                            break;
                        }
                        dataValue = newData; // Use unguarded data as register D
                    }
                }
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
        if (!intent.entry->stateOp || intent.entry->stateOp->kind() != grh::OperationKind::kMemory) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory entry lacks kMemory state op");
            return;
        }
        const int64_t rowWidth = memoryRowWidth(*intent.entry);
        if (intent.data->width() != rowWidth) {
            reportMemoryIssue(intent.entry, intent.originExpr, "memory data width mismatch");
            return;
        }
        grh::Value* enableValue =
            intent.enable ? coerceToCondition(*intent.enable) : ensureMemoryEnableValue();
        if (!enableValue) {
            reportMemoryIssue(intent.entry, intent.originExpr, "failed to resolve write enable");
            return;
        }

        grh::OperationKind opKind = grh::OperationKind::kMemoryWritePort;
        if (resetCtx) {
            opKind = resetCtx->kind == ResetContext::Kind::Async
                         ? grh::OperationKind::kMemoryWritePortArst
                         : grh::OperationKind::kMemoryWritePortRst;
        }
        grh::Operation& port =
            graph().createOperation(opKind, makeFinalizeOpName(*intent.entry, "mem_wr"));
        applyDebug(port, intent.originExpr ? makeDebugInfo(sourceManager_, intent.originExpr)
                                           : makeDebugInfo(sourceManager_, intent.entry->symbol));
        port.setAttribute("memSymbol", intent.entry->stateOp->symbol());
        port.setAttribute("enLevel", std::string("high"));
        if (resetCtx) {
            port.setAttribute("rstPolarity", resetPolarityString(*resetCtx));
        }
        applyClockPolarity(port, "memory write port");
        port.addOperand(clockValue);
        if (resetCtx && resetCtx->signal) {
            port.addOperand(*resetCtx->signal);
        }
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
        grh::Value* enableValue =
            intent.enable ? coerceToCondition(*intent.enable) : ensureMemoryEnableValue();
        if (!enableValue) {
            reportMemoryIssue(intent.entry, intent.originExpr, "failed to resolve mask write enable");
            return;
        }

        grh::OperationKind opKind = grh::OperationKind::kMemoryMaskWritePort;
        if (resetCtx) {
            opKind = resetCtx->kind == ResetContext::Kind::Async
                         ? grh::OperationKind::kMemoryMaskWritePortArst
                         : grh::OperationKind::kMemoryMaskWritePortRst;
        }
        grh::Operation& port =
            graph().createOperation(opKind, makeFinalizeOpName(*intent.entry, "mem_mask_wr"));
        applyDebug(port, intent.originExpr ? makeDebugInfo(sourceManager_, intent.originExpr)
                                           : makeDebugInfo(sourceManager_, intent.entry->symbol));
        port.setAttribute("memSymbol", intent.entry->stateOp->symbol());
        port.setAttribute("enLevel", std::string("high"));
        if (resetCtx) {
            port.setAttribute("rstPolarity", resetPolarityString(*resetCtx));
        }
        applyClockPolarity(port, "memory mask write port");
        port.addOperand(clockValue);
        if (resetCtx && resetCtx->signal) {
            port.addOperand(*resetCtx->signal);
        }
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
    applyDebug(op, makeDebugInfo(sourceManager_, &block()));
    grh::Value& value =
        graph().createValue(makeMemoryHelperValueName("en"), 1, /*isSigned=*/false);
    applyDebug(value, makeDebugInfo(sourceManager_, &block()));
    op.addResult(value);
    op.setAttribute("constValue", "1'h1");
    memoryEnableOne_ = &value;
    return memoryEnableOne_;
}

void SeqAlwaysConverter::recordMemoryWordWrite(const SignalMemoEntry& entry,
                                               const slang::ast::Expression& origin,
                                               grh::Value& addrValue, grh::Value& dataValue,
                                               grh::Value* enable) {
    grh::Value* normalizedAddr = normalizeMemoryAddress(entry, addrValue, &origin);
    if (!normalizedAddr) {
        return;
    }
    MemoryWriteIntent intent;
    intent.entry = &entry;
    intent.originExpr = &origin;
    intent.addr = normalizedAddr;
    intent.data = &dataValue;
    intent.enable = enable;
    memoryWrites_.push_back(intent);
}

void SeqAlwaysConverter::recordMemoryBitWrite(const SignalMemoEntry& entry,
                                              const slang::ast::Expression& origin,
                                              grh::Value& addrValue, grh::Value& bitIndex,
                                              grh::Value& bitValue, grh::Value* enable) {
    grh::Value* normalizedAddr = normalizeMemoryAddress(entry, addrValue, &origin);
    if (!normalizedAddr) {
        return;
    }
    MemoryBitWriteIntent intent;
    intent.entry = &entry;
    intent.originExpr = &origin;
    intent.addr = normalizedAddr;
    intent.bitIndex = &bitIndex;
    intent.bitValue = &bitValue;
    intent.enable = enable;
    memoryBitWrites_.push_back(intent);
}

grh::Value* SeqAlwaysConverter::buildMemorySyncRead(const SignalMemoEntry& entry,
                                                    grh::Value& addrValue,
                                                    const slang::ast::Expression& originExpr,
                                                    grh::Value* enableOverride) {
    if (!entry.stateOp || entry.stateOp->kind() != grh::OperationKind::kMemory) {
        if (diagnostics()) {
            diagnostics()->nyi(block(),
                               "Seq always memory read target is not backed by kMemory operation");
        }
        return nullptr;
    }

    grh::Value* clkValue = ensureClockValue();
    if (!clockPolarityAttr_) {
        if (diagnostics()) {
            diagnostics()->nyi(block(),
                               "Seq always memory sync read lacks clock polarity attribute");
        }
        return nullptr;
    }
    grh::Value* enValue = enableOverride ? coerceToCondition(*enableOverride)
                                         : ensureMemoryEnableValue();
    if (!clkValue || !enValue) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "Seq always memory read lacks clock or enable");
        }
        return nullptr;
    }

    grh::Value* normalizedAddr = normalizeMemoryAddress(entry, addrValue, &originExpr);
    if (!normalizedAddr) {
        return nullptr;
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
    grh::OperationKind kind = grh::OperationKind::kMemorySyncReadPort;
    if (resetCtx) {
        kind = resetCtx->kind == ResetContext::Kind::Async
                   ? grh::OperationKind::kMemorySyncReadPortArst
                   : grh::OperationKind::kMemorySyncReadPortRst;
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
    grh::Operation& port =
        graph().createOperation(kind, makeFinalizeOpName(entry, "mem_sync_rd"));
    applyDebug(port, debugInfo);
    port.setAttribute("memSymbol", entry.stateOp->symbol());
    port.setAttribute("enLevel", std::string("high"));
    if (resetCtx) {
        port.setAttribute("rstPolarity", resetCtx->activeHigh ? "high" : "low");
    }
    applyClockPolarity(port, "memory sync read port");
    port.addOperand(*clkValue);
    if (resetCtx && resetCtx->signal) {
        port.addOperand(*resetCtx->signal);
    }
    port.addOperand(*normalizedAddr);
    port.addOperand(*enValue);

    grh::Value& result =
        graph().createValue(makeFinalizeValueName(entry, "mem_sync_rd"), width, isSigned);
    applyDebug(result, debugInfo);
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

std::optional<int64_t> SeqAlwaysConverter::memoryRowCount(const SignalMemoEntry& entry) const {
    if (entry.stateOp) {
        const auto& attrs = entry.stateOp->attributes();
        auto it = attrs.find("row");
        if (it != attrs.end() && std::holds_alternative<int64_t>(it->second)) {
            int64_t rows = std::get<int64_t>(it->second);
            if (rows > 0) {
                return rows;
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

grh::Value* SeqAlwaysConverter::normalizeMemoryAddress(const SignalMemoEntry& entry,
                                                       grh::Value& addrValue,
                                                       const slang::ast::Expression* originExpr) {
    (void)originExpr;
    const int64_t targetWidth = memoryAddrWidth(entry);
    if (targetWidth <= 0) {
        if (diagnostics()) {
            diagnostics()->nyi(block(), "memory 地址位宽无效");
        }
        return nullptr;
    }

    const auto debugInfo = originExpr ? makeDebugInfo(sourceManager_, originExpr)
                                      : makeDebugInfo(sourceManager_, entry.symbol);
    grh::Value* current = &addrValue;
    const int64_t currentWidth = current->width() > 0 ? current->width() : 1;

    if (currentWidth > targetWidth) {
        grh::Operation& slice =
            graph().createOperation(grh::OperationKind::kSliceStatic,
                                    makeMemoryHelperOpName("addr_trunc"));
        applyDebug(slice, debugInfo);
        slice.addOperand(*current);
        slice.setAttribute("sliceStart", int64_t(0));
        slice.setAttribute("sliceEnd", targetWidth - 1);
        grh::Value& sliced =
            graph().createValue(makeMemoryHelperValueName("addr_trunc"), targetWidth,
                                /*isSigned=*/false);
        applyDebug(sliced, debugInfo);
        slice.addResult(sliced);
        current = &sliced;
    }
    else if (currentWidth < targetWidth) {
        const int64_t padWidth = targetWidth - currentWidth;
        grh::Value* zero = createZeroValue(padWidth);
        if (!zero) {
            if (diagnostics()) {
                diagnostics()->nyi(block(), "memory 地址 zero-extend 失败");
            }
            return nullptr;
        }
        grh::Operation& concat =
            graph().createOperation(grh::OperationKind::kConcat,
                                    makeMemoryHelperOpName("addr_zext"));
        applyDebug(concat, debugInfo);
        concat.addOperand(*zero);
        concat.addOperand(*current);
        grh::Value& extended =
            graph().createValue(makeMemoryHelperValueName("addr_zext"), targetWidth,
                                /*isSigned=*/false);
        applyDebug(extended, debugInfo);
        concat.addResult(extended);
        current = &extended;
    }

    if (current->isSigned()) {
        grh::Operation& assign =
            graph().createOperation(grh::OperationKind::kAssign,
                                    makeMemoryHelperOpName("addr_cast"));
        applyDebug(assign, debugInfo);
        assign.addOperand(*current);
        grh::Value& casted =
            graph().createValue(makeMemoryHelperValueName("addr_cast"), current->width(),
                                /*isSigned=*/false);
        applyDebug(casted, debugInfo);
        assign.addResult(casted);
        current = &casted;
    }

    return current;
}

bool SeqAlwaysConverter::applyClockPolarity(grh::Operation& op, std::string_view context) {
    if (!clockPolarityAttr_) {
        if (diagnostics()) {
            std::string msg = "Seq always ";
            msg.append(context);
            msg.append(" 缺少 clkPolarity");
            diagnostics()->nyi(block(), std::move(msg));
        }
        return false;
    }
    op.setAttribute("clkPolarity", *clockPolarityAttr_);
    return true;
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
    const auto debugInfo = makeDebugInfo(sourceManager_, &block());
    grh::Operation& concat =
        graph().createOperation(grh::OperationKind::kConcat, makeMemoryHelperOpName(label));
    applyDebug(concat, debugInfo);
    concat.addOperand(*zeroPad);
    concat.addOperand(value);

    grh::Value& wide =
        graph().createValue(makeMemoryHelperValueName(label), padWidth + value.width(),
                            value.isSigned());
    applyDebug(wide, debugInfo);
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

    const auto debugInfo = makeDebugInfo(sourceManager_, &block());
    grh::Operation& shl =
        graph().createOperation(grh::OperationKind::kShl, makeMemoryHelperOpName(label));
    applyDebug(shl, debugInfo);
    shl.addOperand(*extended);
    shl.addOperand(bitIndex);
    grh::Value& shifted =
        graph().createValue(makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    applyDebug(shifted, debugInfo);
    shl.addResult(shifted);
    return &shifted;
}

grh::Value* SeqAlwaysConverter::buildShiftedMask(grh::Value& bitIndex, int64_t targetWidth,
                                                 std::string_view label) {
    if (targetWidth <= 0) {
        return nullptr;
    }
    slang::SVInt literal(static_cast<slang::bitwidth_t>(targetWidth), 1, /*isSigned=*/false);
    const auto debugInfo = makeDebugInfo(sourceManager_, &block());
    grh::Operation& constOp =
        graph().createOperation(grh::OperationKind::kConstant, makeMemoryHelperOpName(label));
    applyDebug(constOp, debugInfo);
    grh::Value& baseValue =
        graph().createValue(makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    applyDebug(baseValue, debugInfo);
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
    applyDebug(shl, debugInfo);
    shl.addOperand(*base);
    shl.addOperand(bitIndex);
    grh::Value& shifted =
        graph().createValue(makeMemoryHelperValueName(label), targetWidth, /*isSigned=*/false);
    applyDebug(shifted, debugInfo);
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
            grh::Value* rstValue = asyncInfo->expr ? resolveAsyncResetSignal(*asyncInfo->expr) : nullptr;
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
            if (grh::Value* rstValue = resolveSyncResetSignal(*syncInfo->symbol)) {
                blockResetContext_.kind = ResetContext::Kind::Sync;
                blockResetContext_.signal = rstValue;
                blockResetContext_.activeHigh = syncInfo->activeHigh;
                return blockResetContext_;
            }
        }
    }

    blockResetContext_.kind = ResetContext::Kind::None;
    blockResetContext_.signal = nullptr;
    blockResetContext_.activeHigh = true;
    return std::nullopt;
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

    const auto debugInfo =
        makeDebugInfo(sourceManager_, entry.target ? entry.target->symbol : nullptr);
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

    // Prefer a two-operand top-level concat: [hold_hi , compact_lo],
    // where compact_lo may itself be a concat tree of the remaining pieces.
    if (components.size() > 2) {
        // Build compact_lo from components[1..end] as a left-assoc concat chain.
        auto buildChain = [&](std::vector<grh::Value*>::const_iterator begin,
                              std::vector<grh::Value*>::const_iterator end) -> grh::Value* {
            if (begin == end) {
                return nullptr;
            }
            grh::Value* acc = *begin;
            for (auto it = begin + 1; it != end; ++it) {
                grh::Operation& c =
                    graph().createOperation(grh::OperationKind::kConcat,
                                            makeFinalizeOpName(*entry.target, "seq_concat_lo"));
                applyDebug(c, debugInfo);
                c.addOperand(*acc);
                c.addOperand(**it);
                const int64_t w = acc->width() + (*it)->width();
                grh::Value& v =
                    graph().createValue(makeFinalizeValueName(*entry.target, "seq_concat_lo"),
                                        w, entry.target->isSigned);
                applyDebug(v, debugInfo);
                c.addResult(v);
                acc = &v;
            }
            return acc;
        };

        grh::Value* hi = components.front();
        grh::Value* lo = buildChain(components.begin() + 1, components.end());
        if (!lo) {
            return hi;
        }

        grh::Operation& concatTop =
            graph().createOperation(grh::OperationKind::kConcat,
                                    makeFinalizeOpName(*entry.target, "seq_concat"));
        applyDebug(concatTop, debugInfo);
        concatTop.addOperand(*hi);
        concatTop.addOperand(*lo);
        grh::Value& composed =
            graph().createValue(makeFinalizeValueName(*entry.target, "seq_concat"),
                                targetWidth, entry.target->isSigned);
        applyDebug(composed, debugInfo);
        concatTop.addResult(composed);
        return &composed;
    }

    grh::Operation& concat =
        graph().createOperation(grh::OperationKind::kConcat,
                                makeFinalizeOpName(*entry.target, "seq_concat"));
    applyDebug(concat, debugInfo);
    for (grh::Value* component : components) {
        concat.addOperand(*component);
    }

    grh::Value& composed =
        graph().createValue(makeFinalizeValueName(*entry.target, "seq_concat"), targetWidth,
                            entry.target->isSigned);
    applyDebug(composed, debugInfo);
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

    const auto debugInfo =
        makeDebugInfo(sourceManager_, entry.target ? entry.target->symbol : nullptr);
    grh::Operation& sliceOp = graph().createOperation(grh::OperationKind::kSliceStatic,
                                                      makeFinalizeOpName(*entry.target, "hold"));
    applyDebug(sliceOp, debugInfo);
    sliceOp.addOperand(*source);
    sliceOp.setAttribute("sliceStart", lsb);
    sliceOp.setAttribute("sliceEnd", msb);

    grh::Value& result =
        graph().createValue(makeFinalizeValueName(*entry.target, "hold"), msb - lsb + 1,
                            source->isSigned());
    applyDebug(result, debugInfo);
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
    if (stateOp.kind() == grh::OperationKind::kRegisterEn) {
        expected = 2;
    } else if (stateOp.kind() == grh::OperationKind::kRegisterRst ||
               stateOp.kind() == grh::OperationKind::kRegisterArst) {
        expected = 3;
    } else if (stateOp.kind() == grh::OperationKind::kRegisterEnRst ||
               stateOp.kind() == grh::OperationKind::kRegisterEnArst) {
        expected = 4;
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
    switch (entry.stateOp->kind()) {
    case grh::OperationKind::kRegisterArst:
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
    const bool debugReset = std::getenv("WOLF_DEBUG_RESET") != nullptr;
    if (&condition == &resetSignal) {
        return /* inverted */ false;
    }
    if (grh::Operation* op = condition.definingOp()) {
        if (debugReset) {
            std::fprintf(stderr,
                         "[reset-debug] condition op kind=%d operands=%zu cond_width=%lld "
                         "reset_width=%lld operand0=%p reset=%p\n",
                         static_cast<int>(op->kind()), op->operands().size(),
                         static_cast<long long>(condition.width()),
                         static_cast<long long>(resetSignal.width()),
                         op->operands().empty() ? nullptr : op->operands().front(),
                         &resetSignal);
            if (!op->operands().empty()) {
                grh::Operation* child = op->operands().front()->definingOp();
                std::fprintf(stderr, "[reset-debug] operand0 child kind=%d\n",
                             child ? static_cast<int>(child->kind()) : -1);
            }
        }
        if (op->operands().size() == 1 && op->operands().front() == &resetSignal) {
            if (op->kind() == grh::OperationKind::kLogicNot) {
                return true;
            }
            // Treat a bitwise inversion of the reset signal as an active-low reset condition when
            // the widths match (e.g., "~aresetn" on a 1-bit reset).
            if (op->kind() == grh::OperationKind::kNot && condition.width() == resetSignal.width()) {
                return true;
            }
        }
    }
    if (debugReset) {
        std::fprintf(stderr, "[reset-debug] reset condition did not match expected forms\n");
    }
    return std::nullopt;
}

bool SeqAlwaysConverter::valueDependsOnSignal(grh::Value& root, grh::Value& needle) const {
    std::vector<grh::Value*> stack;
    stack.push_back(&root);
    std::unordered_set<grh::Value*> visited;
    while (!stack.empty()) {
        grh::Value* current = stack.back();
        stack.pop_back();
        if (!current) {
            continue;
        }
        if (!visited.insert(current).second) {
            continue;
        }
        if (current == &needle) {
            return true;
        }
        if (grh::Operation* op = current->definingOp()) {
            for (grh::Value* operand : op->operands()) {
                if (operand) {
                    stack.push_back(operand);
                }
            }
        }
    }
    return false;
}

std::optional<SeqAlwaysConverter::ResetExtraction>
SeqAlwaysConverter::extractResetBranches(grh::Value& dataValue, grh::Value& resetSignal,
                                         bool activeHigh, const WriteBackMemo::Entry& entry) {
    const bool debugReset = std::getenv("WOLF_DEBUG_RESET") != nullptr;
    // Some guarded registers get sliced and re-concatenated during shadow merge. If all slices
    // come from the same mux in natural bit order, peel the concat back to the mux so we can
    // inspect its reset branch.
    auto tryCollapseConcat = [&](grh::Value& candidate) -> grh::Value* {
        if (!entry.target) {
            return &candidate;
        }
        const int64_t targetWidth = entry.target->width > 0 ? entry.target->width
                                                            : candidate.width();
        grh::Operation* concatOp = candidate.definingOp();
        if (!concatOp || concatOp->kind() != grh::OperationKind::kConcat) {
            return &candidate;
        }

        std::vector<grh::Value*> parts;
        auto collectSlices = [&](auto&& self, grh::Value& node) -> bool {
            grh::Operation* op = node.definingOp();
            if (!op) {
                return false;
            }
            if (op->kind() == grh::OperationKind::kConcat) {
                for (grh::Value* operand : op->operands()) {
                    if (!operand) {
                        return false;
                    }
                    if (!self(self, *operand)) {
                        return false;
                    }
                }
                return true;
            }
            if (op->kind() == grh::OperationKind::kSliceStatic && op->operands().size() == 1) {
                parts.push_back(&node);
                return true;
            }
            return false;
        };

        if (!collectSlices(collectSlices, candidate)) {
            return &candidate;
        }

        grh::Value* commonBase = nullptr;
        int64_t expectedMsb = targetWidth - 1;
        for (grh::Value* part : parts) {
            if (!part) {
                return &candidate;
            }
            grh::Operation* sliceOp = part->definingOp();
            if (!sliceOp || sliceOp->kind() != grh::OperationKind::kSliceStatic ||
                sliceOp->operands().size() != 1) {
                return &candidate;
            }
            grh::Value* base = sliceOp->operands().front();
            if (!base) {
                return &candidate;
            }

            const auto& attrs = sliceOp->attributes();
            auto itStart = attrs.find("sliceStart");
            auto itEnd = attrs.find("sliceEnd");
            const int64_t* start = itStart != attrs.end() ? std::get_if<int64_t>(&itStart->second)
                                                          : nullptr;
            const int64_t* end = itEnd != attrs.end() ? std::get_if<int64_t>(&itEnd->second)
                                                      : nullptr;
            if (!start || !end) {
                return &candidate;
            }
            if ((*end - *start + 1) != part->width()) {
                return &candidate;
            }
            if (*end != expectedMsb) {
                return &candidate;
            }
            expectedMsb = *start - 1;

            if (!commonBase) {
                commonBase = base;
            } else if (commonBase != base) {
                return &candidate;
            }
        }

        if (expectedMsb != -1) {
            return &candidate;
        }
        if (!commonBase || commonBase->width() != targetWidth) {
            return &candidate;
        }
        return commonBase;
    };

    grh::Value* muxValue = tryCollapseConcat(dataValue);
    grh::Operation* muxOp = muxValue ? muxValue->definingOp() : nullptr;
    if (!muxOp || muxOp->kind() != grh::OperationKind::kMux || muxOp->operands().size() != 3) {
        if (debugReset) {
            auto logOp = [](const char* label, grh::Operation* op) {
                if (!op) {
                    std::fprintf(stderr, "[reset-debug] %s: <null>\n", label);
                    return;
                }
                std::fprintf(stderr, "[reset-debug] %s: kind=%d operands=%zu\n", label,
                             static_cast<int>(op->kind()), op->operands().size());
                std::size_t idx = 0;
                for (grh::Value* operand : op->operands()) {
                    grh::Operation* child = operand ? operand->definingOp() : nullptr;
                    std::fprintf(stderr, "  operand[%zu]: value_width=%lld child_kind=%d\n", idx,
                                 operand ? static_cast<long long>(operand->width()) : -1LL,
                                 child ? static_cast<int>(child->kind()) : -1);
                    ++idx;
                }
            };
            logOp("dataOp", dataValue.definingOp());
            logOp("collapsedOp", muxOp);
        }
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
    grh::Value* dataWithoutReset = muxOp->operands()[resetBranchIsTrue ? 2 : 1];
    if (!resetValue) {
        reportFinalizeIssue(entry, "Reset branch value is missing");
        return std::nullopt;
    }
    if (resetValue->width() != dataValue.width()) {
        reportFinalizeIssue(entry, "Reset branch width mismatch");
        return std::nullopt;
    }
    return ResetExtraction{resetValue, dataWithoutReset};
}

bool SeqAlwaysConverter::attachResetOperands(grh::Operation& stateOp, grh::Value& rstSignal,
                                             grh::Value& resetValue,
                                             const WriteBackMemo::Entry& entry) {
    auto operands = stateOp.operands();
    if (stateOp.kind() != grh::OperationKind::kRegisterRst &&
        stateOp.kind() != grh::OperationKind::kRegisterArst &&
        stateOp.kind() != grh::OperationKind::kRegisterEnRst &&
        stateOp.kind() != grh::OperationKind::kRegisterEnArst) {
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

    grh::Value* rawCondition = rhsConverter_ ? rhsConverter_->convert(conditionExpr) : nullptr;
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

        auto merged = mergeShadowFrames(*rawCondition, std::move(trueFrame), std::move(falseFrame),
                                        stmt, "if");
        if (!merged) {
            return;
        }
        currentFrame() = std::move(*merged);
        return;
    }

    // Sequential: push guards for true/false branches, accumulate writes in child frames,
    // and merge with hold semantics.
    grh::Value* condBit = coerceToCondition(*rawCondition);
    if (!condBit) {
        return;
    }
    grh::Value* notCond = buildLogicNot(*condBit);
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
        mergeShadowFrames(*condBit, std::move(trueFrame), std::move(falseFrame), stmt, "if");
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

    grh::Value* controlValue = rhsConverter_ ? rhsConverter_->convert(stmt.expr) : nullptr;
    if (!controlValue) {
        return;
    }

    ShadowFrame baseSnapshot = currentFrame();
    const bool combinationalFullCase = isCombinationalFullCase(stmt);
    std::vector<CaseBranch> branches;
    branches.reserve(stmt.items.size());

    grh::Value* anyMatch = nullptr;
    for (const auto& item : stmt.items) {
        grh::Value* match = buildCaseMatch(item, *controlValue, stmt.condition);
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
            anyMatch = buildLogicOr(*anyMatch, *match);
        }
    }

    ShadowFrame accumulator;
    if (stmt.defaultCase) {
        if (isSequential() && anyMatch) {
            grh::Value* notAny = buildLogicNot(*anyMatch);
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
    bool effectiveNonBlocking = isNonBlocking;
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

    grh::Value* rhsValue = rhsConverter_->convert(expr.right());
    if (!rhsValue) {
        return;
    }

    lhsConverter_->convert(expr, *rhsValue);
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
    std::fprintf(stderr, "[shadow-write] %s slices=%zu\n",
                 entry.symbol ? std::string(entry.symbol->name).c_str() : "<null>",
                 slices.size());
    for (const auto& s : slices) {
        std::fprintf(stderr, "  slice %lld:%lld valw=%lld\n",
                     static_cast<long long>(s.msb), static_cast<long long>(s.lsb),
                     s.value ? static_cast<long long>(s.value->width()) : -1LL);
    }
    ShadowState& state = currentFrame().map[&entry];
    for (WriteBackMemo::Slice& slice : slices) {
        insertShadowSlice(state, slice);
    }
    state.dirty = true;
    state.composed = nullptr;
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

grh::Value* AlwaysConverter::sliceExistingValue(const WriteBackMemo::Slice& existing,
                                                int64_t segMsb, int64_t segLsb) {
    if (!existing.value) {
        return nullptr;
    }
    if (segLsb == existing.lsb && segMsb == existing.msb) {
        return existing.value;
    }
    if (segLsb < existing.lsb || segMsb > existing.msb || segMsb < segLsb) {
        return nullptr;
    }

    const int64_t relStart = segLsb - existing.lsb;
    const int64_t relEnd = segMsb - existing.lsb;
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kSliceStatic, makeControlOpName("shadow_slice"));
    applyDebug(op, debugInfo);
    op.addOperand(*existing.value);
    op.setAttribute("sliceStart", relStart);
    op.setAttribute("sliceEnd", relEnd);

    grh::Value& result = graph_.createValue(makeControlValueName("shadow_slice"),
                                            segMsb - segLsb + 1, existing.value->isSigned());
    applyDebug(result, debugInfo);
    op.addResult(result);
    return &result;
}

void AlwaysConverter::insertShadowSlice(ShadowState& state,
                                        const WriteBackMemo::Slice& slice) {
    WriteBackMemo::Slice copy = slice;
    auto& entries = state.slices;
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
            upper.value = sliceExistingValue(existing, upper.msb, upper.lsb);
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
            lower.value = sliceExistingValue(existing, lower.msb, lower.lsb);
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

    auto appendHoldRange = [&](int64_t msb, int64_t lsb) -> bool {
        if (msb < lsb) {
            return true;
        }
        grh::Value* hold = nullptr;
        if (entry.value) {
            if (lsb == 0 && msb == entry.value->width() - 1) {
                hold = entry.value;
            }
            else {
                const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
                grh::Operation& sliceOp =
                    graph_.createOperation(grh::OperationKind::kSliceStatic,
                                            makeShadowOpName(entry, "hold"));
                applyDebug(sliceOp, debugInfo);
                sliceOp.addOperand(*entry.value);
                sliceOp.setAttribute("sliceStart", lsb);
                sliceOp.setAttribute("sliceEnd", msb);

                grh::Value& result = graph_.createValue(
                    makeShadowValueName(entry, "hold"), msb - lsb + 1, entry.isSigned);
                applyDebug(result, debugInfo);
                sliceOp.addResult(result);
                hold = &result;
            }
        }
        else {
            std::fprintf(stderr, "[shadow] entry=%s missing value; zero-fill %lld:%lld\n",
                         entry.symbol ? std::string(entry.symbol->name).c_str() : "<null>",
                         static_cast<long long>(msb), static_cast<long long>(lsb));
            hold = createZeroValue(msb - lsb + 1);
        }
        if (!hold) {
            state.composed = nullptr;
            state.dirty = false;
            return false;
        }
        components.push_back(hold);
        return true;
    };

    for (const WriteBackMemo::Slice& slice : state.slices) {
        const int64_t gapWidth = expectedMsb - slice.msb;
        if (gapWidth > 0) {
            if (!appendHoldRange(expectedMsb, slice.msb + 1)) {
                return nullptr;
            }
            expectedMsb -= gapWidth;
        }
        components.push_back(slice.value);
        expectedMsb = slice.lsb - 1;
    }

    if (expectedMsb >= 0) {
        if (!appendHoldRange(expectedMsb, 0)) {
            return nullptr;
        }
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
    grh::Value* composed = nullptr;
    if (components.size() == 1) {
        composed = components.front();
    }
    else {
        grh::Operation& concat =
            graph_.createOperation(grh::OperationKind::kConcat,
                                   makeShadowOpName(entry, "shadow_concat"));
        applyDebug(concat, debugInfo);
        for (grh::Value* operand : components) {
            concat.addOperand(*operand);
        }
        grh::Value& value =
            graph_.createValue(makeShadowValueName(entry, "shadow"), targetWidth, entry.isSigned);
        applyDebug(value, debugInfo);
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
    opName.append(std::to_string(controlInstanceId_));
    opName.push_back('_');
    opName.append(std::to_string(shadowNameCounter_++));
    std::string valueName = "_comb_zero_val_";
    valueName.append(std::to_string(controlInstanceId_));
    valueName.push_back('_');
    valueName.append(std::to_string(shadowNameCounter_++));

    grh::Operation& op = graph_.createOperation(grh::OperationKind::kConstant, opName);
    applyDebug(op, makeDebugInfo(sourceManager_, &block_));
    grh::Value& value = graph_.createValue(valueName, width, /*isSigned=*/false);
    applyDebug(value, makeDebugInfo(sourceManager_, &block_));
    op.addResult(value);
    std::ostringstream oss;
    oss << width << "'h0";
    op.setAttribute("constValue", oss.str());
    zeroCache_[width] = &value;
    return &value;
}

grh::Value* AlwaysConverter::createOneValue(int64_t width) {
    if (width <= 0) {
        return nullptr;
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
    grh::Operation& op = graph_.createOperation(grh::OperationKind::kConstant, opName);
    applyDebug(op, makeDebugInfo(sourceManager_, &block_));
    grh::Value& value = graph_.createValue(valueName, width, /*isSigned=*/false);
    applyDebug(value, makeDebugInfo(sourceManager_, &block_));
    op.addResult(value);
    std::ostringstream oss;
    // Fill with ones: e.g. 8'hff
    oss << width << "'h";
    const int64_t hexDigits = (width + 3) / 4;
    for (int64_t i = 0; i < hexDigits; ++i) {
        oss << 'f';
    }
    op.setAttribute("constValue", oss.str());
    oneCache_[width] = &value;
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

    grh::Value* condBit = coerceToCondition(condition);
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
        grh::Value* trueValue = nullptr;
        grh::Value* falseValue = nullptr;
        bool inferredLatch = false;

        if (!isSequential()) {
            grh::Value* holdValue = entry->value;
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

        grh::Value* muxValue = createMuxForEntry(*entry, *condBit, *trueValue, *falseValue, label);
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
                mergedState.slices.push_back(buildFullSlice(*entry, *muxValue));
            }
        }
        else {
            mergedState.slices.push_back(buildFullSlice(*entry, *muxValue));
        }
        mergedState.composed = muxValue;
        mergedState.dirty = false;
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

    const auto debugInfo = makeDebugInfo(sourceManager_, entry.symbol);
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kMux, makeShadowOpName(entry, label));
    applyDebug(op, debugInfo);
    op.addOperand(condition);
    op.addOperand(onTrue);
    op.addOperand(onFalse);
    grh::Value& result =
        graph_.createValue(makeShadowValueName(entry, label), width, entry.isSigned);
    applyDebug(result, debugInfo);
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
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kEq, makeControlOpName(hint));
    applyDebug(op, debugInfo);
    op.addOperand(lhs);
    op.addOperand(rhs);
    grh::Value& result = graph_.createValue(makeControlValueName(hint), 1, /*isSigned=*/false);
    applyDebug(result, debugInfo);
    op.addResult(result);
    return &result;
}

grh::Value* AlwaysConverter::buildLogicOr(grh::Value& lhs, grh::Value& rhs) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kOr, makeControlOpName("case_or"));
    applyDebug(op, debugInfo);
    op.addOperand(lhs);
    op.addOperand(rhs);
    grh::Value& result = graph_.createValue(makeControlValueName("case_or"), 1,
                                            /*isSigned=*/false);
    applyDebug(result, debugInfo);
    op.addResult(result);
    return &result;
}

grh::Value* AlwaysConverter::buildLogicAnd(grh::Value& lhs, grh::Value& rhs) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kAnd, makeControlOpName("and"));
    applyDebug(op, debugInfo);
    op.addOperand(lhs);
    op.addOperand(rhs);
    grh::Value& result =
        graph_.createValue(makeControlValueName("and"), 1, /*isSigned=*/false);
    applyDebug(result, debugInfo);
    op.addResult(result);
    return &result;
}

grh::Value* AlwaysConverter::currentGuardValue() const {
    return guardStack_.empty() ? nullptr : guardStack_.back();
}

void AlwaysConverter::pushGuard(grh::Value* guard) {
    if (!guard) {
        return;
    }
    if (guardStack_.empty()) {
        guardStack_.push_back(guard);
        return;
    }
    grh::Value* prev = guardStack_.back();
    grh::Value* combined = buildLogicAnd(*prev, *guard);
    guardStack_.push_back(combined ? combined : guard);
}

void AlwaysConverter::popGuard() {
    if (!guardStack_.empty()) {
        guardStack_.pop_back();
    }
}

grh::Value* AlwaysConverter::buildLogicNot(grh::Value& v) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kLogicNot, makeControlOpName("not"));
    applyDebug(op, debugInfo);
    op.addOperand(v);
    grh::Value& result =
        graph_.createValue(makeControlValueName("not"), 1, /*isSigned=*/false);
    applyDebug(result, debugInfo);
    op.addResult(result);
    return &result;
}

grh::Value* AlwaysConverter::coerceToCondition(grh::Value& v) {
    if (v.width() == 1) {
        return &v;
    }
    grh::Value* zero = createZeroValue(v.width());
    if (!zero) {
        return nullptr;
    }
    grh::Value* eqZero = buildEquality(v, *zero, "eq0");
    if (!eqZero) {
        return nullptr;
    }
    return buildLogicNot(*eqZero);
}

grh::Value* AlwaysConverter::buildWildcardEquality(
    grh::Value& controlValue, grh::Value& rhsValue, const slang::ast::Expression& rhsExpr,
    slang::ast::CaseStatementCondition condition) {
    const int64_t width = controlValue.width();
    if (width <= 0) {
        return nullptr;
    }

    const auto debugInfo = makeDebugInfo(sourceManager_, &rhsExpr);
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
    applyDebug(xorOp, debugInfo);
    xorOp.addOperand(controlValue);
    xorOp.addOperand(rhsValue);
    grh::Value& xorResult =
        graph_.createValue(makeControlValueName("case_wild_xor"), width, /*isSigned=*/false);
    applyDebug(xorResult, debugInfo);
    xorOp.addResult(xorResult);

    grh::Value* maskConst = createLiteralValue(maskValue, /*isSigned=*/false, "case_wild_mask");
    if (!maskConst) {
        return nullptr;
    }

    grh::Operation& andOp =
        graph_.createOperation(grh::OperationKind::kAnd, makeControlOpName("case_wild_and"));
    applyDebug(andOp, debugInfo);
    andOp.addOperand(xorResult);
    andOp.addOperand(*maskConst);
    grh::Value& masked =
        graph_.createValue(makeControlValueName("case_wild_and"), width, /*isSigned=*/false);
    applyDebug(masked, debugInfo);
    andOp.addResult(masked);

    grh::Value* zero = createZeroValue(width);
    if (!zero) {
        return nullptr;
    }
    return buildEquality(masked, *zero, "case_wild_eq0");
}

grh::Value* AlwaysConverter::createLiteralValue(const slang::SVInt& literal, bool isSigned,
                                                    std::string_view hint) {
    const auto debugInfo = makeDebugInfo(sourceManager_, &block_);
    grh::Operation& op =
        graph_.createOperation(grh::OperationKind::kConstant, makeControlOpName(hint));
    applyDebug(op, debugInfo);
    grh::Value& value = graph_.createValue(makeControlValueName(hint),
                                           static_cast<int64_t>(literal.getBitWidth()),
                                           isSigned);
    applyDebug(value, debugInfo);
    op.addResult(value);
    op.setAttribute("constValue",
                    literal.toString(slang::LiteralBase::Hex, true, literal.getBitWidth()));
    return &value;
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

std::optional<grh::SrcLoc> WriteBackMemo::srcLocForEntry(const Entry& entry) const {
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
    const auto debugInfo = srcLocForEntry(entry);
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
    applyDebug(concat, debugInfo);
    for (grh::Value* component : components) {
        concat.addOperand(*component);
    }

    grh::Value& composed =
        graph.createValue(makeValueName(entry, "concat"), targetWidth, entry.target->isSigned);
    applyDebug(composed, debugInfo);
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
        applyDebug(assign, srcLocForEntry(entry));
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

    const auto info = srcLocForEntry(entry);
    grh::Operation& op =
        graph.createOperation(grh::OperationKind::kConstant, makeOperationName(entry, "zero"));
    applyDebug(op, info);
    grh::Value& value = graph.createValue(makeValueName(entry, "zero"), width, /*isSigned=*/false);
    applyDebug(value, info);
    op.addResult(value);
    std::ostringstream oss;
    oss << width << "'h0";
    op.setAttribute("constValue", oss.str());
    return &value;
}

bool WriteBackMemo::tryLowerLatch(Entry& entry, grh::Value& dataValue, grh::Graph& graph,
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

    grh::Value* q = entry.target->value;
    const int64_t targetWidth = q->width();
    auto ensureOneBit = [&](grh::Value* cond, std::string_view label) -> grh::Value* {
        if (!cond) {
            return nullptr;
        }
        if (cond->width() != 1) {
            std::ostringstream oss;
            oss << "Latch " << label << " must be 1 bit (got " << cond->width() << ")";
            reportIssue(entry, oss.str(), diagnostics);
            return nullptr;
        }
        return cond;
    };

    struct LatchInfo {
        grh::Value* enable = nullptr;
        bool enableActiveLow = false;
        grh::Value* data = nullptr;
        grh::Value* resetSignal = nullptr;
        bool resetActiveHigh = true;
        grh::Value* resetValue = nullptr;
        std::vector<grh::Value*> muxValues;
    };

    auto parseEnableMux = [&](grh::Value& candidate) -> std::optional<LatchInfo> {
        grh::Operation* op = candidate.definingOp();
        if (!op || op->kind() != grh::OperationKind::kMux || op->operands().size() != 3) {
            return std::nullopt;
        }
        grh::Value* cond = ensureOneBit(op->operands()[0], "enable condition");
        if (!cond) {
            return std::nullopt;
        }
        grh::Value* tVal = op->operands()[1];
        grh::Value* fVal = op->operands()[2];

        if (tVal && fVal && tVal == q && fVal->width() == targetWidth) {
            return LatchInfo{.enable = cond,
                             .enableActiveLow = true,
                             .data = fVal,
                             .muxValues = {&candidate}};
        }
        if (fVal && tVal && fVal == q && tVal->width() == targetWidth) {
            return LatchInfo{.enable = cond,
                             .enableActiveLow = false,
                             .data = tVal,
                             .muxValues = {&candidate}};
        }
        return std::nullopt;
    };

    auto makeLogicNot = [&](grh::Value& input, std::string_view label) -> grh::Value* {
        const auto info = srcLocForEntry(entry);
        grh::Operation& op = graph.createOperation(grh::OperationKind::kLogicNot,
                                                   makeOperationName(entry, label));
        applyDebug(op, info);
        op.addOperand(input);
        grh::Value& result =
            graph.createValue(makeValueName(entry, label), 1, /*isSigned=*/false);
        applyDebug(result, info);
        op.addResult(result);
        return &result;
    };

    auto makeLogicOr = [&](grh::Value& lhs, grh::Value& rhs, std::string_view label)
                           -> grh::Value* {
        const auto info = srcLocForEntry(entry);
        grh::Operation& op = graph.createOperation(grh::OperationKind::kLogicOr,
                                                   makeOperationName(entry, label));
        applyDebug(op, info);
        op.addOperand(lhs);
        op.addOperand(rhs);
        grh::Value& result =
            graph.createValue(makeValueName(entry, label), 1, /*isSigned=*/false);
        applyDebug(result, info);
        op.addResult(result);
        return &result;
    };

    auto parseResetEnableMux = [&](grh::Value& candidate) -> std::optional<LatchInfo> {
        grh::Operation* op = candidate.definingOp();
        if (!op || op->kind() != grh::OperationKind::kMux || op->operands().size() != 3) {
            return std::nullopt;
        }
        grh::Value* cond = ensureOneBit(op->operands()[0], "reset condition");
        if (!cond) {
            return std::nullopt;
        }
        grh::Value* tVal = op->operands()[1];
        grh::Value* fVal = op->operands()[2];

        auto tryBranch = [&](grh::Value* resetBranch, grh::Value* dataBranch,
                             bool resetActiveHigh) -> std::optional<LatchInfo> {
            if (!resetBranch || resetBranch->width() != targetWidth) {
                return std::nullopt;
            }
            if (!dataBranch) {
                return std::nullopt;
            }
            if (auto info = parseEnableMux(*dataBranch)) {
                info->resetSignal = cond;
                info->resetActiveHigh = resetActiveHigh;
                info->resetValue = resetBranch;
                info->muxValues.push_back(&candidate);
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

    auto parseMuxChainLatch = [&](grh::Value& candidate) -> std::optional<LatchInfo> {
        if (!entry.target || !entry.target->value) {
            return std::nullopt;
        }
        struct ChainStep {
            grh::Value* muxValue = nullptr;
            grh::Value* cond = nullptr;
            grh::Value* dataBranch = nullptr;
            bool holdOnTrue = false;
        };

        grh::Value* q = entry.target->value;
        auto reachesHold = [&](grh::Value* value, auto&& self,
                               std::unordered_set<const grh::Value*>& visited) -> bool {
            if (!value) {
                return false;
            }
            if (!visited.insert(value).second) {
                return false;
            }
            if (value == q) {
                return true;
            }
            grh::Operation* op = value->definingOp();
            if (!op || op->kind() != grh::OperationKind::kMux || op->operands().size() != 3) {
                return false;
            }
            return self(op->operands()[1], self, visited) || self(op->operands()[2], self, visited);
        };

        std::vector<ChainStep> chain;
        std::unordered_set<const grh::Value*> visited;
        grh::Value* cursor = &candidate;
        while (true) {
            if (cursor == q) {
                break;
            }
            if (!visited.insert(cursor).second) {
                return std::nullopt;
            }
            grh::Operation* op = cursor->definingOp();
            if (!op || op->kind() != grh::OperationKind::kMux || op->operands().size() != 3) {
                return std::nullopt;
            }
            grh::Value* cond = ensureOneBit(op->operands()[0], "mux condition");
            if (!cond) {
                return std::nullopt;
            }
            grh::Value* tVal = op->operands()[1];
            grh::Value* fVal = op->operands()[2];
            std::unordered_set<const grh::Value*> seenTrue;
            const bool trueHolds = reachesHold(tVal, reachesHold, seenTrue);
            std::unordered_set<const grh::Value*> seenFalse;
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

        grh::Value* enable = nullptr;
        for (const ChainStep& step : chain) {
            grh::Value* clause =
                step.holdOnTrue ? makeLogicNot(*step.cond, "latch_hold_not") : step.cond;
            if (!clause) {
                return std::nullopt;
            }
            if (!enable) {
                enable = clause;
            }
            else {
                grh::Value* combined = makeLogicOr(*enable, *clause, "latch_hold_or");
                if (!combined) {
                    return std::nullopt;
                }
                enable = combined;
            }
        }

        grh::Value* replacement = createZeroValue(entry, targetWidth, graph);
        if (!replacement) {
            reportIssue(entry, "Latch reconstruction failed to create hold replacement", diagnostics);
            return std::nullopt;
        }
        grh::Value* dataExpr = replacement;
        const auto info = srcLocForEntry(entry);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (!it->dataBranch || it->dataBranch->width() != targetWidth) {
                reportIssue(entry, "Latch mux data width mismatch", diagnostics);
                return std::nullopt;
            }
            grh::Value* trueVal = it->holdOnTrue ? dataExpr : it->dataBranch;
            grh::Value* falseVal = it->holdOnTrue ? it->dataBranch : dataExpr;
            grh::Operation& mux =
                graph.createOperation(grh::OperationKind::kMux, makeOperationName(entry, "latch_mux"));
            applyDebug(mux, info);
            mux.addOperand(*it->cond);
            mux.addOperand(*trueVal);
            mux.addOperand(*falseVal);
            grh::Value& muxResult =
                graph.createValue(makeValueName(entry, "latch_mux"), targetWidth, entry.target->isSigned);
            applyDebug(muxResult, info);
            mux.addResult(muxResult);
            dataExpr = &muxResult;
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
    grh::OperationKind opKind =
        latch->resetSignal ? grh::OperationKind::kLatchArst : grh::OperationKind::kLatch;
    grh::Operation& op = graph.createOperation(opKind, makeOperationName(entry, "latch"));
    applyDebug(op, debugInfo);
    op.addOperand(*latch->enable);
    if (latch->resetSignal && latch->resetValue) {
        op.addOperand(*latch->resetSignal);
        op.addOperand(*latch->resetValue);
    }
    if (latch->data) {
        op.addOperand(*latch->data);
    }
    op.addResult(*q);
    op.setAttribute("enLevel", latch->enableActiveLow ? std::string("low")
                                                      : std::string("high"));
    if (latch->resetSignal) {
        op.setAttribute("rstPolarity",
                        latch->resetActiveHigh ? std::string("high") : std::string("low"));
    }

    auto pruneMuxValue = [&](grh::Value* value) {
        if (!value) {
            return;
        }
        if (!value->users().empty()) {
            return;
        }
        grh::Operation* op = value->definingOp();
        if (!op || op->kind() != grh::OperationKind::kMux) {
            return;
        }
        if (op->results().size() != 1 || op->results().front() != value) {
            return;
        }
        const std::string opSymbol = op->symbol();
        graph.removeOperation(opSymbol);
    };
    for (grh::Value* muxVal : latch->muxValues) {
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

void WriteBackMemo::finalize(grh::Graph& graph, ElaborateDiagnostics* diagnostics) {
    std::fprintf(stderr, "[wb] finalize entries=%zu\n", entries_.size());
    for (auto& [valueHandle, bucket] : multiDriverParts_) {
        const SignalMemoEntry* target = bucket.target;
        if (!target || !target->value || bucket.parts.empty()) {
            continue;
        }
        if (target->value->definingOp()) {
            continue;
        }
        auto& parts = bucket.parts;
        std::sort(parts.begin(), parts.end(), [](const MultiDriverPart& lhs,
                                                 const MultiDriverPart& rhs) {
            return lhs.msb > rhs.msb;
        });
        const int64_t targetWidth = target->width > 0 ? target->width : 1;
        int64_t expectedMsb = targetWidth - 1;
        std::vector<grh::Value*> components;
        components.reserve(parts.size() + 2);
        Entry temp;
        temp.target = target;
        auto appendPad = [&](int64_t msb, int64_t lsb) -> bool {
            if (msb < lsb) {
                return true;
            }
            grh::Value* zero = createZeroValue(temp, msb - lsb + 1, graph);
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
            grh::Operation& assign =
                graph.createOperation(grh::OperationKind::kAssign,
                                      makeOperationName(temp, "split_assign"));
            applyDebug(assign, makeDebugInfo(sourceManager_, target->symbol));
            assign.addOperand(*components.front());
            assign.addResult(*target->value);
            continue;
        }
        grh::Operation& concat =
            graph.createOperation(grh::OperationKind::kConcat,
                                  makeOperationName(temp, "split_concat"));
        applyDebug(concat, makeDebugInfo(sourceManager_, target->symbol));
        for (grh::Value* component : components) {
            concat.addOperand(*component);
        }
        concat.addResult(*target->value);
    }

    for (Entry& entry : entries_) {
        if (entry.consumed) {
            continue;
        }
        std::fprintf(stderr, "[wb] entry target=%p symbol=%s slices=%zu\n",
                     static_cast<const void*>(entry.target),
                     entry.target && entry.target->symbol
                         ? std::string(entry.target->symbol->name).c_str()
                         : "<null>",
                     entry.slices.size());
        for (const auto& s : entry.slices) {
            std::fprintf(stderr, "  slice msb=%lld lsb=%lld width=%lld valw=%lld\n",
                         static_cast<long long>(s.msb),
                         static_cast<long long>(s.lsb),
                         static_cast<long long>(s.msb - s.lsb + 1),
                         s.value ? static_cast<long long>(s.value->width()) : -1LL);
        }
        grh::Value* composed = composeSlices(entry, graph, diagnostics);
        if (!composed) {
            continue;
        }
        if (tryLowerLatch(entry, *composed, graph, diagnostics)) {
            continue;
        }
        attachToTarget(entry, *composed, graph, diagnostics);
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

grh::Value* RHSConverter::convert(const slang::ast::Expression& expr) {
    if (!graph_) {
        return nullptr;
    }

    if (auto it = cache_.find(&expr); it != cache_.end()) {
        return it->second;
    }

    const slang::ast::Expression* previous = currentExpr_;
    currentExpr_ = &expr;
    grh::Value* value = convertExpression(expr);
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
    case ExpressionKind::Call:
        return convertCall(expr.as<slang::ast::CallExpression>());
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
            suppressCache_ = true;
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
    case UnaryOperator::LogicalNot: {
        grh::Value* logicOperand = reduceToLogicValue(*operand, expr);
        return logicOperand ? buildUnaryOp(grh::OperationKind::kLogicNot, *logicOperand, expr,
                                           "lnot")
                            : nullptr;
    }
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

    if (opKind == grh::OperationKind::kLogicAnd || opKind == grh::OperationKind::kLogicOr) {
        lhs = reduceToLogicValue(*lhs, expr);
        rhs = reduceToLogicValue(*rhs, expr);
        if (!lhs || !rhs) {
            return nullptr;
        }
    }

    return buildBinaryOp(opKind, *lhs, *rhs, expr, "bin");
}

grh::Value* RHSConverter::reduceToLogicValue(grh::Value& input,
                                             const slang::ast::Expression& originExpr) {
    if (input.width() <= 1) {
        return &input;
    }
    grh::Operation& op = createOperation(grh::OperationKind::kReduceOr, "logic_truth");
    op.addOperand(input);
    grh::Value& reduced = createTemporaryValue(*originExpr.type, "logic_truth");
    op.addResult(reduced);
    return &reduced;
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
    std::vector<grh::Value*> operands;
    for (const slang::ast::Expression* operandExpr : expr.operands()) {
        if (!operandExpr) {
            continue;
        }
        grh::Value* operandValue = convert(*operandExpr);
        if (!operandValue) {
            return nullptr;
        }
        operands.push_back(operandValue);
    }

    if (operands.empty()) {
        return nullptr;
    }

    if (operands.size() == 1) {
        const TypeInfo info = deriveTypeInfo(*expr.type);
        return resizeValue(*operands.front(), *expr.type, info, expr, "concat");
    }

    grh::Operation& op = createOperation(grh::OperationKind::kConcat, "concat");
    for (grh::Value* operand : operands) {
        op.addOperand(*operand);
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
    const TypeInfo info = deriveTypeInfo(*expr.type);
    if (std::optional<slang::SVInt> constant = evaluateConstantSvInt(expr)) {
        return createConstantValue(*constant, *expr.type, "convert");
    }

    grh::Value* operand = convert(expr.operand());
    if (!operand) {
        return nullptr;
    }

    return resizeValue(*operand, *expr.type, info, expr, "convert");
}

grh::Value* RHSConverter::convertCall(const slang::ast::CallExpression& expr) {
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

    reportUnsupported("call expression", expr);
    return nullptr;
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
    grh::Value& value =
        graph_->createValue(name, info.width > 0 ? info.width : 1, info.isSigned);
    applyDebug(value, makeDebugInfo(sourceManager_, currentExpr_));
    return value;
}

grh::Operation& RHSConverter::createOperation(grh::OperationKind kind, std::string_view hint) {
    std::string name = makeOperationName(hint, operationCounter_++);
    grh::Operation& op = graph_->createOperation(kind, name);
    applyDebug(op, makeDebugInfo(sourceManager_, currentExpr_));
    return op;
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

grh::Value* RHSConverter::resizeValue(grh::Value& input, const slang::ast::Type& targetType,
                                      const TypeInfo& targetInfo,
                                      const slang::ast::Expression& originExpr,
                                      std::string_view hint) {
    if (!graph_) {
        return nullptr;
    }

    const int64_t targetWidth = targetInfo.width > 0 ? targetInfo.width : 1;
    const int64_t inputWidth = input.width() > 0 ? input.width() : 1;

    if (inputWidth == targetWidth && input.isSigned() == targetInfo.isSigned) {
        return &input;
    }

    if (inputWidth == targetWidth) {
        return buildAssign(input, originExpr, hint);
    }

    if (inputWidth > targetWidth) {
        grh::Operation& slice = createOperation(grh::OperationKind::kSliceStatic, hint);
        slice.addOperand(input);
        slice.setAttribute("sliceStart", int64_t(0));
        slice.setAttribute("sliceEnd", targetWidth - 1);
        grh::Value& result = createTemporaryValue(targetType, hint);
        slice.addResult(result);
        return &result;
    }

    const int64_t extendWidth = targetWidth - inputWidth;
    grh::Operation& concat = createOperation(grh::OperationKind::kConcat, hint);

    grh::Value* extendValue = nullptr;
    if (input.isSigned()) {
        // Sign extend using the operand's MSB.
        grh::Operation& signSlice = createOperation(grh::OperationKind::kSliceStatic, "sign");
        signSlice.addOperand(input);
        signSlice.setAttribute("sliceStart", inputWidth - 1);
        signSlice.setAttribute("sliceEnd", inputWidth - 1);

        std::string signName = makeValueName("sign", valueCounter_++);
        grh::Value& signBit = graph_->createValue(signName, 1, input.isSigned());
        applyDebug(signBit, makeDebugInfo(sourceManager_, currentExpr_));
        signSlice.addResult(signBit);

        grh::Operation& rep = createOperation(grh::OperationKind::kReplicate, "signext");
        rep.addOperand(signBit);
        rep.setAttribute("rep", extendWidth);
        std::string repName = makeValueName("signext", valueCounter_++);
        grh::Value& extBits = graph_->createValue(repName, extendWidth, targetInfo.isSigned);
        applyDebug(extBits, makeDebugInfo(sourceManager_, currentExpr_));
        rep.addResult(extBits);
        extendValue = &extBits;
    }
    else {
        grh::Operation& zeroOp = createOperation(grh::OperationKind::kConstant, "zext");
        std::string valName = makeValueName("zext", valueCounter_++);
        grh::Value& zeros = graph_->createValue(valName, extendWidth, /*isSigned=*/false);
        applyDebug(zeros, makeDebugInfo(sourceManager_, currentExpr_));
        zeroOp.addResult(zeros);
        std::ostringstream oss;
        oss << extendWidth << "'h0";
        zeroOp.setAttribute("constValue", oss.str());
        extendValue = &zeros;
    }

    concat.addOperand(*extendValue);
    concat.addOperand(input);
    grh::Value& result = createTemporaryValue(targetType, hint);
    concat.addResult(result);
    return &result;
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

grh::Value* RHSConverter::resolveMemoValue(const SignalMemoEntry& entry) {
    if (entry.value) {
        return entry.value;
    }

    if (entry.stateOp) {
        const grh::OperationKind kind = entry.stateOp->kind();
        if (kind == grh::OperationKind::kRegister || kind == grh::OperationKind::kRegisterRst ||
            kind == grh::OperationKind::kRegisterArst) {
            auto results = entry.stateOp->results();
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

grh::Value* CombRHSConverter::translateDynamicIndex(const slang::ast::Expression& valueExpr,
                                                    grh::Value& rawIndex,
                                                    const slang::ast::Expression& originExpr,
                                                    std::string_view hint) {
    if (valueExpr.type && valueExpr.type->isUnpackedArray()) {
        return &rawIndex;
    }
    if (!valueExpr.type || !valueExpr.type->isFixedSize()) {
        return &rawIndex;
    }
    const slang::ConstantRange range = valueExpr.type->getFixedRange();
    if (range.isLittleEndian()) {
        const int64_t lower = range.lower();
        if (lower == 0) {
            return &rawIndex;
        }
        grh::Value* lowerConst = createIntConstant(lower, *originExpr.type, hint);
        if (!lowerConst) {
            return nullptr;
        }
        return buildBinaryOp(grh::OperationKind::kSub, rawIndex, *lowerConst, originExpr,
                             hint);
    }

    grh::Value* upperConst = createIntConstant(range.upper(), *originExpr.type, hint);
    if (!upperConst) {
        return nullptr;
    }
    return buildBinaryOp(grh::OperationKind::kSub, *upperConst, rawIndex, originExpr, hint);
}

grh::Value*
CombRHSConverter::convertElementSelect(const slang::ast::ElementSelectExpression& expr) {
    if (const SignalMemoEntry* entry = findMemoEntryFromExpression(expr.value())) {
        if (entry->stateOp && entry->stateOp->kind() == grh::OperationKind::kMemory) {
            return buildMemoryRead(*entry, expr);
        }
    }

    grh::Value* input = convert(expr.value());
    if (!input) {
        return nullptr;
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
                    return buildStaticSlice(*input, sliceStart, sliceEnd, expr, "array_static");
                }
            }
        }
    }

    grh::Value* index = convert(expr.selector());
    if (!index) {
        return nullptr;
    }

    grh::Value* normalizedIndex =
        translateDynamicIndex(expr.value(), *index, expr.selector(), "array_index");
    if (!normalizedIndex) {
        return nullptr;
    }

    return buildArraySlice(*input, *normalizedIndex, info.width, expr);
}

grh::Value*
CombRHSConverter::convertRangeSelect(const slang::ast::RangeSelectExpression& expr) {
    grh::Value* input = convert(expr.value());
    if (!input) {
        return nullptr;
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
            return nullptr;
        }
        const int64_t normLeft =
            translateStaticIndex(expr.value(), *left).value_or(*left);
        const int64_t normRight =
            translateStaticIndex(expr.value(), *right).value_or(*right);
        const int64_t sliceStart = std::min(normLeft, normRight);
        const int64_t sliceEnd = std::max(normLeft, normRight);
        return buildStaticSlice(*input, sliceStart, sliceEnd, expr, "range_slice");
    }
    case RangeSelectionKind::IndexedUp: {
        std::optional<int64_t> width = evaluateConstantInt(expr.right());
        if (!width || *width <= 0) {
            reportUnsupported("indexed range width", expr);
            return nullptr;
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
            return buildStaticSlice(*input, sliceStart, sliceEnd, expr, "range_up");
        }

        if (!expr.left().type) {
            reportUnsupported("indexed range base type", expr);
            return nullptr;
        }

        grh::Value* base = convert(expr.left());
        if (!base) {
            return nullptr;
        }

        grh::Value* lsbIndex = base;
        if (*width > 1 && valueRange && !valueRange->isLittleEndian()) {
            grh::Value* widthValue =
                createIntConstant(*width - 1, *expr.left().type, "range_up_width");
            if (!widthValue) {
                return nullptr;
            }
            lsbIndex =
                buildBinaryOp(grh::OperationKind::kAdd, *base, *widthValue, expr.left(),
                              "range_up_base");
            if (!lsbIndex) {
                return nullptr;
            }
        }

        grh::Value* offset =
            translateDynamicIndex(expr.value(), *lsbIndex, expr.left(), "range_up_index");
        if (!offset) {
            return nullptr;
        }

        return buildDynamicSlice(*input, *offset, *width, expr, "range_up");
    }
    case RangeSelectionKind::IndexedDown: {
        std::optional<int64_t> width = evaluateConstantInt(expr.right());
        if (!width || *width <= 0) {
            reportUnsupported("indexed range width", expr);
            return nullptr;
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
            return buildStaticSlice(*input, sliceStart, sliceEnd, expr, "range_down");
        }

        if (!expr.left().type) {
            reportUnsupported("indexed range base type", expr);
            return nullptr;
        }

        grh::Value* base = convert(expr.left());
        if (!base) {
            return nullptr;
        }

        grh::Value* lsbIndex = base;
        if (*width > 1) {
            grh::Value* widthValue =
                createIntConstant(*width - 1, *expr.left().type, "range_down_width");
            if (!widthValue) {
                return nullptr;
            }
            lsbIndex = buildBinaryOp(grh::OperationKind::kSub, *base, *widthValue, expr.left(),
                                     "range_down_base");
            if (!lsbIndex) {
                return nullptr;
            }
        }

        grh::Value* offset =
            translateDynamicIndex(expr.value(), *lsbIndex, expr.left(), "range_down_index");
        if (!offset) {
            return nullptr;
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

grh::Netlist Elaborate::convert(const slang::ast::RootSymbol& root) {
    sourceManager_ = root.getCompilation().getSourceManager();
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
            TypeHelper::Info typeInfo = TypeHelper::analyze(type, *port, diagnostics_);
            const int64_t width = typeInfo.width > 0 ? typeInfo.width : 1;
            const bool isSigned = typeInfo.isSigned;

            std::string portName(port->name);
            grh::Value& value = graph.createValue(portName, width, isSigned);
            applyDebug(value, makeDebugInfo(sourceManager_, port));
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
    applyDebug(op, makeDebugInfo(sourceManager_, &instance));

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

void Elaborate::processNetInitializers(const slang::ast::InstanceBodySymbol& body,
                                       grh::Graph& graph) {
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
        grh::Value* rhsValue = converter.convert(*init);
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
        .memMemo = peekMemMemo(body),
        .origin = &assign,
        .diagnostics = diagnostics_,
        .sourceManager = sourceManager_};
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
        .diagnostics = diagnostics_,
        .sourceManager = sourceManager_};
    ContinuousAssignLHSConverter lhsConverter(lhsContext, memo);
    lhsConverter.convert(*assignment, *rhsValue);
}

void Elaborate::processCombAlways(const slang::ast::ProceduralBlockSymbol& block,
                                  const slang::ast::InstanceBodySymbol& body,
                                  grh::Graph& graph) {
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
                                 grh::Graph& graph) {
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
                                grh::Graph& parentGraph, grh::Netlist& netlist) {
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
    grh::Graph* childGraph = materializeGraph(childInstance, netlist, childCreated);
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
                                        grh::Graph& parentGraph, grh::Graph& targetGraph) {
    std::string baseName =
        childInstance.name.empty() ? std::string("inst") : std::string(childInstance.name);
    std::string opName = makeUniqueOperationName(parentGraph, baseName);
    grh::Operation& op = parentGraph.createOperation(grh::OperationKind::kInstance, opName);
    applyDebug(op, makeDebugInfo(sourceManager_, &childInstance));

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
        bool convert(const slang::ast::Expression& expr, grh::Value& rhsValue,
                     std::vector<WriteResult>& outResults) {
            return lowerExpression(expr, rhsValue, outResults);
        }
    };

    auto makePortValue = [&](const slang::ast::PortSymbol& port) -> grh::Value* {
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
        while (parentGraph.findValue(candidate)) {
            candidate = base;
            candidate.push_back('_');
            candidate.append(std::to_string(++suffix));
        }
        const int64_t width = info.width > 0 ? info.width : 1;
        grh::Value& value = parentGraph.createValue(candidate, width, info.isSigned);
        applyDebug(value, makeDebugInfo(sourceManager_, &port));
        return &value;
    };

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

            switch (port->direction) {
            case slang::ast::ArgumentDirection::In: {
                if (!expr) {
                    break;
                }
                grh::Value* value = resolveConnectionValue(*expr, parentGraph, port);
                if (!value) {
                    break;
                }
                op.addOperand(*value);
                inputPortNames.emplace_back(port->name);
                break;
            }
            case slang::ast::ArgumentDirection::Out: {
                grh::Value* resolved = expr ? resolveConnectionValue(*expr, parentGraph, port)
                                            : nullptr;
                if (resolved && !resolved->definingOp()) {
                    op.addResult(*resolved);
                    outputPortNames.emplace_back(port->name);
                    break;
                }

                grh::Value* resultValue = makePortValue(*port);
                if (!resultValue) {
                    break;
                }
                op.addResult(*resultValue);
                outputPortNames.emplace_back(port->name);

                const slang::ast::Expression* targetExpr = expr;
                if (targetExpr && targetExpr->kind == slang::ast::ExpressionKind::Assignment) {
                    const auto& assign = targetExpr->as<slang::ast::AssignmentExpression>();
                    if (assign.isLValueArg()) {
                        targetExpr = &assign.left();
                    }
                }
                if (targetExpr && portWriteMemo) {
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
                    if (lhsConverter.convert(*targetExpr, *resultValue, writeResults)) {
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

    op.setAttribute("moduleName", targetGraph.symbol());
    op.setAttribute("instanceName", instanceName);
    op.setAttribute("inputPortName", inputPortNames);
    op.setAttribute("outputPortName", outputPortNames);
}

void Elaborate::createBlackboxOperation(const slang::ast::InstanceSymbol& childInstance,
                                        grh::Graph& parentGraph, const BlackboxMemoEntry& memo) {
    std::string baseName =
        childInstance.name.empty() ? std::string("inst") : std::string(childInstance.name);
    std::string opName = makeUniqueOperationName(parentGraph, baseName);
    grh::Operation& op = parentGraph.createOperation(grh::OperationKind::kBlackbox, opName);
    applyDebug(op, makeDebugInfo(sourceManager_, &childInstance));

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

    std::vector<std::string> inputPortNames;
    std::vector<std::string> outputPortNames;
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
        if (!expr) {
            continue;
        }

        grh::Value* value = resolveConnectionValue(*expr, parentGraph, port);
        if (!value) {
            continue;
        }

        if (diagnostics_ && portMeta.width > 0 && value->width() != portMeta.width) {
            std::ostringstream oss;
            oss << "Port width mismatch for " << portMeta.name << " (expected " << portMeta.width
                << ", got " << value->width() << ")";
            diagnostics_->nyi(*port, oss.str());
        }

        switch (portMeta.direction) {
        case slang::ast::ArgumentDirection::In:
            op.addOperand(*value);
            inputPortNames.emplace_back(portMeta.name);
            break;
        case slang::ast::ArgumentDirection::Out:
            op.addResult(*value);
            outputPortNames.emplace_back(portMeta.name);
            break;
        default:
            if (diagnostics_) {
                diagnostics_->nyi(*port,
                                  "InOut/Ref port directions are not supported for blackbox");
            }
            break;
        }
    }

    std::vector<std::string> parameterNames;
    std::vector<std::string> parameterValues;
    parameterNames.reserve(memo.parameters.size());
    parameterValues.reserve(memo.parameters.size());
    for (const BlackboxParameter& param : memo.parameters) {
        parameterNames.push_back(param.name);
        parameterValues.push_back(param.value);
    }

    op.setAttribute("moduleName", memo.moduleName);
    op.setAttribute("instanceName", instanceName);
    op.setAttribute("inputPortName", inputPortNames);
    op.setAttribute("outputPortName", outputPortNames);
    op.setAttribute("parameterNames", parameterNames);
    op.setAttribute("parameterValues", parameterValues);
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
    applyDebug(value, makeDebugInfo(sourceManager_, &symbol));
    registerValueForSymbol(symbol, value);
    return &value;
}

grh::Value* Elaborate::resolveConnectionValue(const slang::ast::Expression& expr,
                                              grh::Graph& graph,
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
            return nullptr;
        }
    }

    if (targetExpr->kind == slang::ast::ExpressionKind::HierarchicalValue) {
        if (diagnostics_ && origin) {
            diagnostics_->nyi(*origin, "Hierarchical port connections are not supported yet");
        }
        return nullptr;
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
    if (grh::Value* value = converter.convert(*targetExpr)) {
        return value;
    }

    if (targetExpr->kind == slang::ast::ExpressionKind::NamedValue) {
        const auto& named = targetExpr->as<slang::ast::NamedValueExpression>();
        return ensureValueForSymbol(named.symbol, graph);
    }

    return nullptr;
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
    ensureMemState(body, graph);
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
        // 跳过 memory（非标量寄存器）的 reg 视图，在 ensureMemState 中生成其 kMemory 占位符。
        if (deriveMemoryLayout(*entry.type, *entry.symbol, diagnostics_)) {
            continue;
        }

        grh::Value* value = ensureValueForSymbol(*entry.symbol, graph);
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

        grh::OperationKind opKind = grh::OperationKind::kRegister;
        std::optional<std::string> rstPolarity;
        if (asyncInfo) {
            entry.asyncResetExpr = asyncInfo->expr;
            entry.asyncResetEdge = asyncInfo->edge;
            if (entry.asyncResetExpr && entry.asyncResetEdge != slang::ast::EdgeKind::None &&
                entry.asyncResetEdge != slang::ast::EdgeKind::BothEdges) {
                const bool activeHigh = entry.asyncResetEdge == slang::ast::EdgeKind::PosEdge;
                rstPolarity = makeRstPolarity(activeHigh);
                opKind = grh::OperationKind::kRegisterArst;
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
            opKind = grh::OperationKind::kRegisterRst;
        }

        if (entry.multiDriver) {
            // Leave stateOp unbound; multi-driver signals will be split per driving block later.
            continue;
        }

        std::string opName = makeOperationNameForSymbol(*entry.symbol, "register", graph);
        grh::Operation& op = graph.createOperation(opKind, opName);
        applyDebug(op, makeDebugInfo(sourceManager_, entry.symbol));
        op.addResult(*value);
        op.setAttribute("clkPolarity", *clkPolarity);
        if (rstPolarity) {
            op.setAttribute("rstPolarity", *rstPolarity);
        }
        entry.stateOp = &op;
    }
}

void Elaborate::ensureMemState(const slang::ast::InstanceBodySymbol& body, grh::Graph& graph) {
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
            grh::Operation& op = graph.createOperation(grh::OperationKind::kMemory, opName);
            applyDebug(op, makeDebugInfo(sourceManager_, entry.symbol));
            op.setAttribute("width", layout->rowWidth);
            op.setAttribute("row", layout->rowCount);
            op.setAttribute("isSigned", layout->isSigned);
            entry.stateOp = &op;
            // 将同名 symbol 在 regMemo 里的视图一并更新 stateOp，方便测试从 regMemo 访问到 memory。
            if (auto regIt = regMemo_.find(&body); regIt != regMemo_.end()) {
                for (SignalMemoEntry& regEntry : regIt->second) {
                    if (regEntry.symbol == entry.symbol) {
                        regEntry.stateOp = &op;
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
                                      grh::Graph& graph) {
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
                    entry.ports.push_back(std::move(memoPort));
                    break;
                case slang::ast::ArgumentDirection::InOut:
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
        if (driver == MemoDriverKind::Reg && block) {
            auto& owners = regDriverBlocks[&symbol];
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
                    if (argInfo.direction != slang::ast::ArgumentDirection::Out) {
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

        if (netOnly || treatAsDriverlessNet) {
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
                    arg->direction != slang::ast::ArgumentDirection::Out) {
                    report(*arg, "DPI import 仅支持 input/output 方向参数");
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
                                      grh::Graph& graph) {
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
        grh::Operation& op = graph.createOperation(grh::OperationKind::kDpicImport, opName);
        applyDebug(op, makeDebugInfo(sourceManager_, entry.symbol));
        entry.importOp = &op;

        std::vector<std::string> directions;
        std::vector<int64_t> widths;
        std::vector<std::string> names;
        directions.reserve(entry.args.size());
        widths.reserve(entry.args.size());
        names.reserve(entry.args.size());

        for (const DpiImportArg& arg : entry.args) {
            directions.emplace_back(arg.direction == slang::ast::ArgumentDirection::In ? "input"
                                                                                       : "output");
            widths.push_back(arg.width);
            names.push_back(arg.name);
        }

        op.setAttribute("argsDirection", std::move(directions));
        op.setAttribute("argsWidth", std::move(widths));
        op.setAttribute("argsName", std::move(names));
        if (!entry.cIdentifier.empty()) {
            op.setAttribute("cIdentifier", entry.cIdentifier);
        }
    }
}

} // namespace wolf_sv
