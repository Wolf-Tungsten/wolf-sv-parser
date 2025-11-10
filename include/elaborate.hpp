#pragma once

#include "grh.hpp"
#include "slang/ast/EvalContext.h"

/** \file elaborate.hpp
 *  \brief Entry points for converting slang AST into the GRH representation.
 *
 *  Stage 2 focuses on wiring up a minimal pipeline that can traverse the slang
 *  AST, populate GRH graph placeholders, and surface diagnostic messages for
 *  unimplemented features.
 */

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "slang/text/SourceLocation.h"

namespace slang {
class SVInt;
} // namespace slang

namespace slang::ast {
class InstanceSymbol;
class InstanceArraySymbol;
class InstanceBodySymbol;
class ValueSymbol;
class Type;
class GenerateBlockSymbol;
class GenerateBlockArraySymbol;
class Expression;
class UnaryExpression;
class BinaryExpression;
class ConditionalExpression;
class ConcatenationExpression;
class ReplicationExpression;
class ConversionExpression;
class AssignmentExpression;
class NamedValueExpression;
class RangeSelectExpression;
class ElementSelectExpression;
class MemberAccessExpression;
class RootSymbol;
class Symbol;
class ProceduralBlockSymbol;
class ContinuousAssignSymbol;
} // namespace slang::ast

namespace wolf_sv {

/// Diagnostic categories emitted by the elaboration pipeline.
enum class ElaborateDiagnosticKind {
    Todo,
    NotYetImplemented
};

/// A diagnostic message generated during elaboration.
struct ElaborateDiagnostic {
    ElaborateDiagnosticKind kind;
    std::string message;
    std::string originSymbol;
    std::optional<slang::SourceLocation> location;
};

/// Collects diagnostic messages generated during elaboration.
class ElaborateDiagnostics {
public:
    void todo(const slang::ast::Symbol& symbol, std::string message);
    void nyi(const slang::ast::Symbol& symbol, std::string message);

    const std::vector<ElaborateDiagnostic>& messages() const noexcept { return messages_; }
    bool empty() const noexcept { return messages_.empty(); }

private:
    void add(ElaborateDiagnosticKind kind, const slang::ast::Symbol& symbol,
             std::string message);

    std::vector<ElaborateDiagnostic> messages_;
};

/// Options controlling the elaboration pipeline behaviour.
struct ElaborateOptions {
    /// When true, create placeholder operations describing the module body.
    bool emitPlaceholders = true;
};

/// Captures a flattened field inside a memoized signal.
struct SignalMemoField {
    std::string path;
    int64_t msb = 0;
    int64_t lsb = 0;
    bool isSigned = false;
};

/// Captures a memoized signal entry discovered during elaboration.
struct SignalMemoEntry {
    const slang::ast::ValueSymbol* symbol = nullptr;
    const slang::ast::Type* type = nullptr;
    int64_t width = 0;
    bool isSigned = false;
    std::vector<SignalMemoField> fields;
    grh::Value* value = nullptr;
    grh::Operation* stateOp = nullptr;
    const slang::ast::ProceduralBlockSymbol* drivingBlock = nullptr;
};

/// Records pending writes against memoized signals before SSA write-back.
class WriteBackMemo {
public:
    enum class AssignmentKind {
        Continuous,
        Procedural
    };

    struct Slice {
        std::string path;
        int64_t msb = 0;
        int64_t lsb = 0;
        grh::Value* value = nullptr;
        const slang::ast::Expression* originExpr = nullptr;
    };

    struct Entry {
        const SignalMemoEntry* target = nullptr;
        AssignmentKind kind = AssignmentKind::Continuous;
        const slang::ast::Symbol* originSymbol = nullptr;
        std::vector<Slice> slices;
    };

    void recordWrite(const SignalMemoEntry& target, AssignmentKind kind,
                     const slang::ast::Symbol* originSymbol, std::vector<Slice> slices);
    std::span<const Entry> entries() const noexcept { return entries_; }
    bool empty() const noexcept { return entries_.empty(); }
    void clear();
    void finalize(grh::Graph& graph, ElaborateDiagnostics* diagnostics);

private:
    std::string makeOperationName(const Entry& entry, std::string_view suffix);
    std::string makeValueName(const Entry& entry, std::string_view suffix);
    const slang::ast::Symbol* originFor(const Entry& entry) const;
    void reportIssue(const Entry& entry, std::string message,
                     ElaborateDiagnostics* diagnostics) const;
    grh::Value* composeSlices(Entry& entry, grh::Graph& graph,
                              ElaborateDiagnostics* diagnostics);
    void attachToTarget(const Entry& entry, grh::Value& composedValue, grh::Graph& graph,
                        ElaborateDiagnostics* diagnostics);
    grh::Value* createZeroValue(const Entry& entry, int64_t width, grh::Graph& graph);

