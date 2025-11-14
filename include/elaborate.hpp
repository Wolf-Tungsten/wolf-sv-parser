#pragma once

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
enum class EdgeKind;
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
    const slang::ast::Expression* asyncResetExpr = nullptr;
    slang::ast::EdgeKind asyncResetEdge = {};
    const slang::ast::ValueSymbol* syncResetSymbol = nullptr;
    bool syncResetActiveHigh = true;
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
        bool consumed = false;
    };

    void recordWrite(const SignalMemoEntry& target, AssignmentKind kind,
                     const slang::ast::Symbol* originSymbol, std::vector<Slice> slices);
    std::span<const Entry> entries() const noexcept { return entries_; }
    std::span<Entry> entriesMutable() noexcept { return entries_; }
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
    virtual grh::Value* handleCustomNamedValue(const slang::ast::NamedValueExpression& expr);

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
        std::span<const SignalMemoEntry> regMemo;
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
    std::span<const SignalMemoEntry> regMemo_;
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

class AlwaysConverter;

/// LHS converter used by procedural always blocks.
class AlwaysBlockLHSConverter : public LHSConverter {
public:
    AlwaysBlockLHSConverter(Context context, AlwaysConverter& owner);

    bool convert(const slang::ast::AssignmentExpression& assignment, grh::Value& rhsValue);

protected:
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
};

/// RHS converter used by procedural always blocks.
class AlwaysBlockRHSConverter : public CombRHSConverter {
public:
    AlwaysBlockRHSConverter(Context context, AlwaysConverter& owner);

protected:
    grh::Value* handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                const SignalMemoEntry& entry) override;
    grh::Value* handleCustomNamedValue(const slang::ast::NamedValueExpression& expr) override;

    AlwaysConverter& owner_;
};

/// RHS converter for combinational always blocks.
class CombAlwaysRHSConverter : public AlwaysBlockRHSConverter {
public:
    using AlwaysBlockRHSConverter::AlwaysBlockRHSConverter;

protected:
    grh::Value* handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                const SignalMemoEntry& entry) override;
};

/// RHS converter for sequential always blocks.
class SeqAlwaysRHSConverter : public AlwaysBlockRHSConverter {
public:
    using AlwaysBlockRHSConverter::AlwaysBlockRHSConverter;

protected:
    grh::Value* handleMemoEntry(const slang::ast::NamedValueExpression& expr,
                                const SignalMemoEntry& entry) override;
};

/// Shared control logic for procedural always blocks.
class AlwaysConverter {
public:
    AlwaysConverter(grh::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                    std::span<const SignalMemoEntry> regMemo, WriteBackMemo& memo,
                    const slang::ast::ProceduralBlockSymbol& block,
                    ElaborateDiagnostics* diagnostics);
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

    void setConverters(std::unique_ptr<AlwaysBlockRHSConverter> rhs,
                       std::unique_ptr<AlwaysBlockLHSConverter> lhs);

    enum class LoopControl { None, Break, Continue };

    struct ShadowState {
        std::vector<WriteBackMemo::Slice> slices;
        grh::Value* composed = nullptr;
        bool dirty = false;
    };

    struct ShadowFrame {
        std::unordered_map<const SignalMemoEntry*, ShadowState> map;
        std::unordered_set<const SignalMemoEntry*> touched;
    };

