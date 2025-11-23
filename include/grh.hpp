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
    kRegisterARst,
    kRegisterEnARst,
    kMemory,
    kMemoryAsyncReadPort,
    kMemorySyncReadPort,
    kMemoryWritePort,
    kMemoryMaskWritePort,
    kInstance,
    kBlackbox,
    kDisplay,
    kAssert,
    kDpicImport,
    kDpicCall
};

std::string_view toString(OperationKind kind) noexcept;
std::optional<OperationKind> parseOperationKind(std::string_view text) noexcept;

using AttributeValue = std::variant<bool,
                                    int64_t,
                                    double,
                                    std::string,
                                    std::vector<bool>,
                                    std::vector<int64_t>,
                                    std::vector<double>,
                                    std::vector<std::string>>;

[[nodiscard]] bool attributeValueIsJsonSerializable(const AttributeValue &value);

class Graph;
class Operation;

struct ValueUser {
    Operation* operation = nullptr;
    std::size_t operandIndex = 0;
};

class Value {
public:
    Graph& graph() const noexcept { return *graph_; }
    const std::string& symbol() const noexcept { return symbol_; }
    int64_t width() const noexcept { return width_; }
    bool isSigned() const noexcept { return isSigned_; }
    bool isInput() const noexcept { return isInput_; }
    bool isOutput() const noexcept { return isOutput_; }
    Operation* definingOp() const noexcept { return defineOp_; }
    const std::vector<ValueUser>& users() const noexcept { return users_; }

private:
    friend class Graph;
    friend class Operation;

    Value(Graph& graph, std::string symbol, int64_t width, bool isSigned);

    void setDefiningOp(Operation* op);
    void addUser(Operation* op, std::size_t operandIndex);
    void removeUser(Operation* op, std::size_t operandIndex);
    void clearDefiningOp(Operation* op);
    void setAsInput();
    void setAsOutput();

    Graph* graph_;
    Operation* defineOp_ = nullptr;
    std::string symbol_;
    int64_t width_;
    bool isSigned_;
    bool isInput_ = false;
    bool isOutput_ = false;
    std::vector<ValueUser> users_;
};

class Operation {
public:
    Graph& graph() const noexcept { return *graph_; }
    OperationKind kind() const noexcept { return kind_; }
    const std::string& symbol() const noexcept { return symbol_; }
    const std::vector<Value*>& operands() const noexcept { return operands_; }
    const std::vector<Value*>& results() const noexcept { return results_; }
    const std::map<std::string, AttributeValue>& attributes() const noexcept { return attributes_; }

    void addOperand(Value& value);
    void addResult(Value& value);
    void replaceOperand(std::size_t index, Value& value);
    void replaceResult(std::size_t index, Value& value);
    void setAttribute(std::string key, AttributeValue value);
    void setKind(OperationKind kind) noexcept { kind_ = kind; }

private:
    friend class Graph;

    Operation(Graph& graph, OperationKind kind, std::string symbol);

    Graph* graph_;
    OperationKind kind_;
    std::string symbol_;
    std::vector<Value*> operands_;
    std::vector<Value*> results_;
    std::map<std::string, AttributeValue> attributes_;
};

class Netlist;

class Graph {
public:
    Graph(Netlist& owner, std::string name);

    const std::string& name() const noexcept { return name_; }
    Netlist& owner() const noexcept { return *owner_; }

    Value& createValue(std::string symbol, int64_t width, bool isSigned);
    Operation& createOperation(OperationKind kind, std::string symbol);

    void bindInputPort(std::string portName, Value& value);
    void bindOutputPort(std::string portName, Value& value);

    Value* findValue(std::string_view symbol) noexcept;
    const Value* findValue(std::string_view symbol) const noexcept;
    Operation* findOperation(std::string_view symbol) noexcept;
    const Operation* findOperation(std::string_view symbol) const noexcept;

    const std::vector<std::unique_ptr<Value>>& values() const noexcept { return values_; }
    const std::vector<std::unique_ptr<Operation>>& operations() const noexcept { return operations_; }
    const std::map<std::string, Value*>& inputPorts() const noexcept { return inputPorts_; }
    const std::map<std::string, Value*>& outputPorts() const noexcept { return outputPorts_; }

    void writeJson(slang::JsonWriter& writer) const;

private:
    friend class Netlist;

    Value& addValueInternal(std::unique_ptr<Value> value);
    Operation& addOperationInternal(std::unique_ptr<Operation> op);

    Netlist* owner_;
    std::string name_;
    std::vector<std::unique_ptr<Value>> values_;
    std::vector<std::unique_ptr<Operation>> operations_;
    std::map<std::string, Value*> valueBySymbol_;
    std::map<std::string, Operation*> opBySymbol_;
    std::map<std::string, Value*> inputPorts_;
    std::map<std::string, Value*> outputPorts_;
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

    const std::vector<std::unique_ptr<Graph>>& graphs() const noexcept { return graphs_; }

    static Netlist fromJsonString(std::string_view json);

private:
    Graph& addGraphInternal(std::unique_ptr<Graph> graph);
    void resetGraphOwners();

    std::vector<std::unique_ptr<Graph>> graphs_;
    std::map<std::string, Graph*> graphByName_;
    std::unordered_map<std::string, Graph*> graphAliasByName_;
    std::vector<std::string> topGraphs_;
};

} // namespace wolf_sv::grh
