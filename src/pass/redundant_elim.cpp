#include "pass/redundant_elim.hpp"

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

namespace wolf_sv_parser::transform
{

    namespace
    {
        std::size_t hashCombine(std::size_t seed, std::size_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
        }

        std::size_t hashAttributeValue(const grh::ir::AttributeValue &value)
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
            grh::ir::OperationKind kind = grh::ir::OperationKind::kConstant;
            std::vector<grh::ir::ValueId> operands;
            std::vector<grh::ir::AttrKV> attrs;
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
                    seed = hashCombine(seed, grh::ir::ValueIdHash{}(operand));
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

        bool isOutputPortValue(const grh::ir::Value &value)
        {
            return value.isOutput() && !value.isInput() && !value.isInout();
        }

        bool hasOtherUsers(const grh::ir::Value &value)
        {
            return !value.users().empty();
        }

        bool isTemporarySymbol(const grh::ir::Value &value)
        {
            if (value.isInput() || value.isOutput() || value.isInout())
            {
                return false;
            }
            const std::string_view name = value.symbolText();
            if (name.rfind("__expr_", 0) == 0)
            {
                return true;
            }
            if (name.rfind("__constfold__", 0) == 0)
            {
                return true;
            }
            if (name.rfind("_expr_tmp_", 0) == 0)
            {
                return true;
            }
            return false;
        }

        bool isSideEffectFreeOp(grh::ir::OperationKind kind)
        {
            switch (kind)
            {
            case grh::ir::OperationKind::kMemory:
            case grh::ir::OperationKind::kMemoryReadPort:
            case grh::ir::OperationKind::kMemoryWritePort:
            case grh::ir::OperationKind::kRegister:
            case grh::ir::OperationKind::kRegisterReadPort:
            case grh::ir::OperationKind::kRegisterWritePort:
            case grh::ir::OperationKind::kLatch:
            case grh::ir::OperationKind::kLatchReadPort:
            case grh::ir::OperationKind::kLatchWritePort:
            case grh::ir::OperationKind::kInstance:
            case grh::ir::OperationKind::kBlackbox:
            case grh::ir::OperationKind::kSystemFunction:
            case grh::ir::OperationKind::kSystemTask:
            case grh::ir::OperationKind::kDpicImport:
            case grh::ir::OperationKind::kDpicCall:
                return false;
            default:
                return true;
            }
        }

        bool isCseCandidate(const grh::ir::Graph &graph,
                            const grh::ir::Operation &op)
        {
            if (!isSideEffectFreeOp(op.kind()))
            {
                return false;
            }
            if (op.results().size() != 1)
            {
                return false;
            }
            const grh::ir::ValueId resultId = op.results()[0];
            if (!resultId.valid())
            {
                return false;
            }
            const grh::ir::Value resultValue = graph.getValue(resultId);
            if (resultValue.isInput() || resultValue.isOutput() || resultValue.isInout())
            {
                return false;
            }
            if (!isTemporarySymbol(resultValue))
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

        OpSignature makeSignature(const grh::ir::Graph &graph,
                                  const grh::ir::Operation &op)
        {
            OpSignature sig;
            sig.kind = op.kind();
            sig.operands.assign(op.operands().begin(), op.operands().end());
            sig.attrs.assign(op.attrs().begin(), op.attrs().end());
            std::stable_sort(sig.attrs.begin(), sig.attrs.end(),
                             [](const grh::ir::AttrKV &lhs, const grh::ir::AttrKV &rhs)
                             { return lhs.key < rhs.key; });
            const grh::ir::ValueId resultId = op.results()[0];
            const grh::ir::Value resultValue = graph.getValue(resultId);
            sig.width = resultValue.width();
            sig.isSigned = resultValue.isSigned();
            return sig;
        }

        bool isSingleUser(const grh::ir::Value &value, grh::ir::OperationId user)
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

        bool isLogicNotOf(const grh::ir::Graph &graph,
                          grh::ir::ValueId maybeNot,
                          grh::ir::ValueId operand)
        {
            if (!maybeNot.valid() || !operand.valid())
            {
                return false;
            }
            const grh::ir::OperationId defOpId = graph.getValue(maybeNot).definingOp();
            if (!defOpId.valid())
            {
                return false;
            }
            const grh::ir::Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != grh::ir::OperationKind::kLogicNot &&
                defOp.kind() != grh::ir::OperationKind::kNot)
            {
                return false;
            }
            const auto &ops = defOp.operands();
            return !ops.empty() && ops.front() == operand;
        }

