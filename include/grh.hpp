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

class NetlistSymbolTable final : public SymbolTable {};
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
    friend constexpr bool operator==(OperationId lhs, OperationId rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation && lhs.graph == rhs.graph;
    }
    friend constexpr bool operator!=(OperationId lhs, OperationId rhs) noexcept { return !(lhs == rhs); }
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
    bool eraseAttr(OperationId op, SymbolId key);
    void setValueSrcLoc(ValueId value, SrcLoc loc);
    void setOpSrcLoc(OperationId op, SrcLoc loc);
    void setOpSymbol(OperationId op, SymbolId sym);
    void setValueSymbol(ValueId value, SymbolId sym);
    void clearOpSymbol(OperationId op);
    void clearValueSymbol(ValueId value);
    GraphView freeze() const;

private:
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

class Graph;
class Operation;

struct ValueUser {
    OperationId operationSymbol;
    Operation* operationPtr = nullptr;
    std::size_t operandIndex = 0;
};

class Value {
public:
    Graph& graph() const noexcept { return *graph_; }
    const ValueId& symbol() const noexcept { return symbol_; }
    int64_t width() const noexcept { return width_; }
    bool isSigned() const noexcept { return isSigned_; }
    bool isInput() const noexcept { return isInput_; }
    bool isOutput() const noexcept { return isOutput_; }
    Operation* definingOp() const noexcept;
    const std::optional<OperationId>& definingOpSymbol() const noexcept { return definingOpSymbol_; }
    const std::vector<ValueUser>& users() const noexcept { return users_; }
    const std::optional<SrcLoc> &srcLoc() const noexcept { return srcLoc_; }
    void setSrcLoc(SrcLoc info) { srcLoc_ = std::move(info); }

private:
    friend class Graph;
    friend class Operation;
    friend class Netlist;

    Value(Graph& graph, ValueId symbol, int64_t width, bool isSigned);

    void setDefiningOp(Operation& op);
    void setDefiningOpSymbol(const OperationId& opSymbol);
    void addUser(Operation& op, std::size_t operandIndex);
    void addUserSymbol(const OperationId& opSymbol, std::size_t operandIndex);
    void removeUser(Operation& op, std::size_t operandIndex);
    void clearDefiningOp(Operation& op);
    void clearDefiningOpSymbol(const OperationId& opSymbol);
    void resetDefiningOpPtr(Graph& graph);
    void resetUserPointers(Graph& graph);
    void setAsInput();
    void setAsOutput();

    Graph* graph_;
    std::optional<OperationId> definingOpSymbol_;
    mutable Operation* definingOpPtr_ = nullptr;
    ValueId symbol_;
    int64_t width_;
    bool isSigned_;
    bool isInput_ = false;
    bool isOutput_ = false;
    std::vector<ValueUser> users_;
    std::optional<SrcLoc> srcLoc_;
};

class Operation {
public:
    class ValueHandleRange
    {
    public:
        class Iterator
        {
        public:
            using value_type = Value *;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::random_access_iterator_tag;

            Iterator(const Operation *owner, const std::vector<ValueId> *symbols, std::vector<Value *> *pointers, std::size_t index) : owner_(owner), symbols_(symbols), pointers_(pointers), index_(index) {}

            value_type operator*() const;
            Iterator &operator++()
            {
                ++index_;
                return *this;
            }
            Iterator operator++(int)
            {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            Iterator &operator--()
            {
                --index_;
                return *this;
            }
            Iterator operator--(int)
            {
                Iterator tmp = *this;
                --(*this);
                return tmp;
            }
            Iterator &operator+=(difference_type delta)
            {
                index_ += static_cast<std::size_t>(delta);
                return *this;
            }
            Iterator &operator-=(difference_type delta)
            {
                index_ -= static_cast<std::size_t>(delta);
                return *this;
            }
            friend Iterator operator+(Iterator it, difference_type delta)
            {
                it += delta;
                return it;
            }
            friend Iterator operator-(Iterator it, difference_type delta)
            {
                it -= delta;
                return it;
            }
            friend difference_type operator-(const Iterator &lhs, const Iterator &rhs)
            {
                return static_cast<difference_type>(lhs.index_) - static_cast<difference_type>(rhs.index_);
            }
            friend bool operator==(const Iterator &lhs, const Iterator &rhs) { return lhs.index_ == rhs.index_ && lhs.symbols_ == rhs.symbols_; }
            friend bool operator!=(const Iterator &lhs, const Iterator &rhs) { return !(lhs == rhs); }
            friend bool operator<(const Iterator &lhs, const Iterator &rhs) { return lhs.index_ < rhs.index_; }
            friend bool operator>(const Iterator &lhs, const Iterator &rhs) { return rhs < lhs; }
            friend bool operator<=(const Iterator &lhs, const Iterator &rhs) { return !(rhs < lhs); }
            friend bool operator>=(const Iterator &lhs, const Iterator &rhs) { return !(lhs < rhs); }

        private:
            const Operation *owner_;
            const std::vector<ValueId> *symbols_;
            mutable std::vector<Value *> *pointers_;
            std::size_t index_;
        };

        ValueHandleRange(const Operation *owner, const std::vector<ValueId> &symbols, std::vector<Value *> &pointers) : owner_(owner), symbols_(&symbols), pointers_(&pointers) {}

