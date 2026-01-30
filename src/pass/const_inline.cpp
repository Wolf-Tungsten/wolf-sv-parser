#include "pass/const_inline.hpp"

#include "grh.hpp"

#include <string>
#include <vector>

namespace wolf_sv_parser::transform
{

    namespace
    {
        bool isOutputPortValue(const grh::ir::Value &value)
        {
            return value.isOutput();
        }

        bool hasOtherUsers(const grh::ir::Value &value)
        {
            return !value.users().empty();
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

    } // namespace

    ConstInlinePass::ConstInlinePass()
        : Pass("const-inline", "const-inline", "Inline constant assigns to output ports")
    {
    }

    PassResult ConstInlinePass::run()
    {
        PassResult result;
        bool anyChanged = false;

        for (const auto &entry : netlist().graphs())
        {
            grh::ir::Graph &graph = *entry.second;
            bool graphChanged = false;

            std::vector<grh::ir::OperationId> assignOps(graph.operations().begin(),
                                                        graph.operations().end());
            for (const auto opId : assignOps)
            {
                const grh::ir::Operation op = graph.getOperation(opId);
                if (op.kind() != grh::ir::OperationKind::kAssign)
                {
                    continue;
                }
                const auto &operands = op.operands();
                const auto &results = op.results();
                if (operands.size() != 1 || results.size() != 1)
                {
                    continue;
                }
                const grh::ir::ValueId outValueId = results[0];
                if (!outValueId.valid())
                {
                    continue;
                }
                grh::ir::Value outValue = graph.getValue(outValueId);
                if (!isOutputPortValue(outValue))
                {
                    continue;
                }
                if (hasOtherUsers(outValue))
                {
                    continue;
                }

                const grh::ir::ValueId constValueId = operands[0];
                if (!constValueId.valid())
                {
                    continue;
                }
                const grh::ir::Value constValue = graph.getValue(constValueId);
                const grh::ir::OperationId constOpId = constValue.definingOp();
                if (!constOpId.valid())
                {
                    continue;
                }
                const grh::ir::Operation constOp = graph.getOperation(constOpId);
                if (constOp.kind() != grh::ir::OperationKind::kConstant ||
                    constOp.results().size() != 1)
                {
                    continue;
                }
                auto constLiteral = constOp.attr("constValue");
                if (!constLiteral)
                {
                    continue;
                }
                const auto *literalText = std::get_if<std::string>(&*constLiteral);
                if (!literalText)
                {
                    continue;
                }

                if (!graph.eraseOp(opId))
                {
                    continue;
                }

                if (isSingleUser(constValue, opId) && !constValue.isOutput())
                {
                    try
                    {
                        graph.replaceResult(constOpId, 0, outValueId);
                        graphChanged = true;
                        continue;
                    }
                    catch (...)
                    {
                        // Fall through to clone path on replacement failures.
                    }
                }

                grh::ir::SymbolId opSym = makeInlineConstSymbol(graph, outValue.symbolText());
                grh::ir::OperationId newConst =
                    graph.createOperation(grh::ir::OperationKind::kConstant, opSym);
                graph.addResult(newConst, outValueId);
                graph.setAttr(newConst, "constValue", *literalText);
                graphChanged = true;
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