    std::vector<Entry> entries_;
    std::size_t nameCounter_ = 0;
};

/// Converts RHS expressions into GRH operations / values.
class RHSConverter {
public:
    struct Context {
        grh::Graph* graph = nullptr;
        std::span<const SignalMemoEntry> netMemo;
        std::span<const SignalMemoEntry> regMemo;
        const slang::ast::Symbol* origin = nullptr;
        ElaborateDiagnostics* diagnostics = nullptr;
    };

    explicit RHSConverter(Context context);
    virtual ~RHSConverter() = default;

    /// Lowers the provided expression into the GRH graph, returning the resulting value.
    grh::Value* convert(const slang::ast::Expression& expr);

protected:
    struct TypeInfo {
        int64_t width = 0;
        bool isSigned = false;
    };

    grh::Graph& graph() const noexcept { return *graph_; }
    ElaborateDiagnostics* diagnostics() const noexcept { return diagnostics_; }
    const slang::ast::Symbol* origin() const noexcept { return origin_; }

    virtual std::string makeValueName(std::string_view hint, std::size_t index) const;
    virtual std::string makeOperationName(std::string_view hint, std::size_t index) const;
    virtual grh::Value* convertElementSelect(const slang::ast::ElementSelectExpression& expr);
    virtual grh::Value* convertRangeSelect(const slang::ast::RangeSelectExpression& expr);
    virtual grh::Value* convertMemberAccess(const slang::ast::MemberAccessExpression& expr);
    virtual grh::Value*
    handleMemoEntry(const slang::ast::NamedValueExpression& expr, const SignalMemoEntry& entry);

    grh::Value& createTemporaryValue(const slang::ast::Type& type, std::string_view hint);
    grh::Operation& createOperation(grh::OperationKind kind, std::string_view hint);
    grh::Value* createConstantValue(const slang::SVInt& value, const slang::ast::Type& type,
                                    std::string_view literalHint);
    grh::Value* createZeroValue(const slang::ast::Type& type, std::string_view hint);
    grh::Value* buildUnaryOp(grh::OperationKind kind, grh::Value& operand,
                             const slang::ast::Expression& originExpr, std::string_view hint);
    grh::Value* buildBinaryOp(grh::OperationKind kind, grh::Value& lhs, grh::Value& rhs,
                              const slang::ast::Expression& originExpr, std::string_view hint);
    grh::Value* buildMux(grh::Value& cond, grh::Value& onTrue, grh::Value& onFalse,
                         const slang::ast::Expression& originExpr);
    grh::Value* buildAssign(grh::Value& input, const slang::ast::Expression& originExpr,
                            std::string_view hint);

    const SignalMemoEntry* findMemoEntry(const slang::ast::ValueSymbol& symbol) const;
    grh::Value* resolveMemoValue(const SignalMemoEntry& entry);
    grh::Value* resolveGraphValue(const slang::ast::ValueSymbol& symbol);
    TypeInfo deriveTypeInfo(const slang::ast::Type& type) const;
    void reportUnsupported(std::string_view what, const slang::ast::Expression& expr);
    slang::ast::EvalContext& ensureEvalContext();
    std::optional<int64_t> evaluateConstantInt(const slang::ast::Expression& expr);

private:
    grh::Value* convertExpression(const slang::ast::Expression& expr);
    grh::Value* convertNamedValue(const slang::ast::Expression& expr);
    grh::Value* convertLiteral(const slang::ast::Expression& expr);
    grh::Value* convertUnary(const slang::ast::UnaryExpression& expr);
    grh::Value* convertBinary(const slang::ast::BinaryExpression& expr);
    grh::Value* convertConditional(const slang::ast::ConditionalExpression& expr);
    grh::Value* convertConcatenation(const slang::ast::ConcatenationExpression& expr);
    grh::Value* convertReplication(const slang::ast::ReplicationExpression& expr);
    grh::Value* convertConversion(const slang::ast::ConversionExpression& expr);
    grh::Value* materializeParameterValue(const slang::ast::NamedValueExpression& expr);
    std::string formatConstantLiteral(const slang::SVInt& value,
                                      const slang::ast::Type& type) const;