    struct CaseBranch {
        grh::Value* match = nullptr;
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
    void visitProceduralAssign(const slang::ast::ProceduralAssignStatement& stmt);
    void visitForLoop(const slang::ast::ForLoopStatement& stmt);
    void visitForeachLoop(const slang::ast::ForeachLoopStatement& stmt);
    void handleAssignment(const slang::ast::AssignmentExpression& expr,
                          const slang::ast::Expression& originExpr);
    void flushProceduralWrites();
    void reportControlFlowTodo(std::string_view label);
    void reportUnsupportedStmt(const slang::ast::Statement& stmt);
    void handleLoopControlRequest(LoopControl kind, const slang::ast::Statement& origin);
    void handleEntryWrite(const SignalMemoEntry& entry, std::vector<WriteBackMemo::Slice> slices);
    void insertShadowSlice(ShadowState& state, const WriteBackMemo::Slice& slice);
    grh::Value* lookupShadowValue(const SignalMemoEntry& entry);
    grh::Value* rebuildShadowValue(const SignalMemoEntry& entry, ShadowState& state);
    grh::Value* createZeroValue(int64_t width);
    std::string makeShadowOpName(const SignalMemoEntry& entry, std::string_view suffix);
    std::string makeShadowValueName(const SignalMemoEntry& entry, std::string_view suffix);
    ShadowFrame& currentFrame();
    const ShadowFrame& currentFrame() const;
    ShadowFrame runWithShadowFrame(const ShadowFrame& seed, const slang::ast::Statement& stmt);
    ShadowFrame runWithShadowFrame(const ShadowFrame& seed, const slang::ast::Statement& stmt,
                                   bool isStaticContext);
    std::optional<ShadowFrame> mergeShadowFrames(grh::Value& condition, ShadowFrame&& trueFrame,
                                                 ShadowFrame&& falseFrame,
                                                 const slang::ast::Statement& originStmt,
                                                 std::string_view label);
    WriteBackMemo::Slice buildFullSlice(const SignalMemoEntry& entry, grh::Value& value);
    grh::Value* createMuxForEntry(const SignalMemoEntry& entry, grh::Value& condition,
                                  grh::Value& onTrue, grh::Value& onFalse, std::string_view label);
    grh::Value* buildCaseMatch(const slang::ast::CaseStatement::ItemGroup& item,
                               grh::Value& controlValue,
                               slang::ast::CaseStatementCondition condition);
    grh::Value* buildEquality(grh::Value& lhs, grh::Value& rhs, std::string_view hint);
    grh::Value* buildLogicOr(grh::Value& lhs, grh::Value& rhs);
    std::string makeControlOpName(std::string_view suffix);
    std::string makeControlValueName(std::string_view suffix);
    void reportLatchIssue(std::string_view context, const SignalMemoEntry* entry = nullptr);
    void checkCaseUniquePriority(const slang::ast::CaseStatement& stmt);
    std::optional<slang::SVInt> evaluateConstantInt(const slang::ast::Expression& expr,
                                                    bool allowUnknown);
    slang::ast::EvalContext& ensureEvalContext();
    slang::ast::EvalContext& ensureLoopEvalContext();
    grh::Value* buildWildcardEquality(grh::Value& controlValue, grh::Value& rhsValue,
                                      const slang::ast::Expression& rhsExpr,
                                      slang::ast::CaseStatementCondition condition);
    grh::Value* createLiteralValue(const slang::SVInt& literal, bool isSigned,
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
    grh::Value* lookupLoopValue(const slang::ast::ValueSymbol& symbol) const;
    grh::Graph& graph() noexcept { return graph_; }
    const slang::ast::ProceduralBlockSymbol& block() const noexcept { return block_; }
    ElaborateDiagnostics* diagnostics() const noexcept { return diagnostics_; }
    WriteBackMemo& memo() noexcept { return memo_; }

    grh::Graph& graph_;
    std::span<const SignalMemoEntry> netMemo_;
    std::span<const SignalMemoEntry> regMemo_;
    WriteBackMemo& memo_;
    const slang::ast::ProceduralBlockSymbol& block_;
    ElaborateDiagnostics* diagnostics_;
    std::unique_ptr<AlwaysBlockRHSConverter> rhsConverter_;
    std::unique_ptr<AlwaysBlockLHSConverter> lhsConverter_;
    std::vector<ShadowFrame> shadowStack_;
    std::unordered_map<int64_t, grh::Value*> zeroCache_;
    std::size_t shadowNameCounter_ = 0;
    std::size_t controlNameCounter_ = 0;
    bool reportedControlFlowTodo_ = false;
    std::unique_ptr<slang::ast::EvalContext> evalContext_;
    std::vector<bool> controlContextStack_;
    std::vector<int> loopContextStack_;
    LoopControl pendingLoopControl_ = LoopControl::None;
    std::size_t pendingLoopDepth_ = 0;
    struct LoopValueInfo {
        slang::SVInt literal;
        grh::Value* value = nullptr;
    };
    std::unordered_map<const slang::ast::ValueSymbol*, LoopValueInfo> loopValueMap_;
    std::vector<std::vector<const slang::ast::ValueSymbol*>> loopScopeStack_;
    std::unique_ptr<slang::ast::EvalContext> loopEvalContext_;
};

/// Comb always converter entry point.
class CombAlwaysConverter : public AlwaysConverter {
public:
    CombAlwaysConverter(grh::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                        std::span<const SignalMemoEntry> regMemo, WriteBackMemo& memo,
                        const slang::ast::ProceduralBlockSymbol& block,
                        ElaborateDiagnostics* diagnostics);

