#ifndef WOLF_SV_ELABORATE_HPP
#define WOLF_SV_ELABORATE_HPP

#include "grh.hpp"
#include "slang/ast/EvalContext.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"

/** \file elaborate.hpp
 *  \brief Entry points for converting slang AST into the GRH representation.
 *
 *  Stage 2 focuses on wiring up a minimal pipeline that can traverse the slang
 *  AST, populate GRH graph placeholders, and surface diagnostic messages for
 *  unimplemented features.
 */

#include <memory>
#include <optional>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "slang/text/SourceLocation.h"
#include "slang/ast/symbols/SubroutineSymbols.h"

namespace slang {
class SVInt;
class SourceManager;
} // namespace slang

namespace slang::ast {
class InstanceSymbol;
class InstanceArraySymbol;
class InstanceBodySymbol;
class ValueSymbol;
enum class EdgeKind;
class Type;
class PortSymbol;
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
class CallExpression;
class ImmediateAssertionStatement;
class RootSymbol;
class Symbol;
class ProceduralBlockSymbol;
class ContinuousAssignSymbol;
} // namespace slang::ast

namespace wolf_sv_parser {
using ValueId = grh::ir::ValueId;
using OperationId = grh::ir::OperationId;
using SymbolId = grh::ir::SymbolId;


/// Diagnostic categories emitted by the elaboration pipeline.
enum class ElaborateDiagnosticKind {
    Todo,
    NotYetImplemented,
    Warning
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
    void warn(const slang::ast::Symbol& symbol, std::string message);

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
    /// This is disabled by default now that module bodies are elaborated.
    bool emitPlaceholders = false;
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
    ValueId value = ValueId::invalid();
    OperationId stateOp = OperationId::invalid();
    const slang::ast::ProceduralBlockSymbol* drivingBlock = nullptr;
    const slang::ast::Expression* asyncResetExpr = nullptr;
    slang::ast::EdgeKind asyncResetEdge = {};
    const slang::ast::ValueSymbol* syncResetSymbol = nullptr;
    bool syncResetActiveHigh = true;
    bool multiDriver = false;
};

/// Describes a single DPI import argument lowered during elaboration.
struct DpiImportArg {
    std::string name;
    slang::ast::ArgumentDirection direction = slang::ast::ArgumentDirection::In;
    int64_t width = 0;
    bool isSigned = false;
    std::vector<SignalMemoField> fields;
};

/// Captures DPI import declarations discovered in a module body.
struct DpiImportEntry {
    const slang::ast::SubroutineSymbol* symbol = nullptr;
    std::string cIdentifier;
    std::vector<DpiImportArg> args;
    OperationId importOp = OperationId::invalid();
};

/// Captures a port entry for a blackbox module.
struct BlackboxPort {
    const slang::ast::PortSymbol* symbol = nullptr;
    std::string name;
    slang::ast::ArgumentDirection direction = slang::ast::ArgumentDirection::In;
    int64_t width = 0;
    bool isSigned = false;
};

/// Captures parameter metadata for a blackbox module.
struct BlackboxParameter {
    std::string name;
    std::string value;
};

/// Records blackbox module metadata for later instantiation.
struct BlackboxMemoEntry {
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string moduleName;
    bool isBlackbox = false;
    bool hasExplicitAttribute = false;
    bool hasImplementation = false;
    std::vector<BlackboxPort> ports;
    std::vector<BlackboxParameter> parameters;
};

/// Tracks GRH values and memo overrides for an inout port.
struct InoutPortMemo {
    const slang::ast::ValueSymbol* symbol = nullptr;
    ValueId in = ValueId::invalid();
    ValueId out = ValueId::invalid();
    ValueId oe = ValueId::invalid();
    SignalMemoEntry outEntry;
    SignalMemoEntry oeEntry;
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
        ValueId value = ValueId::invalid();
        const slang::ast::Expression* originExpr = nullptr;
    };

    struct Entry {
        const SignalMemoEntry* target = nullptr;
        AssignmentKind kind = AssignmentKind::Continuous;
        const slang::ast::Symbol* originSymbol = nullptr;
        std::vector<Slice> slices;
        bool consumed = false;
    };

    void recordWrite(const SignalMemoEntry& target, AssignmentKind kind,
                     const slang::ast::Symbol* originSymbol, std::vector<Slice> slices);
    struct MultiDriverPart {
        int64_t msb = 0;
        int64_t lsb = 0;
        ValueId value = ValueId::invalid();
    };
    struct MultiDriverBucket {
        const SignalMemoEntry* target = nullptr;
        std::vector<MultiDriverPart> parts;
    };
    void recordMultiDriverPart(const SignalMemoEntry& target, MultiDriverPart part);
    std::span<const Entry> entries() const noexcept { return entries_; }
    std::span<Entry> entriesMutable() noexcept { return entries_; }
    bool empty() const noexcept { return entries_.empty(); }
    void clear();
    void finalize(grh::ir::Graph& graph, ElaborateDiagnostics* diagnostics);
    void setSourceManager(const slang::SourceManager* sourceManager) { sourceManager_ = sourceManager; }

private:
    std::string makeOperationName(const Entry& entry, std::string_view suffix);
    std::string makeValueName(const Entry& entry, std::string_view suffix);
    const slang::ast::Symbol* originFor(const Entry& entry) const;
    std::optional<grh::ir::SrcLoc> srcLocForEntry(const Entry& entry) const;
    void reportIssue(const Entry& entry, std::string message,
                     ElaborateDiagnostics* diagnostics) const;
    ValueId composeSlices(Entry& entry, grh::ir::Graph& graph,
                          ElaborateDiagnostics* diagnostics);
    void attachToTarget(const Entry& entry, ValueId composedValue, grh::ir::Graph& graph,
                        ElaborateDiagnostics* diagnostics);
    ValueId createZeroValue(const Entry& entry, int64_t width, grh::ir::Graph& graph);
    bool tryLowerLatch(Entry& entry, ValueId dataValue, grh::ir::Graph& graph,
                       ElaborateDiagnostics* diagnostics);