    grh::Graph* graph_ = nullptr;
    const slang::ast::Symbol* origin_ = nullptr;
    ElaborateDiagnostics* diagnostics_ = nullptr;
    std::span<const SignalMemoEntry> netMemo_;
    std::span<const SignalMemoEntry> regMemo_;
    std::unordered_map<const slang::ast::Expression*, grh::Value*> cache_;
    std::size_t valueCounter_ = 0;
    std::size_t operationCounter_ = 0;
    std::size_t instanceId_ = 0;
    std::unique_ptr<slang::ast::EvalContext> evalContext_;
};

/// Converts RHS expressions specifically for combinational contexts.
class CombRHSConverter : public RHSConverter {
public:
    explicit CombRHSConverter(Context context);

protected:
    grh::Value* convertElementSelect(const slang::ast::ElementSelectExpression& expr) override;
    grh::Value* convertRangeSelect(const slang::ast::RangeSelectExpression& expr) override;
    grh::Value* convertMemberAccess(const slang::ast::MemberAccessExpression& expr) override;

private:
    struct SliceRange {
        int64_t msb = 0;
        int64_t lsb = 0;
    };

    const SignalMemoEntry* findMemoEntryFromExpression(const slang::ast::Expression& expr) const;
    std::optional<SliceRange>
    deriveStructFieldSlice(const slang::ast::MemberAccessExpression& expr) const;
    grh::Value* buildStaticSlice(grh::Value& input, int64_t sliceStart, int64_t sliceEnd,
                                 const slang::ast::Expression& originExpr,
                                 std::string_view hint);
    grh::Value* buildDynamicSlice(grh::Value& input, grh::Value& offset, int64_t sliceWidth,
                                  const slang::ast::Expression& originExpr,
                                  std::string_view hint);
    grh::Value* buildArraySlice(grh::Value& input, grh::Value& index, int64_t sliceWidth,
                                const slang::ast::Expression& originExpr);
    grh::Value* buildMemoryRead(const SignalMemoEntry& entry,
                                const slang::ast::ElementSelectExpression& expr);
    grh::Value* createIntConstant(int64_t value, const slang::ast::Type& type,
                                  std::string_view hint);
};

/// Converts LHS expressions into write-back slices.
class LHSConverter {
public:
    struct Context {
        grh::Graph* graph = nullptr;
        std::span<const SignalMemoEntry> netMemo;
        const slang::ast::Symbol* origin = nullptr;
        ElaborateDiagnostics* diagnostics = nullptr;
    };

    struct WriteResult {
        const SignalMemoEntry* target = nullptr;
        std::vector<WriteBackMemo::Slice> slices;
    };

    explicit LHSConverter(Context context);
    virtual ~LHSConverter() = default;

protected:
    bool lower(const slang::ast::AssignmentExpression& assignment, grh::Value& rhsValue,
               std::vector<WriteResult>& outResults);
    virtual bool allowReplication() const { return false; }
    grh::Graph& graph() const noexcept { return *graph_; }
    ElaborateDiagnostics* diagnostics() const noexcept { return diagnostics_; }
    const slang::ast::Symbol* origin() const noexcept { return origin_; }

private:
    struct BitRange {
        int64_t msb = 0;
        int64_t lsb = 0;
    };

    bool processLhs(const slang::ast::Expression& expr, grh::Value& rhsValue);
    bool handleConcatenation(const slang::ast::ConcatenationExpression& concat,
                             grh::Value& rhsValue);
    bool handleLeaf(const slang::ast::Expression& expr, grh::Value& rhsValue);
    const SignalMemoEntry* resolveMemoEntry(const slang::ast::Expression& expr) const;
    const SignalMemoEntry* findMemoEntry(const slang::ast::ValueSymbol& symbol) const;
    std::optional<BitRange> resolveBitRange(const SignalMemoEntry& entry,
                                            const slang::ast::Expression& expr,
                                            std::string& pathOut);
    std::optional<BitRange> resolveRangeSelect(const SignalMemoEntry& entry,
                                               const slang::ast::RangeSelectExpression& expr,
                                               const std::string& basePath, std::string& pathOut);
    std::optional<std::string> buildFieldPath(const slang::ast::Expression& expr);
    std::optional<int64_t> evaluateConstant(const slang::ast::Expression& expr);
    std::optional<BitRange> lookupRangeByPath(const SignalMemoEntry& entry,
                                              std::string_view path) const;
    grh::Value* createSliceValue(grh::Value& source, int64_t lsb, int64_t msb,
                                 const slang::ast::Expression& originExpr);
    void report(std::string message);
    slang::ast::EvalContext& ensureEvalContext();
    static bool pathMatchesDescendant(std::string_view parent, std::string_view candidate);
    void flushPending(std::vector<WriteResult>& outResults);

