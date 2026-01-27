#ifndef WOLF_SV_CONVERT_HPP
#define WOLF_SV_CONVERT_HPP

#include "grh.hpp"
#include "slang/text/SourceLocation.h"

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
class Symbol;
} // namespace slang::ast

namespace wolf_sv_parser {

using ValueId = grh::ir::ValueId;
using OperationId = grh::ir::OperationId;
using SymbolId = grh::ir::SymbolId;

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
    const std::vector<ConvertDiagnostic>& messages() const noexcept { return messages_; }
    bool empty() const noexcept { return messages_.empty(); }
    bool hasError() const noexcept { return hasError_; }

private:
    void add(ConvertDiagnosticKind kind, const slang::ast::Symbol& symbol,
             std::string message);
    void add(ConvertDiagnosticKind kind, std::string originSymbol,
             std::optional<slang::SourceLocation> location, std::string message);

    std::vector<ConvertDiagnostic> messages_;
    bool hasError_ = false;
    std::function<void()> onError_;
};

class ConvertAbort final : public std::exception {
public:
    const char* what() const noexcept override { return "convert aborted"; }
};

enum class ConvertLogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5
};

struct ConvertLogEvent {
    ConvertLogLevel level;
    std::string tag;
    std::string message;
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

class ConvertLogger {
public:
    using Sink = std::function<void(const ConvertLogEvent&)>;

    void setLevel(ConvertLogLevel level) noexcept { level_ = level; }
    void enable() noexcept { enabled_ = true; }
    void disable() noexcept { enabled_ = false; }
    void setSink(Sink sink) { sink_ = std::move(sink); }

    void allowTag(std::string_view tag);
    void clearTags();
    bool enabled(ConvertLogLevel level, std::string_view tag) const noexcept;
    void log(ConvertLogLevel level, std::string_view tag, std::string_view message);

private:
    bool enabled_ = false;
    ConvertLogLevel level_ = ConvertLogLevel::Warn;
    std::unordered_set<std::string> tags_{};
    Sink sink_{};
};

struct ConvertOptions {
    bool abortOnError = true;
    bool enableLogging = false;
    ConvertLogLevel logLevel = ConvertLogLevel::Warn;
};

class PlanCache;
class PlanTaskQueue;

struct ConvertContext {
    const slang::ast::Compilation* compilation = nullptr;
    const slang::ast::RootSymbol* root = nullptr;
    ConvertOptions options{};
    ConvertDiagnostics* diagnostics = nullptr;
    ConvertLogger* logger = nullptr;
    PlanCache* planCache = nullptr;
    PlanTaskQueue* planQueue = nullptr;
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

using PortId = PlanIndex;
using SignalId = PlanIndex;
using InstanceId = PlanIndex;
using RWOpId = PlanIndex;
using MemoryPortId = PlanIndex;
using ExprNodeId = PlanIndex;

struct PortInfo {
    PlanSymbolId symbol;
    PortDirection direction = PortDirection::Input;
    int32_t width = 0;
    bool isSigned = false;
    struct InoutBinding {
        PlanSymbolId inSymbol;
        PlanSymbolId outSymbol;
        PlanSymbolId oeSymbol;
    };
    std::optional<InoutBinding> inoutSymbol;
};

struct SignalInfo {
    PlanSymbolId symbol;
    SignalKind kind = SignalKind::Net;
    int32_t width = 0;
    bool isSigned = false;
    int64_t memoryRows = 0;
    std::vector<int32_t> packedDims;
    std::vector<int32_t> unpackedDims;
};

struct AccessSite {
    slang::SourceLocation location{};
    uint32_t sequence = 0;
};

struct RWOp {
    SignalId target = kInvalidPlanIndex;
    ControlDomain domain = ControlDomain::Unknown;
    bool isWrite = false;
    std::vector<AccessSite> sites;
};

struct MemoryPortInfo {
    SignalId memory = kInvalidPlanIndex;
    bool isRead = false;
    bool isWrite = false;
    bool isMasked = false;
    bool isSync = false;
    bool hasReset = false;
    std::vector<AccessSite> sites;
};

struct InstanceParameter {
    PlanSymbolId symbol;
    std::string value;
};

struct InstanceInfo {
    PlanSymbolId instanceSymbol;
    PlanSymbolId moduleSymbol;
    bool isBlackbox = false;
    std::vector<InstanceParameter> parameters;
};

struct ModulePlan {
    const slang::ast::InstanceBodySymbol* body = nullptr;
    PlanSymbolTable symbolTable;
    PlanSymbolId moduleSymbol;
    std::vector<PortInfo> ports;
    std::vector<SignalInfo> signals;
    std::vector<RWOp> rwOps;
    std::vector<MemoryPortInfo> memPorts;
    std::vector<InstanceInfo> instances;
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
    Operation
};

struct ExprNode {
    ExprNodeKind kind = ExprNodeKind::Invalid;
    grh::ir::OperationKind op = grh::ir::OperationKind::kConstant;
    PlanSymbolId symbol;
    PlanSymbolId tempSymbol;
    std::string literal;
    std::vector<ExprNodeId> operands;
    slang::SourceLocation location{};
};

struct LoweredRoot {
    ExprNodeId value = kInvalidPlanIndex;
    slang::SourceLocation location{};
};

struct LoweringPlan {
    std::vector<ExprNode> values;
    std::vector<LoweredRoot> roots;
    std::vector<PlanSymbolId> tempSymbols;
};

struct WriteBackPlan {
    std::vector<SignalId> targets;
};

struct PlanArtifacts {
    std::optional<LoweringPlan> loweringPlan;
    std::optional<WriteBackPlan> writeBackPlan;
};

struct PlanKey {
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string paramSignature;