    std::vector<Entry> entries_;
    std::unordered_map<ValueId, MultiDriverBucket, grh::ir::ValueIdHash> multiDriverParts_;
    std::size_t nameCounter_ = 0;
    const slang::SourceManager* sourceManager_ = nullptr;
};

/// Converts RHS expressions into GRH operations / values.
class RHSConverter {
public:
    struct Context {
        grh::ir::Graph* graph = nullptr;
        std::span<const SignalMemoEntry> netMemo;
        std::span<const SignalMemoEntry> regMemo;
        std::span<const SignalMemoEntry> memMemo;
        const std::unordered_map<const slang::ast::ValueSymbol*, const SignalMemoEntry*>*
            inoutOverrides = nullptr;
        const slang::ast::Symbol* origin = nullptr;
        ElaborateDiagnostics* diagnostics = nullptr;
        const slang::SourceManager* sourceManager = nullptr;
        const slang::ast::ProceduralBlockSymbol* preferredBlock = nullptr;
    };

    explicit RHSConverter(Context context);
    virtual ~RHSConverter() = default;

    /// Lowers the provided expression into the GRH graph, returning the resulting value.
    ValueId convert(const slang::ast::Expression& expr);
    void clearCache();

protected:
    struct TypeInfo {
        int64_t width = 0;
        bool isSigned = false;
    };

    grh::ir::Graph& graph() const noexcept { return *graph_; }
    ElaborateDiagnostics* diagnostics() const noexcept { return diagnostics_; }
    const slang::ast::Symbol* origin() const noexcept { return origin_; }

    virtual std::string makeValueName(std::string_view hint, std::size_t index) const;
    virtual std::string makeOperationName(std::string_view hint, std::size_t index) const;
    virtual ValueId convertElementSelect(const slang::ast::ElementSelectExpression& expr);
    virtual ValueId convertRangeSelect(const slang::ast::RangeSelectExpression& expr);
    virtual ValueId convertMemberAccess(const slang::ast::MemberAccessExpression& expr);
    virtual ValueId
    handleMemoEntry(const slang::ast::NamedValueExpression& expr, const SignalMemoEntry& entry);
    virtual ValueId handleCustomNamedValue(const slang::ast::NamedValueExpression& expr);

    ValueId createTemporaryValue(const slang::ast::Type& type, std::string_view hint);
    OperationId createOperation(grh::ir::OperationKind kind, std::string_view hint);
    ValueId createConstantValue(const slang::SVInt& value, const slang::ast::Type& type,
                                    std::string_view literalHint);
    ValueId createZeroValue(const slang::ast::Type& type, std::string_view hint);
    ValueId buildUnaryOp(grh::ir::OperationKind kind, ValueId operand,
                             const slang::ast::Expression& originExpr, std::string_view hint);
    ValueId buildBinaryOp(grh::ir::OperationKind kind, ValueId lhs, ValueId rhs,
                              const slang::ast::Expression& originExpr, std::string_view hint);
    ValueId buildMux(ValueId cond, ValueId onTrue, ValueId onFalse,
                         const slang::ast::Expression& originExpr);
    ValueId buildAssign(ValueId input, const slang::ast::Expression& originExpr,
                            std::string_view hint);
    ValueId resizeValue(ValueId input, const slang::ast::Type& targetType,
                            const TypeInfo& targetInfo, const slang::ast::Expression& originExpr,
                            std::string_view hint);

    const SignalMemoEntry* findMemoEntry(const slang::ast::ValueSymbol& symbol) const;
    ValueId resolveMemoValue(const SignalMemoEntry& entry);
    ValueId resolveGraphValue(const slang::ast::ValueSymbol& symbol);
    TypeInfo deriveTypeInfo(const slang::ast::Type& type) const;
    void reportUnsupported(std::string_view what, const slang::ast::Expression& expr);
    slang::ast::EvalContext& ensureEvalContext();
    std::optional<int64_t> evaluateConstantInt(const slang::ast::Expression& expr);
    std::optional<slang::SVInt> evaluateConstantSvInt(const slang::ast::Expression& expr);

private:
    ValueId convertExpression(const slang::ast::Expression& expr);
    ValueId convertNamedValue(const slang::ast::Expression& expr);
    ValueId convertLiteral(const slang::ast::Expression& expr);
    ValueId convertUnary(const slang::ast::UnaryExpression& expr);
    ValueId convertBinary(const slang::ast::BinaryExpression& expr);
    ValueId convertConditional(const slang::ast::ConditionalExpression& expr);
    ValueId convertConcatenation(const slang::ast::ConcatenationExpression& expr);
    ValueId convertReplication(const slang::ast::ReplicationExpression& expr);
    ValueId convertConversion(const slang::ast::ConversionExpression& expr);
    ValueId convertCall(const slang::ast::CallExpression& expr);
    ValueId reduceToLogicValue(ValueId input, const slang::ast::Expression& originExpr);
    ValueId materializeParameterValue(const slang::ast::NamedValueExpression& expr);
    std::string formatConstantLiteral(const slang::SVInt& value,
                                      const slang::ast::Type& type) const;