        grh::ir::SymbolId makeInlineConstSymbol(grh::ir::Graph &graph,
                                                std::string_view baseName)
        {
            std::string base;
            if (!baseName.empty())
            {
                base = std::string(baseName);
            }
            else
            {
                base = "__const_inline";
            }
            std::string candidate = base + "__const_inline";
            std::size_t suffix = 0;
            while (graph.symbols().contains(candidate))
            {
                candidate = base + "__const_inline_" + std::to_string(++suffix);
            }
            return graph.internSymbol(candidate);
        }

        grh::ir::ValueId createInlineConst(grh::ir::Graph &graph,
                                           std::string_view baseName,
                                           int32_t width,
                                           bool isSigned,
                                           std::string_view literal)
        {
            std::string base;
            if (!baseName.empty())
            {
                base = std::string(baseName);
            }
            else
            {
                base = "__const_inline";
            }
            const grh::ir::SymbolId valueSym =
                makeInlineConstSymbol(graph, base + "__value");
            const grh::ir::SymbolId opSym =
                makeInlineConstSymbol(graph, base + "__op");
            const grh::ir::ValueId val =
                graph.createValue(valueSym, width, isSigned, grh::ir::ValueType::Logic);
            const grh::ir::OperationId op =
                graph.createOperation(grh::ir::OperationKind::kConstant, opSym);
            graph.addResult(op, val);
            graph.setAttr(op, "constValue", std::string(literal));
            return val;
        }