        Iterator begin() const { return Iterator(owner_, symbols_, pointers_, 0); }
        Iterator end() const { return Iterator(owner_, symbols_, pointers_, symbols_->size()); }
        std::size_t size() const noexcept { return symbols_->size(); }
        bool empty() const noexcept { return symbols_->empty(); }
        Value *operator[](std::size_t index) const;
        Value *front() const;
        Value *back() const;

    private:
        const Operation *owner_;
        const std::vector<ValueId> *symbols_;
        mutable std::vector<Value *> *pointers_;
    };

    Graph& graph() const noexcept { return *graph_; }
    OperationKind kind() const noexcept { return kind_; }
    const OperationId& symbol() const noexcept { return symbol_; }
    ValueHandleRange operands() const noexcept { return ValueHandleRange(this, operands_, operandPtrs_); }
    ValueHandleRange results() const noexcept { return ValueHandleRange(this, results_, resultPtrs_); }
    const std::vector<ValueId>& operandSymbols() const noexcept { return operands_; }
    const std::vector<ValueId>& resultSymbols() const noexcept { return results_; }
    const std::map<std::string, AttributeValue>& attributes() const noexcept { return attributes_; }
    const std::optional<SrcLoc> &srcLoc() const noexcept { return srcLoc_; }
    void setSrcLoc(SrcLoc info) { srcLoc_ = std::move(info); }

    Value& operandValue(std::size_t index) const;
    Value& resultValue(std::size_t index) const;

    void addOperand(Value& value);
    void addResult(Value& value);
    void replaceOperand(std::size_t index, Value& value);
    void replaceResult(std::size_t index, Value& value);
    void setAttribute(std::string key, AttributeValue value);
    void clearAttribute(std::string_view key);
    void setKind(OperationKind kind) noexcept { kind_ = kind; }

private:
    friend class Graph;

    Operation(Graph& graph, OperationKind kind, OperationId symbol);
    Value* resolveValueAt(std::size_t index, const std::vector<ValueId>& symbols, std::vector<Value*>& pointers) const;
    void rehydrateOperands(Graph& graph);
    void rehydrateResults(Graph& graph);

    Graph* graph_;
    OperationKind kind_;
    OperationId symbol_;
    std::vector<ValueId> operands_;
    std::vector<ValueId> results_;
    mutable std::vector<Value*> operandPtrs_;
    mutable std::vector<Value*> resultPtrs_;
    std::map<std::string, AttributeValue> attributes_;
    std::optional<SrcLoc> srcLoc_;
};

class Netlist;

class Graph {
public:
    Graph(Netlist& owner, std::string symbol);

    const std::string& symbol() const noexcept { return symbol_; }
    Netlist& owner() const noexcept { return *owner_; }

    Value& createValue(ValueId symbol, int64_t width, bool isSigned);
    Operation& createOperation(OperationKind kind, OperationId symbol);

    void bindInputPort(std::string portName, Value& value);
    void bindOutputPort(std::string portName, Value& value);

    Value* findValue(std::string_view symbol) noexcept;
    const Value* findValue(std::string_view symbol) const noexcept;
    Operation* findOperation(std::string_view symbol) noexcept;
    const Operation* findOperation(std::string_view symbol) const noexcept;
    Value& getValue(std::string_view symbol);
    const Value& getValue(std::string_view symbol) const;
    Operation& getOperation(std::string_view symbol);
    const Operation& getOperation(std::string_view symbol) const;

    const std::unordered_map<ValueId, std::unique_ptr<Value>>& values() const noexcept { return values_; }
    const std::unordered_map<OperationId, std::unique_ptr<Operation>>& operations() const noexcept { return operations_; }
    const std::vector<ValueId>& valueOrder() const noexcept { return valueOrder_; }
    const std::vector<OperationId>& operationOrder() const noexcept { return operationOrder_; }
    const std::map<std::string, ValueId>& inputPorts() const noexcept { return inputPorts_; }
    const std::map<std::string, ValueId>& outputPorts() const noexcept { return outputPorts_; }
    Value* inputPortValue(std::string_view portName) noexcept;
    const Value* inputPortValue(std::string_view portName) const noexcept;
    Value* outputPortValue(std::string_view portName) noexcept;
    const Value* outputPortValue(std::string_view portName) const noexcept;
    void replaceOutputValue(Value& oldValue, Value& newValue);

    bool removeOperation(std::string_view symbol);

    void rehydratePointers();
    void writeJson(slang::JsonWriter& writer) const;

private:
    friend class Netlist;

    Value& addValueInternal(std::unique_ptr<Value> value);
    Operation& addOperationInternal(std::unique_ptr<Operation> op);

    Netlist* owner_;
    std::string symbol_;
    std::unordered_map<ValueId, std::unique_ptr<Value>> values_;
    std::unordered_map<OperationId, std::unique_ptr<Operation>> operations_;
    std::vector<ValueId> valueOrder_;
    std::vector<OperationId> operationOrder_;
    std::map<std::string, ValueId> inputPorts_;
    std::map<std::string, ValueId> outputPorts_;
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

    std::unordered_map<std::string, std::unique_ptr<Graph>> graphs_;
    std::unordered_map<std::string, std::string> graphAliasBySymbol_;
    std::vector<std::string> graphOrder_;
    std::vector<std::string> topGraphs_;
};

} // namespace wolf_sv::grh