    grh::ir::Graph* graph_ = nullptr;
    const slang::ast::Symbol* origin_ = nullptr;
    ElaborateDiagnostics* diagnostics_ = nullptr;
    const slang::SourceManager* sourceManager_ = nullptr;
    const slang::ast::ProceduralBlockSymbol* preferredBlock_ = nullptr;
    std::span<const SignalMemoEntry> netMemo_;
    std::span<const SignalMemoEntry> regMemo_;
    std::span<const SignalMemoEntry> memMemo_;
    const std::unordered_map<const slang::ast::ValueSymbol*, const SignalMemoEntry*>*
        inoutOverrides_ = nullptr;
    std::unordered_map<const slang::ast::Expression*, ValueId> cache_;
    bool suppressCache_ = false;
    std::size_t valueCounter_ = 0;
    std::size_t operationCounter_ = 0;
    std::size_t instanceId_ = 0;
    std::unique_ptr<slang::ast::EvalContext> evalContext_;
    const slang::ast::Expression* currentExpr_ = nullptr;
};

/// Converts RHS expressions specifically for combinational contexts.
class CombRHSConverter : public RHSConverter {
public:
    explicit CombRHSConverter(Context context);

protected:
    ValueId convertElementSelect(const slang::ast::ElementSelectExpression& expr) override;
    ValueId convertRangeSelect(const slang::ast::RangeSelectExpression& expr) override;
    ValueId convertMemberAccess(const slang::ast::MemberAccessExpression& expr) override;
    const SignalMemoEntry* findMemoEntryFromExpression(const slang::ast::Expression& expr) const;

private:
    struct SliceRange {
        int64_t msb = 0;
        int64_t lsb = 0;
    };

    std::optional<SliceRange>
    deriveStructFieldSlice(const slang::ast::MemberAccessExpression& expr) const;
    std::optional<int64_t> translateStaticIndex(const slang::ast::Expression& valueExpr,
                                                int64_t rawIndex) const;
    ValueId translateDynamicIndex(const slang::ast::Expression& valueExpr,
                                      ValueId rawIndex,
                                      const slang::ast::Expression& originExpr,
                                      std::string_view hint);
    ValueId buildStaticSlice(ValueId input, int64_t sliceStart, int64_t sliceEnd,
                                 const slang::ast::Expression& originExpr,
                                 std::string_view hint);
    ValueId buildDynamicSlice(ValueId input, ValueId offset, int64_t sliceWidth,
                                  const slang::ast::Expression& originExpr,
                                  std::string_view hint);
    ValueId buildArraySlice(ValueId input, ValueId index, int64_t sliceWidth,
                                const slang::ast::Expression& originExpr);
    ValueId buildMemoryRead(const SignalMemoEntry& entry,
                                const slang::ast::ElementSelectExpression& expr);
    ValueId createIntConstant(int64_t value, const slang::ast::Type& type,
                                  std::string_view hint);
};

/// Converts LHS expressions into write-back slices.
class LHSConverter {
public:
    struct Context {
        grh::ir::Graph* graph = nullptr;
        std::span<const SignalMemoEntry> netMemo;
        std::span<const SignalMemoEntry> regMemo;
        std::span<const SignalMemoEntry> memMemo;
        const std::unordered_map<const slang::ast::ValueSymbol*, const SignalMemoEntry*>*
            inoutOverrides = nullptr;
        const slang::ast::Symbol* origin = nullptr;
        ElaborateDiagnostics* diagnostics = nullptr;
        const slang::SourceManager* sourceManager = nullptr;
        const slang::ast::ProceduralBlockSymbol* preferredBlock = nullptr;
    };

    struct WriteResult {
        const SignalMemoEntry* target = nullptr;
        std::vector<WriteBackMemo::Slice> slices;
    };

    explicit LHSConverter(Context context);
    virtual ~LHSConverter() = default;

protected:
    bool lower(const slang::ast::AssignmentExpression& assignment, ValueId rhsValue,
               std::vector<WriteResult>& outResults);
    bool lowerExpression(const slang::ast::Expression& expr, ValueId rhsValue,
                         std::vector<WriteResult>& outResults);
    virtual bool allowReplication() const { return false; }
    // Hook to feed contextual constants (e.g., foreach loop indices) into LHS eval.
    virtual void seedEvalContextForLHS(slang::ast::EvalContext&) {}
    grh::ir::Graph& graph() const noexcept { return *graph_; }
    ElaborateDiagnostics* diagnostics() const noexcept { return diagnostics_; }
    const slang::ast::Symbol* origin() const noexcept { return origin_; }
    const SignalMemoEntry* findMemoEntry(const slang::ast::ValueSymbol& symbol) const;
    std::optional<int64_t> evaluateConstant(const slang::ast::Expression& expr);
    slang::ast::EvalContext& ensureEvalContext();

private:
    struct BitRange {
        int64_t msb = 0;
        int64_t lsb = 0;
    };

    bool processLhs(const slang::ast::Expression& expr, ValueId rhsValue);
    bool handleConcatenation(const slang::ast::ConcatenationExpression& concat,
                             ValueId rhsValue);
    bool handleLeaf(const slang::ast::Expression& expr, ValueId rhsValue);
    const SignalMemoEntry* resolveMemoEntry(const slang::ast::Expression& expr) const;
    std::optional<BitRange> resolveBitRange(const SignalMemoEntry& entry,
                                            const slang::ast::Expression& expr,
                                            std::string& pathOut);
    std::optional<BitRange> resolveRangeSelect(const SignalMemoEntry& entry,
                                               const slang::ast::RangeSelectExpression& expr,
                                               const std::string& basePath, std::string& pathOut);
    std::optional<std::string> buildFieldPath(const slang::ast::Expression& expr);

private:
    std::optional<BitRange> lookupRangeByPath(const SignalMemoEntry& entry,
                                              std::string_view path) const;
    ValueId createSliceValue(ValueId source, int64_t lsb, int64_t msb,
                                 const slang::ast::Expression& originExpr);
    void report(std::string message);
    static bool pathMatchesDescendant(std::string_view parent, std::string_view candidate);
    void flushPending(std::vector<WriteResult>& outResults);

