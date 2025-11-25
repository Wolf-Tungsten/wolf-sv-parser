#pragma once

#include <cstdint>
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

[[nodiscard]] bool attributeValueIsJsonSerializable(const AttributeValue &value);

class Graph;
class Operation;

struct ValueUser {
    OperationId operation;
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
    const std::optional<OperationId>& definingOpSymbol() const noexcept { return definingOp_; }
    const std::vector<ValueUser>& users() const noexcept { return users_; }

private:
    friend class Graph;
    friend class Operation;
    friend class Netlist;

    Value(Graph& graph, ValueId symbol, int64_t width, bool isSigned);

    void setDefiningOp(const OperationId& opSymbol);
    void addUser(const OperationId& opSymbol, std::size_t operandIndex);
    void removeUser(const OperationId& opSymbol, std::size_t operandIndex);
    void clearDefiningOp(const OperationId& opSymbol);
    void setAsInput();
    void setAsOutput();

    Graph* graph_;
    std::optional<OperationId> definingOp_;
    ValueId symbol_;
    int64_t width_;
    bool isSigned_;
    bool isInput_ = false;
    bool isOutput_ = false;
    std::vector<ValueUser> users_;
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

            Iterator(Graph *graph, const std::vector<ValueId> *storage, std::size_t index) : graph_(graph), storage_(storage), index_(index) {}

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
            friend bool operator==(const Iterator &lhs, const Iterator &rhs) { return lhs.index_ == rhs.index_ && lhs.storage_ == rhs.storage_; }
            friend bool operator!=(const Iterator &lhs, const Iterator &rhs) { return !(lhs == rhs); }
            friend bool operator<(const Iterator &lhs, const Iterator &rhs) { return lhs.index_ < rhs.index_; }
            friend bool operator>(const Iterator &lhs, const Iterator &rhs) { return rhs < lhs; }
            friend bool operator<=(const Iterator &lhs, const Iterator &rhs) { return !(rhs < lhs); }
            friend bool operator>=(const Iterator &lhs, const Iterator &rhs) { return !(lhs < rhs); }

        private:
            Graph *graph_;
            const std::vector<ValueId> *storage_;
            std::size_t index_;
        };

        ValueHandleRange(Graph *graph, const std::vector<ValueId> &storage) : graph_(graph), storage_(&storage) {}

        Iterator begin() const { return Iterator(graph_, storage_, 0); }
        Iterator end() const { return Iterator(graph_, storage_, storage_->size()); }
        std::size_t size() const noexcept { return storage_->size(); }
        bool empty() const noexcept { return storage_->empty(); }
        Value *operator[](std::size_t index) const;
        Value *front() const;
        Value *back() const;

    private:
        Graph *graph_;
        const std::vector<ValueId> *storage_;
    };

    Graph& graph() const noexcept { return *graph_; }
    OperationKind kind() const noexcept { return kind_; }
    const OperationId& symbol() const noexcept { return symbol_; }
    ValueHandleRange operands() const noexcept { return ValueHandleRange(graph_, operands_); }
    ValueHandleRange results() const noexcept { return ValueHandleRange(graph_, results_); }
    const std::vector<ValueId>& operandSymbols() const noexcept { return operands_; }
    const std::vector<ValueId>& resultSymbols() const noexcept { return results_; }
    const std::map<std::string, AttributeValue>& attributes() const noexcept { return attributes_; }

    Value* operandValue(std::size_t index) const;
    Value* resultValue(std::size_t index) const;

    void addOperand(Value& value);
    void addResult(Value& value);
    void replaceOperand(std::size_t index, Value& value);
    void replaceResult(std::size_t index, Value& value);
    void setAttribute(std::string key, AttributeValue value);
    void setKind(OperationKind kind) noexcept { kind_ = kind; }

private:
    friend class Graph;

    Operation(Graph& graph, OperationKind kind, OperationId symbol);

    Graph* graph_;
    OperationKind kind_;
    OperationId symbol_;
    std::vector<ValueId> operands_;
    std::vector<ValueId> results_;
    std::map<std::string, AttributeValue> attributes_;
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