        void replaceUsers(grh::ir::Graph &graph,
                          grh::ir::ValueId from,
                          grh::ir::ValueId to,
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

            std::vector<grh::ir::SymbolId> outputPortsToUpdate;
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
        logInfo("begin graphs=" + std::to_string(graphCount));
        std::size_t changedGraphs = 0;
        std::size_t opsRemoved = 0;
        std::size_t valuesRemoved = 0;

        for (const auto &entry : netlist().graphs())
        {
            grh::ir::Graph &graph = *entry.second;
            bool graphChanged = false;
            bool progress = true;
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
                std::vector<grh::ir::OperationId> ops(graph.operations().begin(),
                                                      graph.operations().end());
                for (const auto opId : ops)
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    const auto &operands = op.operands();
                    const auto &results = op.results();

                    if (op.kind() == grh::ir::OperationKind::kAssign &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const grh::ir::ValueId srcId = operands[0];
                        const grh::ir::ValueId dstId = results[0];
                        if (!srcId.valid() || !dstId.valid())
                        {
                            continue;
                        }
                        const grh::ir::Value srcValue = graph.getValue(srcId);
                        const grh::ir::Value dstValue = graph.getValue(dstId);

                        if (isOutputPortValue(dstValue) && !hasOtherUsers(dstValue))
                        {
                            const grh::ir::OperationId constOpId = srcValue.definingOp();
                            if (constOpId.valid())
                            {
                                const grh::ir::Operation constOp = graph.getOperation(constOpId);
                                if (constOp.kind() == grh::ir::OperationKind::kConstant &&
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

                                    grh::ir::SymbolId opSym =
                                        makeInlineConstSymbol(graph, dstValue.symbolText());
                                    grh::ir::OperationId newConst =
                                        graph.createOperation(grh::ir::OperationKind::kConstant,
                                                              opSym);
                                    graph.addResult(newConst, dstId);
                                    graph.setAttr(newConst, "constValue", *literalText);
                                    graphChanged = true;
                                    progress = true;
                                    continue;
                                }
                            }
                        }
                    }

                    if (op.kind() == grh::ir::OperationKind::kConcat &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const grh::ir::ValueId operandId = operands[0];
                        const grh::ir::ValueId resultId = results[0];
                        if (!operandId.valid() || !resultId.valid())
                        {
                            continue;
                        }
                        const grh::ir::Value operandValue = graph.getValue(operandId);
                        const grh::ir::Value resultValue = graph.getValue(resultId);
                        if (!isTemporarySymbol(resultValue))
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

                    if (op.kind() == grh::ir::OperationKind::kLogicOr &&
                        operands.size() >= 2 && results.size() == 1)
                    {
                        const grh::ir::ValueId resultId = results[0];
                        if (!resultId.valid())
                        {
                            continue;
                        }
                        const grh::ir::Value resultValue = graph.getValue(resultId);
                        if (resultValue.width() != 1)
                        {
                            continue;
                        }
                        bool alwaysTrue = false;
                        for (std::size_t i = 0; i < operands.size() && !alwaysTrue; ++i)
                        {
                            const grh::ir::ValueId lhs = operands[i];
                            if (!lhs.valid())
                            {
                                continue;
                            }
                            for (std::size_t j = i + 1; j < operands.size(); ++j)
                            {
                                const grh::ir::ValueId rhs = operands[j];
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
                            grh::ir::ValueId constOne =
                                createInlineConst(graph, resultValue.symbolText(),
                                                  1, resultValue.isSigned(), "1'b1");
                            replaceUsers(graph, resultId, constOne, onError);
                            eraseOp(opId);
                            graphChanged = true;
                            progress = true;
                            continue;
                        }
                    }

                    if (op.kind() == grh::ir::OperationKind::kAssign &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const grh::ir::ValueId srcId = operands[0];
                        const grh::ir::ValueId dstId = results[0];
                        if (!srcId.valid() || !dstId.valid())
                        {
                            continue;
                        }
                        const grh::ir::Value srcValue = graph.getValue(srcId);
                        const grh::ir::Value dstValue = graph.getValue(dstId);
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
                            const grh::ir::OperationId defOpId =
                                srcValue.definingOp();
                            if (!defOpId.valid() || defOpId == opId)
                            {
                                continue;
                            }
                            const grh::ir::Operation defOp = graph.getOperation(defOpId);
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
                        if (!isTemporarySymbol(srcValue))
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
                        const grh::ir::OperationId defOpId = srcValue.definingOp();
                        if (!defOpId.valid() || defOpId == opId)
                        {
                            continue;
                        }
                        const grh::ir::Operation defOp = graph.getOperation(defOpId);
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
                        if (!eraseOp(opId, std::array<grh::ir::ValueId, 1>{dstId}))
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

                    if (op.kind() == grh::ir::OperationKind::kNot &&
                        operands.size() == 1 && results.size() == 1)
                    {
                        const grh::ir::ValueId operandId = operands[0];
                        const grh::ir::ValueId resultId = results[0];
                        if (!operandId.valid() || !resultId.valid())
                        {
                            continue;
                        }
                        const grh::ir::Value operandValue = graph.getValue(operandId);
                        if (!isTemporarySymbol(operandValue))
                        {
                            continue;
                        }
                        if (!isSingleUser(operandValue, opId))
                        {
                            continue;
                        }
                        const grh::ir::OperationId defOpId = operandValue.definingOp();
                        if (!defOpId.valid())
                        {
                            continue;
                        }
                        const grh::ir::Operation defOp = graph.getOperation(defOpId);
                        if (defOp.kind() != grh::ir::OperationKind::kXor)
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
                        if (!eraseOp(opId, std::array<grh::ir::ValueId, 1>{resultId}))
                        {
                            continue;
                        }
                        graph.setOpKind(defOpId, grh::ir::OperationKind::kXnor);
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
                    grh::ir::OperationId op;
                    grh::ir::ValueId value;
                };

                std::unordered_map<OpSignature, CseEntry, OpSignatureHash> seen;
                std::vector<grh::ir::OperationId> cseOps(graph.operations().begin(),
                                                          graph.operations().end());
                for (const auto opId : cseOps)
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    if (!isCseCandidate(graph, op))
                    {
                        continue;
                    }
                    const grh::ir::ValueId resultId = op.results()[0];
                    if (graph.getValue(resultId).type() != grh::ir::ValueType::Logic)
                    {
                        continue;
                    }
                    OpSignature sig = makeSignature(graph, op);
                    auto [it, inserted] = seen.emplace(std::move(sig), CseEntry{opId, resultId});
                    if (inserted)
                    {
                        continue;
                    }
                    const grh::ir::ValueId canonicalValue = it->second.value;
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
                grh::ir::Value value = graph.getValue(port.value);
                if (value.symbol() == port.name)
                {
                    continue;
                }
                grh::ir::OperationId def = value.definingOp();
                if (!def.valid())
                {
                    continue;
                }
                if (graph.getOperation(def).kind() != grh::ir::OperationKind::kConstant)
                {
                    continue;
                }
                grh::ir::ValueId existing = graph.findValue(port.name);
                if (existing.valid() && existing != port.value)
                {
                    grh::ir::Value existingValue = graph.getValue(existing);
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
        logInfo(std::move(message));
        return result;
    }

} // namespace wolf_sv_parser::transform