    bool operator==(const PlanKey& other) const noexcept {
        return body == other.body && paramSignature == other.paramSignature;
    }
};

struct PlanKeyHash {
    std::size_t operator()(const PlanKey& key) const noexcept {
        const std::size_t h1 = std::hash<const void*>{}(key.body);
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
    bool tryPop(PlanKey& out);
    void close();
    bool closed() const noexcept;
    std::size_t size() const;
    void reset();

private:
    mutable std::mutex mutex_;
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

class TypeResolverPass {
public:
    explicit TypeResolverPass(ConvertContext& context) : context_(context) {}

    void resolve(ModulePlan& plan);

private:
    ConvertContext& context_;
};

class RWAnalyzerPass {
public:
    explicit RWAnalyzerPass(ConvertContext& context) : context_(context) {}

    void analyze(ModulePlan& plan);

private:
    ConvertContext& context_;
};

class ExprLowererPass {
public:
    explicit ExprLowererPass(ConvertContext& context) : context_(context) {}

    LoweringPlan lower(ModulePlan& plan);

private:
    ConvertContext& context_;
};

class GraphAssembler {
public:
    explicit GraphAssembler(ConvertContext& context, grh::ir::Netlist& netlist)
        : context_(context), netlist_(netlist) {}

    grh::ir::Graph& build(const ModulePlan& plan);

private:
    ConvertContext& context_;
    grh::ir::Netlist& netlist_;
    std::size_t nextAnonymousId_ = 0;
};

class ConvertDriver {
public:
    explicit ConvertDriver(ConvertOptions options = {});

    grh::ir::Netlist convert(const slang::ast::RootSymbol& root);

    ConvertDiagnostics& diagnostics() noexcept { return diagnostics_; }
    ConvertLogger& logger() noexcept { return logger_; }

private:
    ConvertOptions options_{};
    ConvertDiagnostics diagnostics_{};
    ConvertLogger logger_{};
    PlanCache planCache_{};
    PlanTaskQueue planQueue_{};
};

} // namespace wolf_sv_parser

#endif // WOLF_SV_CONVERT_HPP
