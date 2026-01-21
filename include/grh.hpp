#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <span>
#include <vector>

namespace slang {
class JsonWriter;
}

namespace wolf_sv::grh {

enum class OperationKind {
    kConstant,
    kAdd,
    kSub,
    kMul,
    kDiv,
    kMod,
    kEq,
    kNe,
    kLt,
    kLe,
    kGt,
    kGe,
    kAnd,
    kOr,
    kXor,
    kXnor,
    kNot,
    kLogicAnd,
    kLogicOr,
    kLogicNot,
    kReduceAnd,
    kReduceOr,
    kReduceXor,
    kReduceNor,
    kReduceNand,
    kReduceXnor,
    kShl,
    kLShr,
    kAShr,
    kMux,
    kAssign,
    kConcat,
    kReplicate,
    kSliceStatic,
    kSliceDynamic,
    kSliceArray,
    kLatch,
    kLatchArst,
    kRegister,
    kRegisterEn,
    kRegisterRst,
    kRegisterEnRst,
    kRegisterArst,
    kRegisterEnArst,
    kMemory,
    kMemoryAsyncReadPort,
    kMemorySyncReadPort,
    kMemorySyncReadPortRst,
    kMemorySyncReadPortArst,
    kMemoryWritePort,
    kMemoryWritePortRst,
    kMemoryWritePortArst,
    kMemoryMaskWritePort,
    kMemoryMaskWritePortRst,
    kMemoryMaskWritePortArst,
    kInstance,
    kBlackbox,
    kDisplay,
    kAssert,
    kDpicImport,
    kDpicCall
};

std::string_view toString(OperationKind kind) noexcept;
std::optional<OperationKind> parseOperationKind(std::string_view text) noexcept;

using Symbol = std::string;
using ValueId = std::string;
using OperationId = std::string;

using AttributeValue = std::variant<bool, int64_t, double, std::string, std::vector<bool>, std::vector<int64_t>, std::vector<double>, std::vector<std::string>>;

struct SrcLoc
{
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t endLine = 0;
    uint32_t endColumn = 0;
};

using DebugInfo = SrcLoc;

[[nodiscard]] bool attributeValueIsJsonSerializable(const AttributeValue &value);

class Graph;

namespace ir {

struct SymbolId {
    uint32_t value = 0;

    constexpr bool valid() const noexcept { return value != 0; }
    static constexpr SymbolId invalid() noexcept { return {}; }
    friend constexpr bool operator==(SymbolId lhs, SymbolId rhs) noexcept { return lhs.value == rhs.value; }
    friend constexpr bool operator!=(SymbolId lhs, SymbolId rhs) noexcept { return !(lhs == rhs); }
};

class SymbolTable {
public:
    SymbolTable();

    SymbolId intern(std::string_view text);
    SymbolId lookup(std::string_view text) const;
    bool contains(std::string_view text) const;
    std::string_view text(SymbolId id) const;
    bool valid(SymbolId id) const noexcept;

private:
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const noexcept;
        std::size_t operator()(const std::string &value) const noexcept;
    };

    struct StringEq {
        using is_transparent = void;
        bool operator()(const std::string &lhs, const std::string &rhs) const noexcept;
        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept;
        bool operator()(const std::string &lhs, std::string_view rhs) const noexcept;
        bool operator()(std::string_view lhs, const std::string &rhs) const noexcept;
    };

    std::unordered_map<std::string, SymbolId, StringHash, StringEq> symbolsByText_;
    std::deque<std::string> textById_;
};

struct GraphId;

class NetlistSymbolTable final : public SymbolTable {
public:
    NetlistSymbolTable();

    GraphId allocateGraphId(SymbolId symbol);
    GraphId lookupGraphId(SymbolId symbol) const noexcept;
    SymbolId symbolForGraph(GraphId graph) const noexcept;

private:
    uint32_t nextGraphIndex_ = 1;
    std::vector<SymbolId> symbolByGraph_;
    std::unordered_map<uint32_t, uint32_t> graphIndexBySymbol_;
};

class GraphSymbolTable final : public SymbolTable {};

struct GraphId {
    uint32_t index = 0;
    uint32_t generation = 0;