    void run();

protected:
    std::string_view modeLabel() const override;
    bool allowBlockingAssignments() const override;
    bool allowNonBlockingAssignments() const override;
    bool requireNonBlockingAssignments() const override;
};

/// Sequential always converter entry point.
class SeqAlwaysConverter : public AlwaysConverter {
public:
    SeqAlwaysConverter(grh::Graph& graph, std::span<const SignalMemoEntry> netMemo,
                       std::span<const SignalMemoEntry> regMemo, WriteBackMemo& memo,
                       const slang::ast::ProceduralBlockSymbol& block,
                       ElaborateDiagnostics* diagnostics);

    void run();

protected:
    std::string_view modeLabel() const override;
    bool allowBlockingAssignments() const override;
    bool allowNonBlockingAssignments() const override;
    bool requireNonBlockingAssignments() const override;

private:
    void planSequentialFinalize();
    std::optional<grh::Value*> deriveClockValue();
    grh::Value* convertTimingExpr(const slang::ast::Expression& expr);
    grh::Value* buildDataOperand(const WriteBackMemo::Entry& entry);
    grh::Value* createHoldSlice(const WriteBackMemo::Entry& entry, int64_t msb, int64_t lsb);
    bool attachClockOperand(grh::Operation& stateOp, grh::Value& clkValue,
                            const WriteBackMemo::Entry& entry);
    bool attachDataOperand(grh::Operation& stateOp, grh::Value& dataValue,
                           const WriteBackMemo::Entry& entry);
    void reportFinalizeIssue(const WriteBackMemo::Entry& entry, std::string_view message);
    std::string makeFinalizeOpName(const SignalMemoEntry& entry, std::string_view suffix);
    std::string makeFinalizeValueName(const SignalMemoEntry& entry, std::string_view suffix);
    struct ResetContext {
        enum class Kind { None, Sync, Async } kind = Kind::None;
        grh::Value* signal = nullptr;
        bool activeHigh = true;
    };
    struct ResetExtraction {
        grh::Value* resetValue = nullptr;
    };
    std::optional<ResetContext> buildResetContext(const SignalMemoEntry& entry);
    std::optional<ResetExtraction>
    extractResetBranches(grh::Value& dataValue, grh::Value& resetSignal, bool activeHigh,
                         const WriteBackMemo::Entry& entry);
    std::optional<bool> matchResetCondition(grh::Value& condition, grh::Value& resetSignal);
    bool attachResetOperands(grh::Operation& stateOp, grh::Value& rstSignal,
                             grh::Value& resetValue, const WriteBackMemo::Entry& entry);
    grh::Value* resolveAsyncResetSignal(const slang::ast::Expression& expr);
    grh::Value* resolveSyncResetSignal(const slang::ast::ValueSymbol& symbol);

    std::unordered_map<const slang::ast::Expression*, grh::Value*> timingValueCache_;
    std::unordered_map<const slang::ast::ValueSymbol*, grh::Value*> syncResetCache_;
    std::size_t finalizeNameCounter_ = 0;
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
    void processSeqAlways(const slang::ast::ProceduralBlockSymbol& block,
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
