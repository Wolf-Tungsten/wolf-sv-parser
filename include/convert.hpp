#ifndef WOLF_SV_CONVERT_HPP
#define WOLF_SV_CONVERT_HPP

#include "grh.hpp"
#include "logging.hpp"
#include "slang/text/SourceLocation.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slang::ast {
class Compilation;
} // namespace slang::ast

namespace slang::ast {
class RootSymbol;
class InstanceBodySymbol;
class InstanceSymbol;
class DefinitionSymbol;
class Symbol;
} // namespace slang::ast

namespace wolf_sv_parser {

using ValueId = grh::ir::ValueId;
using OperationId = grh::ir::OperationId;
using SymbolId = grh::ir::SymbolId;

class InstanceRegistry;

enum class ConvertDiagnosticKind {
    Todo,
    Error,
    Warning
};

struct ConvertDiagnostic {
    ConvertDiagnosticKind kind;
    std::string message;
    std::string originSymbol;
    std::optional<slang::SourceLocation> location;
};

class ConvertDiagnostics {
public:
    void todo(const slang::ast::Symbol& symbol, std::string message);
    void error(const slang::ast::Symbol& symbol, std::string message);
    void warn(const slang::ast::Symbol& symbol, std::string message);
    void todo(const slang::SourceLocation& location, std::string message,
              std::string originSymbol = {});
    void error(const slang::SourceLocation& location, std::string message,
               std::string originSymbol = {});
    void warn(const slang::SourceLocation& location, std::string message,
              std::string originSymbol = {});

    void setOnError(std::function<void()> callback) { onError_ = std::move(callback); }
    void enableThreadLocal(bool enable) noexcept { threadLocalEnabled_ = enable; }
    void flushThreadLocal();
    const std::vector<ConvertDiagnostic>& messages() const noexcept { return messages_; }
    bool empty() const noexcept { return messages_.empty(); }
    bool hasError() const noexcept { return hasError_.load(std::memory_order_relaxed); }

private:
    struct ThreadLocalBuffer {
        std::vector<ConvertDiagnostic> messages;
        bool hasError = false;
    };

    void add(ConvertDiagnosticKind kind, const slang::ast::Symbol& symbol,
             std::string message);
    void add(ConvertDiagnosticKind kind, std::string originSymbol,
             std::optional<slang::SourceLocation> location, std::string message);

    void flushThreadLocalLocked(ThreadLocalBuffer& buffer);

    static thread_local ThreadLocalBuffer threadLocal_;
    bool threadLocalEnabled_ = false;
    std::vector<ConvertDiagnostic> messages_;
    std::atomic<bool> hasError_{false};
    std::function<void()> onError_;
    mutable std::mutex mutex_;
};

class ConvertAbort final : public std::exception {
public:
    const char* what() const noexcept override { return "convert aborted"; }
};

using PlanIndex = uint32_t;
constexpr PlanIndex kInvalidPlanIndex = std::numeric_limits<PlanIndex>::max();

struct PlanSymbolId {
    PlanIndex index = kInvalidPlanIndex;