    grh::ir::Graph* graph_ = nullptr;
    std::span<const SignalMemoEntry> netMemo_;
    std::span<const SignalMemoEntry> regMemo_;
    std::span<const SignalMemoEntry> memMemo_;
    const std::unordered_map<const slang::ast::ValueSymbol*, const SignalMemoEntry*>*
        inoutOverrides_ = nullptr;
    const slang::ast::Symbol* origin_ = nullptr;
    ElaborateDiagnostics* diagnostics_ = nullptr;
    const slang::SourceManager* sourceManager_ = nullptr;
    const slang::ast::ProceduralBlockSymbol* preferredBlock_ = nullptr;
    std::unordered_map<const SignalMemoEntry*, std::vector<WriteBackMemo::Slice>> pending_;
    std::unique_ptr<slang::ast::EvalContext> evalContext_;
    std::size_t instanceId_ = 0;
    std::size_t sliceCounter_ = 0;
};

/// LHS converter specialization for continuous assigns.
class ContinuousAssignLHSConverter : public LHSConverter {
public:
    ContinuousAssignLHSConverter(Context context, WriteBackMemo& memo);

    bool convert(const slang::ast::AssignmentExpression& assignment, ValueId rhsValue);

private:
    WriteBackMemo& memo_;
};

class AlwaysConverter;

/// LHS converter used by procedural always blocks.
class AlwaysBlockLHSConverter : public LHSConverter {
public:
    AlwaysBlockLHSConverter(Context context, AlwaysConverter& owner);

    virtual bool convert(const slang::ast::AssignmentExpression& assignment,
                         ValueId rhsValue);
    virtual bool convertExpression(const slang::ast::Expression& expr, ValueId rhsValue);

protected:
    void seedEvalContextForLHS(slang::ast::EvalContext& ctx) override;
    AlwaysConverter& owner_;
};

/// LHS converter for combinational always blocks.
class CombAlwaysLHSConverter : public AlwaysBlockLHSConverter {
public:
    using AlwaysBlockLHSConverter::AlwaysBlockLHSConverter;
};

/// LHS converter for sequential always blocks.
class SeqAlwaysLHSConverter : public AlwaysBlockLHSConverter {
public:
    using AlwaysBlockLHSConverter::AlwaysBlockLHSConverter;

    bool convert(const slang::ast::AssignmentExpression& assignment,
                 ValueId rhsValue) override;
    bool convertExpression(const slang::ast::Expression& expr, ValueId rhsValue) override;

private:
    bool handleDynamicElementAssign(const slang::ast::ElementSelectExpression& element,
                                    ValueId rhsValue);
};

/// RHS converter used by procedural always blocks.
class AlwaysBlockRHSConverter : public CombRHSConverter {
public:
    AlwaysBlockRHSConverter(Context context, AlwaysConverter& owner);

protected:
    ValueId handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                const SignalMemoEntry& entry) override;
    ValueId handleCustomNamedValue(const slang::ast::NamedValueExpression& expr) override;

    AlwaysConverter& owner_;
};

/// RHS converter for combinational always blocks.
class CombAlwaysRHSConverter : public AlwaysBlockRHSConverter {
public:
    using AlwaysBlockRHSConverter::AlwaysBlockRHSConverter;

protected:
    ValueId handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                const SignalMemoEntry& entry) override;
};

/// RHS converter for sequential always blocks.
class SeqAlwaysRHSConverter : public AlwaysBlockRHSConverter {
public:
    using AlwaysBlockRHSConverter::AlwaysBlockRHSConverter;

protected:
    ValueId handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                const SignalMemoEntry& entry) override;
    ValueId convertElementSelect(const slang::ast::ElementSelectExpression& expr) override;
};

/// Shared control logic for procedural always blocks.
class AlwaysConverter {
public:
    AlwaysConverter(grh::ir::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                    std::span<const SignalMemoEntry> regMemo,
                    std::span<const SignalMemoEntry> memMemo,
                    std::span<const DpiImportEntry> dpiImports, WriteBackMemo& memo,
                    const slang::ast::ProceduralBlockSymbol& block,
                    ElaborateDiagnostics* diagnostics, const slang::SourceManager* sourceManager);
    virtual ~AlwaysConverter() = default;

    void traverse();

protected:
    friend class AlwaysBlockRHSConverter;
    friend class AlwaysBlockLHSConverter;
    friend class CombAlwaysRHSConverter;
    friend class CombAlwaysLHSConverter;
    friend class SeqAlwaysRHSConverter;
    friend class SeqAlwaysLHSConverter;

    static constexpr std::size_t kMaxLoopIterations = 4096;

    virtual std::string_view modeLabel() const = 0;
    virtual bool allowBlockingAssignments() const = 0;
    virtual bool allowNonBlockingAssignments() const = 0;
    virtual bool requireNonBlockingAssignments() const = 0;
    virtual bool isSequential() const = 0;

    void setConverters(std::unique_ptr<AlwaysBlockRHSConverter> rhs,
                       std::unique_ptr<AlwaysBlockLHSConverter> lhs);

    enum class LoopControl { None, Break, Continue };

    struct ShadowState {
        std::vector<WriteBackMemo::Slice> slices;
        std::vector<WriteBackMemo::Slice> nbaSlices;
        ValueId composedBlocking = ValueId::invalid();
        ValueId composedAll = ValueId::invalid();
        bool dirtyBlocking = false;
        bool dirtyAll = false;
    };

    struct ShadowFrame {
        std::unordered_map<const SignalMemoEntry*, ShadowState> map;
        std::unordered_set<const SignalMemoEntry*> touched;
    };

    struct CaseBranch {
        ValueId match = ValueId::invalid();
        ShadowFrame frame;
    };

    struct ForLoopVarState {
        const slang::ast::ValueSymbol* symbol = nullptr;
    };

    struct ForeachDimState {
        const slang::ast::ValueSymbol* loopVar = nullptr;
        int32_t start = 0;
        int32_t stop = 0;
        int32_t step = 1;
    };

