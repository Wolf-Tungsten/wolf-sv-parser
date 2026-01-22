#include "grh.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "slang/text/Json.h"

namespace wolf_sv::grh::ir
{

    bool attributeValueIsJsonSerializable(const AttributeValue &value)
    {
        struct Visitor
        {
            bool operator()(bool) const noexcept { return true; }
            bool operator()(int64_t) const noexcept { return true; }
            bool operator()(double v) const noexcept { return std::isfinite(v); }
            bool operator()(const std::string &) const noexcept { return true; }
            bool operator()(const std::vector<bool> &) const noexcept { return true; }
            bool operator()(const std::vector<int64_t> &) const noexcept { return true; }
            bool operator()(const std::vector<double> &arr) const noexcept
            {
                for (double entry : arr)
                {
                    if (!std::isfinite(entry))
                    {
                        return false;
                    }
                }
                return true;
            }
            bool operator()(const std::vector<std::string> &) const noexcept { return true; }
        };

        return std::visit(Visitor{}, value);
    }

    namespace
    {
        template <typename T>
        std::span<const T> spanForRange(const std::vector<T> &storage, const Range &range)
        {
            if (range.count == 0)
            {
                return {};
            }
            return std::span<const T>(storage.data() + range.offset, range.count);
        }

        std::string symbolTextOrEmpty(const GraphSymbolTable &symbols, SymbolId sym)
        {
            return sym.valid() ? std::string(symbols.text(sym)) : std::string();
        }

        ValueId findPortValue(std::span<const Port> ports, SymbolId name) noexcept
        {
            if (!name.valid())
            {
                return ValueId::invalid();
            }
            for (const auto &port : ports)
            {
                if (port.name == name)
                {
                    return port.value;
                }
            }
            return ValueId::invalid();
        }
    } // namespace