    constexpr bool valid() const noexcept { return index != 0; }
    static constexpr GraphId invalid() noexcept { return {}; }
    friend constexpr bool operator==(GraphId lhs, GraphId rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }
    friend constexpr bool operator!=(GraphId lhs, GraphId rhs) noexcept { return !(lhs == rhs); }
};

struct ValueId {
    uint32_t index = 0;
    uint32_t generation = 0;
    GraphId graph;

    constexpr bool valid() const noexcept { return index != 0; }
    static constexpr ValueId invalid() noexcept { return {}; }
    void assertGraph(GraphId expected) const;
    explicit constexpr operator bool() const noexcept { return valid(); }
    friend constexpr bool operator==(ValueId lhs, ValueId rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation && lhs.graph == rhs.graph;
    }
    friend constexpr bool operator!=(ValueId lhs, ValueId rhs) noexcept { return !(lhs == rhs); }
};

struct OperationId {
    uint32_t index = 0;
    uint32_t generation = 0;
    GraphId graph;

    constexpr bool valid() const noexcept { return index != 0; }
    static constexpr OperationId invalid() noexcept { return {}; }
    void assertGraph(GraphId expected) const;
    explicit constexpr operator bool() const noexcept { return valid(); }
    friend constexpr bool operator==(OperationId lhs, OperationId rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation && lhs.graph == rhs.graph;
    }
    friend constexpr bool operator!=(OperationId lhs, OperationId rhs) noexcept { return !(lhs == rhs); }
};

struct ValueIdHash {
    std::size_t operator()(const ValueId& id) const noexcept
    {
        std::size_t seed = static_cast<std::size_t>(id.index);
        seed = seed * 1315423911u + static_cast<std::size_t>(id.generation);
        seed = seed * 1315423911u + static_cast<std::size_t>(id.graph.index);
        seed = seed * 1315423911u + static_cast<std::size_t>(id.graph.generation);
        return seed;
    }
};

struct OperationIdHash {
    std::size_t operator()(const OperationId& id) const noexcept
    {
        std::size_t seed = static_cast<std::size_t>(id.index);
        seed = seed * 1315423911u + static_cast<std::size_t>(id.generation);
        seed = seed * 1315423911u + static_cast<std::size_t>(id.graph.index);
        seed = seed * 1315423911u + static_cast<std::size_t>(id.graph.generation);
        return seed;
    }
};

struct Range {
    std::size_t offset = 0;
    std::size_t count = 0;
};

struct ValueUser {
    OperationId operation;
    uint32_t operandIndex = 0;
};

struct Port {
    SymbolId name;
    ValueId value;
};

struct AttrKV {
    SymbolId key;
    AttributeValue value;
};

class GraphView {
public:
    GraphView() = default;

    std::span<const OperationId> operations() const noexcept;
    std::span<const ValueId> values() const noexcept;
    std::span<const Port> inputPorts() const noexcept;
    std::span<const Port> outputPorts() const noexcept;
    OperationKind opKind(OperationId op) const;
    std::span<const ValueId> opOperands(OperationId op) const;
    std::span<const ValueId> opResults(OperationId op) const;
    SymbolId opSymbol(OperationId op) const;
    std::span<const AttrKV> opAttrs(OperationId op) const;
    std::optional<AttributeValue> opAttr(OperationId op, SymbolId key) const;
    std::optional<SrcLoc> opSrcLoc(OperationId op) const;
    SymbolId valueSymbol(ValueId value) const;
    int32_t valueWidth(ValueId value) const;
    bool valueSigned(ValueId value) const;
    bool valueIsInput(ValueId value) const;
    bool valueIsOutput(ValueId value) const;
    OperationId valueDef(ValueId value) const;
    std::span<const ValueUser> valueUsers(ValueId value) const;
    std::optional<SrcLoc> valueSrcLoc(ValueId value) const;

private:
    friend class GraphBuilder;

