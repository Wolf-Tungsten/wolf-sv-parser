#include "grh.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "slang/text/Json.h"

namespace wolf_sv::grh
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

    namespace ir
    {

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
            const Range &range = opOperandRanges_[opIndex(op)];
            if (range.count == 0)
            {
                return {};
            }
            return std::span<const ValueId>(operands_.data() + range.offset, range.count);
        }

        std::span<const ValueId> GraphView::opResults(OperationId op) const
        {
            const Range &range = opResultRanges_[opIndex(op)];
            if (range.count == 0)
            {
                return {};
            }
            return std::span<const ValueId>(results_.data() + range.offset, range.count);
        }

        SymbolId GraphView::opSymbol(OperationId op) const
        {
            return opSymbols_[opIndex(op)];
        }

        std::span<const AttrKV> GraphView::opAttrs(OperationId op) const
        {
            const Range &range = opAttrRanges_[opIndex(op)];
            if (range.count == 0)
            {
                return {};
            }
            return std::span<const AttrKV>(opAttrs_.data() + range.offset, range.count);
        }

        std::optional<AttributeValue> GraphView::opAttr(OperationId op, SymbolId key) const
        {
            if (!key.valid())
            {
                return std::nullopt;
            }
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
            const Range &range = valueUserRanges_[valueIndex(value)];
            if (range.count == 0)
            {
                return {};
            }
            return std::span<const ValueUser>(useList_.data() + range.offset, range.count);
        }

        std::optional<SrcLoc> GraphView::valueSrcLoc(ValueId value) const
        {
            return valueSrcLocs_[valueIndex(value)];
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
                    builder.validateAttrKey(attr.key);
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
            OperationData data;
            data.kind = kind;
            data.symbol = sym;
            operations_.push_back(std::move(data));
            return id;
        }

        void GraphBuilder::addOperand(OperationId op, ValueId value)
        {
            const std::size_t opIdx = opIndex(op);
            valueIndex(value);
            if (!operations_[opIdx].alive)
            {
                throw std::runtime_error("OperationId refers to erased operation");
            }
            if (!values_[valueIndex(value)].alive)
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
            if (!operations_[opIdx].alive)
            {
                throw std::runtime_error("OperationId refers to erased operation");
            }
            if (!values_[valueIndex(value)].alive)
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
            values_[valIdx].alive = false;
            return true;
        }

        void GraphBuilder::bindInputPort(SymbolId name, ValueId value)
        {
            validateSymbol(name, "Input port");
            const std::size_t valIdx = valueIndex(value);
            if (!values_[valIdx].alive)
            {
                throw std::runtime_error("ValueId refers to erased value");
            }
            bool updated = false;
            for (auto &port : inputPorts_)
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
                inputPorts_.push_back(Port{name, value});
            }
            recomputePortFlags();
        }

        void GraphBuilder::bindOutputPort(SymbolId name, ValueId value)
        {
            validateSymbol(name, "Output port");
            const std::size_t valIdx = valueIndex(value);
            if (!values_[valIdx].alive)
            {
                throw std::runtime_error("ValueId refers to erased value");
            }
            bool updated = false;
            for (auto &port : outputPorts_)
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
                outputPorts_.push_back(Port{name, value});
            }
            recomputePortFlags();
        }

        void GraphBuilder::setAttr(OperationId op, SymbolId key, AttributeValue value)
        {
            validateAttrKey(key);
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
            attrs.push_back(AttrKV{key, std::move(value)});
        }

        bool GraphBuilder::eraseAttr(OperationId op, SymbolId key)
        {
            if (!key.valid())
            {
                return false;
            }
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
            values_[valIdx].symbol = sym;
        }

        void GraphBuilder::clearOpSymbol(OperationId op)
        {
            const std::size_t opIdx = opIndex(op);
            if (!operations_[opIdx].alive)
            {
                throw std::runtime_error("OperationId refers to erased operation");
            }
            operations_[opIdx].symbol = SymbolId::invalid();
        }

        void GraphBuilder::clearValueSymbol(ValueId value)
        {
            const std::size_t valIdx = valueIndex(value);
            if (!values_[valIdx].alive)
            {
                throw std::runtime_error("ValueId refers to erased value");
            }
            values_[valIdx].symbol = SymbolId::invalid();
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
            std::size_t count = 0;
            for (std::size_t i = 0; i < operations_.size(); ++i)
            {
                if (!operations_[i].alive)
                {
                    continue;
                }
                OperationId opId;
                opId.index = static_cast<uint32_t>(i + 1);
                opId.generation = 0;
                opId.graph = graphId_;
                if (skipOp && *skipOp == opId)
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

        void GraphBuilder::validateAttrKey(SymbolId key) const
        {
            validateSymbol(key, "Attribute");
        }

    } // namespace ir

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

        enum class AttributeKind
        {
            Bool,
            Int,
            Double,
            String,
            BoolArray,
            IntArray,
            DoubleArray,
            StringArray
        };

        struct JsonValue
        {
            using Array = std::vector<JsonValue>;
            using Object = std::map<std::string, JsonValue, std::less<>>;

            std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array, Object> value;

            bool isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
            bool isBool() const { return std::holds_alternative<bool>(value); }
            bool isInt() const { return std::holds_alternative<int64_t>(value); }
            bool isDouble() const { return std::holds_alternative<double>(value); }
            bool isString() const { return std::holds_alternative<std::string>(value); }
            bool isArray() const { return std::holds_alternative<Array>(value); }
            bool isObject() const { return std::holds_alternative<Object>(value); }

            bool asBool(std::string_view ctx) const
            {
                if (!isBool())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected bool");
                }
                return std::get<bool>(value);
            }

            int64_t asInt(std::string_view ctx) const
            {
                if (isInt())
                {
                    return std::get<int64_t>(value);
                }
                if (isDouble())
                {
                    double v = std::get<double>(value);
                    if (std::trunc(v) != v)
                    {
                        throw std::runtime_error(std::string(ctx) + ": expected integer number");
                    }
                    return static_cast<int64_t>(v);
                }
                throw std::runtime_error(std::string(ctx) + ": expected integer");
            }

            double asDouble(std::string_view ctx) const
            {
                if (isDouble())
                {
                    return std::get<double>(value);
                }
                if (isInt())
                {
                    return static_cast<double>(std::get<int64_t>(value));
                }
                throw std::runtime_error(std::string(ctx) + ": expected number");
            }

            const std::string &asString(std::string_view ctx) const
            {
                if (!isString())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected string");
                }
                return std::get<std::string>(value);
            }

            const Array &asArray(std::string_view ctx) const
            {
                if (!isArray())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected array");
                }
                return std::get<Array>(value);
            }

            const Object &asObject(std::string_view ctx) const
            {
                if (!isObject())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected object");
                }
                return std::get<Object>(value);
            }
        };

        class JsonParser
        {
        public:
            explicit JsonParser(std::string_view text) : text_(text), current_(text_.data()), end_(text_.data() + text_.size()) {}

            JsonValue parse()
            {
                skipWhitespace();
                JsonValue value = parseValue();
                skipWhitespace();
                if (current_ != end_)
                {
                    throw std::runtime_error("Trailing characters after JSON document");
                }
                return value;
            }

        private:
            JsonValue parseValue()
            {
                if (current_ == end_)
                {
                    throw std::runtime_error("Unexpected end of JSON input");
                }

                switch (*current_)
                {
                case '{':
                    return parseObject();
                case '[':
                    return parseArray();
                case '"':
                    return JsonValue{parseString()};
                case 't':
                    return JsonValue{parseLiteral("true", true)};
                case 'f':
                    return JsonValue{parseLiteral("false", false)};
                case 'n':
                    parseLiteral("null");
                    return JsonValue{std::nullptr_t{}};
                default:
                    if (*current_ == '-' || std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        return parseNumber();
                    }
                    break;
                }

                throw std::runtime_error("Invalid JSON value");
            }

            JsonValue parseObject()
            {
                JsonValue::Object obj;
                expect('{');
                skipWhitespace();
                if (consume('}'))
                {
                    return JsonValue{std::move(obj)};
                }

                while (true)
                {
                    skipWhitespace();
                    std::string key = parseString();
                    skipWhitespace();
                    expect(':');
                    skipWhitespace();
                    obj.emplace(std::move(key), parseValue());
                    skipWhitespace();
                    if (consume('}'))
                    {
                        break;
                    }
                    expect(',');
                }

                return JsonValue{std::move(obj)};
            }

            JsonValue parseArray()
            {
                JsonValue::Array arr;
                expect('[');
                skipWhitespace();
                if (consume(']'))
                {
                    return JsonValue{std::move(arr)};
                }

                while (true)
                {
                    skipWhitespace();
                    arr.push_back(parseValue());
                    skipWhitespace();
                    if (consume(']'))
                    {
                        break;
                    }
                    expect(',');
                }

                return JsonValue{std::move(arr)};
            }

            JsonValue parseNumber()
            {
                const char *start = current_;
                if (*current_ == '-')
                {
                    ++current_;
                }
                if (current_ == end_)
                {
                    throw std::runtime_error("Invalid JSON number");
                }
                if (*current_ == '0')
                {
                    ++current_;
                }
                else
                {
                    if (!std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        throw std::runtime_error("Invalid JSON number");
                    }
                    while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        ++current_;
                    }
                }

                bool isFloat = false;
                if (current_ != end_ && *current_ == '.')
                {
                    isFloat = true;
                    ++current_;
                    if (current_ == end_ || !std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        throw std::runtime_error("Invalid JSON number");
                    }
                    while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        ++current_;
                    }
                }

                if (current_ != end_ && (*current_ == 'e' || *current_ == 'E'))
                {
                    isFloat = true;
                    ++current_;
                    if (current_ != end_ && (*current_ == '+' || *current_ == '-'))
                    {
                        ++current_;
                    }
                    if (current_ == end_ || !std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        throw std::runtime_error("Invalid JSON number");
                    }
                    while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        ++current_;
                    }
                }

                std::string_view numberView(start, static_cast<std::size_t>(current_ - start));
                if (isFloat)
                {
                    double value = 0.0;
                    auto [ptr, ec] = std::from_chars(numberView.data(), numberView.data() + numberView.size(), value);
                    if (ec != std::errc())
                    {
                        value = std::stod(std::string(numberView));
                    }
                    return JsonValue{value};
                }

                int64_t intValue = 0;
                auto [ptr, ec] = std::from_chars(numberView.data(), numberView.data() + numberView.size(), intValue);
                if (ec != std::errc())
                {
                    intValue = std::stoll(std::string(numberView));
                }
                return JsonValue{intValue};
            }

            std::string parseString()
            {
                expect('"');
                std::string result;
                while (current_ != end_)
                {
                    char ch = *current_++;
                    if (ch == '"')
                    {
                        break;
                    }
                    if (ch == '\\')
                    {
                        if (current_ == end_)
                        {
                            throw std::runtime_error("Invalid escape sequence");
                        }

                        char esc = *current_++;
                        switch (esc)
                        {
                        case '"':
                        case '\\':
                        case '/':
                            result.push_back(esc);
                            break;
                        case 'b':
                            result.push_back('\b');
                            break;
                        case 'f':
                            result.push_back('\f');
                            break;
                        case 'n':
                            result.push_back('\n');
                            break;
                        case 'r':
                            result.push_back('\r');
                            break;
                        case 't':
                            result.push_back('\t');
                            break;
                        case 'u':
                        {
                            if (end_ - current_ < 4)
                            {
                                throw std::runtime_error("Invalid unicode escape");
                            }
                            unsigned value = 0;
                            for (int i = 0; i < 4; ++i)
                            {
                                char hex = *current_++;
                                value <<= 4;
                                if (hex >= '0' && hex <= '9')
                                {
                                    value |= static_cast<unsigned>(hex - '0');
                                }
                                else if (hex >= 'a' && hex <= 'f')
                                {
                                    value |= static_cast<unsigned>(hex - 'a' + 10);
                                }
                                else if (hex >= 'A' && hex <= 'F')
                                {
                                    value |= static_cast<unsigned>(hex - 'A' + 10);
                                }
                                else
                                {
                                    throw std::runtime_error("Invalid unicode escape");
                                }
                            }
                            if (value <= 0x7F)
                            {
                                result.push_back(static_cast<char>(value));
                            }
                            else if (value <= 0x7FF)
                            {
                                result.push_back(static_cast<char>(0xC0 | ((value >> 6) & 0x1F)));
                                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                            }
                            else
                            {
                                result.push_back(static_cast<char>(0xE0 | ((value >> 12) & 0x0F)));
                                result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                            }
                            break;
                        }
                        default:
                            throw std::runtime_error("Invalid escape sequence");
                        }
                    }
                    else
                    {
                        result.push_back(ch);
                    }
                }
                return result;
            }

            JsonValue parseLiteral(std::string_view literal, bool value)
            {
                for (char expected : literal)
                {
                    if (current_ == end_ || *current_++ != expected)
                    {
                        throw std::runtime_error("Invalid JSON literal");
                    }
                }
                return JsonValue{value};
            }

            void parseLiteral(std::string_view literal)
            {
                for (char expected : literal)
                {
                    if (current_ == end_ || *current_++ != expected)
                    {
                        throw std::runtime_error("Invalid JSON literal");
                    }
                }
            }

            void skipWhitespace()
            {
                while (current_ != end_ && std::isspace(static_cast<unsigned char>(*current_)))
                {
                    ++current_;
                }
            }

            bool consume(char ch)
            {
                if (current_ != end_ && *current_ == ch)
                {
                    ++current_;
                    return true;
                }
                return false;
            }

            void expect(char ch)
            {
                if (current_ == end_ || *current_ != ch)
                {
                    throw std::runtime_error("Unexpected character in JSON stream");
                }
                ++current_;
            }

            std::string_view text_;
            const char *current_;
            const char *end_;
        };

        JsonValue parseJson(std::string_view json)
        {
            JsonParser parser(json);
            return parser.parse();
        }

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

        AttributeKind parseAttributeKind(std::string_view text)
        {
            if (text == "bool")
            {
                return AttributeKind::Bool;
            }
            if (text == "int")
            {
                return AttributeKind::Int;
            }
            if (text == "double")
            {
                return AttributeKind::Double;
            }
            if (text == "string" || text == "str")
            {
                return AttributeKind::String;
            }
            if (text == "bool_array" || text == "bool[]")
            {
                return AttributeKind::BoolArray;
            }
            if (text == "int_array" || text == "int[]")
            {
                return AttributeKind::IntArray;
            }
            if (text == "double_array" || text == "double[]")
            {
                return AttributeKind::DoubleArray;
            }
            if (text == "string_array" || text == "string[]")
            {
                return AttributeKind::StringArray;
            }
            throw std::runtime_error("Unknown attribute kind: " + std::string(text));
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

    Value::Value(ir::ValueId id, ir::SymbolId symbol, std::string symbolText, int32_t width, bool isSigned,
                 bool isInput, bool isOutput, ir::OperationId definingOp,
                 std::vector<ir::ValueUser> users, std::optional<SrcLoc> srcLoc)
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

    Operation::Operation(ir::OperationId id, OperationKind kind, ir::SymbolId symbol, std::string symbolText,
                         std::vector<ir::ValueId> operands, std::vector<ir::ValueId> results,
                         std::vector<ir::AttrKV> attrs, std::optional<SrcLoc> srcLoc)
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

    std::optional<AttributeValue> Operation::attr(ir::SymbolId key) const
    {
        if (!key.valid())
        {
            return std::nullopt;
        }
        for (const auto &entry : attrs_)
        {
            if (entry.key == key)
            {
                return entry.value;
            }
        }
        return std::nullopt;
    }

    Graph::Graph(Netlist &owner, std::string symbol, ir::GraphId graphId)
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
        ir::GraphBuilder builder(symbols_, graphId_);
        view_ = builder.freeze();
    }

    ir::SymbolId Graph::internSymbol(std::string_view text)
    {
        if (symbols_.contains(text))
        {
            return symbols_.lookup(text);
        }
        return symbols_.intern(text);
    }

    ir::SymbolId Graph::lookupSymbol(std::string_view text) const
    {
        return symbols_.lookup(text);
    }

    std::string_view Graph::symbolText(ir::SymbolId id) const
    {
        if (!id.valid())
        {
            return std::string_view{};
        }
        return symbols_.text(id);
    }

    const ir::GraphView *Graph::viewIfFrozen() const noexcept
    {
        if (builder_)
        {
            return nullptr;
        }
        return view_ ? &*view_ : nullptr;
    }

    const ir::GraphView &Graph::freeze()
    {
        if (builder_)
        {
            view_ = builder_->freeze();
            builder_.reset();
            invalidateCaches();
        }
        if (!view_)
        {
            ir::GraphBuilder builder(symbols_, graphId_);
            view_ = builder.freeze();
        }
        return *view_;
    }

    std::span<const ir::OperationId> Graph::operations() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->operations();
            }
            return std::span<const ir::OperationId>();
        }
        ensureCaches();
        return std::span<const ir::OperationId>(operationsCache_.data(), operationsCache_.size());
    }

    std::span<const ir::ValueId> Graph::values() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->values();
            }
            return std::span<const ir::ValueId>();
        }
        ensureCaches();
        return std::span<const ir::ValueId>(valuesCache_.data(), valuesCache_.size());
    }

    std::span<const ir::Port> Graph::inputPorts() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->inputPorts();
            }
            return std::span<const ir::Port>();
        }
        ensureCaches();
        return std::span<const ir::Port>(inputPortsCache_.data(), inputPortsCache_.size());
    }

    std::span<const ir::Port> Graph::outputPorts() const
    {
        if (!builder_)
        {
            if (view_)
            {
                return view_->outputPorts();
            }
            return std::span<const ir::Port>();
        }
        ensureCaches();
        return std::span<const ir::Port>(outputPortsCache_.data(), outputPortsCache_.size());
    }

    ir::ValueId Graph::createValue(ir::SymbolId symbol, int32_t width, bool isSigned)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.addValue(symbol, width, isSigned);
    }

    ir::OperationId Graph::createOperation(OperationKind kind, ir::SymbolId symbol)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.addOp(kind, symbol);
    }

    ir::ValueId Graph::findValue(ir::SymbolId symbol) const noexcept
    {
        if (!symbol.valid())
        {
            return ir::ValueId::invalid();
        }
        if (builder_)
        {
            const auto &values = builder_->values_;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (!values[i].alive)
                {
                    continue;
                }
                if (values[i].symbol == symbol)
                {
                    ir::ValueId id;
                    id.index = static_cast<uint32_t>(i + 1);
                    id.generation = 0;
                    id.graph = graphId_;
                    return id;
                }
            }
            return ir::ValueId::invalid();
        }
        if (!view_)
        {
            return ir::ValueId::invalid();
        }
        for (const auto &id : view_->values())
        {
            if (view_->valueSymbol(id) == symbol)
            {
                return id;
            }
        }
        return ir::ValueId::invalid();
    }

    ir::OperationId Graph::findOperation(ir::SymbolId symbol) const noexcept
    {
        if (!symbol.valid())
        {
            return ir::OperationId::invalid();
        }
        if (builder_)
        {
            const auto &ops = builder_->operations_;
            for (std::size_t i = 0; i < ops.size(); ++i)
            {
                if (!ops[i].alive)
                {
                    continue;
                }
                if (ops[i].symbol == symbol)
                {
                    ir::OperationId id;
                    id.index = static_cast<uint32_t>(i + 1);
                    id.generation = 0;
                    id.graph = graphId_;
                    return id;
                }
            }
            return ir::OperationId::invalid();
        }
        if (!view_)
        {
            return ir::OperationId::invalid();
        }
        for (const auto &id : view_->operations())
        {
            if (view_->opSymbol(id) == symbol)
            {
                return id;
            }
        }
        return ir::OperationId::invalid();
    }

    ir::ValueId Graph::findValue(std::string_view symbol) const
    {
        return findValue(lookupSymbol(symbol));
    }

    ir::OperationId Graph::findOperation(std::string_view symbol) const
    {
        return findOperation(lookupSymbol(symbol));
    }

    Value Graph::getValue(ir::ValueId id) const
    {
        if (builder_)
        {
            return valueFromBuilder(id);
        }
        return valueFromView(id);
    }

    Operation Graph::getOperation(ir::OperationId id) const
    {
        if (builder_)
        {
            return operationFromBuilder(id);
        }
        return operationFromView(id);
    }

    void Graph::bindInputPort(ir::SymbolId name, ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.bindInputPort(name, value);
    }

    void Graph::bindOutputPort(ir::SymbolId name, ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.bindOutputPort(name, value);
    }

    ir::ValueId Graph::inputPortValue(ir::SymbolId name) const noexcept
    {
        if (!name.valid())
        {
            return ir::ValueId::invalid();
        }
        if (builder_)
        {
            for (const auto &port : builder_->inputPorts_)
            {
                if (port.name == name)
                {
                    return port.value;
                }
            }
            return ir::ValueId::invalid();
        }
        if (!view_)
        {
            return ir::ValueId::invalid();
        }
        for (const auto &port : view_->inputPorts())
        {
            if (port.name == name)
            {
                return port.value;
            }
        }
        return ir::ValueId::invalid();
    }

    ir::ValueId Graph::outputPortValue(ir::SymbolId name) const noexcept
    {
        if (!name.valid())
        {
            return ir::ValueId::invalid();
        }
        if (builder_)
        {
            for (const auto &port : builder_->outputPorts_)
            {
                if (port.name == name)
                {
                    return port.value;
                }
            }
            return ir::ValueId::invalid();
        }
        if (!view_)
        {
            return ir::ValueId::invalid();
        }
        for (const auto &port : view_->outputPorts())
        {
            if (port.name == name)
            {
                return port.value;
            }
        }
        return ir::ValueId::invalid();
    }

    void Graph::addOperand(ir::OperationId op, ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.addOperand(op, value);
    }

    void Graph::addResult(ir::OperationId op, ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.addResult(op, value);
    }

    void Graph::replaceOperand(ir::OperationId op, std::size_t index, ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.replaceOperand(op, index, value);
    }

    void Graph::replaceResult(ir::OperationId op, std::size_t index, ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.replaceResult(op, index, value);
    }

    void Graph::replaceAllUses(ir::ValueId from, ir::ValueId to)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.replaceAllUses(from, to);
    }

    bool Graph::eraseOperand(ir::OperationId op, std::size_t index)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseOperand(op, index);
    }

    bool Graph::eraseResult(ir::OperationId op, std::size_t index)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseResult(op, index);
    }

    bool Graph::eraseOp(ir::OperationId op)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseOp(op);
    }

    bool Graph::eraseOp(ir::OperationId op, std::span<const ir::ValueId> replacementResults)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseOp(op, replacementResults);
    }

    bool Graph::eraseValue(ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseValue(value);
    }

    void Graph::setAttr(ir::OperationId op, ir::SymbolId key, AttributeValue value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setAttr(op, key, std::move(value));
    }

    bool Graph::eraseAttr(ir::OperationId op, ir::SymbolId key)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        return builder.eraseAttr(op, key);
    }

    void Graph::setValueSrcLoc(ir::ValueId value, SrcLoc loc)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setValueSrcLoc(value, std::move(loc));
    }

    void Graph::setOpSrcLoc(ir::OperationId op, SrcLoc loc)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setOpSrcLoc(op, std::move(loc));
    }

    void Graph::setOpSymbol(ir::OperationId op, ir::SymbolId sym)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setOpSymbol(op, sym);
    }

    void Graph::setValueSymbol(ir::ValueId value, ir::SymbolId sym)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.setValueSymbol(value, sym);
    }

    void Graph::clearOpSymbol(ir::OperationId op)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.clearOpSymbol(op);
    }

    void Graph::clearValueSymbol(ir::ValueId value)
    {
        ir::GraphBuilder &builder = ensureBuilder();
        invalidateCaches();
        builder.clearValueSymbol(value);
    }

    void Graph::writeJson(slang::JsonWriter &writer) const
    {
        auto symbolTextOrEmpty = [&](ir::SymbolId sym) -> std::string_view
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
                    writer.writeProperty(symbolTextOrEmpty(attr.key));
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
                ir::ValueId id;
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
                ir::OperationId id;
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

    ir::GraphBuilder &Graph::ensureBuilder()
    {
        if (builder_)
        {
            return *builder_;
        }
        if (view_)
        {
            builder_ = ir::GraphBuilder::fromView(*view_, symbols_);
            view_.reset();
        }
        else
        {
            builder_.emplace(symbols_, graphId_);
        }
        invalidateCaches();
        return *builder_;
    }

    const ir::GraphView &Graph::view() const
    {
        if (!view_)
        {
            throw std::runtime_error("GraphView is not available; freeze the graph first");
        }
        return *view_;
    }

    Value Graph::valueFromView(ir::ValueId id) const
    {
        const ir::GraphView &graphView = view();
        id.assertGraph(graphId_);
        ir::SymbolId symbol = graphView.valueSymbol(id);
        std::string symbolText = symbol.valid() ? std::string(symbols_.text(symbol)) : std::string();
        return Value(id,
                     symbol,
                     std::move(symbolText),
                     graphView.valueWidth(id),
                     graphView.valueSigned(id),
                     graphView.valueIsInput(id),
                     graphView.valueIsOutput(id),
                     graphView.valueDef(id),
                     std::vector<ir::ValueUser>(graphView.valueUsers(id).begin(), graphView.valueUsers(id).end()),
                     graphView.valueSrcLoc(id));
    }

    Value Graph::valueFromBuilder(ir::ValueId id) const
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

        std::vector<ir::ValueUser> users;
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
                    ir::OperationId opId;
                    opId.index = static_cast<uint32_t>(opIdx + 1);
                    opId.generation = 0;
                    opId.graph = graphId_;
                    users.push_back(ir::ValueUser{opId, static_cast<uint32_t>(operandIdx)});
                }
            }
        }

        std::string symbolText = data.symbol.valid() ? std::string(symbols_.text(data.symbol)) : std::string();
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

    Operation Graph::operationFromView(ir::OperationId id) const
    {
        const ir::GraphView &graphView = view();
        id.assertGraph(graphId_);
        ir::SymbolId symbol = graphView.opSymbol(id);
        std::string symbolText = symbol.valid() ? std::string(symbols_.text(symbol)) : std::string();
        return Operation(id,
                         graphView.opKind(id),
                         symbol,
                         std::move(symbolText),
                         std::vector<ir::ValueId>(graphView.opOperands(id).begin(), graphView.opOperands(id).end()),
                         std::vector<ir::ValueId>(graphView.opResults(id).begin(), graphView.opResults(id).end()),
                         std::vector<ir::AttrKV>(graphView.opAttrs(id).begin(), graphView.opAttrs(id).end()),
                         graphView.opSrcLoc(id));
    }

    Operation Graph::operationFromBuilder(ir::OperationId id) const
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

        std::string symbolText = data.symbol.valid() ? std::string(symbols_.text(data.symbol)) : std::string();
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
        ir::SymbolId graphSymbol = netlistSymbols_.contains(symbol) ? netlistSymbols_.lookup(symbol) : netlistSymbols_.intern(symbol);
        ir::GraphId graphId = netlistSymbols_.allocateGraphId(graphSymbol);
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

    AttributeValue parseAttributeValue(const JsonValue &jsonAttr)
    {
        const auto &object = jsonAttr.asObject("attribute");
        auto kindIt = object.find("t");
        if (kindIt == object.end())
        {
            kindIt = object.find("k");
        }
        if (kindIt == object.end())
        {
            kindIt = object.find("kind");
        }
        if (kindIt == object.end())
        {
            throw std::runtime_error("Attribute object missing kind");
        }
        const char *kindContext = kindIt->first == "t"   ? "attribute.t"
                                 : kindIt->first == "k" ? "attribute.k"
                                                        : "attribute.kind";
        AttributeKind kind = parseAttributeKind(kindIt->second.asString(kindContext));

        auto valueIt = object.find("v");
        if (valueIt == object.end())
        {
            valueIt = object.find("value");
        }

        auto valuesIt = object.find("vs");
        if (valuesIt == object.end())
        {
            valuesIt = object.find("values");
        }

        auto validateAndReturn = [](AttributeValue attr) -> AttributeValue
        {
            if (!attributeValueIsJsonSerializable(attr))
            {
                throw std::runtime_error("Attribute value is not JSON serializable");
            }
            return attr;
        };

        switch (kind)
        {
        case AttributeKind::Bool:
            if (valueIt == object.end())
            {
                throw std::runtime_error("Bool attribute missing value");
            }
            return validateAndReturn(AttributeValue{valueIt->second.asBool("attribute.v")});
        case AttributeKind::Int:
            if (valueIt == object.end())
            {
                throw std::runtime_error("Int attribute missing value");
            }
            return validateAndReturn(AttributeValue{valueIt->second.asInt("attribute.v")});
        case AttributeKind::Double:
            if (valueIt == object.end())
            {
                throw std::runtime_error("Double attribute missing value");
            }
            return validateAndReturn(AttributeValue{valueIt->second.asDouble("attribute.v")});
        case AttributeKind::String:
            if (valueIt == object.end())
            {
                throw std::runtime_error("String attribute missing value");
            }
            return validateAndReturn(AttributeValue{valueIt->second.asString("attribute.v")});
        case AttributeKind::BoolArray:
        {
            if (valuesIt == object.end())
            {
                throw std::runtime_error("Bool array attribute missing values");
            }
            std::vector<bool> arr;
            for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
            {
                arr.push_back(entry.asBool("attribute.vs[]"));
            }
            return validateAndReturn(AttributeValue{arr});
        }
        case AttributeKind::IntArray:
        {
            if (valuesIt == object.end())
            {
                throw std::runtime_error("Int array attribute missing values");
            }
            std::vector<int64_t> arr;
            for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
            {
                arr.push_back(entry.asInt("attribute.vs[]"));
            }
            return validateAndReturn(AttributeValue{arr});
        }
        case AttributeKind::DoubleArray:
        {
            if (valuesIt == object.end())
            {
                throw std::runtime_error("Double array attribute missing values");
            }
            std::vector<double> arr;
            for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
            {
                arr.push_back(entry.asDouble("attribute.vs[]"));
            }
            return validateAndReturn(AttributeValue{arr});
        }
        case AttributeKind::StringArray:
        {
            if (valuesIt == object.end())
            {
                throw std::runtime_error("String array attribute missing values");
            }
            std::vector<std::string> arr;
            for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
            {
                arr.push_back(entry.asString("attribute.vs[]"));
            }
            return validateAndReturn(AttributeValue{arr});
        }
        }
        throw std::runtime_error("Unhandled attribute kind");
    }

    std::optional<SrcLoc> parseSrcLoc(const JsonValue &json, std::string_view context)
    {
        const auto &object = json.asObject(context);
        SrcLoc info{};

        if (auto fileIt = object.find("file"); fileIt != object.end())
        {
            info.file = fileIt->second.asString(std::string(context) + ".file");
        }

        if (auto lineIt = object.find("line"); lineIt != object.end())
        {
            info.line = static_cast<uint32_t>(lineIt->second.asInt(std::string(context) + ".line"));
        }

        if (auto colIt = object.find("col"); colIt != object.end())
        {
            info.column = static_cast<uint32_t>(colIt->second.asInt(std::string(context) + ".col"));
        }

        if (auto endLineIt = object.find("endLine"); endLineIt != object.end())
        {
            info.endLine = static_cast<uint32_t>(endLineIt->second.asInt(std::string(context) + ".endLine"));
        }

        if (auto endColIt = object.find("endCol"); endColIt != object.end())
        {
            info.endColumn = static_cast<uint32_t>(endColIt->second.asInt(std::string(context) + ".endCol"));
        }

        if (info.file.empty() && info.line == 0)
        {
            return std::nullopt;
        }
        return info;
    }

    Netlist Netlist::fromJsonString(std::string_view json)
    {
        JsonValue root = parseJson(json);
        const auto &rootObj = root.asObject("netlist");

        Netlist netlist;
        auto graphsIt = rootObj.find("graphs");
        if (graphsIt == rootObj.end())
        {
            throw std::runtime_error("Netlist JSON missing graphs field");
        }
        const auto &graphsArray = graphsIt->second.asArray("graphs");
        for (const auto &graphValue : graphsArray)
        {
            const auto &graphObj = graphValue.asObject("graph");
            auto symbolIt = graphObj.find("symbol");
            if (symbolIt == graphObj.end())
            {
                throw std::runtime_error("Graph JSON missing symbol");
            }
            Graph &graph = netlist.createGraph(symbolIt->second.asString("graph.symbol"));

            const auto valuesIt = graphObj.find("vals");
            if (valuesIt == graphObj.end())
            {
                throw std::runtime_error("Graph JSON missing vals array");
            }

            std::unordered_map<std::string, ir::ValueId> valueBySymbol;
            std::unordered_set<uint32_t> declaredInputs;
            std::unordered_set<uint32_t> declaredOutputs;

            const auto &valuesArray = valuesIt->second.asArray("graph.vals");
            for (const auto &valueEntry : valuesArray)
            {
                const auto &valueObj = valueEntry.asObject("value");
                const auto &symbol = valueObj.at("sym").asString("value.sym");
                int64_t width = valueObj.at("w").asInt("value.w");
                bool isSigned = valueObj.at("sgn").asBool("value.sgn");
                ir::SymbolId valueSym = graph.internSymbol(symbol);
                ir::ValueId valueId = graph.createValue(valueSym, static_cast<int32_t>(width), isSigned);
                valueBySymbol.emplace(symbol, valueId);

                bool isInput = valueObj.at("in").asBool("value.in");
                bool isOutput = valueObj.at("out").asBool("value.out");
                if (isInput && isOutput)
                {
                    throw std::runtime_error("Value cannot be both input and output in JSON");
                }

                if (isInput)
                {
                    declaredInputs.insert(valueSym.value);
                }
                if (isOutput)
                {
                    declaredOutputs.insert(valueSym.value);
                }

                if (auto dbgIt = valueObj.find("loc"); dbgIt != valueObj.end())
                {
                    if (auto loc = parseSrcLoc(dbgIt->second, "value.loc"))
                    {
                        graph.setValueSrcLoc(valueId, std::move(*loc));
                    }
                }
            }

            if (auto portsIt = graphObj.find("ports"); portsIt != graphObj.end())
            {
                const auto &portsObj = portsIt->second.asObject("graph.ports");
                auto parsePortArray = [&](std::string_view key, bool isInput)
                {
                    auto arrayIt = portsObj.find(std::string(key));
                    if (arrayIt == portsObj.end())
                    {
                        return;
                    }
                    const auto &portArray = arrayIt->second.asArray(std::string("graph.ports.") + std::string(key));
                    for (const auto &entry : portArray)
                    {
                        const auto &portObj = entry.asObject("graph.port");
                        auto nameField = portObj.find("name");
                        auto valField = portObj.find("val");
                        if (nameField == portObj.end() || valField == portObj.end())
                        {
                            throw std::runtime_error("Port entry missing name or val");
                        }
                        const std::string valueName = valField->second.asString("graph.port.val");
                        auto valueIt = valueBySymbol.find(valueName);
                        if (valueIt == valueBySymbol.end())
                        {
                            throw std::runtime_error("Port references unknown value: " + valueName);
                        }
                        ir::SymbolId portName = graph.internSymbol(nameField->second.asString("graph.port.name"));
                        if (isInput)
                        {
                            graph.bindInputPort(portName, valueIt->second);
                        }
                        else
                        {
                            graph.bindOutputPort(portName, valueIt->second);
                        }
                    }
                };
                parsePortArray("in", true);
                parsePortArray("out", false);
            }
            else
            {
                throw std::runtime_error("Graph JSON missing ports object");
            }

            for (const auto &port : graph.inputPorts())
            {
                Value value = graph.getValue(port.value);
                if (!declaredInputs.contains(value.symbol().value))
                {
                    throw std::runtime_error("Input port missing isInput=true flag for value: " + std::string(graph.symbolText(value.symbol())));
                }
            }
            for (const auto &symbolValue : declaredInputs)
            {
                ir::SymbolId sym{symbolValue};
                ir::ValueId valueId = graph.findValue(sym);
                if (!valueId.valid())
                {
                    throw std::runtime_error("Value marked in=true but not bound to input port");
                }
                Value value = graph.getValue(valueId);
                if (!value.isInput())
                {
                    throw std::runtime_error("Value marked in=true but not bound to input port: " + std::string(graph.symbolText(sym)));
                }
            }

            for (const auto &port : graph.outputPorts())
            {
                Value value = graph.getValue(port.value);
                if (!declaredOutputs.contains(value.symbol().value))
                {
                    throw std::runtime_error("Output port missing isOutput=true flag for value: " + std::string(graph.symbolText(value.symbol())));
                }
            }
            for (const auto &symbolValue : declaredOutputs)
            {
                ir::SymbolId sym{symbolValue};
                ir::ValueId valueId = graph.findValue(sym);
                if (!valueId.valid())
                {
                    throw std::runtime_error("Value marked out=true but not bound to output port");
                }
                Value value = graph.getValue(valueId);
                if (!value.isOutput())
                {
                    throw std::runtime_error("Value marked out=true but not bound to output port: " + std::string(graph.symbolText(sym)));
                }
            }

            if (auto opsIt = graphObj.find("ops"); opsIt != graphObj.end())
            {
                for (const auto &opEntry : opsIt->second.asArray("graph.ops"))
                {
                    const auto &opObj = opEntry.asObject("operation");
                    auto kindIt = opObj.find("kind");
                    if (kindIt == opObj.end())
                    {
                        throw std::runtime_error("Operation missing kind");
                    }
                    auto kind = parseOperationKind(kindIt->second.asString("operation.kind"));
                    if (!kind)
                    {
                        throw std::runtime_error("Unknown operation kind: " + kindIt->second.asString("operation.kind"));
                    }

                    auto symIt = opObj.find("sym");
                    if (symIt == opObj.end())
                    {
                        throw std::runtime_error("Operation missing symbol");
                    }

                    const std::string opSymbol = symIt->second.asString("operation.sym");
                    ir::SymbolId opSym = graph.internSymbol(opSymbol);
                    ir::OperationId opId = graph.createOperation(*kind, opSym);

                    auto parseSymbolList = [&](std::string_view key, std::string_view context) -> std::vector<std::string>
                    {
                        std::vector<std::string> entries;
                        if (auto it = opObj.find(std::string(key)); it != opObj.end())
                        {
                            for (const auto &entry : it->second.asArray(std::string(context)))
                            {
                                entries.push_back(entry.asString(std::string(context) + "[]"));
                            }
                        }
                        return entries;
                    };

                    for (const auto &symbol : parseSymbolList("in", "operation.in"))
                    {
                        auto valueIt = valueBySymbol.find(symbol);
                        if (valueIt == valueBySymbol.end())
                        {
                            throw std::runtime_error("Operand references unknown value: " + symbol);
                        }
                        graph.addOperand(opId, valueIt->second);
                    }

                    for (const auto &symbol : parseSymbolList("out", "operation.out"))
                    {
                        auto valueIt = valueBySymbol.find(symbol);
                        if (valueIt == valueBySymbol.end())
                        {
                            throw std::runtime_error("Result references unknown value: " + symbol);
                        }
                        graph.addResult(opId, valueIt->second);
                    }

                    if (auto attrsIt = opObj.find("attrs"); attrsIt != opObj.end())
                    {
                        for (const auto &[attrName, attrValue] : attrsIt->second.asObject("operation.attrs"))
                        {
                            ir::SymbolId attrKey = graph.internSymbol(attrName);
                            graph.setAttr(opId, attrKey, parseAttributeValue(attrValue));
                        }
                    }

                    if (auto dbgIt = opObj.find("loc"); dbgIt != opObj.end())
                    {
                        if (auto loc = parseSrcLoc(dbgIt->second, "operation.loc"))
                        {
                            graph.setOpSrcLoc(opId, std::move(*loc));
                        }
                    }
                }
            }
        }

        if (auto topIt = rootObj.find("tops"); topIt != rootObj.end())
        {
            for (const auto &entry : topIt->second.asArray("netlist.tops"))
            {
                netlist.markAsTop(entry.asString("netlist.tops[]"));
            }
        }

        return netlist;
    }

} // namespace wolf_sv::grh
