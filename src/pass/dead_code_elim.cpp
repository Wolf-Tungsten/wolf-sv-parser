#include "pass/dead_code_elim.hpp"

#include "grh.hpp"

#include <vector>

namespace wolf_sv_parser::transform
{

    namespace
    {
        bool isSideEffectOp(grh::ir::OperationKind kind)
        {
            switch (kind)
            {
            case grh::ir::OperationKind::kMemory:
            case grh::ir::OperationKind::kMemoryWritePort:
            case grh::ir::OperationKind::kInstance:
            case grh::ir::OperationKind::kBlackbox:
            case grh::ir::OperationKind::kDisplay:
            case grh::ir::OperationKind::kAssert:
            case grh::ir::OperationKind::kDpicImport:
            case grh::ir::OperationKind::kDpicCall:
                return true;
            default:
                return false;
            }
        }

        bool isPortValue(const grh::ir::Value &value)
        {
            return value.isInput() || value.isOutput() || value.isInout();
        }

        bool isDeadOp(const grh::ir::Graph &graph, const grh::ir::Operation &op)
        {
            if (isSideEffectOp(op.kind()))
            {
                return false;
            }
            if (op.results().empty())
            {
                return false;
            }
            for (const auto resId : op.results())
            {
                if (!resId.valid())
                {
                    continue;
                }
                const grh::ir::Value res = graph.getValue(resId);
                if (isPortValue(res))
                {
                    return false;
                }
                if (!res.users().empty())
                {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    DeadCodeElimPass::DeadCodeElimPass()
        : Pass("dead-code-elim", "dead-code-elim", "Remove unused operations and values")
    {
    }

    PassResult DeadCodeElimPass::run()
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
                std::vector<grh::ir::OperationId> toErase;
                for (const auto opId : graph.operations())
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    if (isDeadOp(graph, op))
                    {
                        toErase.push_back(opId);
                    }
                }

                for (const auto opId : toErase)
                {
                    if (graph.eraseOp(opId))
                    {
                        graphChanged = true;
                        progress = true;
                    }
                }
            }

            std::vector<grh::ir::ValueId> deadValues;
            for (const auto valueId : graph.values())
            {
                if (!valueId.valid())
                {
                    continue;
                }
                const grh::ir::Value value = graph.getValue(valueId);
                if (isPortValue(value))
                {
                    continue;
                }
                if (value.definingOp().valid())
                {
                    continue;
                }
                if (!value.users().empty())
                {
                    continue;
                }
                deadValues.push_back(valueId);
            }
            for (const auto valueId : deadValues)
            {
                if (graph.eraseValue(valueId))
                {
                    graphChanged = true;
                }
            }

            anyChanged = anyChanged || graphChanged;
        }

        result.changed = anyChanged;
        result.failed = false;
        return result;
    }

} // namespace wolf_sv_parser::transform