    class LoopScopeGuard {
    public:
        LoopScopeGuard(AlwaysConverter& owner, std::vector<const slang::ast::ValueSymbol*> symbols);
        ~LoopScopeGuard();
        void dismiss();

    private:
        AlwaysConverter& owner_;
        bool active_ = false;
    };

    class LoopContextGuard {
    public:
        explicit LoopContextGuard(AlwaysConverter& owner);
        ~LoopContextGuard();
        void dismiss();

    private:
        AlwaysConverter& owner_;
        bool active_ = true;
    };

    void visitStatement(const slang::ast::Statement& stmt);
    void visitConditional(const slang::ast::ConditionalStatement& stmt);
    void visitCase(const slang::ast::CaseStatement& stmt);
    void visitStatementList(const slang::ast::StatementList& list);
    void visitBlock(const slang::ast::BlockStatement& block);
    void visitExpressionStatement(const slang::ast::ExpressionStatement& stmt);
    void visitImmediateAssertion(const slang::ast::ImmediateAssertionStatement& stmt);
    void visitProceduralAssign(const slang::ast::ProceduralAssignStatement& stmt);
    void visitForLoop(const slang::ast::ForLoopStatement& stmt);
    void visitForeachLoop(const slang::ast::ForeachLoopStatement& stmt);
    void handleAssignment(const slang::ast::AssignmentExpression& expr,
                          const slang::ast::Expression& originExpr);
    void flushProceduralWrites();
    void reportControlFlowTodo(std::string_view label);
    void reportInvalidStmt(const slang::ast::Statement& stmt);
    void reportUnsupportedStmt(const slang::ast::Statement& stmt);
    virtual void recordAssignmentKind(bool /*isNonBlocking*/) {}
    void handleLoopControlRequest(LoopControl kind, const slang::ast::Statement& origin);
    void handleEntryWrite(const SignalMemoEntry& entry, std::vector<WriteBackMemo::Slice> slices);
    const SignalMemoEntry* findMemoEntryForSymbol(const slang::ast::ValueSymbol& symbol) const;
    void insertShadowSlice(ShadowState& state, const WriteBackMemo::Slice& slice,
                           bool nonBlocking);
    ValueId lookupShadowValue(const SignalMemoEntry& entry);
    ValueId rebuildShadowValue(const SignalMemoEntry& entry, ShadowState& state);
    ValueId rebuildShadowValue(const SignalMemoEntry& entry, ShadowState& state,
                                   bool includeNonBlocking);
    ValueId createZeroValue(int64_t width);
    ValueId createOneValue(int64_t width);
    std::string makeShadowOpName(const SignalMemoEntry& entry, std::string_view suffix);
    std::string makeShadowValueName(const SignalMemoEntry& entry, std::string_view suffix);
    ShadowFrame& currentFrame();
    const ShadowFrame& currentFrame() const;
    ShadowFrame runWithShadowFrame(const ShadowFrame& seed, const slang::ast::Statement& stmt);
    ShadowFrame runWithShadowFrame(const ShadowFrame& seed, const slang::ast::Statement& stmt,
                                   bool isStaticContext);
    std::optional<ShadowFrame> mergeShadowFrames(ValueId condition, ShadowFrame&& trueFrame,
                                                 ShadowFrame&& falseFrame,
                                                 const slang::ast::Statement& originStmt,
                                                 std::string_view label);
    WriteBackMemo::Slice buildFullSlice(const SignalMemoEntry& entry, ValueId value);
    ValueId sliceExistingValue(const WriteBackMemo::Slice& existing, int64_t segMsb,
                                   int64_t segLsb);
    ValueId createMuxForEntry(const SignalMemoEntry& entry, ValueId condition,
                                  ValueId onTrue, ValueId onFalse, std::string_view label);
    ValueId buildCaseMatch(const slang::ast::CaseStatement::ItemGroup& item,
                               ValueId controlValue,
                               slang::ast::CaseStatementCondition condition);
    ValueId buildEquality(ValueId lhs, ValueId rhs, std::string_view hint);
    ValueId buildLogicOr(ValueId lhs, ValueId rhs);
    ValueId buildLogicAnd(ValueId lhs, ValueId rhs);
    ValueId buildLogicNot(ValueId v);
    ValueId coerceToCondition(ValueId v);
    std::string makeControlOpName(std::string_view suffix);
    std::string makeControlValueName(std::string_view suffix);
    bool isCombinationalFullCase(const slang::ast::CaseStatement& stmt);
    void reportLatchIssue(std::string_view context, const SignalMemoEntry* entry = nullptr);
    void checkCaseUniquePriority(const slang::ast::CaseStatement& stmt);
    std::optional<slang::SVInt> evaluateConstantInt(const slang::ast::Expression& expr,
                                                    bool allowUnknown);
    slang::ast::EvalContext& ensureEvalContext();
    slang::ast::EvalContext& ensureLoopEvalContext();
    ValueId buildWildcardEquality(ValueId controlValue, ValueId rhsValue,
                                      const slang::ast::Expression& rhsExpr,
                                      slang::ast::CaseStatementCondition condition);
    ValueId createLiteralValue(const slang::SVInt& literal, bool isSigned,
                                   std::string_view hint);
    std::optional<bool> evaluateStaticCondition(const slang::ast::Expression& expr);
    bool currentContextStatic() const;
    bool loopControlTargetsCurrentLoop() const;
    void seedEvalContextWithLoopValues(slang::ast::EvalContext& ctx);
    bool prepareForLoopState(const slang::ast::ForLoopStatement& stmt,
                             std::vector<ForLoopVarState>& states, slang::ast::EvalContext& ctx);
    bool evaluateForLoopCondition(const slang::ast::ForLoopStatement& stmt,
                                  slang::ast::EvalContext& ctx, bool& result);
    bool executeForLoopSteps(const slang::ast::ForLoopStatement& stmt,
                             slang::ast::EvalContext& ctx);
    bool updateLoopBindings(std::span<const ForLoopVarState> states,
                            slang::ast::EvalContext& ctx);
    bool assignLoopValue(const slang::ast::ValueSymbol& symbol, const slang::SVInt& value);
    bool runForeachRecursive(const slang::ast::ForeachLoopStatement& stmt,
                             std::span<const ForeachDimState> dims, std::size_t depth,
                             std::size_t& iterationCount);
    void pushLoopScope(std::vector<const slang::ast::ValueSymbol*> symbols);
    void popLoopScope();
    ValueId lookupLoopValue(const slang::ast::ValueSymbol& symbol) const;
    bool handleSystemCall(const slang::ast::CallExpression& call,
                          const slang::ast::ExpressionStatement& stmt);
    virtual bool handleDisplaySystemTask(const slang::ast::CallExpression& call,
                                         const slang::ast::ExpressionStatement& stmt);
    virtual bool handleDpiCall(const slang::ast::CallExpression& call,
                               const DpiImportEntry& entry,
                               const slang::ast::ExpressionStatement& stmt);
    virtual bool handleAssertionIntent(const slang::ast::Expression* condition,
                                       const slang::ast::ExpressionStatement* origin,
                                       std::string_view message,
                                       std::string_view severity);
    grh::ir::Graph& graph() noexcept { return graph_; }
    const grh::ir::Graph& graph() const noexcept { return graph_; }
    const slang::ast::ProceduralBlockSymbol& block() const noexcept { return block_; }
    ElaborateDiagnostics* diagnostics() const noexcept { return diagnostics_; }
    WriteBackMemo& memo() noexcept { return memo_; }

