#include "pass/redundant_elim.hpp"

#include "grh.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wolf_sv_parser::transform
{

    namespace
    {
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

        for (const auto &entry : netlist().graphs())
        {
            grh::ir::Graph &graph = *entry.second;
            bool graphChanged = false;
            bool progress = true;

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
                                    if (!graph.eraseOp(opId))
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
                        if (graph.eraseOp(opId))
                        {
                            graphChanged = true;
                            progress = true;
                        }
                        continue;
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
                            if (defOp.results().size() != 1)
                            {
                                continue;
                            }
                            if (defOp.results()[0] != srcId)
                            {
                                continue;
                            }
                            if (!graph.eraseOp(opId))
                            {
                                continue;
                            }
                            try
                            {
                                graph.replaceResult(defOpId, 0, dstId);
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
                        if (!graph.eraseOp(opId, std::array<grh::ir::ValueId, 1>{dstId}))
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
                        if (!graph.eraseOp(opId, std::array<grh::ir::ValueId, 1>{resultId}))
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
                }
                if (graph.findOperation(port.name).valid())
                {
                    continue;
                }
                graph.setValueSymbol(port.value, port.name);
                graphChanged = true;
            }

            anyChanged = anyChanged || graphChanged;
        }

        result.changed = anyChanged;
        result.failed = false;
        return result;
    }

} // namespace wolf_sv_parser::transform