    GraphId graphId_{};
    std::vector<OperationId> operations_;
    std::vector<ValueId> values_;
    std::vector<Port> inputPorts_;
    std::vector<Port> outputPorts_;
    std::vector<OperationKind> opKinds_;
    std::vector<SymbolId> opSymbols_;
    std::vector<Range> opOperandRanges_;
    std::vector<Range> opResultRanges_;
    std::vector<Range> opAttrRanges_;
    std::vector<ValueId> operands_;
    std::vector<ValueId> results_;
    std::vector<AttrKV> opAttrs_;
    std::vector<std::optional<SrcLoc>> opSrcLocs_;
    std::vector<SymbolId> valueSymbols_;
    std::vector<int32_t> valueWidths_;
    std::vector<uint8_t> valueSigned_;
    std::vector<uint8_t> valueIsInput_;
    std::vector<uint8_t> valueIsOutput_;
    std::vector<OperationId> valueDefs_;
    std::vector<Range> valueUserRanges_;
    std::vector<ValueUser> useList_;
    std::vector<std::optional<SrcLoc>> valueSrcLocs_;

    std::size_t opIndex(OperationId op) const;
    std::size_t valueIndex(ValueId value) const;
};

class GraphBuilder {
public:
    explicit GraphBuilder(GraphId graphId);
    GraphBuilder(GraphSymbolTable& symbols, GraphId graphId = GraphId{1, 0});
    static GraphBuilder fromView(const GraphView& view, GraphSymbolTable& symbols);

    ValueId addValue(SymbolId sym, int32_t width, bool isSigned);
    OperationId addOp(OperationKind kind, SymbolId sym);
    void addOperand(OperationId op, ValueId value);
    void addResult(OperationId op, ValueId value);
    void replaceOperand(OperationId op, std::size_t index, ValueId value);
    void replaceResult(OperationId op, std::size_t index, ValueId value);
    void replaceAllUses(ValueId from, ValueId to);
    bool eraseOperand(OperationId op, std::size_t index);
    bool eraseResult(OperationId op, std::size_t index);
    bool eraseOp(OperationId op);
    bool eraseOp(OperationId op, std::span<const ValueId> replacementResults);
    bool eraseValue(ValueId value);
    void bindInputPort(SymbolId name, ValueId value);
    void bindOutputPort(SymbolId name, ValueId value);
    void setAttr(OperationId op, SymbolId key, AttributeValue value);
    void setOpKind(OperationId op, OperationKind kind);
    bool eraseAttr(OperationId op, SymbolId key);
    void setValueSrcLoc(ValueId value, SrcLoc loc);
    void setOpSrcLoc(OperationId op, SrcLoc loc);
    void setOpSymbol(OperationId op, SymbolId sym);
    void setValueSymbol(ValueId value, SymbolId sym);
    void clearOpSymbol(OperationId op);
    void clearValueSymbol(ValueId value);
    GraphView freeze() const;

private:
    friend class ::wolf_sv::grh::Graph;

    struct ValueData {
        SymbolId symbol;
        int32_t width = 0;
        bool isSigned = false;
        bool isInput = false;
        bool isOutput = false;
        OperationId definingOp = OperationId::invalid();
        std::optional<SrcLoc> srcLoc;
        bool alive = true;
    };

    struct OperationData {
        OperationKind kind = OperationKind::kConstant;
        SymbolId symbol;
        std::vector<ValueId> operands;
        std::vector<ValueId> results;
        std::vector<AttrKV> attrs;
        std::optional<SrcLoc> srcLoc;
        bool alive = true;
    };

    std::size_t valueIndex(ValueId value) const;
    std::size_t opIndex(OperationId op) const;
    bool valueAlive(ValueId value) const;
    bool opAlive(OperationId op) const;
    std::size_t countValueUses(ValueId value, std::optional<OperationId> skipOp) const;
    void recomputePortFlags();
    void validateSymbol(SymbolId sym, std::string_view context) const;
    void validateAttrKey(SymbolId key) const;

    GraphId graphId_;
    GraphSymbolTable* symbols_ = nullptr;
    std::vector<ValueData> values_;
    std::vector<OperationData> operations_;
    std::vector<Port> inputPorts_;
    std::vector<Port> outputPorts_;
};

} // namespace ir

