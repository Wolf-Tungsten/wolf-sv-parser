#include "transform/redundant_elim.hpp"

#include "grh.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wolvrix::lib::transform
{

    namespace
    {
        std::size_t hashCombine(std::size_t seed, std::size_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
        }

        std::size_t hashAttributeValue(const wolvrix::lib::grh::AttributeValue &value)
        {
            const std::size_t typeTag = value.index();
            const std::size_t payload = std::visit(
                [](const auto &val) -> std::size_t
                {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, bool>)
                    {
                        return std::hash<bool>{}(val);
                    }
                    else if constexpr (std::is_same_v<T, int64_t>)
                    {
                        return std::hash<int64_t>{}(val);
                    }
                    else if constexpr (std::is_same_v<T, double>)
                    {
                        return std::hash<double>{}(val);
                    }
                    else if constexpr (std::is_same_v<T, std::string>)
                    {
                        return std::hash<std::string>{}(val);
                    }
                    else if constexpr (std::is_same_v<T, std::vector<bool>>)
                    {
                        std::size_t seed = 0;
                        for (const bool bit : val)
                        {
                            seed = hashCombine(seed, std::hash<bool>{}(bit));
                        }
                        return seed;
                    }
                    else if constexpr (std::is_same_v<T, std::vector<int64_t>>)
                    {
                        std::size_t seed = 0;
                        for (const int64_t item : val)
                        {
                            seed = hashCombine(seed, std::hash<int64_t>{}(item));
                        }
                        return seed;
                    }
                    else if constexpr (std::is_same_v<T, std::vector<double>>)
                    {
                        std::size_t seed = 0;
                        for (const double item : val)
                        {
                            seed = hashCombine(seed, std::hash<double>{}(item));
                        }
                        return seed;
                    }
                    else if constexpr (std::is_same_v<T, std::vector<std::string>>)
                    {
                        std::size_t seed = 0;
                        for (const auto &item : val)
                        {
                            seed = hashCombine(seed, std::hash<std::string>{}(item));
                        }
                        return seed;
                    }
                    else
                    {
                        return 0U;
                    }
                },
                value);
            return hashCombine(typeTag, payload);
        }

        struct OpSignature
        {
            wolvrix::lib::grh::OperationKind kind = wolvrix::lib::grh::OperationKind::kConstant;
            std::vector<wolvrix::lib::grh::ValueId> operands;
            std::vector<wolvrix::lib::grh::AttrKV> attrs;
            int32_t width = 0;
            bool isSigned = false;

            bool operator==(const OpSignature &other) const
            {
                if (kind != other.kind || width != other.width || isSigned != other.isSigned)
                {
                    return false;
                }
                if (operands != other.operands)
                {
                    return false;
                }
                if (attrs.size() != other.attrs.size())
                {
                    return false;
                }
                for (std::size_t i = 0; i < attrs.size(); ++i)
                {
                    if (attrs[i].key != other.attrs[i].key || attrs[i].value != other.attrs[i].value)
                    {
                        return false;
                    }
                }
                return true;
            }
        };

        struct OpSignatureHash
        {
            std::size_t operator()(const OpSignature &sig) const
            {
                std::size_t seed = static_cast<std::size_t>(sig.kind);
                seed = hashCombine(seed, std::hash<int32_t>{}(sig.width));
                seed = hashCombine(seed, std::hash<bool>{}(sig.isSigned));
                seed = hashCombine(seed, std::hash<std::size_t>{}(sig.operands.size()));
                for (const auto &operand : sig.operands)
                {
                    seed = hashCombine(seed, wolvrix::lib::grh::ValueIdHash{}(operand));
                }
                seed = hashCombine(seed, std::hash<std::size_t>{}(sig.attrs.size()));
                for (const auto &attr : sig.attrs)
                {
                    seed = hashCombine(seed, std::hash<std::string>{}(attr.key));
                    seed = hashCombine(seed, hashAttributeValue(attr.value));
                }
                return seed;
            }
        };

        bool isOutputPortValue(const wolvrix::lib::grh::Value &value)
        {
            return value.isOutput() && !value.isInput() && !value.isInout();
        }

        bool hasOtherUsers(const wolvrix::lib::grh::Value &value)
        {
            return !value.users().empty();
        }

        bool isTemporarySymbol(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Value &value)
        {
            if (value.isInput() || value.isOutput() || value.isInout())
            {
                return false;
            }
            if (graph.isDeclaredSymbol(value.symbol()))
            {
                return false;
            }
            const std::string_view name = value.symbolText();
            if (name.rfind("_val_", 0) == 0)
            {
                return true;
            }
            return false;
        }

        bool isSideEffectFreeOp(wolvrix::lib::grh::OperationKind kind)
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kMemory:
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
            case wolvrix::lib::grh::OperationKind::kRegister:
            case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kLatch:
            case wolvrix::lib::grh::OperationKind::kLatchReadPort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kInstance:
            case wolvrix::lib::grh::OperationKind::kBlackbox:
            case wolvrix::lib::grh::OperationKind::kSystemFunction:
            case wolvrix::lib::grh::OperationKind::kSystemTask:
            case wolvrix::lib::grh::OperationKind::kDpicImport:
            case wolvrix::lib::grh::OperationKind::kDpicCall:
                return false;
            default:
                return true;
            }
        }

        bool isCseCandidate(const wolvrix::lib::grh::Graph &graph,
                            const wolvrix::lib::grh::Operation &op)
        {
            if (!isSideEffectFreeOp(op.kind()))
            {
                return false;
            }
            if (op.results().size() != 1)
            {
                return false;
            }
            const wolvrix::lib::grh::ValueId resultId = op.results()[0];
            if (!resultId.valid())
            {
                return false;
            }
            const wolvrix::lib::grh::Value resultValue = graph.getValue(resultId);
            if (resultValue.isInput() || resultValue.isOutput() || resultValue.isInout())
            {
                return false;
            }
            if (!isTemporarySymbol(graph, resultValue))
            {
                return false;
            }
            for (const auto operandId : op.operands())
            {
                if (!operandId.valid())
                {
                    return false;
                }
            }
            return true;
        }

        OpSignature makeSignature(const wolvrix::lib::grh::Graph &graph,
                                  const wolvrix::lib::grh::Operation &op)
        {
            OpSignature sig;
            sig.kind = op.kind();
            sig.operands.assign(op.operands().begin(), op.operands().end());
            sig.attrs.assign(op.attrs().begin(), op.attrs().end());
            std::stable_sort(sig.attrs.begin(), sig.attrs.end(),
                             [](const wolvrix::lib::grh::AttrKV &lhs, const wolvrix::lib::grh::AttrKV &rhs)
                             { return lhs.key < rhs.key; });
            const wolvrix::lib::grh::ValueId resultId = op.results()[0];
            const wolvrix::lib::grh::Value resultValue = graph.getValue(resultId);
            sig.width = resultValue.width();
            sig.isSigned = resultValue.isSigned();
            return sig;
        }

        bool isSingleUser(const wolvrix::lib::grh::Value &value, wolvrix::lib::grh::OperationId user)
        {
            std::size_t count = 0;
            for (const auto &use : value.users())
            {
                if (use.operation == user)
                {
                    ++count;
                }
                else
                {
                    return false;
                }
            }
            return count > 0;
        }

        bool isLogicNotOf(const wolvrix::lib::grh::Graph &graph,
                          wolvrix::lib::grh::ValueId maybeNot,
                          wolvrix::lib::grh::ValueId operand)
        {
            if (!maybeNot.valid() || !operand.valid())
            {
                return false;
            }
            const wolvrix::lib::grh::OperationId defOpId = graph.getValue(maybeNot).definingOp();
            if (!defOpId.valid())
            {
                return false;
            }
            const wolvrix::lib::grh::Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != wolvrix::lib::grh::OperationKind::kLogicNot &&
                defOp.kind() != wolvrix::lib::grh::OperationKind::kNot)
            {
                return false;
            }
            const auto &ops = defOp.operands();
            return !ops.empty() && ops.front() == operand;
        }

        std::string makeInlineConstName(const wolvrix::lib::grh::Graph &graph,
                                        std::string_view kind,
                                        std::string_view baseName,
                                        uint32_t &counter)
        {
            (void)baseName;
            std::string base = wolvrix::lib::grh::symbol_utils::makeInternalBase(kind);
            for (;;)
            {
                std::string candidate = base + "_" + std::to_string(counter++);
                if (!graph.findOperation(candidate).valid() &&
                    !graph.findValue(candidate).valid())
                {
                    return candidate;
                }
            }
        }

        wolvrix::lib::grh::ValueId createInlineConst(wolvrix::lib::grh::Graph &graph,
                                           std::string_view baseName,
                                           int32_t width,
                                           bool isSigned,
                                           std::string_view literal,
                                           uint32_t &counter)
        {
            std::string base;
            if (!baseName.empty())
            {
                base = std::string(baseName);
            }
            const std::string valueName = makeInlineConstName(graph, "val", base, counter);
            const std::string opName = makeInlineConstName(graph, "op", base, counter);
            const wolvrix::lib::grh::SymbolId valueSym = graph.internSymbol(valueName);
            const wolvrix::lib::grh::SymbolId opSym = graph.internSymbol(opName);
            const wolvrix::lib::grh::ValueId val =
                graph.createValue(valueSym, width, isSigned, wolvrix::lib::grh::ValueType::Logic);
            const wolvrix::lib::grh::OperationId op =
                graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant, opSym);
            graph.addResult(op, val);
            graph.setAttr(op, "constValue", std::string(literal));
            const wolvrix::lib::grh::SrcLoc genLoc = makeTransformSrcLoc("redundant-elim", "inline_const");
            graph.setValueSrcLoc(val, genLoc);
            graph.setOpSrcLoc(op, genLoc);
            return val;
        }

        void replaceUsers(wolvrix::lib::grh::Graph &graph,
                          wolvrix::lib::grh::ValueId from,
                          wolvrix::lib::grh::ValueId to,
                          const std::function<void(std::string)> &onError)
        {
            try
            {
                graph.replaceAllUses(from, to);
            }
            catch (const std::exception &ex)
            {
                onError(std::string("Failed to replace operands: ") + ex.what());
                return;
            }

            std::vector<wolvrix::lib::grh::SymbolId> outputPortsToUpdate;
            for (const auto &port : graph.outputPorts())
            {
                if (port.value == from)
                {
                    outputPortsToUpdate.push_back(port.name);
                }
            }
            for (const auto portName : outputPortsToUpdate)
            {
                try
                {
                    graph.bindOutputPort(portName, to);
                }
                catch (const std::exception &ex)
                {
                    onError(std::string("Failed to rebind output port: ") + ex.what());
                }
            }
        }

    } // namespace

    RedundantElimPass::RedundantElimPass()
        : Pass("redundant-elim", "redundant-elim",
               "Inline trivial assigns and eliminate redundant temps")
    {
    }

    PassResult RedundantElimPass::run()
    {
        PassResult result;
        bool anyChanged = false;
        const std::size_t graphCount = netlist().graphs().size();
        logDebug("begin graphs=" + std::to_string(graphCount));
        std::size_t changedGraphs = 0;
        std::size_t opsRemoved = 0;
        std::size_t valuesRemoved = 0;

        for (const auto &entry : netlist().graphs())
        {
            wolvrix::lib::grh::Graph &graph = *entry.second;
            bool graphChanged = false;
            bool progress = true;
            uint32_t inlineConstCounter = 0;
            auto eraseOp = [&](auto opId, auto... args) -> bool {
                if (graph.eraseOp(opId, args...))
                {
                    ++opsRemoved;
                    return true;
                }
                return false;
            };

            while (progress)
            {
                progress = false;
                std::vector<wolvrix::lib::grh::OperationId> ops(graph.operations().begin(),
                                                      graph.operations().end());
                for (const auto opId : ops)
                {
                    const wolvrix::lib::grh::Operation op = graph.getOperation(opId);
                    const auto &operands = op.operands();
                    const auto &results = op.results();

                    if (op.kind() == wolvrix::lib::grh::OperationKind::kAssign &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const wolvrix::lib::grh::ValueId srcId = operands[0];
                        const wolvrix::lib::grh::ValueId dstId = results[0];
                        if (!srcId.valid() || !dstId.valid())
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::Value srcValue = graph.getValue(srcId);
                        const wolvrix::lib::grh::Value dstValue = graph.getValue(dstId);

                        if (isOutputPortValue(dstValue) && !hasOtherUsers(dstValue))
                        {
                            const wolvrix::lib::grh::OperationId constOpId = srcValue.definingOp();
                            if (constOpId.valid())
                            {
                                const wolvrix::lib::grh::Operation constOp = graph.getOperation(constOpId);
                                if (constOp.kind() == wolvrix::lib::grh::OperationKind::kConstant &&
                                    constOp.results().size() == 1)
                                {
                                    auto constLiteral = constOp.attr("constValue");
                                    if (!constLiteral)
                                    {
                                        continue;
                                    }
                                    const auto *literalText =
                                        std::get_if<std::string>(&*constLiteral);
                                    if (!literalText)
                                    {
                                        continue;
                                    }
                                    if (!eraseOp(opId))
                                    {
                                        continue;
                                    }
                                    if (isSingleUser(srcValue, opId) &&
                                        !srcValue.isOutput())
                                    {
                                        try
                                        {
                                            graph.replaceResult(constOpId, 0, dstId);
                                            graphChanged = true;
                                            progress = true;
                                            continue;
                                        }
                                        catch (const std::exception &)
                                        {
                                            // Fall through to clone path.
                                        }
                                    }

                                    const std::string opName =
                                        makeInlineConstName(graph, "op", dstValue.symbolText(),
                                                            inlineConstCounter);
                                    wolvrix::lib::grh::SymbolId opSym = graph.internSymbol(opName);
                                    wolvrix::lib::grh::OperationId newConst =
                                        graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant,
                                                              opSym);
                                    graph.addResult(newConst, dstId);
                                    graph.setAttr(newConst, "constValue", *literalText);
                                    graph.setOpSrcLoc(newConst,
                                                      makeTransformSrcLoc("redundant-elim", "clone_const"));
                                    graphChanged = true;
                                    progress = true;
                                    continue;
                                }
                            }
                        }
                    }

                    if (op.kind() == wolvrix::lib::grh::OperationKind::kConcat &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const wolvrix::lib::grh::ValueId operandId = operands[0];
                        const wolvrix::lib::grh::ValueId resultId = results[0];
                        if (!operandId.valid() || !resultId.valid())
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::Value operandValue = graph.getValue(operandId);
                        const wolvrix::lib::grh::Value resultValue = graph.getValue(resultId);
                        if (!isTemporarySymbol(graph, resultValue))
                        {
                            continue;
                        }
                        if (operandValue.width() != resultValue.width() ||
                            operandValue.isSigned() != resultValue.isSigned())
                        {
                            continue;
                        }
                        auto onError = [&](const std::string &msg)
                        { this->error(graph, op, msg); };
                        replaceUsers(graph, resultId, operandId, onError);
                        if (eraseOp(opId))
                        {
                            graphChanged = true;
                            progress = true;
                        }
                        continue;
                    }

                    if (op.kind() == wolvrix::lib::grh::OperationKind::kLogicOr &&
                        operands.size() >= 2 && results.size() == 1)
                    {
                        const wolvrix::lib::grh::ValueId resultId = results[0];
                        if (!resultId.valid())
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::Value resultValue = graph.getValue(resultId);
                        if (resultValue.width() != 1)
                        {
                            continue;
                        }
                        bool alwaysTrue = false;
                        for (std::size_t i = 0; i < operands.size() && !alwaysTrue; ++i)
                        {
                            const wolvrix::lib::grh::ValueId lhs = operands[i];
                            if (!lhs.valid())
                            {
                                continue;
                            }
                            for (std::size_t j = i + 1; j < operands.size(); ++j)
                            {
                                const wolvrix::lib::grh::ValueId rhs = operands[j];
                                if (!rhs.valid())
                                {
                                    continue;
                                }
                                if (isLogicNotOf(graph, lhs, rhs) ||
                                    isLogicNotOf(graph, rhs, lhs))
                                {
                                    alwaysTrue = true;
                                    break;
                                }
                            }
                        }
                        if (alwaysTrue)
                        {
                            auto onError = [&](const std::string &msg)
                            { this->error(graph, op, msg); };
                            wolvrix::lib::grh::ValueId constOne =
                                createInlineConst(graph, resultValue.symbolText(),
                                                  1, resultValue.isSigned(), "1'b1",
                                                  inlineConstCounter);
                            replaceUsers(graph, resultId, constOne, onError);
                            eraseOp(opId);
                            graphChanged = true;
                            progress = true;
                            continue;
                        }
                    }

                    if (op.kind() == wolvrix::lib::grh::OperationKind::kAssign &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const wolvrix::lib::grh::ValueId srcId = operands[0];
                        const wolvrix::lib::grh::ValueId dstId = results[0];
                        if (!srcId.valid() || !dstId.valid())
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::Value srcValue = graph.getValue(srcId);
                        const wolvrix::lib::grh::Value dstValue = graph.getValue(dstId);
                        if (isOutputPortValue(dstValue) && !hasOtherUsers(dstValue))
                        {
                            if (srcValue.isInput() || srcValue.isOutput() || srcValue.isInout())
                            {
                                continue;
                            }
                            if (!dstValue.definingOp().valid() ||
                                dstValue.definingOp() != opId)
                            {
                                continue;
                            }
                            if (!isSingleUser(srcValue, opId))
                            {
                                continue;
                            }
                            if (srcValue.width() != dstValue.width() ||
                                srcValue.isSigned() != dstValue.isSigned())
                            {
                                continue;
                            }
                            const wolvrix::lib::grh::OperationId defOpId =
                                srcValue.definingOp();
                            if (!defOpId.valid() || defOpId == opId)
                            {
                                continue;
                            }
                            const wolvrix::lib::grh::Operation defOp = graph.getOperation(defOpId);
                            if (defOp.results().empty())
                            {
                                continue;
                            }
                            std::size_t defIndex = defOp.results().size();
                            for (std::size_t i = 0; i < defOp.results().size(); ++i)
                            {
                                if (defOp.results()[i] == srcId)
                                {
                                    defIndex = i;
                                    break;
                                }
                            }
                            if (defIndex >= defOp.results().size())
                            {
                                continue;
                            }
                            if (!eraseOp(opId))
                            {
                                continue;
                            }
                            try
                            {
                                graph.replaceResult(defOpId, defIndex, dstId);
                                graphChanged = true;
                                progress = true;
                            }
                            catch (const std::exception &ex)
                            {
                                this->error(graph, defOp,
                                            std::string("Failed to inline output assign: ") +
                                                ex.what());
                            }
                            continue;
                        }
                        if (dstValue.isInput() || dstValue.isOutput() || dstValue.isInout())
                        {
                            continue;
                        }
                        if (!isTemporarySymbol(graph, srcValue))
                        {
                            continue;
                        }
                        if (!isSingleUser(srcValue, opId))
                        {
                            continue;
                        }
                        if (srcValue.width() != dstValue.width() ||
                            srcValue.isSigned() != dstValue.isSigned())
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::OperationId defOpId = srcValue.definingOp();
                        if (!defOpId.valid() || defOpId == opId)
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::Operation defOp = graph.getOperation(defOpId);
                        if (defOp.results().empty())
                        {
                            continue;
                        }
                        std::size_t defIndex = defOp.results().size();
                        for (std::size_t i = 0; i < defOp.results().size(); ++i)
                        {
                            if (defOp.results()[i] == srcId)
                            {
                                defIndex = i;
                                break;
                            }
                        }
                        if (defIndex >= defOp.results().size())
                        {
                            continue;
                        }
                        if (!eraseOp(opId, std::array<wolvrix::lib::grh::ValueId, 1>{dstId}))
                        {
                            continue;
                        }
                        try
                        {
                            graph.replaceResult(defOpId, defIndex, dstId);
                            graphChanged = true;
                            progress = true;
                        }
                        catch (const std::exception &ex)
                        {
                            this->error(graph, defOp,
                                        std::string("Failed to inline assign: ") + ex.what());
                        }
                        continue;
                    }

                    if (op.kind() == wolvrix::lib::grh::OperationKind::kNot &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const wolvrix::lib::grh::ValueId operandId = operands[0];
                        const wolvrix::lib::grh::ValueId resultId = results[0];
                        if (!operandId.valid() || !resultId.valid())
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::Value operandValue = graph.getValue(operandId);
                        if (!isTemporarySymbol(graph, operandValue))
                        {
                            continue;
                        }
                        if (!isSingleUser(operandValue, opId))
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::OperationId defOpId = operandValue.definingOp();
                        if (!defOpId.valid())
                        {
                            continue;
                        }
                        const wolvrix::lib::grh::Operation defOp = graph.getOperation(defOpId);
                        if (defOp.kind() != wolvrix::lib::grh::OperationKind::kXor)
                        {
                            continue;
                        }
                        if (defOp.results().empty())
                        {
                            continue;
                        }
                        std::size_t defIndex = defOp.results().size();
                        for (std::size_t i = 0; i < defOp.results().size(); ++i)
                        {
                            if (defOp.results()[i] == operandId)
                            {
                                defIndex = i;
                                break;
                            }
                        }
                        if (defIndex >= defOp.results().size())
                        {
                            continue;
                        }
                        if (!eraseOp(opId, std::array<wolvrix::lib::grh::ValueId, 1>{resultId}))
                        {
                            continue;
                        }
                        graph.setOpKind(defOpId, wolvrix::lib::grh::OperationKind::kXnor);
                        try
                        {
                            graph.replaceResult(defOpId, defIndex, resultId);
                            graphChanged = true;
                            progress = true;
                        }
                        catch (const std::exception &ex)
                        {
                            this->error(graph, defOp,
                                        std::string("Failed to fold NOT/XOR: ") + ex.what());
                        }
                        continue;
                    }
                }

                struct CseEntry
                {
                    wolvrix::lib::grh::OperationId op;
                    wolvrix::lib::grh::ValueId value;
                };

                std::unordered_map<OpSignature, CseEntry, OpSignatureHash> seen;
                std::vector<wolvrix::lib::grh::OperationId> cseOps(graph.operations().begin(),
                                                          graph.operations().end());
                for (const auto opId : cseOps)
                {
                    const wolvrix::lib::grh::Operation op = graph.getOperation(opId);
                    if (!isCseCandidate(graph, op))
                    {
                        continue;
                    }
                    const wolvrix::lib::grh::ValueId resultId = op.results()[0];
                    if (graph.getValue(resultId).type() != wolvrix::lib::grh::ValueType::Logic)
                    {
                        continue;
                    }
                    OpSignature sig = makeSignature(graph, op);
                    auto [it, inserted] = seen.emplace(std::move(sig), CseEntry{opId, resultId});
                    if (inserted)
                    {
                        continue;
                    }
                    const wolvrix::lib::grh::ValueId canonicalValue = it->second.value;
                    if (canonicalValue == resultId)
                    {
                        continue;
                    }
                    auto onError = [&](const std::string &msg)
                    { this->error(graph, op, msg); };
                    replaceUsers(graph, resultId, canonicalValue, onError);
                    if (eraseOp(opId))
                    {
                        graphChanged = true;
                        progress = true;
                    }
                }
            }

            for (const auto &port : graph.outputPorts())
            {
                if (!port.name.valid() || !port.value.valid())
                {
                    continue;
                }
                wolvrix::lib::grh::Value value = graph.getValue(port.value);
                if (value.symbol() == port.name)
                {
                    continue;
                }
                wolvrix::lib::grh::OperationId def = value.definingOp();
                if (!def.valid())
                {
                    continue;
                }
                if (graph.getOperation(def).kind() != wolvrix::lib::grh::OperationKind::kConstant)
                {
                    continue;
                }
                wolvrix::lib::grh::ValueId existing = graph.findValue(port.name);
                if (existing.valid() && existing != port.value)
                {
                    wolvrix::lib::grh::Value existingValue = graph.getValue(existing);
                    if (existingValue.isInput() || existingValue.isOutput() ||
                        existingValue.isInout() || existingValue.definingOp().valid() ||
                        !existingValue.users().empty())
                    {
                        continue;
                    }
                    graph.eraseValue(existing);
                    ++valuesRemoved;
                }
                if (graph.findOperation(port.name).valid())
                {
                    continue;
                }
                graph.setValueSymbol(port.value, port.name);
                graphChanged = true;
            }

            anyChanged = anyChanged || graphChanged;
            if (graphChanged)
            {
                ++changedGraphs;
            }
        }

        result.changed = anyChanged;
        result.failed = false;
        std::string message = "graphs=" + std::to_string(graphCount);
        message.append(", changedGraphs=");
        message.append(std::to_string(changedGraphs));
        message.append(result.changed ? ", changed=true" : ", changed=false");
        message.append(", opsRemoved=");
        message.append(std::to_string(opsRemoved));
        message.append(", valuesRemoved=");
        message.append(std::to_string(valuesRemoved));
        logDebug(std::move(message));
        return result;
    }

} // namespace wolvrix::lib::transform