    bool valid() const noexcept { return index != kInvalidPlanIndex; }
};

struct StringViewHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct StringViewEq {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

class PlanSymbolTable {
public:
    PlanSymbolId intern(std::string_view text);
    PlanSymbolId lookup(std::string_view text) const;
    std::string_view text(PlanSymbolId id) const;
    std::size_t size() const noexcept { return storage_.size(); }

private:
    std::deque<std::string> storage_;
    std::unordered_map<std::string_view, PlanSymbolId, StringViewHash, StringViewEq> index_;
};

struct ConvertOptions {
    bool abortOnError = true;
    bool enableLogging = false;
    bool enableTiming = false;
    LogLevel logLevel = LogLevel::Warn;
    uint32_t maxLoopIterations = 65536;
    uint32_t threadCount = 32;
    bool singleThread = false;
};

class PlanCache;
class PlanTaskQueue;

struct ConvertContext {
    const slang::ast::Compilation* compilation = nullptr;
    const slang::ast::RootSymbol* root = nullptr;
    ConvertOptions options{};
    ConvertDiagnostics* diagnostics = nullptr;
    Logger* logger = nullptr;
    PlanCache* planCache = nullptr;
    PlanTaskQueue* planQueue = nullptr;
    InstanceRegistry* instanceRegistry = nullptr;
    std::atomic<std::size_t>* taskCounter = nullptr;
    std::atomic<bool>* cancelFlag = nullptr;
};

enum class PortDirection {
    Input,
    Output,
    Inout
};

enum class SignalKind {
    Net,
    Variable,
    Memory,
    Port
};

enum class ControlDomain {
    Combinational,
    Sequential,
    Latch,
    Unknown
};

enum class ProcKind {
    Initial,
    Final,
    AlwaysComb,
    AlwaysLatch,
    AlwaysFF,
    Always,
    Unknown
};

using PortId = PlanIndex;
using SignalId = PlanIndex;
using InstanceId = PlanIndex;
using ExprNodeId = PlanIndex;

struct PortInfo {
    PlanSymbolId symbol;
    PortDirection direction = PortDirection::Input;
    int32_t width = 0;
    bool isSigned = false;
    grh::ir::ValueType valueType = grh::ir::ValueType::Logic;
    struct InoutBinding {
        PlanSymbolId inSymbol;
        PlanSymbolId outSymbol;
        PlanSymbolId oeSymbol;
    };
    std::optional<InoutBinding> inoutSymbol;
};

struct InoutSignalInfo {
    PlanSymbolId symbol;
    PortInfo::InoutBinding binding;
};

struct UnpackedDimInfo {
    int32_t extent = 1;
    int32_t left = 0;
    int32_t right = 0;
};

struct SignalInfo {
    PlanSymbolId symbol;
    SignalKind kind = SignalKind::Net;
    int32_t width = 0;
    bool isSigned = false;
    grh::ir::ValueType valueType = grh::ir::ValueType::Logic;
    int64_t memoryRows = 0;
    std::vector<int32_t> packedDims;
    std::vector<UnpackedDimInfo> unpackedDims;
};

struct InstanceParameter {
    PlanSymbolId symbol;
    std::string value;
};

struct InstanceInfo {
    const slang::ast::InstanceSymbol* instance = nullptr;
    PlanSymbolId instanceSymbol;
    PlanSymbolId moduleSymbol;
    bool isBlackbox = false;
    std::vector<InstanceParameter> parameters;
    std::string paramSignature;
};

struct ModulePlan {
    const slang::ast::InstanceBodySymbol* body = nullptr;
    PlanSymbolTable symbolTable;
    PlanSymbolId moduleSymbol;
    std::vector<PortInfo> ports;
    std::vector<SignalInfo> signals;
    std::vector<InstanceInfo> instances;
    std::vector<InoutSignalInfo> inoutSignals;
};

inline const PortInfo* findPortByName(const ModulePlan& plan, std::string_view name)
{
    const PlanSymbolId id = plan.symbolTable.lookup(name);
    if (!id.valid())
    {
        return nullptr;
    }
    for (const auto& port : plan.ports)
    {
        if (port.symbol.index == id.index)
        {
            return &port;
        }
    }
    return nullptr;
}

inline const PortInfo* findPortByInoutName(const ModulePlan& plan, std::string_view name)
{
    const PlanSymbolId id = plan.symbolTable.lookup(name);
    if (!id.valid())
    {
        return nullptr;
    }
    for (const auto& port : plan.ports)
    {
        if (!port.inoutSymbol)
        {
            continue;
        }
        const PortInfo::InoutBinding& inout = *port.inoutSymbol;
        if (inout.inSymbol.index == id.index || inout.outSymbol.index == id.index ||
            inout.oeSymbol.index == id.index)
        {
            return &port;
        }
    }
    return nullptr;
}

enum class ExprNodeKind {
    Invalid,
    Constant,
    Symbol,
    XmrRead,
    Operation
};

struct ExprNode {
    ExprNodeKind kind = ExprNodeKind::Invalid;
    grh::ir::OperationKind op = grh::ir::OperationKind::kConstant;
    PlanSymbolId symbol;
    PlanSymbolId tempSymbol;
    std::string literal;
    std::string systemName;
    std::string xmrPath;
    std::vector<ExprNodeId> operands;
    int32_t widthHint = 0;
    bool isSigned = false;
    grh::ir::ValueType valueType = grh::ir::ValueType::Logic;
    bool hasSideEffects = false;
    slang::SourceLocation location{};
};


enum class WriteSliceKind {
    None,
    BitSelect,
    RangeSelect,
    MemberSelect
};

enum class WriteRangeKind {
    Simple,
    IndexedUp,
    IndexedDown
};

struct WriteSlice {
    WriteSliceKind kind = WriteSliceKind::None;
    WriteRangeKind rangeKind = WriteRangeKind::Simple;
    ExprNodeId index = kInvalidPlanIndex;
    ExprNodeId left = kInvalidPlanIndex;
    ExprNodeId right = kInvalidPlanIndex;
    PlanSymbolId member;
    slang::SourceLocation location{};
};

struct WriteIntent {
    PlanSymbolId target;
    std::vector<WriteSlice> slices;
    ExprNodeId value = kInvalidPlanIndex;
    ExprNodeId guard = kInvalidPlanIndex;
    ControlDomain domain = ControlDomain::Unknown;
    bool isNonBlocking = false;
    bool coversAllTwoState = false;
    bool isXmr = false;
    std::string xmrPath;
    slang::SourceLocation location{};
};

enum class EventEdge {
    Posedge,
    Negedge
};

struct SystemTaskStmt {
    std::string name;
    std::vector<ExprNodeId> args;
};

struct DpiCallStmt {
    std::string targetImportSymbol;
    std::vector<std::string> inArgNames;
    std::vector<std::string> outArgNames;
    std::vector<ExprNodeId> inArgs;
    std::vector<PlanSymbolId> results;
    bool hasReturn = false;
};

struct DpiImportInfo {
    std::string symbol;
    std::vector<std::string> argsDirection;
    std::vector<int64_t> argsWidth;
    std::vector<std::string> argsName;
    std::vector<bool> argsSigned;
    std::vector<std::string> argsType;
    bool hasReturn = false;
    int64_t returnWidth = 0;
    bool returnSigned = false;
    std::string returnType;
};

enum class LoweredStmtKind {
    Write,
    SystemTask,
    DpiCall
};

struct LoweredStmt {
    LoweredStmtKind kind = LoweredStmtKind::Write;
    grh::ir::OperationKind op = grh::ir::OperationKind::kAssign;
    ExprNodeId updateCond = kInvalidPlanIndex;
    ProcKind procKind = ProcKind::Unknown;
    bool hasTiming = false;
    std::vector<EventEdge> eventEdges;
    std::vector<ExprNodeId> eventOperands;
    slang::SourceLocation location{};
    WriteIntent write;
    SystemTaskStmt systemTask;
    DpiCallStmt dpiCall;
};

struct MemoryReadPort {
    PlanSymbolId memory;
    SignalId signal = kInvalidPlanIndex;
    ExprNodeId address = kInvalidPlanIndex;
    ExprNodeId data = kInvalidPlanIndex;
    bool isSync = false;
    ExprNodeId updateCond = kInvalidPlanIndex;
    std::vector<EventEdge> eventEdges;
    std::vector<ExprNodeId> eventOperands;
    slang::SourceLocation location{};
};

struct MemoryWritePort {
    PlanSymbolId memory;
    SignalId signal = kInvalidPlanIndex;
    ExprNodeId address = kInvalidPlanIndex;
    ExprNodeId data = kInvalidPlanIndex;
    ExprNodeId mask = kInvalidPlanIndex;
    ExprNodeId updateCond = kInvalidPlanIndex;
    bool isMasked = false;
    std::vector<EventEdge> eventEdges;
    std::vector<ExprNodeId> eventOperands;
    slang::SourceLocation location{};
};

struct MemoryInit {
    PlanSymbolId memory;
    std::string kind;  // "readmemh", "readmemb", "literal", "random", "random_seeded"
    std::string file;  // for readmemh/readmemb
    std::string initValue;  // for literal/random: "0", "1", "8'hAB", "$random", "$random(12345)"
    bool hasStart = false;
    bool hasFinish = false;
    int64_t start = 0;
    int64_t finish = 0;
    int64_t address = -1;  // for per-element init: -1 means all elements, >=0 means specific address
    slang::SourceLocation location{};
};

struct RegisterInit {
    PlanSymbolId reg;
    std::string kind;  // "literal", "random", "random_seeded"
    std::string initValue;  // "0", "1", "8'hAB", "$random", "$random(12345)"
    slang::SourceLocation location{};
};

struct LoweringPlan {
    std::vector<ExprNode> values;
    std::vector<PlanSymbolId> tempSymbols;
    std::vector<WriteIntent> writes;
    std::vector<LoweredStmt> loweredStmts;
    std::vector<DpiImportInfo> dpiImports;
    std::vector<MemoryReadPort> memoryReads;
    std::vector<MemoryWritePort> memoryWrites;
    std::vector<MemoryInit> memoryInits;
    std::vector<RegisterInit> registerInits;
};

struct WriteBackPlan {
    struct Entry {
        PlanSymbolId target;
        SignalId signal = kInvalidPlanIndex;
        ControlDomain domain = ControlDomain::Unknown;
        ExprNodeId updateCond = kInvalidPlanIndex;
        ExprNodeId nextValue = kInvalidPlanIndex;
        bool hasStaticSlice = false;
        int64_t sliceLow = 0;
        int64_t sliceWidth = 0;
        std::vector<EventEdge> eventEdges;
        std::vector<ExprNodeId> eventOperands;
        slang::SourceLocation location{};
    };