    std::size_t SymbolTable::StringHash::operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }

    std::size_t SymbolTable::StringHash::operator()(const std::string &value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }

    bool SymbolTable::StringEq::operator()(std::string_view lhs, std::string_view rhs) const noexcept
    {
        return lhs == rhs;
    }

    bool SymbolTable::StringEq::operator()(const std::string &lhs, const std::string &rhs) const noexcept
    {
        return lhs == rhs;
    }

    bool SymbolTable::StringEq::operator()(const std::string &lhs, std::string_view rhs) const noexcept
    {
        return lhs == rhs;
    }

    bool SymbolTable::StringEq::operator()(std::string_view lhs, const std::string &rhs) const noexcept
    {
        return lhs == rhs;
    }

    SymbolTable::SymbolTable()
    {
        textById_.emplace_back();
    }

    NetlistSymbolTable::NetlistSymbolTable()
    {
        symbolByGraph_.push_back(SymbolId::invalid());
    }

    GraphId NetlistSymbolTable::allocateGraphId(SymbolId symbol)
    {
        if (!symbol.valid())
        {
            throw std::runtime_error("Graph symbol is invalid");
        }
        if (!valid(symbol))
        {
            throw std::runtime_error("Graph symbol is not in the symbol table");
        }
        if (graphIndexBySymbol_.contains(symbol.value))
        {
            throw std::runtime_error("Graph symbol already has an assigned GraphId");
        }
        GraphId graphId;
        graphId.index = nextGraphIndex_++;
        graphId.generation = 0;
        if (symbolByGraph_.size() <= graphId.index)
        {
            symbolByGraph_.resize(graphId.index + 1);
        }
        symbolByGraph_[graphId.index] = symbol;
        graphIndexBySymbol_[symbol.value] = graphId.index;
        return graphId;
    }

    GraphId NetlistSymbolTable::lookupGraphId(SymbolId symbol) const noexcept
    {
        if (!symbol.valid())
        {
            return GraphId::invalid();
        }
        auto it = graphIndexBySymbol_.find(symbol.value);
        if (it == graphIndexBySymbol_.end())
        {
            return GraphId::invalid();
        }
        GraphId graphId;
        graphId.index = it->second;
        graphId.generation = 0;
        return graphId;
    }

    SymbolId NetlistSymbolTable::symbolForGraph(GraphId graph) const noexcept
    {
        if (!graph.valid())
        {
            return SymbolId::invalid();
        }
        if (graph.index >= symbolByGraph_.size())
        {
            return SymbolId::invalid();
        }
        return symbolByGraph_[graph.index];
    }

    SymbolId SymbolTable::intern(std::string_view text)
    {
        if (symbolsByText_.find(text) != symbolsByText_.end())
        {
            return SymbolId::invalid();
        }

        SymbolId id;
        id.value = static_cast<uint32_t>(textById_.size());
        textById_.emplace_back(text);
        symbolsByText_.emplace(textById_.back(), id);
        return id;
    }

    SymbolId SymbolTable::lookup(std::string_view text) const
    {
        auto it = symbolsByText_.find(text);
        if (it == symbolsByText_.end())
        {
            return SymbolId::invalid();
        }
        return it->second;
    }

    bool SymbolTable::contains(std::string_view text) const
    {
        return symbolsByText_.find(text) != symbolsByText_.end();
    }

    std::string_view SymbolTable::text(SymbolId id) const
    {
        if (!valid(id))
        {
            throw std::runtime_error("Invalid SymbolId");
        }
        return textById_[id.value];
    }

    bool SymbolTable::valid(SymbolId id) const noexcept
    {
        return id.value != 0 && id.value < textById_.size();
    }

    void ValueId::assertGraph(GraphId expected) const
    {
        if (!expected.valid())
        {
            throw std::runtime_error("Expected GraphId is invalid");
        }
        if (!valid())
        {
            throw std::runtime_error("ValueId is invalid");
        }
        if (!graph.valid())
        {
            throw std::runtime_error("ValueId has invalid GraphId");
        }
        if (graph != expected)
        {
            if (std::getenv("WOLF_DEBUG_GRAPH_ID"))
            {
                std::fprintf(stderr,
                             "[graph-id] value index=%u gen=%u graph=%u/%u expected=%u/%u\n",
                             index, generation, graph.index, graph.generation,
                             expected.index, expected.generation);
            }
            throw std::runtime_error("ValueId used with mismatched GraphId");
        }
    }

    void OperationId::assertGraph(GraphId expected) const
    {
        if (!expected.valid())
        {
            throw std::runtime_error("Expected GraphId is invalid");
        }
        if (!valid())
        {
            throw std::runtime_error("OperationId is invalid");
        }
        if (!graph.valid())
        {
            throw std::runtime_error("OperationId has invalid GraphId");
        }
        if (graph != expected)
        {
            throw std::runtime_error("OperationId used with mismatched GraphId");
        }
    }

    std::span<const OperationId> GraphView::operations() const noexcept
    {
        return std::span<const OperationId>(operations_.data(), operations_.size());
    }

    std::span<const ValueId> GraphView::values() const noexcept
    {
        return std::span<const ValueId>(values_.data(), values_.size());
    }

    std::span<const Port> GraphView::inputPorts() const noexcept
    {
        return std::span<const Port>(inputPorts_.data(), inputPorts_.size());
    }

    std::span<const Port> GraphView::outputPorts() const noexcept
    {
        return std::span<const Port>(outputPorts_.data(), outputPorts_.size());
    }

    OperationKind GraphView::opKind(OperationId op) const
    {
        return opKinds_[opIndex(op)];
    }

    std::span<const ValueId> GraphView::opOperands(OperationId op) const
    {
        return spanForRange(operands_, opOperandRanges_[opIndex(op)]);
    }

    std::span<const ValueId> GraphView::opResults(OperationId op) const
    {
        return spanForRange(results_, opResultRanges_[opIndex(op)]);
    }

    SymbolId GraphView::opSymbol(OperationId op) const
    {
        return opSymbols_[opIndex(op)];
    }

    std::span<const AttrKV> GraphView::opAttrs(OperationId op) const
    {
        return spanForRange(opAttrs_, opAttrRanges_[opIndex(op)]);
    }

    std::optional<AttributeValue> GraphView::opAttr(OperationId op, std::string_view key) const
    {
        const auto attrs = opAttrs(op);
        for (const auto &attr : attrs)
        {
            if (attr.key == key)
            {
                return attr.value;
            }
        }
        return std::nullopt;
    }

    std::optional<SrcLoc> GraphView::opSrcLoc(OperationId op) const
    {
        return opSrcLocs_[opIndex(op)];
    }

    SymbolId GraphView::valueSymbol(ValueId value) const
    {
        return valueSymbols_[valueIndex(value)];
    }

    int32_t GraphView::valueWidth(ValueId value) const
    {
        return valueWidths_[valueIndex(value)];
    }

    bool GraphView::valueSigned(ValueId value) const
    {
        return valueSigned_[valueIndex(value)] != 0;
    }

    bool GraphView::valueIsInput(ValueId value) const
    {
        return valueIsInput_[valueIndex(value)] != 0;
    }

    bool GraphView::valueIsOutput(ValueId value) const
    {
        return valueIsOutput_[valueIndex(value)] != 0;
    }

    OperationId GraphView::valueDef(ValueId value) const
    {
        return valueDefs_[valueIndex(value)];
    }

    std::span<const ValueUser> GraphView::valueUsers(ValueId value) const
    {
        return spanForRange(useList_, valueUserRanges_[valueIndex(value)]);
    }

    std::optional<SrcLoc> GraphView::valueSrcLoc(ValueId value) const
    {
        return valueSrcLocs_[valueIndex(value)];
    }

    ValueId GraphView::findValue(SymbolId symbol) const noexcept
    {
        if (!symbol.valid())
        {
            return ValueId::invalid();
        }
        auto it = symbolIndex_.find(symbol.value);
        if (it == symbolIndex_.end() || it->second.kind != SymbolKind::kValue)
        {
            return ValueId::invalid();
        }
        ValueId id;
        id.index = it->second.index;
        id.generation = 0;
        id.graph = graphId_;
        return id;
    }

    OperationId GraphView::findOperation(SymbolId symbol) const noexcept
    {
        if (!symbol.valid())
        {
            return OperationId::invalid();
        }
        auto it = symbolIndex_.find(symbol.value);
        if (it == symbolIndex_.end() || it->second.kind != SymbolKind::kOperation)
        {
            return OperationId::invalid();
        }
        OperationId id;
        id.index = it->second.index;
        id.generation = 0;
        id.graph = graphId_;
        return id;
    }

    std::size_t GraphView::opIndex(OperationId op) const
    {
        op.assertGraph(graphId_);
        const std::size_t index = static_cast<std::size_t>(op.index);
        if (index == 0 || index > opKinds_.size())
        {
            throw std::runtime_error("OperationId out of range");
        }
        return index - 1;
    }

    std::size_t GraphView::valueIndex(ValueId value) const
    {
        value.assertGraph(graphId_);
        const std::size_t index = static_cast<std::size_t>(value.index);
        if (index == 0 || index > valueWidths_.size())
        {
            throw std::runtime_error("ValueId out of range");
        }
        return index - 1;
    }

    GraphBuilder::GraphBuilder(GraphId graphId) : graphId_(graphId)
    {
        if (!graphId_.valid())
        {
            throw std::runtime_error("GraphBuilder requires a valid GraphId");
        }
    }

    GraphBuilder::GraphBuilder(GraphSymbolTable &symbols, GraphId graphId) : GraphBuilder(graphId)
    {
        symbols_ = &symbols;
    }

    GraphBuilder GraphBuilder::fromView(const GraphView &view, GraphSymbolTable &symbols)
    {
        if (!view.graphId_.valid())
        {
            throw std::runtime_error("GraphView has invalid GraphId");
        }

        GraphBuilder builder(symbols, view.graphId_);

        auto require = [](bool condition, const char *message) {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        };

        const std::size_t valueCount = view.values_.size();
        const std::size_t opCount = view.operations_.size();

        require(view.valueSymbols_.size() == valueCount, "GraphView value metadata size mismatch");
        require(view.valueWidths_.size() == valueCount, "GraphView value metadata size mismatch");
        require(view.valueSigned_.size() == valueCount, "GraphView value metadata size mismatch");
        require(view.valueIsInput_.size() == valueCount, "GraphView value metadata size mismatch");
        require(view.valueIsOutput_.size() == valueCount, "GraphView value metadata size mismatch");
        require(view.valueDefs_.size() == valueCount, "GraphView value metadata size mismatch");
        require(view.valueUserRanges_.size() == valueCount, "GraphView value user range size mismatch");
        require(view.valueSrcLocs_.size() == valueCount, "GraphView value metadata size mismatch");

        require(view.opKinds_.size() == opCount, "GraphView operation metadata size mismatch");
        require(view.opSymbols_.size() == opCount, "GraphView operation metadata size mismatch");
        require(view.opOperandRanges_.size() == opCount, "GraphView operation metadata size mismatch");
        require(view.opResultRanges_.size() == opCount, "GraphView operation metadata size mismatch");
        require(view.opAttrRanges_.size() == opCount, "GraphView operation metadata size mismatch");
        require(view.opSrcLocs_.size() == opCount, "GraphView operation metadata size mismatch");

        auto checkRangeBounds = [&](const Range &range, std::size_t total, const char *label) {
            if (range.offset > total || range.offset + range.count > total)
            {
                throw std::runtime_error(std::string("GraphView ") + label + " range out of bounds");
            }
        };

        std::size_t operandOffset = 0;
        for (std::size_t i = 0; i < opCount; ++i)
        {
            const Range &range = view.opOperandRanges_[i];
            require(range.offset == operandOffset, "GraphView operand ranges are not contiguous");
            checkRangeBounds(range, view.operands_.size(), "operand");
            operandOffset = range.offset + range.count;
        }
        require(operandOffset == view.operands_.size(), "GraphView operand range size mismatch");

        std::size_t resultOffset = 0;
        for (std::size_t i = 0; i < opCount; ++i)
        {
            const Range &range = view.opResultRanges_[i];
            require(range.offset == resultOffset, "GraphView result ranges are not contiguous");
            checkRangeBounds(range, view.results_.size(), "result");
            resultOffset = range.offset + range.count;
        }
        require(resultOffset == view.results_.size(), "GraphView result range size mismatch");

        std::size_t attrOffset = 0;
        for (std::size_t i = 0; i < opCount; ++i)
        {
            const Range &range = view.opAttrRanges_[i];
            require(range.offset == attrOffset, "GraphView attribute ranges are not contiguous");
            checkRangeBounds(range, view.opAttrs_.size(), "attribute");
            attrOffset = range.offset + range.count;
        }
        require(attrOffset == view.opAttrs_.size(), "GraphView attribute range size mismatch");

        std::size_t userOffset = 0;
        for (std::size_t i = 0; i < valueCount; ++i)
        {
            const Range &range = view.valueUserRanges_[i];
            require(range.offset == userOffset, "GraphView use-list ranges are not contiguous");
            checkRangeBounds(range, view.useList_.size(), "use-list");
            userOffset = range.offset + range.count;
        }
        require(userOffset == view.useList_.size(), "GraphView use-list size mismatch");

        builder.values_.reserve(valueCount);
        for (std::size_t i = 0; i < valueCount; ++i)
        {
            const ValueId valueId = view.values_[i];
            valueId.assertGraph(view.graphId_);
            require(valueId.generation == 0, "GraphView value generation must be zero");
            require(valueId.index == i + 1, "GraphView values must be contiguous");

            ValueData data;
            data.symbol = view.valueSymbols_[i];
            if (data.symbol.valid())
            {
                builder.validateSymbol(data.symbol, "Value");
            }
            const int32_t width = view.valueWidths_[i];
            if (width <= 0)
            {
                throw std::runtime_error("GraphView value width must be positive");
            }
            data.width = width;
            data.isSigned = view.valueSigned_[i] != 0;
            data.isInput = false;
            data.isOutput = false;
            data.definingOp = OperationId::invalid();
            data.srcLoc = view.valueSrcLocs_[i];
            data.alive = true;
            builder.values_.push_back(std::move(data));
            builder.bindSymbol(builder.values_.back().symbol,
                               GraphBuilder::SymbolKind::kValue,
                               static_cast<uint32_t>(i + 1),
                               "Value");
        }

        builder.operations_.reserve(opCount);
        for (std::size_t i = 0; i < opCount; ++i)
        {
            const OperationId opId = view.operations_[i];
            opId.assertGraph(view.graphId_);
            require(opId.generation == 0, "GraphView operation generation must be zero");
            require(opId.index == i + 1, "GraphView operations must be contiguous");

            OperationData opData;
            opData.kind = view.opKinds_[i];
            opData.symbol = view.opSymbols_[i];
            if (opData.symbol.valid())
            {
                builder.validateSymbol(opData.symbol, "Operation");
            }
            opData.srcLoc = view.opSrcLocs_[i];
            opData.alive = true;
            builder.bindSymbol(opData.symbol,
                               GraphBuilder::SymbolKind::kOperation,
                               static_cast<uint32_t>(i + 1),
                               "Operation");

            const Range &operandRange = view.opOperandRanges_[i];
            opData.operands.reserve(operandRange.count);
            for (std::size_t j = 0; j < operandRange.count; ++j)
            {
                const ValueId operand = view.operands_[operandRange.offset + j];
                operand.assertGraph(view.graphId_);
                require(operand.generation == 0, "GraphView operand generation must be zero");
                if (operand.index == 0 || operand.index > valueCount)
                {
                    throw std::runtime_error("GraphView operand refers to invalid value");
                }
                opData.operands.push_back(operand);
            }

            const Range &resultRange = view.opResultRanges_[i];
            opData.results.reserve(resultRange.count);
            for (std::size_t j = 0; j < resultRange.count; ++j)
            {
                const ValueId result = view.results_[resultRange.offset + j];
                result.assertGraph(view.graphId_);
                require(result.generation == 0, "GraphView result generation must be zero");
                if (result.index == 0 || result.index > valueCount)
                {
                    throw std::runtime_error("GraphView result refers to invalid value");
                }
                const std::size_t valIdx = static_cast<std::size_t>(result.index - 1);
                if (builder.values_[valIdx].definingOp.valid())
                {
                    throw std::runtime_error("GraphView value has multiple defining operations");
                }
                builder.values_[valIdx].definingOp = opId;
                opData.results.push_back(result);
            }

            const Range &attrRange = view.opAttrRanges_[i];
            opData.attrs.reserve(attrRange.count);
            for (std::size_t j = 0; j < attrRange.count; ++j)
            {
                const AttrKV &attr = view.opAttrs_[attrRange.offset + j];
                if (!attributeValueIsJsonSerializable(attr.value))
                {
                    throw std::runtime_error("GraphView attribute value must be JSON-serializable");
                }
                opData.attrs.push_back(attr);
            }

            builder.operations_.push_back(std::move(opData));
        }

        for (std::size_t i = 0; i < valueCount; ++i)
        {
            OperationId viewDef = view.valueDefs_[i];
            if (viewDef.valid())
            {
                viewDef.assertGraph(view.graphId_);
                require(viewDef.generation == 0, "GraphView value definition generation must be zero");
                if (viewDef.index == 0 || viewDef.index > opCount)
                {
                    throw std::runtime_error("GraphView value definition refers to invalid operation");
                }
            }
            if (builder.values_[i].definingOp != viewDef)
            {
                throw std::runtime_error("GraphView value definition mismatch");
            }
        }

        builder.inputPorts_ = view.inputPorts_;
        builder.outputPorts_ = view.outputPorts_;

        for (const auto &port : builder.inputPorts_)
        {
            builder.validateSymbol(port.name, "Input port");
            port.value.assertGraph(view.graphId_);
            require(port.value.generation == 0, "GraphView input port value generation must be zero");
            if (port.value.index == 0 || port.value.index > valueCount)
            {
                throw std::runtime_error("GraphView input port refers to invalid value");
            }
        }
        for (const auto &port : builder.outputPorts_)
        {
            builder.validateSymbol(port.name, "Output port");
            port.value.assertGraph(view.graphId_);
            require(port.value.generation == 0, "GraphView output port value generation must be zero");
            if (port.value.index == 0 || port.value.index > valueCount)
            {
                throw std::runtime_error("GraphView output port refers to invalid value");
            }
        }

        builder.recomputePortFlags();

        for (std::size_t i = 0; i < valueCount; ++i)
        {
            const bool viewInput = view.valueIsInput_[i] != 0;
            if (builder.values_[i].isInput != viewInput)
            {
                throw std::runtime_error("GraphView input port flag mismatch");
            }
            const bool viewOutput = view.valueIsOutput_[i] != 0;
            if (builder.values_[i].isOutput != viewOutput)
            {
                throw std::runtime_error("GraphView output port flag mismatch");
            }
        }

        std::vector<std::vector<ValueUser>> expectedUsers(valueCount);
        for (std::size_t i = 0; i < opCount; ++i)
        {
            const OperationId opId = view.operations_[i];
            const auto &operands = builder.operations_[i].operands;
            for (std::size_t operandIndex = 0; operandIndex < operands.size(); ++operandIndex)
            {
                const ValueId valueId = operands[operandIndex];
                expectedUsers[valueId.index - 1].push_back(
                    ValueUser{opId, static_cast<uint32_t>(operandIndex)});
            }
        }

        for (std::size_t i = 0; i < valueCount; ++i)
        {
            const ValueId valueId = view.values_[i];
            auto actualUsers = view.valueUsers(valueId);
            const auto &expected = expectedUsers[i];
            if (actualUsers.size() != expected.size())
            {
                throw std::runtime_error("GraphView use-list mismatch");
            }
            for (std::size_t j = 0; j < expected.size(); ++j)
            {
                if (actualUsers[j].operation != expected[j].operation ||
                    actualUsers[j].operandIndex != expected[j].operandIndex)
                {
                    throw std::runtime_error("GraphView use-list mismatch");
                }
            }
        }

        return builder;
    }

    ValueId GraphBuilder::addValue(SymbolId sym, int32_t width, bool isSigned)
    {
        if (width <= 0)
        {
            throw std::runtime_error("Value width must be positive");
        }
        validateSymbol(sym, "Value");

        ValueId id;
        id.index = static_cast<uint32_t>(values_.size() + 1);
        id.generation = 0;
        id.graph = graphId_;
        bindSymbol(sym, SymbolKind::kValue, id.index, "Value");
        ValueData data;
        data.symbol = sym;
        data.width = width;
        data.isSigned = isSigned;
        values_.push_back(std::move(data));
        return id;
    }

    OperationId GraphBuilder::addOp(OperationKind kind, SymbolId sym)
    {
        if (sym.valid())
        {
            validateSymbol(sym, "Operation");
        }

        OperationId id;
        id.index = static_cast<uint32_t>(operations_.size() + 1);
        id.generation = 0;
        id.graph = graphId_;
        bindSymbol(sym, SymbolKind::kOperation, id.index, "Operation");
        OperationData data;
        data.kind = kind;
        data.symbol = sym;
        operations_.push_back(std::move(data));
        return id;
    }

    void GraphBuilder::addOperand(OperationId op, ValueId value)
    {
        const std::size_t opIdx = opIndex(op);
        const std::size_t valIdx = valueIndex(value);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }
        operations_[opIdx].operands.push_back(value);
    }

    void GraphBuilder::addResult(OperationId op, ValueId value)
    {
        const std::size_t opIdx = opIndex(op);
        const std::size_t valIdx = valueIndex(value);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }
        ValueData &data = values_[valIdx];
        if (data.definingOp.valid())
        {
            throw std::runtime_error("Value already has a defining operation");
        }
        data.definingOp = op;
        operations_[opIdx].results.push_back(value);
    }

    void GraphBuilder::replaceOperand(OperationId op, std::size_t index, ValueId value)
    {
        const std::size_t opIdx = opIndex(op);
        const std::size_t valIdx = valueIndex(value);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }
        auto &operands = operations_[opIdx].operands;
        if (index >= operands.size())
        {
            throw std::runtime_error("Operand index out of range");
        }
        operands[index] = value;
    }

    void GraphBuilder::replaceResult(OperationId op, std::size_t index, ValueId value)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        const std::size_t valIdx = valueIndex(value);
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }

        auto &results = operations_[opIdx].results;
        if (index >= results.size())
        {
            throw std::runtime_error("Result index out of range");
        }

        const ValueId current = results[index];
        if (current == value)
        {
            return;
        }
        if (values_[valIdx].definingOp.valid())
        {
            throw std::runtime_error("Value already has a defining operation");
        }

        const std::size_t currentIdx = valueIndex(current);
        if (values_[currentIdx].definingOp == op)
        {
            values_[currentIdx].definingOp = OperationId::invalid();
        }
        values_[valIdx].definingOp = op;
        results[index] = value;
    }

    void GraphBuilder::replaceAllUses(ValueId from, ValueId to)
    {
        if (from == to)
        {
            return;
        }
        const std::size_t fromIdx = valueIndex(from);
        const std::size_t toIdx = valueIndex(to);
        if (!values_[fromIdx].alive || !values_[toIdx].alive)
        {
            throw std::runtime_error("replaceAllUses requires live values");
        }

        for (std::size_t i = 0; i < operations_.size(); ++i)
        {
            if (!operations_[i].alive)
            {
                continue;
            }
            for (auto &operand : operations_[i].operands)
            {
                if (operand == from)
                {
                    operand = to;
                }
            }
        }
    }

    bool GraphBuilder::eraseOperand(OperationId op, std::size_t index)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            return false;
        }
        auto &operands = operations_[opIdx].operands;
        if (index >= operands.size())
        {
            return false;
        }
        operands.erase(operands.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bool GraphBuilder::eraseResult(OperationId op, std::size_t index)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            return false;
        }
        auto &results = operations_[opIdx].results;
        if (index >= results.size())
        {
            return false;
        }
        const ValueId value = results[index];
        const std::size_t valIdx = valueIndex(value);
        if (!values_[valIdx].alive)
        {
            return false;
        }
        if (countValueUses(value, std::nullopt) != 0)
        {
            return false;
        }
        if (values_[valIdx].isInput || values_[valIdx].isOutput)
        {
            return false;
        }
        values_[valIdx].definingOp = OperationId::invalid();
        results.erase(results.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bool GraphBuilder::eraseOp(OperationId op)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            return false;
        }
        const auto &results = operations_[opIdx].results;
        for (const auto &result : results)
        {
            if (countValueUses(result, op) != 0)
            {
                return false;
            }
        }
        for (const auto &result : results)
        {
            const std::size_t valIdx = valueIndex(result);
            if (values_[valIdx].definingOp == op)
            {
                values_[valIdx].definingOp = OperationId::invalid();
            }
        }
        const SymbolId symbol = operations_[opIdx].symbol;
        if (symbol.valid())
        {
            unbindSymbol(symbol, SymbolKind::kOperation, static_cast<uint32_t>(opIdx + 1));
        }
        operations_[opIdx].alive = false;
        return true;
    }

    bool GraphBuilder::eraseOp(OperationId op, std::span<const ValueId> replacementResults)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            return false;
        }
        const auto &results = operations_[opIdx].results;
        if (replacementResults.size() != results.size())
        {
            return false;
        }
        for (const auto &value : replacementResults)
        {
            const std::size_t valIdx = valueIndex(value);
            if (!values_[valIdx].alive)
            {
                throw std::runtime_error("Replacement value is erased");
            }
        }
        for (std::size_t i = 0; i < results.size(); ++i)
        {
            const ValueId from = results[i];
            const ValueId to = replacementResults[i];
            if (from == to)
            {
                continue;
            }
            for (std::size_t opIdxIter = 0; opIdxIter < operations_.size(); ++opIdxIter)
            {
                if (!operations_[opIdxIter].alive)
                {
                    continue;
                }
                OperationId iterId;
                iterId.index = static_cast<uint32_t>(opIdxIter + 1);
                iterId.generation = 0;
                iterId.graph = graphId_;
                if (iterId == op)
                {
                    continue;
                }
                for (auto &operand : operations_[opIdxIter].operands)
                {
                    if (operand == from)
                    {
                        operand = to;
                    }
                }
            }
        }
        for (const auto &result : results)
        {
            const std::size_t valIdx = valueIndex(result);
            if (values_[valIdx].definingOp == op)
            {
                values_[valIdx].definingOp = OperationId::invalid();
            }
        }
        const SymbolId symbol = operations_[opIdx].symbol;
        if (symbol.valid())
        {
            unbindSymbol(symbol, SymbolKind::kOperation, static_cast<uint32_t>(opIdx + 1));
        }
        operations_[opIdx].alive = false;
        return true;
    }

    bool GraphBuilder::eraseValue(ValueId value)
    {
        const std::size_t valIdx = valueIndex(value);
        if (!values_[valIdx].alive)
        {
            return false;
        }
        if (countValueUses(value, std::nullopt) != 0)
        {
            return false;
        }
        if (values_[valIdx].isInput || values_[valIdx].isOutput)
        {
            return false;
        }
        if (values_[valIdx].definingOp.valid())
        {
            return false;
        }
        const SymbolId symbol = values_[valIdx].symbol;
        if (symbol.valid())
        {
            unbindSymbol(symbol, SymbolKind::kValue, static_cast<uint32_t>(valIdx + 1));
        }
        values_[valIdx].alive = false;
        return true;
    }

    void GraphBuilder::bindPort(std::vector<Port> &ports, SymbolId name, ValueId value, std::string_view context)
    {
        validateSymbol(name, context);
        const std::size_t valIdx = valueIndex(value);
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }
        bool updated = false;
        for (auto &port : ports)
        {
            if (port.name == name)
            {
                port.value = value;
                updated = true;
                break;
            }
        }
        if (!updated)
        {
            ports.push_back(Port{name, value});
        }
        recomputePortFlags();
    }

    void GraphBuilder::bindInputPort(SymbolId name, ValueId value)
    {
        bindPort(inputPorts_, name, value, "Input port");
    }

    void GraphBuilder::bindOutputPort(SymbolId name, ValueId value)
    {
        bindPort(outputPorts_, name, value, "Output port");
    }

    void GraphBuilder::setAttr(OperationId op, std::string_view key, AttributeValue value)
    {
        if (!attributeValueIsJsonSerializable(value))
        {
            throw std::runtime_error("Attribute value must be JSON-serializable");
        }
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        auto &attrs = operations_[opIdx].attrs;
        for (auto &attr : attrs)
        {
            if (attr.key == key)
            {
                attr.value = std::move(value);
                return;
            }
        }
        attrs.push_back(AttrKV{std::string(key), std::move(value)});
    }

    void GraphBuilder::setOpKind(OperationId op, OperationKind kind)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        operations_[opIdx].kind = kind;
    }

    bool GraphBuilder::eraseAttr(OperationId op, std::string_view key)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            return false;
        }
        auto &attrs = operations_[opIdx].attrs;
        for (auto it = attrs.begin(); it != attrs.end(); ++it)
        {
            if (it->key == key)
            {
                attrs.erase(it);
                return true;
            }
        }
        return false;
    }

    void GraphBuilder::setValueSrcLoc(ValueId value, SrcLoc loc)
    {
        const std::size_t valIdx = valueIndex(value);
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }
        values_[valIdx].srcLoc = std::move(loc);
    }

    void GraphBuilder::setOpSrcLoc(OperationId op, SrcLoc loc)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        operations_[opIdx].srcLoc = std::move(loc);
    }

    void GraphBuilder::setOpSymbol(OperationId op, SymbolId sym)
    {
        validateSymbol(sym, "Operation");
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        const SymbolId old = operations_[opIdx].symbol;
        if (old == sym)
        {
            return;
        }
        if (symbolIndex_.find(sym.value) != symbolIndex_.end())
        {
            throw std::runtime_error("Operation symbol already bound to value or operation");
        }
        if (old.valid())
        {
            unbindSymbol(old, SymbolKind::kOperation, static_cast<uint32_t>(opIdx + 1));
        }
        bindSymbol(sym, SymbolKind::kOperation, static_cast<uint32_t>(opIdx + 1), "Operation");
        operations_[opIdx].symbol = sym;
    }

    void GraphBuilder::setValueSymbol(ValueId value, SymbolId sym)
    {
        validateSymbol(sym, "Value");
        const std::size_t valIdx = valueIndex(value);
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }
        const SymbolId old = values_[valIdx].symbol;
        if (old == sym)
        {
            return;
        }
        if (symbolIndex_.find(sym.value) != symbolIndex_.end())
        {
            throw std::runtime_error("Value symbol already bound to value or operation");
        }
        if (old.valid())
        {
            unbindSymbol(old, SymbolKind::kValue, static_cast<uint32_t>(valIdx + 1));
        }
        bindSymbol(sym, SymbolKind::kValue, static_cast<uint32_t>(valIdx + 1), "Value");
        values_[valIdx].symbol = sym;
    }

    void GraphBuilder::clearOpSymbol(OperationId op)
    {
        const std::size_t opIdx = opIndex(op);
        if (!operations_[opIdx].alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }
        const SymbolId old = operations_[opIdx].symbol;
        if (old.valid())
        {
            unbindSymbol(old, SymbolKind::kOperation, static_cast<uint32_t>(opIdx + 1));
            operations_[opIdx].symbol = SymbolId::invalid();
        }
    }

    void GraphBuilder::clearValueSymbol(ValueId value)
    {
        const std::size_t valIdx = valueIndex(value);
        if (!values_[valIdx].alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }
        const SymbolId old = values_[valIdx].symbol;
        if (old.valid())
        {
            unbindSymbol(old, SymbolKind::kValue, static_cast<uint32_t>(valIdx + 1));
            values_[valIdx].symbol = SymbolId::invalid();
        }
    }

    GraphView GraphBuilder::freeze() const
    {
        GraphView view;
        view.graphId_ = graphId_;

        std::vector<uint32_t> valueRemap(values_.size() + 1, 0);
        std::vector<uint32_t> opRemap(operations_.size() + 1, 0);
        std::size_t valueCount = 0;
        std::size_t opCount = 0;
        for (std::size_t i = 0; i < values_.size(); ++i)
        {
            if (!values_[i].alive)
            {
                continue;
            }
            valueRemap[i + 1] = static_cast<uint32_t>(++valueCount);
        }
        for (std::size_t i = 0; i < operations_.size(); ++i)
        {
            if (!operations_[i].alive)
            {
                continue;
            }
            opRemap[i + 1] = static_cast<uint32_t>(++opCount);
        }

        auto remapValue = [&](ValueId value) -> ValueId {
            value.assertGraph(graphId_);
            if (value.index == 0 || value.index >= valueRemap.size())
            {
                throw std::runtime_error("ValueId out of range during freeze");
            }
            uint32_t newIndex = valueRemap[value.index];
            if (newIndex == 0)
            {
                throw std::runtime_error("ValueId refers to erased value during freeze");
            }
            ValueId remapped;
            remapped.index = newIndex;
            remapped.generation = 0;
            remapped.graph = graphId_;
            return remapped;
        };

        auto remapOp = [&](OperationId op) -> OperationId {
            op.assertGraph(graphId_);
            if (op.index == 0 || op.index >= opRemap.size())
            {
                throw std::runtime_error("OperationId out of range during freeze");
            }
            uint32_t newIndex = opRemap[op.index];
            if (newIndex == 0)
            {
                return OperationId::invalid();
            }
            OperationId remapped;
            remapped.index = newIndex;
            remapped.generation = 0;
            remapped.graph = graphId_;
            return remapped;
        };

        auto portLess = [&](const Port &lhs, const Port &rhs) {
            if (symbols_)
            {
                return symbols_->text(lhs.name) < symbols_->text(rhs.name);
            }
            return lhs.name.value < rhs.name.value;
        };

        auto bindViewSymbol = [&](SymbolId sym, GraphView::SymbolKind kind, uint32_t index, std::string_view context) {
            if (!sym.valid())
            {
                return;
            }
            auto [it, inserted] = view.symbolIndex_.emplace(sym.value, GraphView::SymbolBinding{kind, index});
            if (!inserted)
            {
                const char *owner = it->second.kind == GraphView::SymbolKind::kValue ? "value" : "operation";
                throw std::runtime_error(std::string(context) + " symbol already bound to " + owner);
            }
        };

        view.operations_.reserve(opCount);
        view.opKinds_.reserve(opCount);
        view.opSymbols_.reserve(opCount);
        view.opOperandRanges_.reserve(opCount);
        view.opResultRanges_.reserve(opCount);
        view.opAttrRanges_.reserve(opCount);
        view.opSrcLocs_.reserve(opCount);

        std::size_t operandOffset = 0;
        std::size_t resultOffset = 0;
        std::size_t attrOffset = 0;
        for (std::size_t i = 0; i < operations_.size(); ++i)
        {
            const OperationData &opData = operations_[i];
            if (!opData.alive)
            {
                continue;
            }
            OperationId opId;
            opId.index = opRemap[i + 1];
            opId.generation = 0;
            opId.graph = graphId_;

            view.operations_.push_back(opId);
            view.opKinds_.push_back(opData.kind);
            view.opSymbols_.push_back(opData.symbol);
            view.opSrcLocs_.push_back(opData.srcLoc);
            bindViewSymbol(opData.symbol, GraphView::SymbolKind::kOperation, opId.index, "Operation");

            view.opOperandRanges_.push_back(Range{operandOffset, opData.operands.size()});
            for (const auto &operand : opData.operands)
            {
                view.operands_.push_back(remapValue(operand));
            }
            operandOffset += opData.operands.size();

            view.opResultRanges_.push_back(Range{resultOffset, opData.results.size()});
            for (const auto &result : opData.results)
            {
                view.results_.push_back(remapValue(result));
            }
            resultOffset += opData.results.size();

            view.opAttrRanges_.push_back(Range{attrOffset, opData.attrs.size()});
            view.opAttrs_.insert(view.opAttrs_.end(), opData.attrs.begin(), opData.attrs.end());
            attrOffset += opData.attrs.size();
        }

        view.values_.reserve(valueCount);
        view.valueSymbols_.reserve(valueCount);
        view.valueWidths_.reserve(valueCount);
        view.valueSigned_.reserve(valueCount);
        view.valueIsInput_.reserve(valueCount);
        view.valueIsOutput_.reserve(valueCount);
        view.valueDefs_.reserve(valueCount);
        view.valueUserRanges_.reserve(valueCount);
        view.valueSrcLocs_.reserve(valueCount);

        for (std::size_t i = 0; i < values_.size(); ++i)
        {
            const ValueData &valueData = values_[i];
            if (!valueData.alive)
            {
                continue;
            }
            ValueId valueId;
            valueId.index = valueRemap[i + 1];
            valueId.generation = 0;
            valueId.graph = graphId_;
            view.values_.push_back(valueId);
            view.valueSymbols_.push_back(valueData.symbol);
            view.valueWidths_.push_back(valueData.width);
            view.valueSigned_.push_back(valueData.isSigned ? 1 : 0);
            view.valueIsInput_.push_back(valueData.isInput ? 1 : 0);
            view.valueIsOutput_.push_back(valueData.isOutput ? 1 : 0);
            view.valueSrcLocs_.push_back(valueData.srcLoc);
            bindViewSymbol(valueData.symbol, GraphView::SymbolKind::kValue, valueId.index, "Value");

            OperationId def = OperationId::invalid();
            if (valueData.definingOp.valid())
            {
                def = remapOp(valueData.definingOp);
            }
            view.valueDefs_.push_back(def);
        }

        view.inputPorts_.reserve(inputPorts_.size());
        for (const auto &port : inputPorts_)
        {
            Port mapped{port.name, remapValue(port.value)};
            view.inputPorts_.push_back(mapped);
        }
        std::sort(view.inputPorts_.begin(), view.inputPorts_.end(), portLess);

        view.outputPorts_.reserve(outputPorts_.size());
        for (const auto &port : outputPorts_)
        {
            Port mapped{port.name, remapValue(port.value)};
            view.outputPorts_.push_back(mapped);
        }
        std::sort(view.outputPorts_.begin(), view.outputPorts_.end(), portLess);

        std::vector<std::vector<ValueUser>> users(valueCount);
        for (std::size_t i = 0; i < operations_.size(); ++i)
        {
            const OperationData &opData = operations_[i];
            if (!opData.alive)
            {
                continue;
            }
            OperationId opId;
            opId.index = opRemap[i + 1];
            opId.generation = 0;
            opId.graph = graphId_;
            for (std::size_t operandIndex = 0; operandIndex < opData.operands.size(); ++operandIndex)
            {
                const ValueId valueId = remapValue(opData.operands[operandIndex]);
                const std::size_t valIdx = static_cast<std::size_t>(valueId.index - 1);
                users[valIdx].push_back(ValueUser{opId, static_cast<uint32_t>(operandIndex)});
            }
        }

        std::size_t userOffset = 0;
        for (const auto &bucket : users)
        {
            view.valueUserRanges_.push_back(Range{userOffset, bucket.size()});
            view.useList_.insert(view.useList_.end(), bucket.begin(), bucket.end());
            userOffset += bucket.size();
        }

        return view;
    }

    std::size_t GraphBuilder::valueIndex(ValueId value) const
    {
        value.assertGraph(graphId_);
        const std::size_t index = static_cast<std::size_t>(value.index);
        if (index == 0 || index > values_.size())
        {
            throw std::runtime_error("ValueId out of range");
        }
        return index - 1;
    }

    std::size_t GraphBuilder::opIndex(OperationId op) const
    {
        op.assertGraph(graphId_);
        const std::size_t index = static_cast<std::size_t>(op.index);
        if (index == 0 || index > operations_.size())
        {
            throw std::runtime_error("OperationId out of range");
        }
        return index - 1;
    }

    bool GraphBuilder::valueAlive(ValueId value) const
    {
        return values_[valueIndex(value)].alive;
    }

    bool GraphBuilder::opAlive(OperationId op) const
    {
        return operations_[opIndex(op)].alive;
    }

    std::size_t GraphBuilder::countValueUses(ValueId value, std::optional<OperationId> skipOp) const
    {
        std::optional<uint32_t> skipIndex;
        if (skipOp)
        {
            skipOp->assertGraph(graphId_);
            skipIndex = skipOp->index;
        }
        std::size_t count = 0;
        for (std::size_t i = 0; i < operations_.size(); ++i)
        {
            if (!operations_[i].alive)
            {
                continue;
            }
            if (skipIndex && *skipIndex == static_cast<uint32_t>(i + 1))
            {
                continue;
            }
            for (const auto &operand : operations_[i].operands)
            {
                if (operand == value)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    void GraphBuilder::recomputePortFlags()
    {
        for (auto &value : values_)
        {
            value.isInput = false;
            value.isOutput = false;
        }
        for (const auto &port : inputPorts_)
        {
            const std::size_t valIdx = valueIndex(port.value);
            if (!values_[valIdx].alive)
            {
                throw std::runtime_error("Input port references erased value");
            }
            values_[valIdx].isInput = true;
        }
        for (const auto &port : outputPorts_)
        {
            const std::size_t valIdx = valueIndex(port.value);
            if (!values_[valIdx].alive)
            {
                throw std::runtime_error("Output port references erased value");
            }
            values_[valIdx].isOutput = true;
        }
    }

    void GraphBuilder::validateSymbol(SymbolId sym, std::string_view context) const
    {
        if (!sym.valid())
        {
            throw std::runtime_error(std::string(context) + " symbol is invalid");
        }
        if (symbols_ && !symbols_->valid(sym))
        {
            throw std::runtime_error(std::string(context) + " symbol is not in the symbol table");
        }
    }

    void GraphBuilder::bindSymbol(SymbolId sym, SymbolKind kind, uint32_t index, std::string_view context)
    {
        if (!sym.valid())
        {
            return;
        }
        auto [it, inserted] = symbolIndex_.emplace(sym.value, SymbolBinding{kind, index});
        if (!inserted)
        {
            if (it->second.kind == kind && it->second.index == index)
            {
                return;
            }
            const char *owner = it->second.kind == SymbolKind::kValue ? "value" : "operation";
            std::string message = std::string(context) + " symbol already bound to " + owner;
            if (symbols_ && symbols_->valid(sym))
            {
                std::string_view symbolText = symbols_->text(sym);
                if (!symbolText.empty())
                {
                    message.append(": ");
                    message.append(symbolText);
                }
            }
            throw std::runtime_error(message);
        }
    }

    void GraphBuilder::unbindSymbol(SymbolId sym, SymbolKind kind, uint32_t index)
    {
        if (!sym.valid())
        {
            return;
        }
        auto it = symbolIndex_.find(sym.value);
        if (it == symbolIndex_.end())
        {
            throw std::runtime_error("Symbol binding missing during unbind");
        }
        if (it->second.kind != kind || it->second.index != index)
        {
            throw std::runtime_error("Symbol binding mismatch during unbind");
        }
        symbolIndex_.erase(it);
    }

    Netlist::Netlist(Netlist &&other) noexcept
    {
        *this = std::move(other);
    }

    Netlist &Netlist::operator=(Netlist &&other) noexcept
    {
        if (this != &other)
        {
            graphs_ = std::move(other.graphs_);
            graphAliasBySymbol_ = std::move(other.graphAliasBySymbol_);
            graphOrder_ = std::move(other.graphOrder_);
            topGraphs_ = std::move(other.topGraphs_);
            netlistSymbols_ = std::move(other.netlistSymbols_);
            resetGraphOwners();

            other.graphAliasBySymbol_.clear();
            other.graphOrder_.clear();
            other.topGraphs_.clear();
        }
        return *this;
    }

    void Netlist::resetGraphOwners()
    {
        for (auto &entry : graphs_)
        {
            if (entry.second)
            {
                entry.second->owner_ = this;
            }
        }
    }

    namespace
    {

        template <class... Ts>
        struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <class... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

        constexpr std::string_view kOperationNames[] = {
            "kConstant",
            "kAdd",
            "kSub",
            "kMul",
            "kDiv",
            "kMod",
            "kEq",
            "kNe",
            "kLt",
            "kLe",
            "kGt",
            "kGe",
            "kAnd",
            "kOr",
            "kXor",
            "kXnor",
            "kNot",
            "kLogicAnd",
            "kLogicOr",
            "kLogicNot",
            "kReduceAnd",
            "kReduceOr",
            "kReduceXor",
            "kReduceNor",
            "kReduceNand",
            "kReduceXnor",
            "kShl",
            "kLShr",
            "kAShr",
            "kMux",
            "kAssign",
            "kConcat",
            "kReplicate",
            "kSliceStatic",
            "kSliceDynamic",
            "kSliceArray",
            "kLatch",
            "kLatchArst",
            "kRegister",
            "kRegisterEn",
            "kRegisterRst",
            "kRegisterEnRst",
            "kRegisterArst",
            "kRegisterEnArst",
            "kMemory",
            "kMemoryAsyncReadPort",
            "kMemorySyncReadPort",
            "kMemorySyncReadPortRst",
            "kMemorySyncReadPortArst",
            "kMemoryWritePort",
            "kMemoryWritePortRst",
            "kMemoryWritePortArst",
            "kMemoryMaskWritePort",
            "kMemoryMaskWritePortRst",
            "kMemoryMaskWritePortArst",
            "kInstance",
            "kBlackbox",
            "kDisplay",
            "kAssert",
            "kDpicImport",
            "kDpicCall"};

        void writeAttributeValue(slang::JsonWriter &writer, const AttributeValue &value)
        {
            if (!attributeValueIsJsonSerializable(value))
            {
                throw std::runtime_error("Attribute value is not JSON serializable");
            }
            std::visit(
                Overloaded{
                    [&](bool v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("bool"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](int64_t v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("int"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](double v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("double"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](const std::string &v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("string"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](const std::vector<bool> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("bool[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (bool entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<int64_t> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("int[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (int64_t entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<double> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("double[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (double entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<std::string> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("string[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (const auto &entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    }},
                value);
        }

        void writeSrcLoc(slang::JsonWriter &writer, const std::optional<SrcLoc> &srcLoc)
        {
            if (!srcLoc || srcLoc->file.empty())
            {
                return;
            }
            writer.writeProperty("loc");
            writer.startObject();
            writer.writeProperty("file");
            writer.writeValue(srcLoc->file);
            writer.writeProperty("line");
            writer.writeValue(static_cast<int64_t>(srcLoc->line));
            writer.writeProperty("col");
            writer.writeValue(static_cast<int64_t>(srcLoc->column));
            writer.writeProperty("endLine");
            writer.writeValue(static_cast<int64_t>(srcLoc->endLine));
            writer.writeProperty("endCol");
            writer.writeValue(static_cast<int64_t>(srcLoc->endColumn));
            writer.endObject();
        }

    } // namespace

    std::string_view toString(OperationKind kind) noexcept
    {
        auto index = static_cast<std::size_t>(kind);
        if (index >= std::size(kOperationNames))
        {
            return "<unknown>";
        }
        return kOperationNames[index];
    }

    std::optional<OperationKind> parseOperationKind(std::string_view text) noexcept
    {
        for (std::size_t i = 0; i < std::size(kOperationNames); ++i)
        {
            if (kOperationNames[i] == text)
            {
                return static_cast<OperationKind>(i);
            }
        }
        return std::nullopt;
    }

    Value::Value(ValueId id, SymbolId symbol, std::string symbolText, int32_t width, bool isSigned,
                 bool isInput, bool isOutput, OperationId definingOp,
                 std::vector<ValueUser> users, std::optional<SrcLoc> srcLoc)
        : id_(id),
          symbol_(symbol),
          symbolText_(std::move(symbolText)),
          width_(width),
          isSigned_(isSigned),
          isInput_(isInput),
          isOutput_(isOutput),
          definingOp_(definingOp),
          users_(std::move(users)),
          srcLoc_(std::move(srcLoc))
    {
    }

    Operation::Operation(OperationId id, OperationKind kind, SymbolId symbol, std::string symbolText,
                         std::vector<ValueId> operands, std::vector<ValueId> results,
                         std::vector<AttrKV> attrs, std::optional<SrcLoc> srcLoc)
        : id_(id),
          kind_(kind),
          symbol_(symbol),
          symbolText_(std::move(symbolText)),
          operands_(std::move(operands)),
          results_(std::move(results)),
          attrs_(std::move(attrs)),
          srcLoc_(std::move(srcLoc))
    {
    }

    std::optional<AttributeValue> Operation::attr(std::string_view key) const
    {
        for (const auto &entry : attrs_)
        {
            if (entry.key == key)
            {
                return entry.value;
            }
        }
        return std::nullopt;
    }

    Graph::Graph(Netlist &owner, std::string symbol, GraphId graphId)
        : owner_(&owner),
          symbol_(std::move(symbol)),
          graphId_(graphId)
    {
        if (symbol_.empty())
        {
            throw std::invalid_argument("Graph symbol must not be empty");
        }
        if (!graphId_.valid())
        {
            throw std::invalid_argument("GraphId must be valid");
        }
        GraphBuilder builder(symbols_, graphId_);
        view_ = builder.freeze();
    }

    SymbolId Graph::internSymbol(std::string_view text)
    {
        if (symbols_.contains(text))
        {
            return symbols_.lookup(text);
        }
        return symbols_.intern(text);
    }

    SymbolId Graph::lookupSymbol(std::string_view text) const
    {
        return symbols_.lookup(text);
    }

    std::string_view Graph::symbolText(SymbolId id) const
    {
        if (!id.valid())
        {
            return std::string_view{};
        }
        return symbols_.text(id);
    }

    const GraphView *Graph::viewIfFrozen() const noexcept
    {
        if (builder_)
        {
            return nullptr;
        }
        return view_ ? &*view_ : nullptr;
    }

    const GraphView &Graph::freeze()
    {
        if (builder_)
        {
            view_ = builder_->freeze();
            builder_.reset();
            invalidateCaches();
        }
        if (!view_)
        {
            GraphBuilder builder(symbols_, graphId_);
            view_ = builder.freeze();
        }
        return *view_;
    }

    std::span<const OperationId> Graph::operations() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->operations();
            }
            return std::span<const OperationId>();
        }
        ensureCaches();
        return std::span<const OperationId>(operationsCache_.data(), operationsCache_.size());
    }

    std::span<const ValueId> Graph::values() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->values();
            }
            return std::span<const ValueId>();
        }
        ensureCaches();
        return std::span<const ValueId>(valuesCache_.data(), valuesCache_.size());
    }

    std::span<const Port> Graph::inputPorts() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->inputPorts();
            }
            return std::span<const Port>();
        }
        ensureCaches();
        return std::span<const Port>(inputPortsCache_.data(), inputPortsCache_.size());
    }

    std::span<const Port> Graph::outputPorts() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->outputPorts();
            }
            return std::span<const Port>();
        }
        ensureCaches();
        return std::span<const Port>(outputPortsCache_.data(), outputPortsCache_.size());
    }

    ValueId Graph::createValue(SymbolId symbol, int32_t width, bool isSigned)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.addValue(symbol, width, isSigned);
    }

    OperationId Graph::createOperation(OperationKind kind, SymbolId symbol)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.addOp(kind, symbol);
    }

    ValueId Graph::findValue(SymbolId symbol) const noexcept
    {
        if (!symbol.valid())
        {
            return ValueId::invalid();
        }
        if (builder_)
        {
            auto it = builder_->symbolIndex_.find(symbol.value);
            if (it == builder_->symbolIndex_.end() || it->second.kind != GraphBuilder::SymbolKind::kValue)
            {
                return ValueId::invalid();
            }
            ValueId id;
            id.index = it->second.index;
            id.generation = 0;
            id.graph = graphId_;
            return id;
        }
        if (!view_)
        {
            return ValueId::invalid();
        }
        return view_->findValue(symbol);
    }

    OperationId Graph::findOperation(SymbolId symbol) const noexcept
    {
        if (!symbol.valid())
        {
            return OperationId::invalid();
        }
        if (builder_)
        {
            auto it = builder_->symbolIndex_.find(symbol.value);
            if (it == builder_->symbolIndex_.end() || it->second.kind != GraphBuilder::SymbolKind::kOperation)
            {
                return OperationId::invalid();
            }
            OperationId id;
            id.index = it->second.index;
            id.generation = 0;
            id.graph = graphId_;
            return id;
        }
        if (!view_)
        {
            return OperationId::invalid();
        }
        return view_->findOperation(symbol);
    }

    ValueId Graph::findValue(std::string_view symbol) const
    {
        return findValue(lookupSymbol(symbol));
    }

    OperationId Graph::findOperation(std::string_view symbol) const
    {
        return findOperation(lookupSymbol(symbol));
    }

    Value Graph::getValue(ValueId id) const
    {
        if (builder_)
        {
            return valueFromBuilder(id);
        }
        return valueFromView(id);
    }

    Operation Graph::getOperation(OperationId id) const
    {
        if (builder_)
        {
            return operationFromBuilder(id);
        }
        return operationFromView(id);
    }

    void Graph::bindInputPort(SymbolId name, ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.bindInputPort(name, value);
    }

    void Graph::bindOutputPort(SymbolId name, ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.bindOutputPort(name, value);
    }

    ValueId Graph::inputPortValue(SymbolId name) const noexcept
    {
        if (builder_)
        {
            return findPortValue(std::span<const Port>(builder_->inputPorts_.data(),
                                                       builder_->inputPorts_.size()),
                                 name);
        }
        if (!view_)
        {
            return ValueId::invalid();
        }
        return findPortValue(view_->inputPorts(), name);
    }

    ValueId Graph::outputPortValue(SymbolId name) const noexcept
    {
        if (builder_)
        {
            return findPortValue(std::span<const Port>(builder_->outputPorts_.data(),
                                                       builder_->outputPorts_.size()),
                                 name);
        }
        if (!view_)
        {
            return ValueId::invalid();
        }
        return findPortValue(view_->outputPorts(), name);
    }

    void Graph::addOperand(OperationId op, ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.addOperand(op, value);
    }

    void Graph::addResult(OperationId op, ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.addResult(op, value);
    }

    void Graph::replaceOperand(OperationId op, std::size_t index, ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.replaceOperand(op, index, value);
    }

    void Graph::replaceResult(OperationId op, std::size_t index, ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.replaceResult(op, index, value);
    }

    void Graph::replaceAllUses(ValueId from, ValueId to)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.replaceAllUses(from, to);
    }

    bool Graph::eraseOperand(OperationId op, std::size_t index)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseOperand(op, index);
    }

    bool Graph::eraseResult(OperationId op, std::size_t index)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseResult(op, index);
    }

    bool Graph::eraseOp(OperationId op)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseOp(op);
    }

    bool Graph::eraseOp(OperationId op, std::span<const ValueId> replacementResults)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseOp(op, replacementResults);
    }

    bool Graph::eraseValue(ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseValue(value);
    }

    void Graph::setAttr(OperationId op, std::string_view key, AttributeValue value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setAttr(op, key, std::move(value));
    }

    void Graph::setOpKind(OperationId op, OperationKind kind)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setOpKind(op, kind);
    }

    bool Graph::eraseAttr(OperationId op, std::string_view key)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseAttr(op, key);
    }

    void Graph::setValueSrcLoc(ValueId value, SrcLoc loc)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setValueSrcLoc(value, std::move(loc));
    }

    void Graph::setOpSrcLoc(OperationId op, SrcLoc loc)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setOpSrcLoc(op, std::move(loc));
    }

    void Graph::setOpSymbol(OperationId op, SymbolId sym)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setOpSymbol(op, sym);
    }

    void Graph::setValueSymbol(ValueId value, SymbolId sym)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setValueSymbol(value, sym);
    }

    void Graph::clearOpSymbol(OperationId op)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.clearOpSymbol(op);
    }

    void Graph::clearValueSymbol(ValueId value)
    {
        GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.clearValueSymbol(value);
    }

    void Graph::writeJson(slang::JsonWriter &writer) const
    {
        auto symbolTextOrEmpty = [&](SymbolId sym) -> std::string_view
        {
            return sym.valid() ? symbols_.text(sym) : std::string_view{};
        };

        writer.startObject();
        writer.writeProperty("symbol");
        writer.writeValue(symbol_);

        writer.writeProperty("vals");
        writer.startArray();
        for (const auto &valueId : values())
        {
            const Value value = getValue(valueId);
            writer.startObject();
            writer.writeProperty("sym");
            writer.writeValue(symbolTextOrEmpty(value.symbol()));
            writer.writeProperty("w");
            writer.writeValue(static_cast<int64_t>(value.width()));
            writer.writeProperty("sgn");
            writer.writeValue(value.isSigned());
            writer.writeProperty("in");
            writer.writeValue(value.isInput());
            writer.writeProperty("out");
            writer.writeValue(value.isOutput());
            if (value.definingOp().valid())
            {
                Operation defOp = getOperation(value.definingOp());
                writer.writeProperty("def");
                writer.writeValue(symbolTextOrEmpty(defOp.symbol()));
            }

            writer.writeProperty("users");
            writer.startArray();
            for (const auto &user : value.users())
            {
                writer.startObject();
                writer.writeProperty("op");
                Operation userOp = getOperation(user.operation);
                writer.writeValue(symbolTextOrEmpty(userOp.symbol()));
                writer.writeProperty("idx");
                writer.writeValue(static_cast<int64_t>(user.operandIndex));
                writer.endObject();
            }
            writer.endArray();
            writeSrcLoc(writer, value.srcLoc());
            writer.endObject();
        }
        writer.endArray();

        writer.writeProperty("ports");
        writer.startObject();

        writer.writeProperty("in");
        writer.startArray();
        for (const auto &port : inputPorts())
        {
            writer.startObject();
            writer.writeProperty("name");
            writer.writeValue(symbolTextOrEmpty(port.name));
            writer.writeProperty("val");
            Value value = getValue(port.value);
            writer.writeValue(symbolTextOrEmpty(value.symbol()));
            writer.endObject();
        }
        writer.endArray();

        writer.writeProperty("out");
        writer.startArray();
        for (const auto &port : outputPorts())
        {
            writer.startObject();
            writer.writeProperty("name");
            writer.writeValue(symbolTextOrEmpty(port.name));
            writer.writeProperty("val");
            Value value = getValue(port.value);
            writer.writeValue(symbolTextOrEmpty(value.symbol()));
            writer.endObject();
        }
        writer.endArray();

        writer.endObject(); // ports

        writer.writeProperty("ops");
        writer.startArray();
        for (const auto &opId : operations())
        {
            const Operation op = getOperation(opId);
            writer.startObject();
            writer.writeProperty("sym");
            writer.writeValue(symbolTextOrEmpty(op.symbol()));
            writer.writeProperty("kind");
            writer.writeValue(toString(op.kind()));

            writer.writeProperty("in");
            writer.startArray();
            for (const auto &operand : op.operands())
            {
                Value value = getValue(operand);
                writer.writeValue(symbolTextOrEmpty(value.symbol()));
            }
            writer.endArray();

            writer.writeProperty("out");
            writer.startArray();
            for (const auto &result : op.results())
            {
                Value value = getValue(result);
                writer.writeValue(symbolTextOrEmpty(value.symbol()));
            }
            writer.endArray();

            if (!op.attrs().empty())
            {
                writer.writeProperty("attrs");
                writer.startObject();
                for (const auto &attr : op.attrs())
                {
                    writer.writeProperty(attr.key);
                    writer.startObject();
                    writeAttributeValue(writer, attr.value);
                    writer.endObject();
                }
                writer.endObject();
            }

            writeSrcLoc(writer, op.srcLoc());
            writer.endObject();
        }
        writer.endArray();

        writer.endObject();
    }

    void Graph::invalidateCaches() const
    {
        cacheValid_ = false;
        valuesCache_.clear();
        operationsCache_.clear();
        inputPortsCache_.clear();
        outputPortsCache_.clear();
    }

    void Graph::ensureCaches() const
    {
        if (cacheValid_)
        {
            return;
        }
        valuesCache_.clear();
        operationsCache_.clear();
        inputPortsCache_.clear();
        outputPortsCache_.clear();
        if (builder_)
        {
            const auto &values = builder_->values_;
            valuesCache_.reserve(values.size());
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (!values[i].alive)
                {
                    continue;
                }
                ValueId id;
                id.index = static_cast<uint32_t>(i + 1);
                id.generation = 0;
                id.graph = graphId_;
                valuesCache_.push_back(id);
            }

            const auto &ops = builder_->operations_;
            operationsCache_.reserve(ops.size());
            for (std::size_t i = 0; i < ops.size(); ++i)
            {
                if (!ops[i].alive)
                {
                    continue;
                }
                OperationId id;
                id.index = static_cast<uint32_t>(i + 1);
                id.generation = 0;
                id.graph = graphId_;
                operationsCache_.push_back(id);
            }
            inputPortsCache_ = builder_->inputPorts_;
            outputPortsCache_ = builder_->outputPorts_;
        }
        cacheValid_ = true;
    }

    GraphBuilder &Graph::ensureBuilder()
    {
        if (builder_)
        {
            return *builder_;
        }
        if (view_)
        {
            builder_ = GraphBuilder::fromView(*view_, symbols_);
            view_.reset();
        }
        else
        {
            builder_.emplace(symbols_, graphId_);
        }
        invalidateCaches();
        return *builder_;
    }

    const GraphView &Graph::view() const
    {
        if (!view_)
        {
            throw std::runtime_error("GraphView is not available; freeze the graph first");
        }
        return *view_;
    }

    Value Graph::valueFromView(ValueId id) const
    {
        const GraphView &graphView = view();
        id.assertGraph(graphId_);
        SymbolId symbol = graphView.valueSymbol(id);
        std::string symbolText = symbolTextOrEmpty(symbols_, symbol);
        return Value(id,
                     symbol,
                     std::move(symbolText),
                     graphView.valueWidth(id),
                     graphView.valueSigned(id),
                     graphView.valueIsInput(id),
                     graphView.valueIsOutput(id),
                     graphView.valueDef(id),
                     std::vector<ValueUser>(graphView.valueUsers(id).begin(), graphView.valueUsers(id).end()),
                     graphView.valueSrcLoc(id));
    }

    Value Graph::valueFromBuilder(ValueId id) const
    {
        if (!builder_)
        {
            throw std::runtime_error("GraphBuilder is not available");
        }
        id.assertGraph(graphId_);
        if (id.index == 0 || id.index > builder_->values_.size())
        {
            throw std::runtime_error("ValueId out of range");
        }
        const std::size_t idx = static_cast<std::size_t>(id.index - 1);
        const auto &data = builder_->values_[idx];
        if (!data.alive)
        {
            throw std::runtime_error("ValueId refers to erased value");
        }

        std::vector<ValueUser> users;
        const auto &ops = builder_->operations_;
        for (std::size_t opIdx = 0; opIdx < ops.size(); ++opIdx)
        {
            if (!ops[opIdx].alive)
            {
                continue;
            }
            const auto &operands = ops[opIdx].operands;
            for (std::size_t operandIdx = 0; operandIdx < operands.size(); ++operandIdx)
            {
                if (operands[operandIdx] == id)
                {
                    OperationId opId;
                    opId.index = static_cast<uint32_t>(opIdx + 1);
                    opId.generation = 0;
                    opId.graph = graphId_;
                    users.push_back(ValueUser{opId, static_cast<uint32_t>(operandIdx)});
                }
            }
        }

        std::string symbolText = symbolTextOrEmpty(symbols_, data.symbol);
        return Value(id,
                     data.symbol,
                     std::move(symbolText),
                     data.width,
                     data.isSigned,
                     data.isInput,
                     data.isOutput,
                     data.definingOp,
                     std::move(users),
                     data.srcLoc);
    }

    Operation Graph::operationFromView(OperationId id) const
    {
        const GraphView &graphView = view();
        id.assertGraph(graphId_);
        SymbolId symbol = graphView.opSymbol(id);
        std::string symbolText = symbolTextOrEmpty(symbols_, symbol);
        return Operation(id,
                         graphView.opKind(id),
                         symbol,
                         std::move(symbolText),
                         std::vector<ValueId>(graphView.opOperands(id).begin(), graphView.opOperands(id).end()),
                         std::vector<ValueId>(graphView.opResults(id).begin(), graphView.opResults(id).end()),
                         std::vector<AttrKV>(graphView.opAttrs(id).begin(), graphView.opAttrs(id).end()),
                         graphView.opSrcLoc(id));
    }

    Operation Graph::operationFromBuilder(OperationId id) const
    {
        if (!builder_)
        {
            throw std::runtime_error("GraphBuilder is not available");
        }
        id.assertGraph(graphId_);
        if (id.index == 0 || id.index > builder_->operations_.size())
        {
            throw std::runtime_error("OperationId out of range");
        }
        const std::size_t idx = static_cast<std::size_t>(id.index - 1);
        const auto &data = builder_->operations_[idx];
        if (!data.alive)
        {
            throw std::runtime_error("OperationId refers to erased operation");
        }

        std::string symbolText = symbolTextOrEmpty(symbols_, data.symbol);
        return Operation(id,
                         data.kind,
                         data.symbol,
                         std::move(symbolText),
                         data.operands,
                         data.results,
                         data.attrs,
                         data.srcLoc);
    }

    Graph &Netlist::addGraphInternal(std::unique_ptr<Graph> graph)
    {
        auto sym = graph->symbol();
        auto [it, inserted] = graphs_.emplace(sym, std::move(graph));
        if (!inserted)
        {
            throw std::runtime_error("Duplicated graph symbol: " + sym);
        }
        graphOrder_.push_back(sym);
        return *it->second;
    }

    Graph &Netlist::createGraph(std::string symbol)
    {
        if (symbol.empty())
        {
            throw std::invalid_argument("Graph symbol must not be empty");
        }
        if (graphs_.contains(symbol))
        {
            throw std::runtime_error("Duplicated graph symbol: " + symbol);
        }
        SymbolId graphSymbol = netlistSymbols_.contains(symbol) ? netlistSymbols_.lookup(symbol) : netlistSymbols_.intern(symbol);
        GraphId graphId = netlistSymbols_.allocateGraphId(graphSymbol);
        auto instance = std::make_unique<Graph>(*this, std::move(symbol), graphId);
        return addGraphInternal(std::move(instance));
    }

    Graph *Netlist::findGraph(std::string_view symbol) noexcept
    {
        std::string key(symbol);
        if (auto it = graphs_.find(key); it != graphs_.end())
        {
            return it->second.get();
        }
        if (auto aliasIt = graphAliasBySymbol_.find(key); aliasIt != graphAliasBySymbol_.end())
        {
            if (auto resolved = graphs_.find(aliasIt->second); resolved != graphs_.end())
            {
                return resolved->second.get();
            }
        }
        return nullptr;
    }

    const Graph *Netlist::findGraph(std::string_view symbol) const noexcept
    {
        std::string key(symbol);
        if (auto it = graphs_.find(key); it != graphs_.end())
        {
            return it->second.get();
        }
        if (auto aliasIt = graphAliasBySymbol_.find(key); aliasIt != graphAliasBySymbol_.end())
        {
            if (auto resolved = graphs_.find(aliasIt->second); resolved != graphs_.end())
            {
                return resolved->second.get();
            }
        }
        return nullptr;
    }

    std::vector<std::string> Netlist::aliasesForGraph(std::string_view symbol) const
    {
        std::vector<std::string> aliases;
        for (const auto &entry : graphAliasBySymbol_)
        {
            if (entry.second == symbol)
            {
                aliases.push_back(entry.first);
            }
        }
        std::sort(aliases.begin(), aliases.end());
        aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());
        return aliases;
    }

    void Netlist::registerGraphAlias(std::string alias, Graph &graph)
    {
        if (alias.empty())
        {
            return;
        }
        graphAliasBySymbol_[std::move(alias)] = graph.symbol();
    }

    void Netlist::markAsTop(std::string_view graphSymbol)
    {
        if (!findGraph(graphSymbol))
        {
            throw std::runtime_error("Cannot mark unknown graph as top: " + std::string(graphSymbol));
        }
        auto symbolStr = std::string(graphSymbol);
        if (std::find(topGraphs_.begin(), topGraphs_.end(), symbolStr) == topGraphs_.end())
        {
            topGraphs_.push_back(std::move(symbolStr));
        }
    }

} // namespace wolf_sv::grh::ir
