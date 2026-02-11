#include "pass/dead_code_elim.hpp"

#include "grh.hpp"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
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
            case grh::ir::OperationKind::kSystemFunction:
            case grh::ir::OperationKind::kSystemTask:
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
        const std::size_t graphCount = netlist().graphs().size();
        logInfo("begin graphs=" + std::to_string(graphCount));
        std::size_t changedGraphs = 0;

        for (const auto &entry : netlist().graphs())
        {
            grh::ir::Graph &graph = *entry.second;
            bool graphChanged = false;

            const auto opSpan = graph.operations();
            const auto valueSpan = graph.values();
            if (opSpan.empty() && valueSpan.empty())
            {
                continue;
            }

            uint32_t maxValueIndex = 0;
            for (const auto valueId : valueSpan)
            {
                if (valueId.valid())
                {
                    maxValueIndex = std::max(maxValueIndex, valueId.index);
                }
            }
            uint32_t maxOpIndex = 0;
            for (const auto opId : opSpan)
            {
                if (opId.valid())
                {
                    maxOpIndex = std::max(maxOpIndex, opId.index);
                }
            }

            std::vector<uint8_t> isPort;
            std::vector<std::size_t> useCounts;
            std::vector<grh::ir::OperationId> defOpByValue;
            if (maxValueIndex > 0)
            {
                isPort.assign(static_cast<std::size_t>(maxValueIndex + 1), 0);
                useCounts.assign(static_cast<std::size_t>(maxValueIndex + 1), 0);
                defOpByValue.assign(static_cast<std::size_t>(maxValueIndex + 1),
                                    grh::ir::OperationId::invalid());
            }

            auto markPort = [&](grh::ir::ValueId valueId) {
                if (!valueId.valid())
                {
                    return;
                }
                if (valueId.index >= isPort.size())
                {
                    return;
                }
                isPort[valueId.index] = 1;
            };
            for (const auto &port : graph.inputPorts())
            {
                markPort(port.value);
            }
            for (const auto &port : graph.outputPorts())
            {
                markPort(port.value);
            }
            for (const auto &port : graph.inoutPorts())
            {
                markPort(port.in);
                markPort(port.out);
                markPort(port.oe);
            }

            struct OpInfo
            {
                grh::ir::OperationId id;
                bool sideEffect = false;
                std::vector<grh::ir::ValueId> operands;
                std::vector<grh::ir::ValueId> results;
            };

            std::vector<OpInfo> ops;
            ops.reserve(opSpan.size());
            std::vector<int32_t> opIndexById;
            if (maxOpIndex > 0)
            {
                opIndexById.assign(static_cast<std::size_t>(maxOpIndex + 1), -1);
            }

            for (const auto opId : opSpan)
            {
                if (!opId.valid())
                {
                    continue;
                }
                const grh::ir::Operation op = graph.getOperation(opId);
                OpInfo info;
                info.id = opId;
                info.sideEffect = isSideEffectOp(op.kind());
                info.operands = std::vector<grh::ir::ValueId>(op.operands().begin(),
                                                             op.operands().end());
                info.results = std::vector<grh::ir::ValueId>(op.results().begin(),
                                                            op.results().end());
                const std::size_t infoIndex = ops.size();
                ops.push_back(std::move(info));
                if (opId.index < opIndexById.size())
                {
                    opIndexById[opId.index] = static_cast<int32_t>(infoIndex);
                }
                for (const auto valueId : ops.back().operands)
                {
                    if (valueId.valid() && valueId.index < useCounts.size())
                    {
                        useCounts[valueId.index] += 1;
                    }
                }
                for (const auto valueId : ops.back().results)
                {
                    if (valueId.valid() && valueId.index < defOpByValue.size())
                    {
                        defOpByValue[valueId.index] = opId;
                    }
                }
            }

            auto isDeadByCounts = [&](const OpInfo &info) -> bool {
                if (info.sideEffect)
                {
                    return false;
                }
                if (info.results.empty())
                {
                    return false;
                }
                for (const auto valueId : info.results)
                {
                    if (!valueId.valid() || valueId.index >= useCounts.size())
                    {
                        continue;
                    }
                    if (isPort[valueId.index] != 0)
                    {
                        return false;
                    }
                    if (useCounts[valueId.index] != 0)
                    {
                        return false;
                    }
                }
                return true;
            };

            std::deque<int32_t> worklist;
            worklist.resize(0);
            for (std::size_t idx = 0; idx < ops.size(); ++idx)
            {
                if (isDeadByCounts(ops[idx]))
                {
                    worklist.push_back(static_cast<int32_t>(idx));
                }
            }

            std::vector<uint8_t> opRemoved(ops.size(), 0);

            while (!worklist.empty())
            {
                const int32_t idx = worklist.front();
                worklist.pop_front();
                if (idx < 0 || static_cast<std::size_t>(idx) >= ops.size())
                {
                    continue;
                }
                if (opRemoved[static_cast<std::size_t>(idx)] != 0)
                {
                    continue;
                }
                const OpInfo &info = ops[static_cast<std::size_t>(idx)];
                if (!isDeadByCounts(info))
                {
                    continue;
                }
                if (!graph.eraseOpUnchecked(info.id))
                {
                    continue;
                }
                opRemoved[static_cast<std::size_t>(idx)] = 1;
                graphChanged = true;

                for (const auto valueId : info.results)
                {
                    if (valueId.valid() && valueId.index < defOpByValue.size())
                    {
                        defOpByValue[valueId.index] = grh::ir::OperationId::invalid();
                    }
                }

                for (const auto valueId : info.operands)
                {
                    if (!valueId.valid() || valueId.index >= useCounts.size())
                    {
                        continue;
                    }
                    if (useCounts[valueId.index] > 0)
                    {
                        useCounts[valueId.index] -= 1;
                    }
                    if (useCounts[valueId.index] != 0)
                    {
                        continue;
                    }
                    const grh::ir::OperationId defOp = defOpByValue[valueId.index];
                    if (!defOp.valid())
                    {
                        continue;
                    }
                    if (defOp.index >= opIndexById.size())
                    {
                        continue;
                    }
                    const int32_t defIdx = opIndexById[defOp.index];
                    if (defIdx >= 0)
                    {
                        worklist.push_back(defIdx);
                    }
                }
            }

            std::vector<grh::ir::ValueId> deadValues;
            for (const auto valueId : valueSpan)
            {
                if (!valueId.valid() || valueId.index >= useCounts.size())
                {
                    continue;
                }
                if (isPort[valueId.index] != 0)
                {
                    continue;
                }
                if (useCounts[valueId.index] != 0)
                {
                    continue;
                }
                if (defOpByValue[valueId.index].valid())
                {
                    continue;
                }
                deadValues.push_back(valueId);
            }
            for (const auto valueId : deadValues)
            {
                if (graph.eraseValueUnchecked(valueId))
                {
                    graphChanged = true;
                }
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
        logInfo(std::move(message));
        return result;
    }

} // namespace wolf_sv_parser::transform