    grh::ir::Graph& graph_;
    std::span<const SignalMemoEntry> netMemo_;
    std::span<const SignalMemoEntry> regMemo_;
    std::span<const SignalMemoEntry> memMemo_;
    std::span<const DpiImportEntry> dpiImports_;
    WriteBackMemo& memo_;
    const slang::ast::ProceduralBlockSymbol& block_;
    ElaborateDiagnostics* diagnostics_;
    const slang::SourceManager* sourceManager_ = nullptr;
    std::unique_ptr<AlwaysBlockRHSConverter> rhsConverter_;
    std::unique_ptr<AlwaysBlockLHSConverter> lhsConverter_;
    std::vector<ShadowFrame> shadowStack_;
    bool currentAssignmentIsNonBlocking_ = false;
    std::unordered_map<int64_t, ValueId> zeroCache_;
    std::unordered_map<int64_t, ValueId> oneCache_;
    std::size_t shadowNameCounter_ = 0;
    std::size_t controlNameCounter_ = 0;
    std::size_t controlInstanceId_ = 0;
    bool reportedControlFlowTodo_ = false;
    std::unique_ptr<slang::ast::EvalContext> evalContext_;
    std::vector<bool> controlContextStack_;
    std::vector<int> loopContextStack_;
    LoopControl pendingLoopControl_ = LoopControl::None;
    std::size_t pendingLoopDepth_ = 0;
    std::vector<ValueId> guardStack_;
    ValueId currentGuardValue() const;
    void pushGuard(ValueId guard);
    void popGuard();
    struct LoopValueInfo {
        slang::SVInt literal;
        ValueId value = ValueId::invalid();
    };
    std::unordered_map<const slang::ast::ValueSymbol*, LoopValueInfo> loopValueMap_;
    std::vector<std::vector<const slang::ast::ValueSymbol*>> loopScopeStack_;
    std::unique_ptr<slang::ast::EvalContext> loopEvalContext_;
    std::unordered_map<const slang::ast::SubroutineSymbol*, const DpiImportEntry*> dpiImportMap_;
    const DpiImportEntry* findDpiImport(const slang::ast::SubroutineSymbol& symbol) const;
};

/// Comb always converter entry point.
class CombAlwaysConverter : public AlwaysConverter {
public:
    CombAlwaysConverter(grh::ir::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                        std::span<const SignalMemoEntry> regMemo,
                        std::span<const SignalMemoEntry> memMemo,
                        std::span<const DpiImportEntry> dpiImports, WriteBackMemo& memo,
                        const slang::ast::ProceduralBlockSymbol& block,
                        ElaborateDiagnostics* diagnostics,
                        const slang::SourceManager* sourceManager);

    void run();

protected:
    std::string_view modeLabel() const override;
    bool allowBlockingAssignments() const override;
    bool allowNonBlockingAssignments() const override;
    bool requireNonBlockingAssignments() const override;
    bool isSequential() const override { return false; }
    bool handleDisplaySystemTask(const slang::ast::CallExpression& call,
                                 const slang::ast::ExpressionStatement& stmt) override;
    bool handleDpiCall(const slang::ast::CallExpression& call, const DpiImportEntry& entry,
                       const slang::ast::ExpressionStatement& stmt) override;
    bool handleAssertionIntent(const slang::ast::Expression* condition,
                               const slang::ast::ExpressionStatement* origin,
                               std::string_view message,
                               std::string_view severity) override;
};

/// Sequential always converter entry point.
class SeqAlwaysConverter : public AlwaysConverter {
public:
    SeqAlwaysConverter(grh::ir::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                       std::span<const SignalMemoEntry> regMemo,
                       std::span<const SignalMemoEntry> memMemo,
                       std::span<const DpiImportEntry> dpiImports, WriteBackMemo& memo,
                       const slang::ast::ProceduralBlockSymbol& block,
                       ElaborateDiagnostics* diagnostics,
                       const slang::SourceManager* sourceManager);