    std::vector<Entry> entries;
};

struct PlanArtifacts {
    std::optional<LoweringPlan> loweringPlan;
    std::optional<WriteBackPlan> writeBackPlan;
};

struct PlanKey {
    const slang::ast::DefinitionSymbol* definition = nullptr;
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string paramSignature;

    bool operator==(const PlanKey& other) const noexcept {
        if (definition || other.definition) {
            return definition == other.definition && paramSignature == other.paramSignature;
        }
        return body == other.body && paramSignature == other.paramSignature;
    }
};

struct PlanKeyHash {
    std::size_t operator()(const PlanKey& key) const noexcept {
        const void* defKey = key.definition ? static_cast<const void*>(key.definition)
                                            : static_cast<const void*>(key.body);
        const std::size_t h1 = std::hash<const void*>{}(defKey);
        const std::size_t h2 = std::hash<std::string>{}(key.paramSignature);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

enum class PlanStatus {
    Pending,
    Planning,
    Done,
    Failed
};

struct PlanEntry {
    PlanStatus status = PlanStatus::Pending;
    std::optional<ModulePlan> plan;
    PlanArtifacts artifacts;
};

class PlanCache {
public:
    bool tryClaim(const PlanKey& key);
    void storePlan(const PlanKey& key, ModulePlan plan);
    void markFailed(const PlanKey& key);
    std::optional<ModulePlan> findReady(const PlanKey& key) const;
    void clear();
    bool setLoweringPlan(const PlanKey& key, LoweringPlan plan);
    bool setWriteBackPlan(const PlanKey& key, WriteBackPlan plan);
    std::optional<LoweringPlan> getLoweringPlan(const PlanKey& key) const;
    std::optional<WriteBackPlan> getWriteBackPlan(const PlanKey& key) const;
    bool withLoweringPlan(const PlanKey& key,
                          const std::function<void(const LoweringPlan&)>& fn) const;
    bool withWriteBackPlan(const PlanKey& key,
                           const std::function<void(const WriteBackPlan&)>& fn) const;
    bool withLoweringPlanMut(const PlanKey& key,
                             const std::function<void(LoweringPlan&)>& fn);
    bool withWriteBackPlanMut(const PlanKey& key,
                              const std::function<void(WriteBackPlan&)>& fn);

private:
    PlanEntry& getOrCreateEntryLocked(const PlanKey& key);
    PlanEntry* findEntryLocked(const PlanKey& key);
    const PlanEntry* findEntryLocked(const PlanKey& key) const;

    mutable std::mutex mutex_;
    std::unordered_map<PlanKey, PlanEntry, PlanKeyHash> entries_;
};

class PlanTaskQueue {
public:
    void push(PlanKey key);
    bool tryPush(PlanKey key);
    bool tryPop(PlanKey& out);
    bool waitPop(PlanKey& out, const std::atomic<bool>* cancelFlag = nullptr);
    void close();
    std::size_t drain();
    bool closed() const noexcept;
    std::size_t size() const;
    void reset();

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<PlanKey> queue_;
    bool closed_ = false;
};

class ModulePlanner {
public:
    explicit ModulePlanner(ConvertContext& context) : context_(context) {}

    ModulePlan plan(const slang::ast::InstanceBodySymbol& body);

private:
    ConvertContext& context_;
};

class StmtLowererPass {
public:
    explicit StmtLowererPass(ConvertContext& context) : context_(context) {}

    void lower(ModulePlan& plan, LoweringPlan& lowering);

private:
    ConvertContext& context_;
};

class WriteBackPass {
public:
    explicit WriteBackPass(ConvertContext& context) : context_(context) {}

    WriteBackPlan lower(ModulePlan& plan, LoweringPlan& lowering);

private:
    ConvertContext& context_;
};

class MemoryPortLowererPass {
public:
    explicit MemoryPortLowererPass(ConvertContext& context) : context_(context) {}

    void lower(ModulePlan& plan, LoweringPlan& lowering);

private:
    ConvertContext& context_;
};

class GraphAssembler {
public:
    explicit GraphAssembler(ConvertContext& context, grh::ir::Netlist& netlist,
                            std::mutex* netlistMutex = nullptr)
        : context_(context), netlist_(netlist), netlistMutex_(netlistMutex) {}

    const std::string& resolveGraphName(const PlanKey& key, std::string_view moduleName);

    grh::ir::Graph& build(const PlanKey& key, const ModulePlan& plan, LoweringPlan& lowering,
                          const WriteBackPlan& writeBack);

private:
    ConvertContext& context_;
    grh::ir::Netlist& netlist_;
    std::mutex* netlistMutex_ = nullptr;
    std::size_t nextAnonymousId_ = 0;
    std::unordered_map<PlanKey, std::string, PlanKeyHash> graphNames_{};
    std::unordered_set<std::string> reservedGraphNames_{};
    mutable std::mutex nameMutex_{};
};

class ConvertDriver {
public:
    explicit ConvertDriver(ConvertOptions options = {});

    grh::ir::Netlist convert(const slang::ast::RootSymbol& root);

    ConvertDiagnostics& diagnostics() noexcept { return diagnostics_; }
    Logger& logger() noexcept { return logger_; }

private:
    ConvertOptions options_{};
    ConvertDiagnostics diagnostics_{};
    Logger logger_{};
    PlanCache planCache_{};
    PlanTaskQueue planQueue_{};
};

} // namespace wolf_sv_parser

#endif // WOLF_SV_CONVERT_HPP