class Value {
public:
    const ir::ValueId& id() const noexcept { return id_; }
    ir::SymbolId symbol() const noexcept { return symbol_; }
    std::string_view symbolText() const noexcept { return symbolText_; }
    int32_t width() const noexcept { return width_; }
    bool isSigned() const noexcept { return isSigned_; }
    bool isInput() const noexcept { return isInput_; }
    bool isOutput() const noexcept { return isOutput_; }
    ir::OperationId definingOp() const noexcept { return definingOp_; }
    std::span<const ir::ValueUser> users() const noexcept { return std::span<const ir::ValueUser>(users_.data(), users_.size()); }
    const std::optional<SrcLoc>& srcLoc() const noexcept { return srcLoc_; }

private:
    friend class Graph;

    Value(ir::ValueId id, ir::SymbolId symbol, std::string symbolText, int32_t width, bool isSigned,
          bool isInput, bool isOutput, ir::OperationId definingOp,
          std::vector<ir::ValueUser> users, std::optional<SrcLoc> srcLoc);

    ir::ValueId id_{};
    ir::SymbolId symbol_{};
    std::string symbolText_;
    int32_t width_ = 0;
    bool isSigned_ = false;
    bool isInput_ = false;
    bool isOutput_ = false;
    ir::OperationId definingOp_{};
    std::vector<ir::ValueUser> users_;
    std::optional<SrcLoc> srcLoc_;
};

class Operation {
public:
    const ir::OperationId& id() const noexcept { return id_; }
    OperationKind kind() const noexcept { return kind_; }
    ir::SymbolId symbol() const noexcept { return symbol_; }
    std::string_view symbolText() const noexcept { return symbolText_; }
    std::span<const ir::ValueId> operands() const noexcept { return std::span<const ir::ValueId>(operands_.data(), operands_.size()); }
    std::span<const ir::ValueId> results() const noexcept { return std::span<const ir::ValueId>(results_.data(), results_.size()); }
    std::span<const ir::AttrKV> attrs() const noexcept { return std::span<const ir::AttrKV>(attrs_.data(), attrs_.size()); }
    std::optional<AttributeValue> attr(ir::SymbolId key) const;
    const std::optional<SrcLoc>& srcLoc() const noexcept { return srcLoc_; }

private:
    friend class Graph;

    Operation(ir::OperationId id, OperationKind kind, ir::SymbolId symbol, std::string symbolText,
              std::vector<ir::ValueId> operands, std::vector<ir::ValueId> results,
              std::vector<ir::AttrKV> attrs, std::optional<SrcLoc> srcLoc);

    ir::OperationId id_{};
    OperationKind kind_{};
    ir::SymbolId symbol_{};
    std::string symbolText_;
    std::vector<ir::ValueId> operands_;
    std::vector<ir::ValueId> results_;
    std::vector<ir::AttrKV> attrs_;
    std::optional<SrcLoc> srcLoc_;
};

class Netlist;

class Graph {
public:
    Graph(Netlist& owner, std::string symbol, ir::GraphId graphId);

    const std::string& symbol() const noexcept { return symbol_; }
    const ir::GraphId& id() const noexcept { return graphId_; }
    Netlist& owner() const noexcept { return *owner_; }

    ir::GraphSymbolTable& symbols() noexcept { return symbols_; }
    const ir::GraphSymbolTable& symbols() const noexcept { return symbols_; }
    ir::SymbolId internSymbol(std::string_view text);
    ir::SymbolId lookupSymbol(std::string_view text) const;
    std::string_view symbolText(ir::SymbolId id) const;

    bool frozen() const noexcept { return !builder_.has_value(); }
    const ir::GraphView* viewIfFrozen() const noexcept;
    const ir::GraphView& freeze();

    std::span<const ir::OperationId> operations() const;
    std::span<const ir::ValueId> values() const;
    std::span<const ir::Port> inputPorts() const;
    std::span<const ir::Port> outputPorts() const;

    ir::ValueId createValue(ir::SymbolId symbol, int32_t width, bool isSigned);
    ir::OperationId createOperation(OperationKind kind, ir::SymbolId symbol);

    ir::ValueId findValue(ir::SymbolId symbol) const noexcept;
    ir::OperationId findOperation(ir::SymbolId symbol) const noexcept;
    ir::ValueId findValue(std::string_view symbol) const;
    ir::OperationId findOperation(std::string_view symbol) const;
    Value getValue(ir::ValueId id) const;
    Operation getOperation(ir::OperationId id) const;