    void run();

protected:
    std::string_view modeLabel() const override;
    bool allowBlockingAssignments() const override;
    bool allowNonBlockingAssignments() const override;
    bool requireNonBlockingAssignments() const override;
    bool isSequential() const override { return true; }
    bool handleDisplaySystemTask(const slang::ast::CallExpression& call,
                                 const slang::ast::ExpressionStatement& stmt) override;
    bool handleDpiCall(const slang::ast::CallExpression& call, const DpiImportEntry& entry,
                       const slang::ast::ExpressionStatement& stmt) override;
    bool handleAssertionIntent(const slang::ast::Expression* condition,
                               const slang::ast::ExpressionStatement* origin,
                               std::string_view message,
                               std::string_view severity) override;

private:
    friend class SeqAlwaysRHSConverter;
    friend class SeqAlwaysLHSConverter;

    struct ResetContext;
    void planSequentialFinalize();
    bool finalizeRegisterWrites(ValueId clockValue);
    bool finalizeMemoryWrites(ValueId clockValue);
    ValueId ensureClockValue();
    ValueId ensureMemoryEnableValue();
    ValueId buildMemorySyncRead(const SignalMemoEntry& entry, ValueId addrValue,
                                    const slang::ast::Expression& originExpr,
                                    ValueId enableOverride = ValueId::invalid());
    int64_t memoryRowWidth(const SignalMemoEntry& entry) const;
    std::optional<int64_t> memoryRowCount(const SignalMemoEntry& entry) const;
    int64_t memoryAddrWidth(const SignalMemoEntry& entry) const;
    ValueId normalizeMemoryAddress(const SignalMemoEntry& entry, ValueId addrValue,
                                       const slang::ast::Expression* originExpr);
    bool applyClockPolarity(OperationId op, std::string_view context);
    std::optional<ResetContext> deriveBlockResetContext();
    void recordMemoryWordWrite(const SignalMemoEntry& entry, const slang::ast::Expression& origin,
                               ValueId addrValue, ValueId dataValue, ValueId enable);
    void recordMemoryBitWrite(const SignalMemoEntry& entry, const slang::ast::Expression& origin,
                              ValueId addrValue, ValueId bitIndex, ValueId bitValue,
                              ValueId enable);
    ValueId buildShiftedBitValue(ValueId sourceBit, ValueId bitIndex,
                                     int64_t targetWidth, std::string_view label);
    ValueId buildShiftedMask(ValueId bitIndex, int64_t targetWidth,
                                 std::string_view label);
    ValueId createConcatWithZeroPadding(ValueId value, int64_t padWidth,
                                            std::string_view label);
    std::string makeMemoryHelperOpName(std::string_view suffix);
    std::string makeMemoryHelperValueName(std::string_view suffix);
    std::optional<ValueId> deriveClockValue();
    ValueId convertTimingExpr(const slang::ast::Expression& expr);
    ValueId buildDataOperand(const WriteBackMemo::Entry& entry);
    ValueId createHoldSlice(const WriteBackMemo::Entry& entry, ValueId source,
                            int64_t msb, int64_t lsb);
    bool attachClockOperand(OperationId stateOp, ValueId clkValue,
                            const WriteBackMemo::Entry& entry);
    bool attachDataOperand(OperationId stateOp, ValueId dataValue,
                           const WriteBackMemo::Entry& entry);
    void reportFinalizeIssue(const WriteBackMemo::Entry& entry, std::string_view message);
    std::string makeFinalizeOpName(const SignalMemoEntry& entry, std::string_view suffix);
    std::string makeFinalizeValueName(const SignalMemoEntry& entry, std::string_view suffix);
    struct ResetContext {
        enum class Kind { None, Sync, Async } kind = Kind::None;
        ValueId signal = ValueId::invalid();
        bool activeHigh = true;
    };
    struct ResetExtraction {
        ValueId resetValue = ValueId::invalid();
        ValueId dataWithoutReset = ValueId::invalid();
    };
    std::optional<ResetContext> buildResetContext(const SignalMemoEntry& entry);
    std::optional<ResetExtraction>
    extractResetBranches(ValueId dataValue, ValueId resetSignal, bool activeHigh,
                         const WriteBackMemo::Entry& entry);
    std::optional<ResetExtraction>
    extractAsyncResetAssignment(const SignalMemoEntry& entry, const ResetContext& context);
    std::optional<bool> matchResetCondition(ValueId condition, ValueId resetSignal);
    bool valueDependsOnSignal(ValueId root, ValueId needle) const;
    void recordAssignmentKind(bool isNonBlocking) override;
    bool attachResetOperands(OperationId stateOp, ValueId rstSignal,
                             ValueId resetValue, const WriteBackMemo::Entry& entry);
    ValueId resolveAsyncResetSignal(const slang::ast::Expression& expr);
    ValueId resolveSyncResetSignal(const slang::ast::ValueSymbol& symbol);
    bool useSeqShadowValues() const;

    struct MemoryWriteIntent {
        const SignalMemoEntry* entry = nullptr;
        const slang::ast::Expression* originExpr = nullptr;
        ValueId addr = ValueId::invalid();
        ValueId data = ValueId::invalid();
        ValueId enable = ValueId::invalid();
    };

    struct MemoryBitWriteIntent {
        const SignalMemoEntry* entry = nullptr;
        const slang::ast::Expression* originExpr = nullptr;
        ValueId addr = ValueId::invalid();
        ValueId bitIndex = ValueId::invalid();
        ValueId bitValue = ValueId::invalid();
        ValueId enable = ValueId::invalid();
    };

