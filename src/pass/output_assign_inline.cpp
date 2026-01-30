#include "pass/output_assign_inline.hpp"

#include "grh.hpp"

#include <vector>

namespace wolf_sv_parser::transform
{

    namespace
    {
        bool isOutputPortValue(const grh::ir::Value &value)
        {
            return value.isOutput() && !value.isInput() && !value.isInout();
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

    } // namespace

    OutputAssignInlinePass::OutputAssignInlinePass()
        : Pass("output-assign-inline", "output-assign-inline", "Inline output port assigns")
    {
    }

    PassResult OutputAssignInlinePass::run()
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
                const grh::ir::Value outValue = graph.getValue(outValueId);
                if (!isOutputPortValue(outValue))
                {
                    continue;
                }
                if (!outValue.definingOp().valid() || outValue.definingOp() != opId)
                {
                    continue;
                }
                if (!outValue.users().empty())
                {
                    continue;
                }

                const grh::ir::ValueId operandId = operands[0];
                if (!operandId.valid())
                {
                    continue;
                }
                const grh::ir::Value operandValue = graph.getValue(operandId);
                if (!isSingleUser(operandValue, opId))
                {
                    continue;
                }
                if (operandValue.width() != outValue.width() ||
                    operandValue.isSigned() != outValue.isSigned())
                {
                    continue;
                }

                const grh::ir::OperationId defOpId = operandValue.definingOp();
                if (!defOpId.valid())
                {
                    continue;
                }
                if (defOpId == opId)
                {
                    continue;
                }
                const grh::ir::Operation defOp = graph.getOperation(defOpId);
                if (defOp.results().size() != 1)
                {
                    continue;
                }
                if (defOp.results()[0] != operandId)
                {
                    continue;
                }

                if (!graph.eraseOp(opId))
                {
                    continue;
                }

                graph.replaceResult(defOpId, 0, outValueId);
                graphChanged = true;
            }

            anyChanged = anyChanged || graphChanged;
        }

        result.changed = anyChanged;
        result.failed = false;
        return result;
    }

} // namespace wolf_sv_parser::transform