    grh::Graph* graph_ = nullptr;
    std::span<const SignalMemoEntry> netMemo_;
    const slang::ast::Symbol* origin_ = nullptr;
    ElaborateDiagnostics* diagnostics_ = nullptr;
    std::unordered_map<const SignalMemoEntry*, std::vector<WriteBackMemo::Slice>> pending_;
    std::unique_ptr<slang::ast::EvalContext> evalContext_;
    std::size_t sliceCounter_ = 0;
};

/// LHS converter specialization for continuous assigns.
class ContinuousAssignLHSConverter : public LHSConverter {
public:
    ContinuousAssignLHSConverter(Context context, WriteBackMemo& memo);

    bool convert(const slang::ast::AssignmentExpression& assignment, grh::Value& rhsValue);

private:
    WriteBackMemo& memo_;
};

/// Elaborates slang AST into GRH representation.
class Elaborate {
public:
    explicit Elaborate(ElaborateDiagnostics* diagnostics = nullptr,
                       ElaborateOptions options = {});

    /// Convert the provided slang AST root symbol into a GRH netlist.
    grh::Netlist convert(const slang::ast::RootSymbol& root);

    /// Returns memoized net declarations for the provided module body.
    std::span<const SignalMemoEntry>
    peekNetMemo(const slang::ast::InstanceBodySymbol& body) const;

    /// Returns memoized register declarations for the provided module body.
    std::span<const SignalMemoEntry>
    peekRegMemo(const slang::ast::InstanceBodySymbol& body) const;

private:
    grh::Graph* materializeGraph(const slang::ast::InstanceSymbol& instance,
                                 grh::Netlist& netlist, bool& wasCreated);
    void populatePorts(const slang::ast::InstanceSymbol& instance,
                       const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);
    void emitModulePlaceholder(const slang::ast::InstanceSymbol& instance, grh::Graph& graph);
    void convertInstanceBody(const slang::ast::InstanceSymbol& instance, grh::Graph& graph,
                             grh::Netlist& netlist);
    void processInstanceArray(const slang::ast::InstanceArraySymbol& array, grh::Graph& graph,
                              grh::Netlist& netlist);
    void processGenerateBlock(const slang::ast::GenerateBlockSymbol& block, grh::Graph& graph,
                              grh::Netlist& netlist);
    void processGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                                   grh::Graph& graph, grh::Netlist& netlist);
    void processContinuousAssign(const slang::ast::ContinuousAssignSymbol& assign,
                                 const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);
    void processCombAlways(const slang::ast::ProceduralBlockSymbol& block,
                           const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);
    void processInstance(const slang::ast::InstanceSymbol& childInstance, grh::Graph& parentGraph,
                         grh::Netlist& netlist);
    void createInstanceOperation(const slang::ast::InstanceSymbol& childInstance,
                                 grh::Graph& parentGraph, grh::Graph& targetGraph);
    grh::Value* ensureValueForSymbol(const slang::ast::ValueSymbol& symbol, grh::Graph& graph);
    grh::Value* resolveConnectionValue(const slang::ast::Expression& expr, grh::Graph& graph,
                                       const slang::ast::Symbol* origin);
    std::string makeUniqueOperationName(grh::Graph& graph, std::string baseName);
    std::string makeOperationNameForSymbol(const slang::ast::ValueSymbol& symbol,
                                           std::string_view fallback, grh::Graph& graph);
    void registerValueForSymbol(const slang::ast::Symbol& symbol, grh::Value& value);
    void collectSignalMemos(const slang::ast::InstanceBodySymbol& body);
    void materializeSignalMemos(const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);
    void ensureNetValues(const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);
    void ensureRegState(const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);
    WriteBackMemo& ensureWriteBackMemo(const slang::ast::InstanceBodySymbol& body);
    void finalizeWriteBackMemo(const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);

    ElaborateDiagnostics* diagnostics_;
    ElaborateOptions options_;
    std::size_t placeholderCounter_ = 0;
    std::size_t instanceCounter_ = 0;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, grh::Graph*> graphByBody_;
    std::unordered_set<const slang::ast::InstanceBodySymbol*> processedBodies_;
    std::unordered_map<const slang::ast::Symbol*, grh::Value*> valueCache_;
    std::unordered_map<std::string, std::size_t> graphNameUsage_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<SignalMemoEntry>>
        netMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<SignalMemoEntry>>
        regMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, WriteBackMemo> writeBackMemo_;
};

} // namespace wolf_sv