    std::unordered_map<const slang::ast::Expression*, ValueId> timingValueCache_;
    std::unordered_map<const slang::ast::ValueSymbol*, ValueId> syncResetCache_;
    std::size_t finalizeNameCounter_ = 0;
    std::vector<MemoryWriteIntent> memoryWrites_;
    std::vector<MemoryBitWriteIntent> memoryBitWrites_;
    ValueId cachedClockValue_ = ValueId::invalid();
    bool clockDeriveAttempted_ = false;
    ValueId memoryEnableOne_ = ValueId::invalid();
    std::optional<std::string> clockPolarityAttr_;
    bool blockResetDerived_ = false;
    ResetContext blockResetContext_{};
    bool seenBlockingAssignments_ = false;
    bool seenNonBlockingAssignments_ = false;
};

/// Elaborates slang AST into GRH representation.
class Elaborate {
public:
    explicit Elaborate(ElaborateDiagnostics* diagnostics = nullptr,
                       ElaborateOptions options = {});

    /// Convert the provided slang AST root symbol into a GRH netlist.
    grh::ir::Netlist convert(const slang::ast::RootSymbol& root);

    /// Returns memoized net declarations for the provided module body.
    std::span<const SignalMemoEntry>
    peekNetMemo(const slang::ast::InstanceBodySymbol& body) const;

    /// Returns memoized register declarations for the provided module body.
    std::span<const SignalMemoEntry>
    peekRegMemo(const slang::ast::InstanceBodySymbol& body) const;
    /// Returns memoized memory declarations for the provided module body.
    std::span<const SignalMemoEntry>
    peekMemMemo(const slang::ast::InstanceBodySymbol& body) const;
    /// Returns memoized DPI import declarations for the provided module body.
    std::span<const DpiImportEntry>
    peekDpiImports(const slang::ast::InstanceBodySymbol& body) const;
    const InoutPortMemo* findInoutMemo(const slang::ast::InstanceBodySymbol& body,
                                       const slang::ast::ValueSymbol& symbol) const;

private:
    grh::ir::Graph* materializeGraph(const slang::ast::InstanceSymbol& instance,
                                 grh::ir::Netlist& netlist, bool& wasCreated);
    void populatePorts(const slang::ast::InstanceSymbol& instance,
                       const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void emitModulePlaceholder(const slang::ast::InstanceSymbol& instance, grh::ir::Graph& graph);
    void convertInstanceBody(const slang::ast::InstanceSymbol& instance, grh::ir::Graph& graph,
                             grh::ir::Netlist& netlist);
    void processInstanceArray(const slang::ast::InstanceArraySymbol& array, grh::ir::Graph& graph,
                              grh::ir::Netlist& netlist);
    void processGenerateBlock(const slang::ast::GenerateBlockSymbol& block, grh::ir::Graph& graph,
                              grh::ir::Netlist& netlist);
    void processGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                                   grh::ir::Graph& graph, grh::ir::Netlist& netlist);
    void processNetInitializers(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void processContinuousAssign(const slang::ast::ContinuousAssignSymbol& assign,
                                 const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void processCombAlways(const slang::ast::ProceduralBlockSymbol& block,
                           const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void processSeqAlways(const slang::ast::ProceduralBlockSymbol& block,
                          const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void processInstance(const slang::ast::InstanceSymbol& childInstance, grh::ir::Graph& parentGraph,
                         grh::ir::Netlist& netlist);
    void createInstanceOperation(const slang::ast::InstanceSymbol& childInstance,
                                 grh::ir::Graph& parentGraph, grh::ir::Graph& targetGraph);
    void createBlackboxOperation(const slang::ast::InstanceSymbol& childInstance,
                                 grh::ir::Graph& parentGraph, const BlackboxMemoEntry& memo);
    ValueId ensureValueForSymbol(const slang::ast::ValueSymbol& symbol, grh::ir::Graph& graph);
    ValueId resolveConnectionValue(const slang::ast::Expression& expr, grh::ir::Graph& graph,
                                       const slang::ast::Symbol* origin);
    std::string makeUniqueOperationName(grh::ir::Graph& graph, std::string baseName);
    std::string makeOperationNameForSymbol(const slang::ast::ValueSymbol& symbol,
                                           std::string_view fallback, grh::ir::Graph& graph);
    void registerValueForSymbol(const slang::ast::Symbol& symbol, ValueId value);
    void collectSignalMemos(const slang::ast::InstanceBodySymbol& body);
    void collectDpiImports(const slang::ast::InstanceBodySymbol& body);
    void materializeSignalMemos(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void materializeDpiImports(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void ensureNetValues(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void ensureRegState(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    void ensureMemState(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    WriteBackMemo& ensureWriteBackMemo(const slang::ast::InstanceBodySymbol& body);
    void finalizeWriteBackMemo(const slang::ast::InstanceBodySymbol& body, grh::ir::Graph& graph);
    const BlackboxMemoEntry* ensureBlackboxMemo(const slang::ast::InstanceBodySymbol& body);
    const BlackboxMemoEntry* peekBlackboxMemo(const slang::ast::InstanceBodySymbol& body) const;

    ElaborateDiagnostics* diagnostics_;
    ElaborateOptions options_;
    std::size_t placeholderCounter_ = 0;
    std::size_t instanceCounter_ = 0;
    const slang::SourceManager* sourceManager_ = nullptr;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, grh::ir::Graph*> graphByBody_;
    std::unordered_set<const slang::ast::InstanceBodySymbol*> processedBodies_;
    std::unordered_map<const slang::ast::Symbol*, std::vector<ValueId>> valueCache_;
    std::unordered_map<std::string, std::size_t> graphNameUsage_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<SignalMemoEntry>>
        netMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<SignalMemoEntry>>
        regMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<SignalMemoEntry>>
        memMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<DpiImportEntry>>
        dpiImports_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*,
                       std::unordered_map<const slang::ast::ValueSymbol*, InoutPortMemo>>
        inoutMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, WriteBackMemo> writeBackMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, BlackboxMemoEntry> blackboxMemo_;
};

} // namespace wolf_sv_parser

#endif // WOLF_SV_ELABORATE_HPP