    void bindInputPort(ir::SymbolId name, ir::ValueId value);
    void bindOutputPort(ir::SymbolId name, ir::ValueId value);
    ir::ValueId inputPortValue(ir::SymbolId name) const noexcept;
    ir::ValueId outputPortValue(ir::SymbolId name) const noexcept;

    void addOperand(ir::OperationId op, ir::ValueId value);
    void addResult(ir::OperationId op, ir::ValueId value);
    void replaceOperand(ir::OperationId op, std::size_t index, ir::ValueId value);
    void replaceResult(ir::OperationId op, std::size_t index, ir::ValueId value);
    void replaceAllUses(ir::ValueId from, ir::ValueId to);
    bool eraseOperand(ir::OperationId op, std::size_t index);
    bool eraseResult(ir::OperationId op, std::size_t index);
    bool eraseOp(ir::OperationId op);
    bool eraseOp(ir::OperationId op, std::span<const ir::ValueId> replacementResults);
    bool eraseValue(ir::ValueId value);
    void setAttr(ir::OperationId op, ir::SymbolId key, AttributeValue value);
    void setOpKind(ir::OperationId op, OperationKind kind);
    bool eraseAttr(ir::OperationId op, ir::SymbolId key);
    void setValueSrcLoc(ir::ValueId value, SrcLoc loc);
    void setOpSrcLoc(ir::OperationId op, SrcLoc loc);
    void setOpSymbol(ir::OperationId op, ir::SymbolId sym);
    void setValueSymbol(ir::ValueId value, ir::SymbolId sym);
    void clearOpSymbol(ir::OperationId op);
    void clearValueSymbol(ir::ValueId value);

    void writeJson(slang::JsonWriter& writer) const;

private:
    friend class Netlist;

    void invalidateCaches() const;
    void ensureCaches() const;
    ir::GraphBuilder& ensureBuilder();
    const ir::GraphView& view() const;
    Value valueFromView(ir::ValueId id) const;
    Value valueFromBuilder(ir::ValueId id) const;
    Operation operationFromView(ir::OperationId id) const;
    Operation operationFromBuilder(ir::OperationId id) const;

    Netlist* owner_;
    std::string symbol_;
    ir::GraphId graphId_{};
    ir::GraphSymbolTable symbols_;
    std::optional<ir::GraphView> view_;
    std::optional<ir::GraphBuilder> builder_;
    mutable std::vector<ir::ValueId> valuesCache_;
    mutable std::vector<ir::OperationId> operationsCache_;
    mutable std::vector<ir::Port> inputPortsCache_;
    mutable std::vector<ir::Port> outputPortsCache_;
    mutable bool cacheValid_ = false;
};

class Netlist {
public:
    Netlist() = default;
    Netlist(Netlist&& other) noexcept;
    Netlist& operator=(Netlist&& other) noexcept;
    Netlist(const Netlist&) = delete;
    Netlist& operator=(const Netlist&) = delete;

    Graph& createGraph(std::string name);
    Graph* findGraph(std::string_view name) noexcept;
    const Graph* findGraph(std::string_view name) const noexcept;
    std::vector<std::string> aliasesForGraph(std::string_view name) const;
    void registerGraphAlias(std::string alias, Graph& graph);

    void markAsTop(std::string_view graphName);
    const std::vector<std::string>& topGraphs() const noexcept { return topGraphs_; }

    const std::unordered_map<std::string, std::unique_ptr<Graph>>& graphs() const noexcept { return graphs_; }
    const std::vector<std::string>& graphOrder() const noexcept { return graphOrder_; }

    static Netlist fromJsonString(std::string_view json);

private:
    Graph& addGraphInternal(std::unique_ptr<Graph> graph);
    void resetGraphOwners();

    ir::NetlistSymbolTable netlistSymbols_;
    std::unordered_map<std::string, std::unique_ptr<Graph>> graphs_;
    std::unordered_map<std::string, std::string> graphAliasBySymbol_;
    std::vector<std::string> graphOrder_;
    std::vector<std::string> topGraphs_;
};

} // namespace wolf_sv::grh
