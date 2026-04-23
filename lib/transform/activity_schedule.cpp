#include "transform/activity_schedule.hpp"

#include "core/toposort.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wolvrix::lib::transform
{

    namespace
    {

        std::optional<std::string> getAttrString(const wolvrix::lib::grh::Operation &op,
                                                 std::string_view key)
        {
            const auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<std::string>(&*attr))
            {
                return *value;
            }
            return std::nullopt;
        }

        template <typename T>
        std::optional<T> getAttrValue(const wolvrix::lib::grh::Operation &op, std::string_view key)
        {
            const auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<T>(&*attr))
            {
                return *value;
            }
            return std::nullopt;
        }

        bool opHasReturnedEffectValue(const wolvrix::lib::grh::Operation &op)
        {
            if (!getAttrValue<bool>(op, "hasReturn").value_or(false))
            {
                return false;
            }
            return op.kind() == wolvrix::lib::grh::OperationKind::kDpicCall ||
                   op.kind() == wolvrix::lib::grh::OperationKind::kSystemTask;
        }

        bool isTailSinkOp(const wolvrix::lib::grh::Operation &op)
        {
            if (!op.results().empty())
            {
                return false;
            }
            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
                return true;
            case wolvrix::lib::grh::OperationKind::kSystemTask:
            case wolvrix::lib::grh::OperationKind::kDpicCall:
                return !opHasReturnedEffectValue(op);
            default:
                return false;
            }
        }

        bool isTailSinkKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
            case wolvrix::lib::grh::OperationKind::kSystemTask:
            case wolvrix::lib::grh::OperationKind::kDpicCall:
                return true;
            default:
                return false;
            }
        }

        std::string describeOp(const wolvrix::lib::grh::Graph &graph,
                               wolvrix::lib::grh::OperationId opId)
        {
            const wolvrix::lib::grh::Operation op = graph.getOperation(opId);
            if (!op.symbolText().empty())
            {
                return std::string(op.symbolText());
            }
            std::ostringstream oss;
            oss << wolvrix::lib::grh::toString(op.kind()) << "#" << opId.index;
            return oss.str();
        }

        std::vector<std::string> splitPath(std::string_view path)
        {
            std::vector<std::string> out;
            std::string current;
            for (const char ch : path)
            {
                if (ch == '.')
                {
                    if (!current.empty())
                    {
                        out.push_back(current);
                        current.clear();
                    }
                    continue;
                }
                current.push_back(ch);
            }
            if (!current.empty())
            {
                out.push_back(current);
            }
            return out;
        }

        wolvrix::lib::grh::OperationId findUniqueInstance(const wolvrix::lib::grh::Graph &graph,
                                                          std::string_view instanceName)
        {
            wolvrix::lib::grh::OperationId found = wolvrix::lib::grh::OperationId::invalid();
            for (const auto opId : graph.operations())
            {
                const auto op = graph.getOperation(opId);
                if (op.kind() != wolvrix::lib::grh::OperationKind::kInstance)
                {
                    continue;
                }
                const auto name = getAttrString(op, "instanceName");
                if (!name || *name != instanceName)
                {
                    continue;
                }
                if (found.valid())
                {
                    return wolvrix::lib::grh::OperationId::invalid();
                }
                found = opId;
            }
            return found;
        }

        std::optional<std::string> resolveTargetGraphName(wolvrix::lib::grh::Design &design,
                                                          std::string_view path,
                                                          std::string &error)
        {
            const std::vector<std::string> segments = splitPath(path);
            if (segments.empty())
            {
                error = "activity-schedule path must not be empty";
                return std::nullopt;
            }
            if (segments.size() == 1)
            {
                if (design.findGraph(segments.front()) == nullptr)
                {
                    error = "activity-schedule graph not found: " + segments.front();
                    return std::nullopt;
                }
                return segments.front();
            }

            auto *current = design.findGraph(segments.front());
            if (current == nullptr)
            {
                error = "activity-schedule root graph not found: " + segments.front();
                return std::nullopt;
            }
            for (std::size_t i = 1; i < segments.size(); ++i)
            {
                const auto instOp = findUniqueInstance(*current, segments[i]);
                if (!instOp.valid())
                {
                    error = "activity-schedule instance not found or not unique: " + segments[i];
                    return std::nullopt;
                }
                const auto op = current->getOperation(instOp);
                const auto moduleName = getAttrString(op, "moduleName");
                if (!moduleName || moduleName->empty())
                {
                    error = "activity-schedule instance missing moduleName: " + segments[i];
                    return std::nullopt;
                }
                current = design.findGraph(*moduleName);
                if (current == nullptr)
                {
                    error = "activity-schedule target module graph not found: " + *moduleName;
                    return std::nullopt;
                }
            }
            return current->symbol();
        }

        bool isHierLikeOpKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kInstance:
            case wolvrix::lib::grh::OperationKind::kBlackbox:
            case wolvrix::lib::grh::OperationKind::kXMRRead:
            case wolvrix::lib::grh::OperationKind::kXMRWrite:
                return true;
            default:
                return false;
            }
        }

        bool isStorageDeclOpKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegister:
            case wolvrix::lib::grh::OperationKind::kMemory:
            case wolvrix::lib::grh::OperationKind::kLatch:
            case wolvrix::lib::grh::OperationKind::kDpicImport:
                return true;
            default:
                return false;
            }
        }

        bool isStateReadOpKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
            case wolvrix::lib::grh::OperationKind::kLatchReadPort:
                return true;
            default:
                return false;
            }
        }

        bool isSideEffectBoundaryKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            (void)kind;
            // GrhSIM side effects are controlled by the emitted event / commit semantics rather
            // than by hard schedule boundaries, so activity-schedule does not isolate any op here.
            return false;
        }

        bool isPartitionableOpKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            return !isStorageDeclOpKind(kind) && !isHierLikeOpKind(kind);
        }

        std::optional<std::string> stateSymbolForReadOp(const wolvrix::lib::grh::Operation &op)
        {
            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
                return getAttrString(op, "regSymbol");
            case wolvrix::lib::grh::OperationKind::kLatchReadPort:
                return getAttrString(op, "latchSymbol");
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
                return getAttrString(op, "memSymbol");
            default:
                return std::nullopt;
            }
        }

        struct ActivityScheduleBuild
        {
            ActivityScheduleSupernodeToOps supernodeToOps;
            ActivityScheduleOpToSupernode opToSupernode;
            std::vector<std::vector<uint32_t>> dag;
            ActivityScheduleValueFanout valueFanout;
            ActivityScheduleTopoOrder topoOrder;
            ActivityScheduleStateReadSupernodes stateReadSupernodes;
        };

        struct ActivityOpData
        {
            std::vector<wolvrix::lib::grh::OperationId> topoOps;
            std::vector<wolvrix::lib::grh::SymbolId> topoSymbols;
            std::vector<wolvrix::lib::grh::OperationKind> topoKinds;
            std::vector<uint8_t> topoSinkOnly;
            std::vector<std::pair<uint32_t, uint32_t>> topoEdges;
            std::vector<uint32_t> topoPosByOpIndex;
            std::size_t maxOpIndex = 0;
        };

        struct WorkingPartition
        {
            std::vector<std::vector<uint32_t>> clusters;
            std::vector<uint8_t> fixedBoundary;
        };

        struct ClusterView
        {
            std::vector<std::vector<uint32_t>> members;
            std::vector<uint8_t> fixedBoundary;
            std::vector<uint8_t> sinkOnly;
            std::vector<std::vector<uint32_t>> preds;
            std::vector<std::vector<uint32_t>> succs;
            std::vector<uint32_t> clusterOfTopoPos;
        };

        struct SymbolPartition
        {
            std::vector<std::vector<wolvrix::lib::grh::SymbolId>> clusters;
            std::vector<uint8_t> fixedBoundary;
        };

        struct LiveCluster
        {
            uint32_t minTopoPos = kInvalidActivitySupernodeId;
            uint32_t maxTopoPos = 0;
            bool sinkOnly = true;
            std::vector<uint32_t> topoPositions;
        };

        struct ReplicationStats
        {
            std::size_t clonedOps = 0;
            std::size_t erasedOps = 0;
        };

        struct ReplicationPerfStats
        {
            std::size_t scannedOps = 0;
            std::size_t candidateOps = 0;
            std::size_t userEdgesVisited = 0;
            std::size_t targetBuckets = 0;
            std::uint64_t collectObservableMs = 0;
            std::uint64_t buildSymbolMapMs = 0;
            std::uint64_t scanAndCloneMs = 0;
        };

        struct FinalMaterializePerfStats
        {
            std::uint64_t rebuildOpDataMs = 0;
            std::uint64_t mapLiveOpsMs = 0;
            std::uint64_t collectLiveClustersMs = 0;
            std::uint64_t buildSupernodeMapsMs = 0;
            std::uint64_t buildDagMs = 0;
            std::uint64_t buildValueFanoutMs = 0;
            std::uint64_t buildStateReadSetsMs = 0;
            std::uint64_t finalTopoMs = 0;
        };

        struct TailPartitionStats
        {
            std::size_t initialSeedOps = 0;
            std::size_t sharedSeedOps = 0;
            std::size_t absorbedOps = 0;
        };

        struct StateReadTailAbsorbStats
        {
            std::size_t clonedOps = 0;
            std::size_t erasedOps = 0;
            std::size_t keptObservableReads = 0;
            std::size_t keptLocalReads = 0;
            std::size_t skippedTooManyTargets = 0;
        };

        ClusterView buildClusterView(const WorkingPartition &partition, const ActivityOpData &opData);

        std::vector<uint32_t> buildTopoOrderedClusterIds(const ClusterView &view)
        {
            if (view.members.empty())
            {
                return {};
            }

            wolvrix::lib::toposort::TopoDag<uint32_t> topoDag;
            topoDag.reserveNodes(view.members.size());
            for (uint32_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                topoDag.addNode(clusterId);
            }
            for (uint32_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                for (const auto succ : view.succs[clusterId])
                {
                    topoDag.addEdge(clusterId, succ);
                }
            }

            std::vector<uint32_t> ordered;
            try
            {
                const auto layers = topoDag.toposort();
                ordered.reserve(view.members.size());
                for (const auto &layer : layers)
                {
                    std::vector<uint32_t> orderedLayer(layer.begin(), layer.end());
                    std::sort(orderedLayer.begin(),
                              orderedLayer.end(),
                              [&](uint32_t lhs, uint32_t rhs)
                              {
                                  const uint32_t lhsMin = view.members[lhs].empty() ? kInvalidActivitySupernodeId
                                                                                    : view.members[lhs].front();
                                  const uint32_t rhsMin = view.members[rhs].empty() ? kInvalidActivitySupernodeId
                                                                                    : view.members[rhs].front();
                                  if (lhsMin != rhsMin)
                                  {
                                      return lhsMin < rhsMin;
                                  }
                                  return lhs < rhs;
                              });
                    ordered.insert(ordered.end(), orderedLayer.begin(), orderedLayer.end());
                }
            }
            catch (const std::exception &)
            {
                return {};
            }
            if (ordered.size() != view.members.size())
            {
                return {};
            }
            return ordered;
        }

        ClusterView buildTopoOrderedClusterView(const ClusterView &view)
        {
            if (view.members.empty())
            {
                return view;
            }

            const std::vector<uint32_t> orderedClusterIds = buildTopoOrderedClusterIds(view);
            if (orderedClusterIds.size() != view.members.size())
            {
                return view;
            }

            ClusterView out;
            out.members.reserve(view.members.size());
            out.fixedBoundary.reserve(view.fixedBoundary.size());
            out.sinkOnly.reserve(view.sinkOnly.size());
            out.preds.resize(view.preds.size());
            out.succs.resize(view.succs.size());
            out.clusterOfTopoPos.assign(view.clusterOfTopoPos.size(), kInvalidActivitySupernodeId);

            std::vector<uint32_t> newIdByOldId(view.members.size(), kInvalidActivitySupernodeId);
            for (uint32_t newId = 0; newId < orderedClusterIds.size(); ++newId)
            {
                const uint32_t oldId = orderedClusterIds[newId];
                newIdByOldId[oldId] = newId;
                out.members.push_back(view.members[oldId]);
                out.fixedBoundary.push_back(view.fixedBoundary[oldId]);
                out.sinkOnly.push_back(oldId < view.sinkOnly.size() ? view.sinkOnly[oldId] : 0U);
                for (const auto topoPos : out.members.back())
                {
                    if (topoPos < out.clusterOfTopoPos.size())
                    {
                        out.clusterOfTopoPos[topoPos] = newId;
                    }
                }
            }

            for (uint32_t newId = 0; newId < orderedClusterIds.size(); ++newId)
            {
                const uint32_t oldId = orderedClusterIds[newId];
                for (const auto oldPred : view.preds[oldId])
                {
                    if (oldPred < newIdByOldId.size() && newIdByOldId[oldPred] != kInvalidActivitySupernodeId)
                    {
                        out.preds[newId].push_back(newIdByOldId[oldPred]);
                    }
                }
                for (const auto oldSucc : view.succs[oldId])
                {
                    if (oldSucc < newIdByOldId.size() && newIdByOldId[oldSucc] != kInvalidActivitySupernodeId)
                    {
                        out.succs[newId].push_back(newIdByOldId[oldSucc]);
                    }
                }
                std::sort(out.preds[newId].begin(), out.preds[newId].end());
                out.preds[newId].erase(std::unique(out.preds[newId].begin(), out.preds[newId].end()),
                                       out.preds[newId].end());
                std::sort(out.succs[newId].begin(), out.succs[newId].end());
                out.succs[newId].erase(std::unique(out.succs[newId].begin(), out.succs[newId].end()),
                                       out.succs[newId].end());
            }

            return out;
        }

        WorkingPartition canonicalizePartition(const WorkingPartition &partition,
                                               const ActivityOpData &opData)
        {
            if (partition.clusters.empty())
            {
                return partition;
            }

            const ClusterView orderedView = buildTopoOrderedClusterView(buildClusterView(partition, opData));
            WorkingPartition out;
            out.clusters = orderedView.members;
            out.fixedBoundary = orderedView.fixedBoundary;
            if (out.clusters.size() != partition.clusters.size())
            {
                return partition;
            }
            return out;
        }

        std::vector<uint32_t> findCyclePath(const std::vector<std::vector<uint32_t>> &dag)
        {
            std::vector<uint8_t> color(dag.size(), 0U);
            std::vector<uint32_t> stack;
            std::vector<uint32_t> stackIndex(dag.size(), kInvalidActivitySupernodeId);
            std::vector<uint32_t> cycle;

            const auto dfs = [&](auto &&self, uint32_t node) -> bool
            {
                color[node] = 1U;
                stackIndex[node] = static_cast<uint32_t>(stack.size());
                stack.push_back(node);
                for (const auto succ : dag[node])
                {
                    if (succ >= dag.size())
                    {
                        continue;
                    }
                    if (color[succ] == 0U)
                    {
                        if (self(self, succ))
                        {
                            return true;
                        }
                        continue;
                    }
                    if (color[succ] == 1U)
                    {
                        const uint32_t begin = stackIndex[succ];
                        cycle.assign(stack.begin() + begin, stack.end());
                        cycle.push_back(succ);
                        return true;
                    }
                }
                stackIndex[node] = kInvalidActivitySupernodeId;
                stack.pop_back();
                color[node] = 2U;
                return false;
            };

            for (uint32_t node = 0; node < dag.size(); ++node)
            {
                if (color[node] == 0U && dfs(dfs, node))
                {
                    break;
                }
            }
            return cycle;
        }

        std::string describeCyclePath(const std::vector<uint32_t> &cycle,
                                      const std::vector<LiveCluster> &liveClusters)
        {
            if (cycle.empty())
            {
                return "cycle=<unavailable>";
            }

            std::ostringstream oss;
            oss << "cycle=";
            const std::size_t limit = std::min<std::size_t>(cycle.size(), 8);
            for (std::size_t i = 0; i < limit; ++i)
            {
                const uint32_t node = cycle[i];
                if (i != 0)
                {
                    oss << " -> ";
                }
                oss << node;
                if (node < liveClusters.size())
                {
                    const auto &cluster = liveClusters[node];
                    oss << "(min=" << cluster.minTopoPos
                        << ",max=" << cluster.maxTopoPos
                        << ",ops=" << cluster.topoPositions.size()
                        << ",sinkOnly=" << (cluster.sinkOnly ? "1" : "0") << ")";
                }
            }
            if (cycle.size() > limit)
            {
                oss << " -> ...";
            }
            return oss.str();
        }

        std::string describeWorkingPartitionCycle(const WorkingPartition &partition,
                                                  const ActivityOpData &opData)
        {
            if (partition.clusters.empty())
            {
                return {};
            }

            const ClusterView view = buildClusterView(partition, opData);
            const std::vector<uint32_t> cycle = findCyclePath(view.succs);
            if (cycle.empty())
            {
                return {};
            }

            std::ostringstream oss;
            oss << "cycle=";
            const std::size_t limit = std::min<std::size_t>(cycle.size(), 8);
            for (std::size_t i = 0; i < limit; ++i)
            {
                const uint32_t clusterId = cycle[i];
                if (i != 0)
                {
                    oss << " -> ";
                }
                oss << clusterId;
                if (clusterId < view.members.size() && !view.members[clusterId].empty())
                {
                    bool sinkOnly = true;
                    for (const auto topoPos : view.members[clusterId])
                    {
                        if (topoPos >= opData.topoSinkOnly.size() ||
                            opData.topoSinkOnly[topoPos] == 0)
                        {
                            sinkOnly = false;
                            break;
                        }
                    }
                    const uint32_t headTopo = view.members[clusterId].front();
                    const uint32_t tailTopo = view.members[clusterId].back();
                    oss << "(min=" << view.members[clusterId].front()
                        << ",max=" << view.members[clusterId].back()
                        << ",ops=" << view.members[clusterId].size()
                        << ",fixed=" << static_cast<uint32_t>(view.fixedBoundary[clusterId])
                        << ",sinkOnly=" << (sinkOnly ? "1" : "0")
                        << ",headKind=" << wolvrix::lib::grh::toString(opData.topoKinds[headTopo])
                        << ",tailKind=" << wolvrix::lib::grh::toString(opData.topoKinds[tailTopo]) << ")";
                }
            }
            if (cycle.size() > limit)
            {
                oss << " -> ...";
            }
            return oss.str();
        }

        constexpr std::size_t kCoarsenTailLargeClusterThreshold = 100000;
        constexpr std::size_t kCoarsenTailMaxClusterDeltaExclusive = 10;
        constexpr std::size_t kCoarsenTailMaxConsecutiveIters = 3;

        std::uint64_t elapsedMs(const std::chrono::steady_clock::time_point &start) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count());
        }

        struct DisjointSet
        {
            explicit DisjointSet(std::size_t count)
                : parent(count), size(count, 1)
            {
                std::iota(parent.begin(), parent.end(), 0);
            }

            uint32_t find(uint32_t node)
            {
                uint32_t root = node;
                while (parent[root] != root)
                {
                    root = parent[root];
                }
                while (parent[node] != node)
                {
                    const uint32_t next = parent[node];
                    parent[node] = root;
                    node = next;
                }
                return root;
            }

            bool unite(uint32_t lhs, uint32_t rhs)
            {
                lhs = find(lhs);
                rhs = find(rhs);
                if (lhs == rhs)
                {
                    return false;
                }
                if (size[lhs] < size[rhs])
                {
                    std::swap(lhs, rhs);
                }
                parent[rhs] = lhs;
                size[lhs] += size[rhs];
                return true;
            }

            std::vector<uint32_t> parent;
            std::vector<uint32_t> size;
        };

        ActivityOpData buildActivityOpData(const wolvrix::lib::grh::Graph &graph,
                                           std::string &error)
        {
            ActivityOpData data;

            for (const auto opId : graph.operations())
            {
                data.maxOpIndex = std::max<std::size_t>(data.maxOpIndex, opId.index);
            }

            std::vector<wolvrix::lib::grh::OperationId> eligibleOps;
            eligibleOps.reserve(graph.operations().size());
            for (const auto opId : graph.operations())
            {
                if (isPartitionableOpKind(graph.opKind(opId)))
                {
                    eligibleOps.push_back(opId);
                }
            }

            data.topoPosByOpIndex.assign(data.maxOpIndex + 1, kInvalidActivitySupernodeId);
            if (eligibleOps.empty())
            {
                return data;
            }

            std::vector<uint8_t> eligibleByOpIndex(data.maxOpIndex + 1, 0);
            for (const auto opId : eligibleOps)
            {
                eligibleByOpIndex[opId.index] = 1;
            }

            wolvrix::lib::toposort::TopoDag<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> topoDag;
            topoDag.reserveNodes(eligibleOps.size());
            for (const auto opId : eligibleOps)
            {
                topoDag.addNode(opId);
            }

            std::vector<std::pair<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationId>> opEdges;
            opEdges.reserve(eligibleOps.size() * 2);
            for (const auto opId : eligibleOps)
            {
                for (const auto operand : graph.opOperands(opId))
                {
                    const auto defOp = graph.valueDef(operand);
                    if (!defOp.valid())
                    {
                        continue;
                    }
                    if (defOp.index >= eligibleByOpIndex.size() || eligibleByOpIndex[defOp.index] == 0)
                    {
                        continue;
                    }
                    topoDag.addEdge(defOp, opId);
                    opEdges.emplace_back(defOp, opId);
                }
            }

            try
            {
                const auto layers = topoDag.toposort();
                for (const auto &layer : layers)
                {
                    data.topoOps.insert(data.topoOps.end(), layer.begin(), layer.end());
                }
            }
            catch (const std::exception &ex)
            {
                error = std::string("activity-schedule topo failed: ") + ex.what();
                return data;
            }

            if (data.topoOps.size() != eligibleOps.size())
            {
                error = "activity-schedule topo failed: combinational dependency cycle detected";
                data.topoOps.clear();
                return data;
            }

            data.topoSymbols.reserve(data.topoOps.size());
            data.topoKinds.reserve(data.topoOps.size());
            data.topoSinkOnly.reserve(data.topoOps.size());
            for (std::size_t pos = 0; pos < data.topoOps.size(); ++pos)
            {
                const auto opId = data.topoOps[pos];
                data.topoPosByOpIndex[opId.index] = static_cast<uint32_t>(pos);
                data.topoSymbols.push_back(graph.operationSymbol(opId));
                data.topoKinds.push_back(graph.opKind(opId));
                data.topoSinkOnly.push_back(isTailSinkOp(graph.getOperation(opId)) ? 1U : 0U);
            }

            data.topoEdges.reserve(opEdges.size());
            for (const auto &[srcOp, dstOp] : opEdges)
            {
                const uint32_t srcPos = data.topoPosByOpIndex[srcOp.index];
                const uint32_t dstPos = data.topoPosByOpIndex[dstOp.index];
                if (srcPos == kInvalidActivitySupernodeId || dstPos == kInvalidActivitySupernodeId)
                {
                    continue;
                }
                data.topoEdges.emplace_back(srcPos, dstPos);
            }
            std::sort(data.topoEdges.begin(), data.topoEdges.end());
            data.topoEdges.erase(std::unique(data.topoEdges.begin(), data.topoEdges.end()),
                                 data.topoEdges.end());

            return data;
        }

        WorkingPartition makeSeedPartition(const ActivityOpData &opData,
                                           const std::vector<uint8_t> *includeTopoPos = nullptr)
        {
            WorkingPartition partition;
            partition.clusters.reserve(opData.topoOps.size());
            partition.fixedBoundary.reserve(opData.topoOps.size());
            for (std::size_t pos = 0; pos < opData.topoOps.size(); ++pos)
            {
                if (includeTopoPos != nullptr &&
                    (pos >= includeTopoPos->size() || (*includeTopoPos)[pos] == 0))
                {
                    continue;
                }
                partition.clusters.push_back(std::vector<uint32_t>{static_cast<uint32_t>(pos)});
                partition.fixedBoundary.push_back(isSideEffectBoundaryKind(opData.topoKinds[pos]) ? 1U : 0U);
            }
            return partition;
        }

        std::vector<uint32_t> collectSinkTopoPositions(const wolvrix::lib::grh::Graph &graph,
                                                       const ActivityOpData &opData)
        {
            std::vector<uint32_t> sinks;
            sinks.reserve(opData.topoOps.size());
            for (std::size_t topoPos = 0; topoPos < opData.topoOps.size(); ++topoPos)
            {
                if (isTailSinkOp(graph.getOperation(opData.topoOps[topoPos])))
                {
                    sinks.push_back(static_cast<uint32_t>(topoPos));
                }
            }
            return sinks;
        }

        WorkingPartition buildChunkedPartitionFromTopoPositions(const std::vector<uint32_t> &topoPositions,
                                                                std::size_t maxSize)
        {
            WorkingPartition partition;
            if (topoPositions.empty())
            {
                return partition;
            }
            const std::size_t chunkSize = maxSize == 0 ? topoPositions.size() : maxSize;
            partition.clusters.reserve((topoPositions.size() + chunkSize - 1) / chunkSize);
            partition.fixedBoundary.reserve(partition.clusters.capacity());
            for (std::size_t begin = 0; begin < topoPositions.size(); begin += chunkSize)
            {
                const std::size_t end = std::min(begin + chunkSize, topoPositions.size());
                auto &cluster = partition.clusters.emplace_back();
                cluster.reserve(end - begin);
                for (std::size_t index = begin; index < end; ++index)
                {
                    cluster.push_back(topoPositions[index]);
                }
                partition.fixedBoundary.push_back(0);
            }
            return partition;
        }

        bool clusterMembersAreSinkOnly(const std::vector<uint32_t> &members,
                                       const ActivityOpData &opData) noexcept
        {
            if (members.empty())
            {
                return false;
            }
            for (const auto topoPos : members)
            {
                if (topoPos >= opData.topoSinkOnly.size() || opData.topoSinkOnly[topoPos] == 0)
                {
                    return false;
                }
            }
            return true;
        }

        void markSinkOnlyClustersFixedBoundary(WorkingPartition &partition,
                                               const ActivityOpData &opData)
        {
            if (partition.fixedBoundary.size() < partition.clusters.size())
            {
                partition.fixedBoundary.resize(partition.clusters.size(), 0U);
            }
            for (std::size_t clusterId = 0; clusterId < partition.clusters.size(); ++clusterId)
            {
                if (clusterMembersAreSinkOnly(partition.clusters[clusterId], opData))
                {
                    partition.fixedBoundary[clusterId] = 1U;
                }
            }
        }

        ClusterView buildClusterView(const WorkingPartition &partition, const ActivityOpData &opData)
        {
            ClusterView view;
            view.members = partition.clusters;
            view.fixedBoundary = partition.fixedBoundary;
            view.sinkOnly.resize(view.members.size(), 0U);
            view.preds.resize(view.members.size());
            view.succs.resize(view.members.size());
            view.clusterOfTopoPos.assign(opData.topoOps.size(), kInvalidActivitySupernodeId);

            for (std::size_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                auto &members = view.members[clusterId];
                std::sort(members.begin(), members.end());
                members.erase(std::unique(members.begin(), members.end()), members.end());
                view.sinkOnly[clusterId] = clusterMembersAreSinkOnly(members, opData) ? 1U : 0U;
                for (const auto topoPos : members)
                {
                    view.clusterOfTopoPos[topoPos] = static_cast<uint32_t>(clusterId);
                }
            }

            for (const auto &[srcPos, dstPos] : opData.topoEdges)
            {
                const uint32_t srcCluster = view.clusterOfTopoPos[srcPos];
                const uint32_t dstCluster = view.clusterOfTopoPos[dstPos];
                if (srcCluster == kInvalidActivitySupernodeId ||
                    dstCluster == kInvalidActivitySupernodeId ||
                    srcCluster == dstCluster)
                {
                    continue;
                }
                view.succs[srcCluster].push_back(dstCluster);
                view.preds[dstCluster].push_back(srcCluster);
            }

            for (auto &preds : view.preds)
            {
                std::sort(preds.begin(), preds.end());
                preds.erase(std::unique(preds.begin(), preds.end()), preds.end());
            }
            for (auto &succs : view.succs)
            {
                std::sort(succs.begin(), succs.end());
                succs.erase(std::unique(succs.begin(), succs.end()), succs.end());
            }
            return view;
        }

        void appendPartition(WorkingPartition &dst, const WorkingPartition &src)
        {
            dst.clusters.insert(dst.clusters.end(), src.clusters.begin(), src.clusters.end());
            dst.fixedBoundary.insert(dst.fixedBoundary.end(), src.fixedBoundary.begin(), src.fixedBoundary.end());
        }

        WorkingPartition splitPartitionIntoContiguousTopoRuns(const WorkingPartition &partition)
        {
            WorkingPartition out;
            out.clusters.reserve(partition.clusters.size());
            out.fixedBoundary.reserve(partition.fixedBoundary.size());

            for (std::size_t clusterId = 0; clusterId < partition.clusters.size(); ++clusterId)
            {
                if (partition.clusters[clusterId].empty())
                {
                    continue;
                }

                std::vector<uint32_t> members = partition.clusters[clusterId];
                std::sort(members.begin(), members.end());
                members.erase(std::unique(members.begin(), members.end()), members.end());

                std::vector<uint32_t> run;
                run.reserve(members.size());
                run.push_back(members.front());
                for (std::size_t index = 1; index < members.size(); ++index)
                {
                    if (members[index] != members[index - 1] + 1U)
                    {
                        out.clusters.push_back(std::move(run));
                        out.fixedBoundary.push_back(partition.fixedBoundary[clusterId]);
                        run = {};
                        run.reserve(members.size() - index);
                    }
                    run.push_back(members[index]);
                }
                out.clusters.push_back(std::move(run));
                out.fixedBoundary.push_back(partition.fixedBoundary[clusterId]);
            }

            return out;
        }

        WorkingPartition buildInitialPartition(const ActivityOpData &opData,
                                               const WorkingPartition &sinkPartition,
                                               const WorkingPartition &tailPartition)
        {
            WorkingPartition partition;
            partition.clusters.reserve(opData.topoOps.size());
            partition.fixedBoundary.reserve(opData.topoOps.size());

            std::vector<uint32_t> tailClusterOfTopoPos(opData.topoOps.size(), kInvalidActivitySupernodeId);
            for (std::size_t clusterId = 0; clusterId < tailPartition.clusters.size(); ++clusterId)
            {
                for (const auto topoPos : tailPartition.clusters[clusterId])
                {
                    if (topoPos < tailClusterOfTopoPos.size())
                    {
                        tailClusterOfTopoPos[topoPos] = static_cast<uint32_t>(clusterId);
                    }
                }
            }

            std::vector<uint32_t> sinkClusterOfTopoPos(opData.topoOps.size(), kInvalidActivitySupernodeId);
            for (std::size_t clusterId = 0; clusterId < sinkPartition.clusters.size(); ++clusterId)
            {
                for (const auto topoPos : sinkPartition.clusters[clusterId])
                {
                    if (topoPos < sinkClusterOfTopoPos.size())
                    {
                        sinkClusterOfTopoPos[topoPos] = static_cast<uint32_t>(clusterId);
                    }
                }
            }

            std::vector<uint8_t> emittedTail(tailPartition.clusters.size(), 0U);
            std::vector<uint8_t> emittedSink(sinkPartition.clusters.size(), 0U);
            for (std::size_t topoPos = 0; topoPos < opData.topoOps.size(); ++topoPos)
            {
                const uint32_t tailClusterId =
                    topoPos < tailClusterOfTopoPos.size() ? tailClusterOfTopoPos[topoPos] : kInvalidActivitySupernodeId;
                if (tailClusterId != kInvalidActivitySupernodeId)
                {
                    if (tailClusterId < tailPartition.clusters.size() && emittedTail[tailClusterId] == 0)
                    {
                        partition.clusters.push_back(tailPartition.clusters[tailClusterId]);
                        partition.fixedBoundary.push_back(tailPartition.fixedBoundary[tailClusterId]);
                        emittedTail[tailClusterId] = 1U;
                    }
                    continue;
                }

                const uint32_t sinkClusterId =
                    topoPos < sinkClusterOfTopoPos.size() ? sinkClusterOfTopoPos[topoPos] : kInvalidActivitySupernodeId;
                if (sinkClusterId != kInvalidActivitySupernodeId)
                {
                    if (sinkClusterId < sinkPartition.clusters.size() && emittedSink[sinkClusterId] == 0)
                    {
                        partition.clusters.push_back(sinkPartition.clusters[sinkClusterId]);
                        partition.fixedBoundary.push_back(sinkPartition.fixedBoundary[sinkClusterId]);
                        emittedSink[sinkClusterId] = 1U;
                    }
                    continue;
                }

                partition.clusters.push_back(std::vector<uint32_t>{static_cast<uint32_t>(topoPos)});
                partition.fixedBoundary.push_back(isSideEffectBoundaryKind(opData.topoKinds[topoPos]) ? 1U : 0U);
            }
            return partition;
        }

        void markCoveredTopoPositions(const WorkingPartition &partition, std::vector<uint8_t> &coveredTopoMask)
        {
            for (const auto &cluster : partition.clusters)
            {
                for (const auto topoPos : cluster)
                {
                    if (topoPos < coveredTopoMask.size())
                    {
                        coveredTopoMask[topoPos] = 1U;
                    }
                }
            }
        }

        std::vector<uint8_t> buildCoveredTopoMask(std::size_t topoCount,
                                                  const WorkingPartition &lhs,
                                                  const WorkingPartition &rhs)
        {
            std::vector<uint8_t> coveredTopoMask(topoCount, 0U);
            markCoveredTopoPositions(lhs, coveredTopoMask);
            markCoveredTopoPositions(rhs, coveredTopoMask);
            return coveredTopoMask;
        }

        std::vector<uint8_t> buildResidualTopoMask(std::size_t topoCount,
                                                   const WorkingPartition &sinkPartition,
                                                   const WorkingPartition &tailPartition)
        {
            std::vector<uint8_t> residualTopoMask(topoCount, 1U);
            const std::vector<uint8_t> coveredTopoMask =
                buildCoveredTopoMask(topoCount, sinkPartition, tailPartition);
            for (std::size_t topoPos = 0; topoPos < residualTopoMask.size(); ++topoPos)
            {
                if (coveredTopoMask[topoPos] != 0)
                {
                    residualTopoMask[topoPos] = 0U;
                }
            }
            return residualTopoMask;
        }

        std::vector<uint32_t> buildClusterIndexByTopoPos(const WorkingPartition &partition,
                                                         std::size_t topoCount)
        {
            std::vector<uint32_t> clusterOfTopoPos(topoCount, kInvalidActivitySupernodeId);
            for (std::size_t clusterId = 0; clusterId < partition.clusters.size(); ++clusterId)
            {
                for (const auto topoPos : partition.clusters[clusterId])
                {
                    if (topoPos < clusterOfTopoPos.size())
                    {
                        clusterOfTopoPos[topoPos] = static_cast<uint32_t>(clusterId);
                    }
                }
            }
            return clusterOfTopoPos;
        }

        struct ResidualConsumerInfo
        {
            std::vector<uint32_t> tailSupernodes;
            std::vector<uint32_t> sinkSupernodes;
        };

        ResidualConsumerInfo collectResidualConsumerSupernodes(const wolvrix::lib::grh::Graph &graph,
                                                               const ActivityOpData &opData,
                                                               uint32_t topoPos,
                                                               const std::vector<uint32_t> &sinkClusterOfTopoPos,
                                                               const std::vector<uint32_t> &tailClusterOfTopoPos)
        {
            ResidualConsumerInfo out;
            if (topoPos >= opData.topoOps.size())
            {
                return out;
            }

            const auto opId = opData.topoOps[topoPos];
            for (const auto result : graph.opResults(opId))
            {
                for (const auto user : graph.getValue(result).users())
                {
                    if (!user.operation.valid() || user.operation.index >= opData.topoPosByOpIndex.size())
                    {
                        continue;
                    }
                    const uint32_t userTopoPos = opData.topoPosByOpIndex[user.operation.index];
                    if (userTopoPos == kInvalidActivitySupernodeId)
                    {
                        continue;
                    }
                    const uint32_t sinkClusterId =
                        userTopoPos < sinkClusterOfTopoPos.size() ? sinkClusterOfTopoPos[userTopoPos]
                                                                  : kInvalidActivitySupernodeId;
                    if (sinkClusterId != kInvalidActivitySupernodeId)
                    {
                        out.sinkSupernodes.push_back(sinkClusterId);
                        continue;
                    }
                    const uint32_t tailClusterId =
                        userTopoPos < tailClusterOfTopoPos.size() ? tailClusterOfTopoPos[userTopoPos]
                                                                  : kInvalidActivitySupernodeId;
                    if (tailClusterId != kInvalidActivitySupernodeId)
                    {
                        out.tailSupernodes.push_back(tailClusterId);
                    }
                }
            }

            std::sort(out.tailSupernodes.begin(), out.tailSupernodes.end());
            out.tailSupernodes.erase(std::unique(out.tailSupernodes.begin(), out.tailSupernodes.end()),
                                     out.tailSupernodes.end());
            std::sort(out.sinkSupernodes.begin(), out.sinkSupernodes.end());
            out.sinkSupernodes.erase(std::unique(out.sinkSupernodes.begin(), out.sinkSupernodes.end()),
                                     out.sinkSupernodes.end());
            return out;
        }

        WorkingPartition buildTailPartition(const wolvrix::lib::grh::Graph &graph,
                                            const ActivityOpData &opData,
                                            const WorkingPartition &sinkPartition,
                                            TailPartitionStats *stats = nullptr)
        {
            WorkingPartition partition;
            if (opData.topoOps.empty())
            {
                return partition;
            }

            const std::vector<uint32_t> sinkClusterOfTopoPos =
                buildClusterIndexByTopoPos(sinkPartition, opData.topoOps.size());
            std::vector<uint32_t> tailClusterOfTopoPos(opData.topoOps.size(), kInvalidActivitySupernodeId);

            for (std::size_t revIndex = opData.topoOps.size(); revIndex > 0; --revIndex)
            {
                const uint32_t topoPos = static_cast<uint32_t>(revIndex - 1);
                if (topoPos >= sinkClusterOfTopoPos.size() ||
                    sinkClusterOfTopoPos[topoPos] != kInvalidActivitySupernodeId)
                {
                    continue;
                }
                if (topoPos >= tailClusterOfTopoPos.size() ||
                    tailClusterOfTopoPos[topoPos] != kInvalidActivitySupernodeId)
                {
                    continue;
                }

                const ResidualConsumerInfo consumers =
                    collectResidualConsumerSupernodes(graph,
                                                     opData,
                                                     topoPos,
                                                     sinkClusterOfTopoPos,
                                                     tailClusterOfTopoPos);
                const std::size_t tailConsumerCount = consumers.tailSupernodes.size();
                const std::size_t sinkConsumerCount = consumers.sinkSupernodes.size();
                const std::size_t totalConsumerCount = tailConsumerCount + sinkConsumerCount;

                if (tailConsumerCount == 1 && totalConsumerCount == 1)
                {
                    const uint32_t clusterId = consumers.tailSupernodes.front();
                    if (clusterId < partition.clusters.size())
                    {
                        partition.clusters[clusterId].push_back(topoPos);
                        tailClusterOfTopoPos[topoPos] = clusterId;
                        if (stats != nullptr)
                        {
                            ++stats->absorbedOps;
                        }
                    }
                    continue;
                }

                partition.clusters.push_back(std::vector<uint32_t>{topoPos});
                partition.fixedBoundary.push_back(0U);
                const uint32_t newClusterId = static_cast<uint32_t>(partition.clusters.size() - 1);
                tailClusterOfTopoPos[topoPos] = newClusterId;

                if (stats != nullptr)
                {
                    if (totalConsumerCount == 0 || (tailConsumerCount == 0 && sinkConsumerCount == 1))
                    {
                        ++stats->initialSeedOps;
                    }
                    else
                    {
                        ++stats->sharedSeedOps;
                    }
                }
            }

            return partition;
        }

        WorkingPartition rebuildPartitionFromDsu(const ClusterView &view,
                                                 DisjointSet &dsu,
                                                 const ActivityOpData &opData)
        {
            std::unordered_map<uint32_t, uint32_t> rootToNew;
            rootToNew.reserve(view.members.size());

            WorkingPartition out;
            for (uint32_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                const uint32_t root = dsu.find(clusterId);
                auto [it, inserted] = rootToNew.emplace(root, static_cast<uint32_t>(out.clusters.size()));
                if (inserted)
                {
                    out.clusters.push_back({});
                    out.fixedBoundary.push_back(0);
                }
                auto &dstMembers = out.clusters[it->second];
                dstMembers.insert(dstMembers.end(), view.members[clusterId].begin(), view.members[clusterId].end());
                out.fixedBoundary[it->second] =
                    static_cast<uint8_t>(out.fixedBoundary[it->second] || view.fixedBoundary[clusterId]);
            }

            for (auto &members : out.clusters)
            {
                std::sort(members.begin(), members.end());
                members.erase(std::unique(members.begin(), members.end()), members.end());
            }
            return canonicalizePartition(out, opData);
        }

        bool tryMergeOut1(WorkingPartition &partition,
                          const ActivityOpData &opData,
                          std::size_t maxSize)
        {
            const ClusterView view = buildClusterView(partition, opData);
            DisjointSet dsu(view.members.size());
            std::vector<uint32_t> mergedSize(view.members.size(), 0);
            for (std::size_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                mergedSize[clusterId] = static_cast<uint32_t>(view.members[clusterId].size());
            }

            bool changed = false;
            for (std::size_t idx = view.members.size(); idx > 0; --idx)
            {
                const uint32_t clusterId = static_cast<uint32_t>(idx - 1);
                if (view.fixedBoundary[clusterId] != 0 || view.succs[clusterId].size() != 1)
                {
                    continue;
                }
                const uint32_t succId = view.succs[clusterId].front();
                if (view.fixedBoundary[succId] != 0 || view.sinkOnly[clusterId] != view.sinkOnly[succId])
                {
                    continue;
                }
                uint32_t lhs = dsu.find(clusterId);
                uint32_t rhs = dsu.find(succId);
                if (lhs == rhs)
                {
                    continue;
                }
                if (static_cast<std::size_t>(mergedSize[lhs] + mergedSize[rhs]) > maxSize)
                {
                    continue;
                }
                if (dsu.unite(lhs, rhs))
                {
                    const uint32_t root = dsu.find(lhs);
                    mergedSize[root] = mergedSize[lhs] + mergedSize[rhs];
                    changed = true;
                }
            }
            if (!changed)
            {
                return false;
            }
            partition = rebuildPartitionFromDsu(view, dsu, opData);
            return true;
        }

        bool tryMergeIn1(WorkingPartition &partition,
                         const ActivityOpData &opData,
                         std::size_t maxSize)
        {
            const ClusterView view = buildClusterView(partition, opData);
            DisjointSet dsu(view.members.size());
            std::vector<uint32_t> mergedSize(view.members.size(), 0);
            for (std::size_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                mergedSize[clusterId] = static_cast<uint32_t>(view.members[clusterId].size());
            }

            bool changed = false;
            for (uint32_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                if (view.fixedBoundary[clusterId] != 0 || view.preds[clusterId].size() != 1)
                {
                    continue;
                }
                const uint32_t predId = view.preds[clusterId].front();
                if (view.fixedBoundary[predId] != 0 || view.sinkOnly[clusterId] != view.sinkOnly[predId])
                {
                    continue;
                }
                uint32_t lhs = dsu.find(clusterId);
                uint32_t rhs = dsu.find(predId);
                if (lhs == rhs)
                {
                    continue;
                }
                if (static_cast<std::size_t>(mergedSize[lhs] + mergedSize[rhs]) > maxSize)
                {
                    continue;
                }
                if (dsu.unite(lhs, rhs))
                {
                    const uint32_t root = dsu.find(lhs);
                    mergedSize[root] = mergedSize[lhs] + mergedSize[rhs];
                    changed = true;
                }
            }
            if (!changed)
            {
                return false;
            }
            partition = rebuildPartitionFromDsu(view, dsu, opData);
            return true;
        }

        bool tryMergeSiblings(WorkingPartition &partition,
                              const ActivityOpData &opData,
                              std::size_t maxSize)
        {
            const ClusterView view = buildClusterView(partition, opData);
            std::unordered_map<std::string, std::vector<uint32_t>> groups;
            groups.reserve(view.members.size());
            for (uint32_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                if (view.fixedBoundary[clusterId] != 0 || view.preds[clusterId].empty())
                {
                    continue;
                }
                std::ostringstream key;
                key << "p";
                for (const auto pred : view.preds[clusterId])
                {
                    key << '_' << pred;
                }
                groups[key.str()].push_back(clusterId);
            }

            DisjointSet dsu(view.members.size());
            std::vector<uint32_t> mergedSize(view.members.size(), 0);
            for (std::size_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                mergedSize[clusterId] = static_cast<uint32_t>(view.members[clusterId].size());
            }

            bool changed = false;
            for (auto &[_, siblings] : groups)
            {
                if (siblings.size() < 2)
                {
                    continue;
                }
                uint32_t anchor = siblings.front();
                for (std::size_t idx = 1; idx < siblings.size(); ++idx)
                {
                    const uint32_t candidate = siblings[idx];
                    uint32_t lhs = dsu.find(anchor);
                    uint32_t rhs = dsu.find(candidate);
                    if (lhs == rhs)
                    {
                        continue;
                    }
                    if (view.sinkOnly[lhs] != view.sinkOnly[rhs])
                    {
                        continue;
                    }
                    if (static_cast<std::size_t>(mergedSize[lhs] + mergedSize[rhs]) > maxSize)
                    {
                        continue;
                    }
                    if (dsu.unite(lhs, rhs))
                    {
                        const uint32_t root = dsu.find(lhs);
                        mergedSize[root] = mergedSize[lhs] + mergedSize[rhs];
                        anchor = root;
                        changed = true;
                    }
                }
            }
            if (!changed)
            {
                return false;
            }
            partition = rebuildPartitionFromDsu(view, dsu, opData);
            return true;
        }

        bool isForwarderKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kAssign:
            case wolvrix::lib::grh::OperationKind::kConcat:
            case wolvrix::lib::grh::OperationKind::kSliceStatic:
            case wolvrix::lib::grh::OperationKind::kSliceDynamic:
                return true;
            default:
                return false;
            }
        }

        bool tryMergeForwarders(WorkingPartition &partition,
                                const ActivityOpData &opData,
                                std::size_t maxSize)
        {
            const ClusterView view = buildClusterView(partition, opData);
            DisjointSet dsu(view.members.size());
            std::vector<uint32_t> mergedSize(view.members.size(), 0);
            for (std::size_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                mergedSize[clusterId] = static_cast<uint32_t>(view.members[clusterId].size());
            }

            bool changed = false;
            for (uint32_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                if (view.fixedBoundary[clusterId] != 0 || view.members[clusterId].size() != 1)
                {
                    continue;
                }
                const uint32_t topoPos = view.members[clusterId].front();
                if (!isForwarderKind(opData.topoKinds[topoPos]))
                {
                    continue;
                }

                std::optional<uint32_t> mergeTarget;
                if (view.succs[clusterId].size() == 1 && view.fixedBoundary[view.succs[clusterId].front()] == 0)
                {
                    mergeTarget = view.succs[clusterId].front();
                }
                else if (view.preds[clusterId].size() == 1 && view.fixedBoundary[view.preds[clusterId].front()] == 0)
                {
                    mergeTarget = view.preds[clusterId].front();
                }
                if (!mergeTarget)
                {
                    continue;
                }
                if (view.sinkOnly[clusterId] != view.sinkOnly[*mergeTarget])
                {
                    continue;
                }

                uint32_t lhs = dsu.find(clusterId);
                uint32_t rhs = dsu.find(*mergeTarget);
                if (lhs == rhs)
                {
                    continue;
                }
                if (static_cast<std::size_t>(mergedSize[lhs] + mergedSize[rhs]) > maxSize)
                {
                    continue;
                }
                if (dsu.unite(lhs, rhs))
                {
                    const uint32_t root = dsu.find(lhs);
                    mergedSize[root] = mergedSize[lhs] + mergedSize[rhs];
                    changed = true;
                }
            }
            if (!changed)
            {
                return false;
            }
            partition = rebuildPartitionFromDsu(view, dsu, opData);
            return true;
        }

        std::vector<std::vector<uint32_t>> buildDpSegments(const ClusterView &view,
                                                           std::size_t maxSize)
        {
            const std::size_t count = view.members.size();
            std::vector<int> bestCost(count + 1, std::numeric_limits<int>::max());
            std::vector<int> backtrace(count + 1, -1);
            bestCost[0] = 0;

            for (std::size_t begin = 0; begin < count; ++begin)
            {
                if (bestCost[begin] == std::numeric_limits<int>::max())
                {
                    continue;
                }

                std::size_t accumSize = 0;
                int cutCost = 0;
                bool fixedSingleton = false;
                std::optional<uint8_t> segmentSinkOnly;
                for (std::size_t end = begin + 1; end <= count; ++end)
                {
                    const std::size_t clusterId = end - 1;
                    const uint8_t clusterSinkOnly = clusterId < view.sinkOnly.size() ? view.sinkOnly[clusterId] : 0U;
                    if (!segmentSinkOnly.has_value())
                    {
                        segmentSinkOnly = clusterSinkOnly;
                    }
                    else if (*segmentSinkOnly != clusterSinkOnly)
                    {
                        break;
                    }
                    if (view.fixedBoundary[clusterId] != 0)
                    {
                        if (clusterId != begin)
                        {
                            break;
                        }
                        fixedSingleton = true;
                    }
                    else if (fixedSingleton)
                    {
                        break;
                    }

                    const std::size_t clusterSize = view.members[clusterId].size();
                    accumSize += clusterSize;
                    bool oversizedSingleton = false;
                    if (accumSize > maxSize)
                    {
                        if (clusterId != begin)
                        {
                            break;
                        }
                        oversizedSingleton = true;
                    }

                    cutCost += static_cast<int>(view.succs[clusterId].size());
                    for (const auto pred : view.preds[clusterId])
                    {
                        if (pred >= begin && pred < clusterId)
                        {
                            --cutCost;
                        }
                    }

                    const int newCost = bestCost[begin] + cutCost;
                    if (newCost < bestCost[end] ||
                        (newCost == bestCost[end] && (backtrace[end] < 0 || static_cast<int>(begin) < backtrace[end])))
                    {
                        bestCost[end] = newCost;
                        backtrace[end] = static_cast<int>(begin);
                    }
                    if (oversizedSingleton)
                    {
                        break;
                    }
                }
            }

            if (backtrace[count] < 0)
            {
                std::vector<std::vector<uint32_t>> fallback;
                fallback.reserve(count);
                for (uint32_t clusterId = 0; clusterId < count; ++clusterId)
                {
                    fallback.push_back(std::vector<uint32_t>{clusterId});
                }
                return fallback;
            }

            std::vector<std::vector<uint32_t>> segments;
            int end = static_cast<int>(count);
            while (end > 0)
            {
                const int begin = backtrace[end];
                std::vector<uint32_t> segment;
                segment.reserve(static_cast<std::size_t>(end - begin));
                for (int clusterId = begin; clusterId < end; ++clusterId)
                {
                    segment.push_back(static_cast<uint32_t>(clusterId));
                }
                segments.push_back(std::move(segment));
                end = begin;
            }
            std::reverse(segments.begin(), segments.end());
            return segments;
        }

        std::size_t segmentSize(const ClusterView &view,
                                const std::vector<uint32_t> &segment)
        {
            std::size_t size = 0;
            for (const auto clusterId : segment)
            {
                size += view.members[clusterId].size();
            }
            return size;
        }

        int computeMoveGain(const ClusterView &view,
                            const std::vector<uint32_t> &ownerByCluster,
                            uint32_t clusterId,
                            uint32_t oldSegment,
                            uint32_t newSegment)
        {
            int gain = 0;
            for (const auto pred : view.preds[clusterId])
            {
                const int oldCut = ownerByCluster[pred] == oldSegment ? 0 : 1;
                const int newCut = ownerByCluster[pred] == newSegment ? 0 : 1;
                gain += oldCut - newCut;
            }
            for (const auto succ : view.succs[clusterId])
            {
                const int oldCut = ownerByCluster[succ] == oldSegment ? 0 : 1;
                const int newCut = ownerByCluster[succ] == newSegment ? 0 : 1;
                gain += oldCut - newCut;
            }
            return gain;
        }

        std::vector<std::vector<uint32_t>> refineSegments(const ClusterView &view,
                                                          std::vector<std::vector<uint32_t>> segments,
                                                          std::size_t maxSize,
                                                          std::size_t maxIter)
        {
            if (segments.size() < 2)
            {
                return segments;
            }

            struct Move
            {
                int gain = 0;
                std::size_t boundary = 0;
                bool rightToLeft = false;
            };

            for (std::size_t iter = 0; iter < maxIter; ++iter)
            {
                std::vector<uint32_t> ownerByCluster(view.members.size(), kInvalidActivitySupernodeId);
                for (std::size_t segmentId = 0; segmentId < segments.size(); ++segmentId)
                {
                    for (const auto clusterId : segments[segmentId])
                    {
                        ownerByCluster[clusterId] = static_cast<uint32_t>(segmentId);
                    }
                }

                std::vector<std::size_t> segmentSizes(segments.size(), 0);
                for (std::size_t segmentId = 0; segmentId < segments.size(); ++segmentId)
                {
                    segmentSizes[segmentId] = segmentSize(view, segments[segmentId]);
                }

                Move bestMove;
                for (std::size_t boundary = 0; boundary + 1 < segments.size(); ++boundary)
                {
                    auto &left = segments[boundary];
                    auto &right = segments[boundary + 1];

                    if (left.size() > 1)
                    {
                        const uint32_t clusterId = left.back();
                        const std::size_t clusterSize = view.members[clusterId].size();
                        if (view.fixedBoundary[clusterId] == 0 &&
                            !right.empty() &&
                            view.sinkOnly[clusterId] == view.sinkOnly[right.front()] &&
                            segmentSizes[boundary + 1] + clusterSize <= maxSize)
                        {
                            const int gain = computeMoveGain(view,
                                                             ownerByCluster,
                                                             clusterId,
                                                             static_cast<uint32_t>(boundary),
                                                             static_cast<uint32_t>(boundary + 1));
                            if (gain > bestMove.gain)
                            {
                                bestMove = Move{gain, boundary, false};
                            }
                        }
                    }

                    if (right.size() > 1)
                    {
                        const uint32_t clusterId = right.front();
                        const std::size_t clusterSize = view.members[clusterId].size();
                        if (view.fixedBoundary[clusterId] == 0 &&
                            !left.empty() &&
                            view.sinkOnly[clusterId] == view.sinkOnly[left.back()] &&
                            segmentSizes[boundary] + clusterSize <= maxSize)
                        {
                            const int gain = computeMoveGain(view,
                                                             ownerByCluster,
                                                             clusterId,
                                                             static_cast<uint32_t>(boundary + 1),
                                                             static_cast<uint32_t>(boundary));
                            if (gain > bestMove.gain)
                            {
                                bestMove = Move{gain, boundary, true};
                            }
                        }
                    }
                }

                if (bestMove.gain <= 0)
                {
                    break;
                }

                auto &left = segments[bestMove.boundary];
                auto &right = segments[bestMove.boundary + 1];
                if (bestMove.rightToLeft)
                {
                    left.push_back(right.front());
                    right.erase(right.begin());
                }
                else
                {
                    right.insert(right.begin(), left.back());
                    left.pop_back();
                }
            }

            return segments;
        }

        WorkingPartition materializeSegments(const ClusterView &view,
                                            const std::vector<std::vector<uint32_t>> &segments)
        {
            WorkingPartition out;
            out.clusters.reserve(segments.size());
            out.fixedBoundary.reserve(segments.size());
            for (const auto &segment : segments)
            {
                std::vector<uint32_t> members;
                uint8_t fixed = 0;
                for (const auto clusterId : segment)
                {
                    members.insert(members.end(),
                                   view.members[clusterId].begin(),
                                   view.members[clusterId].end());
                    fixed = static_cast<uint8_t>(fixed || view.fixedBoundary[clusterId]);
                }
                std::sort(members.begin(), members.end());
                out.clusters.push_back(std::move(members));
                out.fixedBoundary.push_back(fixed);
            }
            return out;
        }

        SymbolPartition buildSymbolPartition(const WorkingPartition &partition,
                                             const ActivityOpData &opData)
        {
            SymbolPartition out;
            out.clusters.resize(partition.clusters.size());
            out.fixedBoundary = partition.fixedBoundary;
            for (std::size_t clusterId = 0; clusterId < partition.clusters.size(); ++clusterId)
            {
                auto &symbols = out.clusters[clusterId];
                symbols.reserve(partition.clusters[clusterId].size());
                for (const auto topoPos : partition.clusters[clusterId])
                {
                    symbols.push_back(opData.topoSymbols[topoPos]);
                }
            }
            return out;
        }

        WorkingPartition rebuildWorkingPartitionFromSymbolPartition(const SymbolPartition &partition,
                                                                   const ActivityOpData &opData)
        {
            WorkingPartition out;
            out.clusters.reserve(partition.clusters.size());
            out.fixedBoundary.reserve(partition.fixedBoundary.size());

            std::unordered_map<wolvrix::lib::grh::SymbolId, uint32_t, ActivityScheduleSymbolIdHash> topoPosBySymbol;
            topoPosBySymbol.reserve(opData.topoSymbols.size());
            for (std::size_t topoPos = 0; topoPos < opData.topoSymbols.size(); ++topoPos)
            {
                topoPosBySymbol.emplace(opData.topoSymbols[topoPos], static_cast<uint32_t>(topoPos));
            }

            for (std::size_t clusterId = 0; clusterId < partition.clusters.size(); ++clusterId)
            {
                std::vector<uint32_t> members;
                members.reserve(partition.clusters[clusterId].size());
                for (const auto symbol : partition.clusters[clusterId])
                {
                    const auto it = topoPosBySymbol.find(symbol);
                    if (it == topoPosBySymbol.end())
                    {
                        continue;
                    }
                    members.push_back(it->second);
                }
                std::sort(members.begin(), members.end());
                members.erase(std::unique(members.begin(), members.end()), members.end());
                if (members.empty())
                {
                    continue;
                }
                out.clusters.push_back(std::move(members));
                out.fixedBoundary.push_back(
                    clusterId < partition.fixedBoundary.size() ? partition.fixedBoundary[clusterId] : 0U);
            }

            return out;
        }

        bool isReplicationCandidateKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kConstant:
            case wolvrix::lib::grh::OperationKind::kEq:
            case wolvrix::lib::grh::OperationKind::kNe:
            case wolvrix::lib::grh::OperationKind::kCaseEq:
            case wolvrix::lib::grh::OperationKind::kCaseNe:
            case wolvrix::lib::grh::OperationKind::kWildcardEq:
            case wolvrix::lib::grh::OperationKind::kWildcardNe:
            case wolvrix::lib::grh::OperationKind::kLt:
            case wolvrix::lib::grh::OperationKind::kLe:
            case wolvrix::lib::grh::OperationKind::kGt:
            case wolvrix::lib::grh::OperationKind::kGe:
            case wolvrix::lib::grh::OperationKind::kAnd:
            case wolvrix::lib::grh::OperationKind::kOr:
            case wolvrix::lib::grh::OperationKind::kXor:
            case wolvrix::lib::grh::OperationKind::kXnor:
            case wolvrix::lib::grh::OperationKind::kNot:
            case wolvrix::lib::grh::OperationKind::kLogicAnd:
            case wolvrix::lib::grh::OperationKind::kLogicOr:
            case wolvrix::lib::grh::OperationKind::kLogicNot:
            case wolvrix::lib::grh::OperationKind::kReduceAnd:
            case wolvrix::lib::grh::OperationKind::kReduceOr:
            case wolvrix::lib::grh::OperationKind::kReduceXor:
            case wolvrix::lib::grh::OperationKind::kReduceNor:
            case wolvrix::lib::grh::OperationKind::kReduceNand:
            case wolvrix::lib::grh::OperationKind::kReduceXnor:
            case wolvrix::lib::grh::OperationKind::kShl:
            case wolvrix::lib::grh::OperationKind::kLShr:
            case wolvrix::lib::grh::OperationKind::kAShr:
            case wolvrix::lib::grh::OperationKind::kMux:
            case wolvrix::lib::grh::OperationKind::kAssign:
            case wolvrix::lib::grh::OperationKind::kConcat:
            case wolvrix::lib::grh::OperationKind::kReplicate:
            case wolvrix::lib::grh::OperationKind::kSliceStatic:
            case wolvrix::lib::grh::OperationKind::kSliceDynamic:
            case wolvrix::lib::grh::OperationKind::kAdd:
            case wolvrix::lib::grh::OperationKind::kSub:
                return true;
            default:
                return false;
            }
        }

        int replicationCost(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kConstant:
                return 0;
            case wolvrix::lib::grh::OperationKind::kAssign:
            case wolvrix::lib::grh::OperationKind::kNot:
            case wolvrix::lib::grh::OperationKind::kLogicNot:
            case wolvrix::lib::grh::OperationKind::kReduceAnd:
            case wolvrix::lib::grh::OperationKind::kReduceOr:
            case wolvrix::lib::grh::OperationKind::kReduceXor:
            case wolvrix::lib::grh::OperationKind::kReduceNor:
            case wolvrix::lib::grh::OperationKind::kReduceNand:
            case wolvrix::lib::grh::OperationKind::kReduceXnor:
            case wolvrix::lib::grh::OperationKind::kSliceStatic:
            case wolvrix::lib::grh::OperationKind::kSliceDynamic:
            case wolvrix::lib::grh::OperationKind::kReplicate:
                return 1;
            case wolvrix::lib::grh::OperationKind::kEq:
            case wolvrix::lib::grh::OperationKind::kNe:
            case wolvrix::lib::grh::OperationKind::kCaseEq:
            case wolvrix::lib::grh::OperationKind::kCaseNe:
            case wolvrix::lib::grh::OperationKind::kWildcardEq:
            case wolvrix::lib::grh::OperationKind::kWildcardNe:
            case wolvrix::lib::grh::OperationKind::kLt:
            case wolvrix::lib::grh::OperationKind::kLe:
            case wolvrix::lib::grh::OperationKind::kGt:
            case wolvrix::lib::grh::OperationKind::kGe:
            case wolvrix::lib::grh::OperationKind::kAnd:
            case wolvrix::lib::grh::OperationKind::kOr:
            case wolvrix::lib::grh::OperationKind::kXor:
            case wolvrix::lib::grh::OperationKind::kXnor:
            case wolvrix::lib::grh::OperationKind::kLogicAnd:
            case wolvrix::lib::grh::OperationKind::kLogicOr:
            case wolvrix::lib::grh::OperationKind::kShl:
            case wolvrix::lib::grh::OperationKind::kLShr:
            case wolvrix::lib::grh::OperationKind::kAShr:
            case wolvrix::lib::grh::OperationKind::kMux:
            case wolvrix::lib::grh::OperationKind::kConcat:
            case wolvrix::lib::grh::OperationKind::kAdd:
            case wolvrix::lib::grh::OperationKind::kSub:
                return 2;
            default:
                return -1;
            }
        }

        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash>
        collectObservableBoundaryValues(const wolvrix::lib::grh::Graph &graph)
        {
            std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> values;
            values.reserve(graph.outputPorts().size() + graph.inoutPorts().size() * 2);
            for (const auto &port : graph.outputPorts())
            {
                values.insert(port.value);
            }
            for (const auto &port : graph.inoutPorts())
            {
                values.insert(port.out);
                values.insert(port.oe);
            }
            return values;
        }

        std::unordered_map<wolvrix::lib::grh::SymbolId, uint32_t, ActivityScheduleSymbolIdHash>
        buildSymbolToSupernodeMap(const SymbolPartition &partition)
        {
            std::unordered_map<wolvrix::lib::grh::SymbolId, uint32_t, ActivityScheduleSymbolIdHash> out;
            std::size_t total = 0;
            for (const auto &cluster : partition.clusters)
            {
                total += cluster.size();
            }
            out.reserve(total);
            for (std::size_t supernodeId = 0; supernodeId < partition.clusters.size(); ++supernodeId)
            {
                for (const auto symbol : partition.clusters[supernodeId])
                {
                    out[symbol] = static_cast<uint32_t>(supernodeId);
                }
            }
            return out;
        }

        bool isTailAbsorbableStateReadKind(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            return kind == wolvrix::lib::grh::OperationKind::kRegisterReadPort ||
                   kind == wolvrix::lib::grh::OperationKind::kLatchReadPort;
        }

        bool runStateReadTailAbsorbPhase(
            wolvrix::lib::grh::Graph &graph,
            SymbolPartition &partition,
            std::size_t maxTargets,
            StateReadTailAbsorbStats &stats,
            std::string &error)
        {
            if (partition.clusters.empty() || maxTargets == 0)
            {
                return false;
            }

            const auto observableValues = collectObservableBoundaryValues(graph);
            auto symbolToSupernode = buildSymbolToSupernodeMap(partition);
            std::vector<wolvrix::lib::grh::OperationId> candidates;
            candidates.reserve(graph.operations().size());
            for (const auto opId : graph.operations())
            {
                const auto op = graph.getOperation(opId);
                if (isTailAbsorbableStateReadKind(op.kind()))
                {
                    candidates.push_back(opId);
                }
            }

            bool changed = false;
            for (const auto opId : candidates)
            {
                const auto op = graph.getOperation(opId);
                if (!isTailAbsorbableStateReadKind(op.kind()) || op.results().size() != 1)
                {
                    continue;
                }

                const auto opSym = graph.operationSymbol(opId);
                const auto ownerIt = symbolToSupernode.find(opSym);
                if (ownerIt == symbolToSupernode.end())
                {
                    continue;
                }
                const uint32_t ownerSupernode = ownerIt->second;
                const auto result = op.results().front();

                bool mustKeepOriginal = observableValues.find(result) != observableValues.end();
                std::size_t localUserCount = 0;
                std::unordered_map<uint32_t, std::vector<wolvrix::lib::grh::ValueUser>> usersByTarget;
                std::vector<wolvrix::lib::grh::ValueUser> resultUsers;
                try
                {
                    const auto valueInfo = graph.getValue(result);
                    resultUsers.assign(valueInfo.users().begin(), valueInfo.users().end());
                }
                catch (const std::exception &ex)
                {
                    error = "activity-schedule state-read-tail-absorb getValue/users failed source=" +
                            describeOp(graph, opId) + ": " + ex.what();
                    return false;
                }

                for (const auto user : resultUsers)
                {
                    const auto userSym = graph.operationSymbol(user.operation);
                    const auto targetIt = symbolToSupernode.find(userSym);
                    if (targetIt == symbolToSupernode.end())
                    {
                        mustKeepOriginal = true;
                        continue;
                    }
                    if (targetIt->second == ownerSupernode)
                    {
                        ++localUserCount;
                        continue;
                    }
                    if (targetIt->second < partition.fixedBoundary.size() &&
                        partition.fixedBoundary[targetIt->second] != 0)
                    {
                        mustKeepOriginal = true;
                        continue;
                    }
                    usersByTarget[targetIt->second].push_back(user);
                }

                if (usersByTarget.empty())
                {
                    if (mustKeepOriginal)
                    {
                        ++stats.keptObservableReads;
                    }
                    else if (localUserCount != 0)
                    {
                        ++stats.keptLocalReads;
                    }
                    continue;
                }
                if (usersByTarget.size() > maxTargets)
                {
                    ++stats.skippedTooManyTargets;
                    continue;
                }

                std::optional<wolvrix::lib::grh::Value> resultInfo;
                try
                {
                    resultInfo = graph.getValue(result);
                }
                catch (const std::exception &ex)
                {
                    error = "activity-schedule state-read-tail-absorb getValue(resultInfo) failed source=" +
                            describeOp(graph, opId) + ": " + ex.what();
                    return false;
                }

                for (const auto &[targetSupernode, users] : usersByTarget)
                {
                    if (users.empty())
                    {
                        continue;
                    }

                    const auto cloneSym = graph.makeInternalOpSym();
                    const auto cloneOp = graph.createOperation(op.kind(), cloneSym);
                    if (op.srcLoc())
                    {
                        graph.setOpSrcLoc(cloneOp, *op.srcLoc());
                    }
                    for (const auto &attr : op.attrs())
                    {
                        graph.setAttr(cloneOp, attr.key, attr.value);
                    }
                    for (const auto operand : op.operands())
                    {
                        graph.addOperand(cloneOp, operand);
                    }

                    const auto cloneResult = graph.createValue(graph.makeInternalValSym(),
                                                               resultInfo->width(),
                                                               resultInfo->isSigned(),
                                                               resultInfo->type());
                    if (resultInfo->srcLoc())
                    {
                        graph.setValueSrcLoc(cloneResult, *resultInfo->srcLoc());
                    }
                    graph.addResult(cloneOp, cloneResult);

                    for (const auto user : users)
                    {
                        try
                        {
                            const auto userOp = graph.getOperation(user.operation);
                            const auto userOperands = userOp.operands();
                            if (user.operandIndex >= userOperands.size() || userOperands[user.operandIndex] != result)
                            {
                                std::ostringstream oss;
                                oss << "activity-schedule state-read-tail-absorb detected stale user edge: source="
                                    << describeOp(graph, opId) << " result=" << result.index
                                    << " targetSupernode=" << targetSupernode
                                    << " user=" << describeOp(graph, user.operation)
                                    << " operandIndex=" << user.operandIndex;
                                if (user.operandIndex < userOperands.size())
                                {
                                    oss << " currentOperand=" << userOperands[user.operandIndex].index;
                                }
                                error = oss.str();
                                return false;
                            }
                            graph.replaceOperand(user.operation, user.operandIndex, cloneResult);
                        }
                        catch (const std::exception &ex)
                        {
                            error = "activity-schedule state-read-tail-absorb replaceOperand failed source=" +
                                    describeOp(graph, opId) +
                                    " targetSupernode=" + std::to_string(targetSupernode) +
                                    " userOpIndex=" + std::to_string(user.operation.index) +
                                    " operandIndex=" + std::to_string(user.operandIndex) +
                                    ": " + ex.what();
                            return false;
                        }
                    }

                    partition.clusters[targetSupernode].push_back(cloneSym);
                    symbolToSupernode.emplace(cloneSym, targetSupernode);
                    ++stats.clonedOps;
                    changed = true;
                }

                if (mustKeepOriginal)
                {
                    ++stats.keptObservableReads;
                    continue;
                }
                if (localUserCount != 0)
                {
                    ++stats.keptLocalReads;
                    continue;
                }

                if (!graph.eraseOp(opId))
                {
                    error = "activity-schedule state-read-tail-absorb failed to erase source op: " +
                            describeOp(graph, opId);
                    return false;
                }
                auto &ownerCluster = partition.clusters[ownerSupernode];
                ownerCluster.erase(std::remove(ownerCluster.begin(), ownerCluster.end(), opSym),
                                   ownerCluster.end());
                symbolToSupernode.erase(opSym);
                ++stats.erasedOps;
                changed = true;
            }

            return changed;
        }

        WorkingPartition buildStateReadTailAbsorbTargetPartition(const WorkingPartition &seedPartition,
                                                                 const ActivityOpData &opData,
                                                                 const ActivityScheduleOptions &options)
        {
            WorkingPartition partition = seedPartition;
            if (partition.clusters.empty())
            {
                return partition;
            }

            if (options.enableCoarsen)
            {
                std::size_t tailIterations = 0;
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    const std::size_t clustersBeforeIter = partition.clusters.size();
                    if (options.enableChainMerge)
                    {
                        changed = tryMergeOut1(partition, opData, options.supernodeMaxSize) || changed;
                        changed = tryMergeIn1(partition, opData, options.supernodeMaxSize) || changed;
                    }
                    if (options.enableSiblingMerge)
                    {
                        changed = tryMergeSiblings(partition, opData, options.supernodeMaxSize) || changed;
                    }
                    if (options.enableForwardMerge)
                    {
                        changed = tryMergeForwarders(partition, opData, options.supernodeMaxSize) || changed;
                    }

                    const std::size_t clustersAfterIter = partition.clusters.size();
                    const std::size_t clusterDelta =
                        clustersBeforeIter >= clustersAfterIter ? (clustersBeforeIter - clustersAfterIter) : 0;
                    const bool smallDeltaTail =
                        clustersBeforeIter >= kCoarsenTailLargeClusterThreshold &&
                        clusterDelta < kCoarsenTailMaxClusterDeltaExclusive;
                    if (smallDeltaTail)
                    {
                        ++tailIterations;
                    }
                    else
                    {
                        tailIterations = 0;
                    }
                    if (tailIterations >= kCoarsenTailMaxConsecutiveIters)
                    {
                        break;
                    }
                }
            }

            partition = canonicalizePartition(partition, opData);
            markSinkOnlyClustersFixedBoundary(partition, opData);
            const ClusterView coarseView = buildClusterView(partition, opData);
            const ClusterView dpView = buildTopoOrderedClusterView(coarseView);
            std::vector<std::vector<uint32_t>> segments = buildDpSegments(dpView, options.supernodeMaxSize);
            if (options.enableRefine)
            {
                segments = refineSegments(dpView, std::move(segments), options.supernodeMaxSize, options.refineMaxIter);
            }
            partition = materializeSegments(dpView, segments);
            partition = canonicalizePartition(partition, opData);
            markSinkOnlyClustersFixedBoundary(partition, opData);
            return partition;
        }

        bool runReplicationPhase(wolvrix::lib::grh::Graph &graph,
                                 SymbolPartition &partition,
                                 const ActivityOpData &opData,
                                 std::size_t maxCost,
                                 std::size_t maxTargets,
                                 ReplicationStats &stats,
                                 ReplicationPerfStats *perf,
                                 std::string &error)
        {
            if (partition.clusters.empty() || maxTargets == 0)
            {
                return false;
            }

            const auto observableStart = std::chrono::steady_clock::now();
            const auto observableValues = collectObservableBoundaryValues(graph);
            if (perf)
            {
                perf->collectObservableMs = elapsedMs(observableStart);
            }
            const auto symbolMapStart = std::chrono::steady_clock::now();
            auto symbolToSupernode = buildSymbolToSupernodeMap(partition);
            if (perf)
            {
                perf->buildSymbolMapMs = elapsedMs(symbolMapStart);
            }
            bool changed = false;
            const auto scanStart = std::chrono::steady_clock::now();

            for (std::size_t revPos = opData.topoSymbols.size(); revPos > 0; --revPos)
            {
                if (perf)
                {
                    ++perf->scannedOps;
                }
                const auto opSym = opData.topoSymbols[revPos - 1];
                const auto ownerIt = symbolToSupernode.find(opSym);
                if (ownerIt == symbolToSupernode.end())
                {
                    continue;
                }
                const uint32_t ownerSupernode = ownerIt->second;
                if (ownerSupernode >= partition.fixedBoundary.size() ||
                    partition.fixedBoundary[ownerSupernode] != 0)
                {
                    continue;
                }

                const auto opId = graph.findOperation(opSym);
                if (!opId.valid())
                {
                    continue;
                }
                std::optional<wolvrix::lib::grh::Operation> op;
                try
                {
                    op = graph.getOperation(opId);
                }
                catch (const std::exception &ex)
                {
                    error = "activity-schedule replication getOperation failed symbol=" +
                            std::string(graph.symbolText(opSym)) + ": " + ex.what();
                    return false;
                }
                if (!isReplicationCandidateKind(op->kind()) || op->results().size() != 1)
                {
                    continue;
                }
                const int cost = replicationCost(op->kind());
                if (cost < 0 || static_cast<std::size_t>(cost) > maxCost)
                {
                    continue;
                }
                if (perf)
                {
                    ++perf->candidateOps;
                }

                const auto result = op->results().front();
                std::unordered_map<uint32_t, std::vector<wolvrix::lib::grh::ValueUser>> usersByTarget;
                usersByTarget.reserve(4);
                std::vector<wolvrix::lib::grh::ValueUser> resultUsers;
                try
                {
                    const auto resultValue = graph.getValue(result);
                    resultUsers.assign(resultValue.users().begin(), resultValue.users().end());
                }
                catch (const std::exception &ex)
                {
                    error = "activity-schedule replication getValue/users failed source=" +
                            describeOp(graph, opId) + ": " + ex.what();
                    return false;
                }
                for (const auto user : resultUsers)
                {
                    if (perf)
                    {
                        ++perf->userEdgesVisited;
                    }
                    const auto userSym = graph.operationSymbol(user.operation);
                    const auto userIt = symbolToSupernode.find(userSym);
                    if (userIt == symbolToSupernode.end())
                    {
                        continue;
                    }
                    const uint32_t targetSupernode = userIt->second;
                    if (targetSupernode == ownerSupernode ||
                        targetSupernode >= partition.fixedBoundary.size() ||
                        partition.fixedBoundary[targetSupernode] != 0)
                    {
                        continue;
                    }
                    usersByTarget[targetSupernode].push_back(user);
                }
                if (usersByTarget.empty() || usersByTarget.size() > maxTargets)
                {
                    continue;
                }
                if (perf)
                {
                    perf->targetBuckets += usersByTarget.size();
                }

                std::optional<wolvrix::lib::grh::Value> resultInfo;
                try
                {
                    resultInfo = graph.getValue(result);
                }
                catch (const std::exception &ex)
                {
                    error = "activity-schedule replication getValue(resultInfo) failed source=" +
                            describeOp(graph, opId) + ": " + ex.what();
                    return false;
                }
                for (const auto &[targetSupernode, users] : usersByTarget)
                {
                    if (users.empty())
                    {
                        continue;
                    }

                    const auto cloneSym = graph.makeInternalOpSym();
                    const auto cloneOp = graph.createOperation(op->kind(), cloneSym);
                    if (op->srcLoc())
                    {
                        graph.setOpSrcLoc(cloneOp, *op->srcLoc());
                    }
                    for (const auto &attr : op->attrs())
                    {
                        graph.setAttr(cloneOp, attr.key, attr.value);
                    }
                    for (const auto operand : op->operands())
                    {
                        graph.addOperand(cloneOp, operand);
                    }

                    const auto cloneResult = graph.createValue(graph.makeInternalValSym(),
                                                               resultInfo->width(),
                                                               resultInfo->isSigned(),
                                                               resultInfo->type());
                    if (resultInfo->srcLoc())
                    {
                        graph.setValueSrcLoc(cloneResult, *resultInfo->srcLoc());
                    }
                    graph.addResult(cloneOp, cloneResult);

                    for (const auto user : users)
                    {
                        try
                        {
                            const auto userOp = graph.getOperation(user.operation);
                            const auto userOperands = userOp.operands();
                            if (user.operandIndex >= userOperands.size() || userOperands[user.operandIndex] != result)
                            {
                                std::ostringstream oss;
                                oss << "activity-schedule replication detected stale user edge: source="
                                    << describeOp(graph, opId) << " result=" << result.index
                                    << " targetSupernode=" << targetSupernode
                                    << " user=" << describeOp(graph, user.operation)
                                    << " operandIndex=" << user.operandIndex;
                                if (user.operandIndex < userOperands.size())
                                {
                                    oss << " currentOperand=" << userOperands[user.operandIndex].index;
                                }
                                error = oss.str();
                                return false;
                            }
                            graph.replaceOperand(user.operation, user.operandIndex, cloneResult);
                        }
                        catch (const std::exception &ex)
                        {
                            error = "activity-schedule replication replaceOperand failed source=" +
                                    describeOp(graph, opId) +
                                    " targetSupernode=" + std::to_string(targetSupernode) +
                                    " userOpIndex=" + std::to_string(user.operation.index) +
                                    " operandIndex=" + std::to_string(user.operandIndex) +
                                    ": " + ex.what();
                            return false;
                        }
                    }

                    partition.clusters[targetSupernode].push_back(cloneSym);
                    symbolToSupernode.emplace(cloneSym, targetSupernode);
                    ++stats.clonedOps;
                    changed = true;
                }

                if (observableValues.find(result) == observableValues.end() &&
                    graph.getValue(result).users().empty())
                {
                    if (!graph.eraseOp(opId))
                    {
                        error = "activity-schedule replication failed to erase dead source op: " +
                                describeOp(graph, opId);
                        return false;
                    }
                    auto &ownerCluster = partition.clusters[ownerSupernode];
                    ownerCluster.erase(std::remove(ownerCluster.begin(), ownerCluster.end(), opSym),
                                       ownerCluster.end());
                    symbolToSupernode.erase(opSym);
                    ++stats.erasedOps;
                }
            }

            if (perf)
            {
                perf->scanAndCloneMs = elapsedMs(scanStart);
            }

            return changed;
        }

        bool materializeFinalPartition(const wolvrix::lib::grh::Graph &graph,
                                       const SymbolPartition &partition,
                                       ActivityOpData &finalOpData,
                                       ActivityScheduleBuild &build,
                                       std::vector<uint32_t> &supernodeOfOp,
                                       FinalMaterializePerfStats *perf,
                                       std::string &error)
        {
            const auto rebuildOpDataStart = std::chrono::steady_clock::now();
            finalOpData = buildActivityOpData(graph, error);
            if (perf)
            {
                perf->rebuildOpDataMs = elapsedMs(rebuildOpDataStart);
            }
            if (!error.empty())
            {
                return false;
            }

            const auto mapLiveOpsStart = std::chrono::steady_clock::now();
            std::unordered_map<wolvrix::lib::grh::SymbolId, std::pair<wolvrix::lib::grh::OperationId, uint32_t>,
                               ActivityScheduleSymbolIdHash>
                liveOpsBySymbol;
            liveOpsBySymbol.reserve(finalOpData.topoOps.size());
            for (std::size_t topoPos = 0; topoPos < finalOpData.topoOps.size(); ++topoPos)
            {
                liveOpsBySymbol.emplace(finalOpData.topoSymbols[topoPos],
                                        std::make_pair(finalOpData.topoOps[topoPos], static_cast<uint32_t>(topoPos)));
            }
            if (perf)
            {
                perf->mapLiveOpsMs = elapsedMs(mapLiveOpsStart);
            }

            const auto collectLiveClustersStart = std::chrono::steady_clock::now();
            std::vector<LiveCluster> liveClusters;
            liveClusters.reserve(partition.clusters.size());
            for (const auto &cluster : partition.clusters)
            {
                LiveCluster live;
                live.topoPositions.reserve(cluster.size());
                for (const auto symbol : cluster)
                {
                    const auto it = liveOpsBySymbol.find(symbol);
                    if (it == liveOpsBySymbol.end())
                    {
                        continue;
                    }
                    live.topoPositions.push_back(it->second.second);
                    live.minTopoPos = std::min(live.minTopoPos, it->second.second);
                    live.maxTopoPos = std::max(live.maxTopoPos, it->second.second);
                    live.sinkOnly = static_cast<bool>(live.sinkOnly && isTailSinkOp(graph.getOperation(it->second.first)));
                }
                if (live.topoPositions.empty())
                {
                    continue;
                }
                std::sort(live.topoPositions.begin(), live.topoPositions.end());
                live.topoPositions.erase(std::unique(live.topoPositions.begin(), live.topoPositions.end()),
                                         live.topoPositions.end());
                liveClusters.push_back(std::move(live));
            }

            std::sort(liveClusters.begin(), liveClusters.end(),
                      [](const auto &lhs, const auto &rhs)
                      {
                          if (lhs.sinkOnly != rhs.sinkOnly)
                          {
                              return !lhs.sinkOnly && rhs.sinkOnly;
                          }
                          return lhs.minTopoPos < rhs.minTopoPos;
                      });
            if (perf)
            {
                perf->collectLiveClustersMs = elapsedMs(collectLiveClustersStart);
            }

            const auto buildSupernodeMapsStart = std::chrono::steady_clock::now();
            build = ActivityScheduleBuild{};
            build.supernodeToOps.resize(liveClusters.size());
            build.opToSupernode.assign(finalOpData.maxOpIndex, kInvalidActivitySupernodeId);
            build.dag.resize(liveClusters.size());
            if (!graph.values().empty())
            {
                build.valueFanout.resize(graph.values().back().index);
            }
            supernodeOfOp.assign(finalOpData.maxOpIndex + 1, kInvalidActivitySupernodeId);

            for (std::size_t supernodeId = 0; supernodeId < liveClusters.size(); ++supernodeId)
            {
                for (const auto topoPos : liveClusters[supernodeId].topoPositions)
                {
                    const auto opId = finalOpData.topoOps[topoPos];
                    build.supernodeToOps[supernodeId].push_back(opId);
                    build.opToSupernode[opId.index - 1] = static_cast<uint32_t>(supernodeId);
                    supernodeOfOp[opId.index] = static_cast<uint32_t>(supernodeId);
                }
            }
            if (perf)
            {
                perf->buildSupernodeMapsMs = elapsedMs(buildSupernodeMapsStart);
            }

            const auto buildDagStart = std::chrono::steady_clock::now();
            std::unordered_set<uint64_t> seenDagEdges;
            seenDagEdges.reserve(finalOpData.topoEdges.size());
            for (const auto &[fromPos, toPos] : finalOpData.topoEdges)
            {
                const auto fromOp = finalOpData.topoOps[fromPos];
                const auto toOp = finalOpData.topoOps[toPos];
                const uint32_t fromNode = supernodeOfOp[fromOp.index];
                const uint32_t toNode = supernodeOfOp[toOp.index];
                if (fromNode == kInvalidActivitySupernodeId || toNode == kInvalidActivitySupernodeId ||
                    fromNode == toNode)
                {
                    continue;
                }
                const uint64_t packed = (static_cast<uint64_t>(fromNode) << 32) | static_cast<uint64_t>(toNode);
                if (!seenDagEdges.insert(packed).second)
                {
                    continue;
                }
                build.dag[fromNode].push_back(toNode);
            }
            for (auto &succs : build.dag)
            {
                std::sort(succs.begin(), succs.end());
            }
            if (perf)
            {
                perf->buildDagMs = elapsedMs(buildDagStart);
            }

            const auto buildValueFanoutStart = std::chrono::steady_clock::now();
            for (wolvrix::lib::grh::OperationId toOpId : finalOpData.topoOps)
            {
                const wolvrix::lib::grh::Operation toOp = graph.getOperation(toOpId);
                const uint32_t toNode = supernodeOfOp[toOpId.index];
                if (toNode == kInvalidActivitySupernodeId)
                {
                    continue;
                }
                for (wolvrix::lib::grh::ValueId operand : toOp.operands())
                {
                    const wolvrix::lib::grh::OperationId defOpId = graph.valueDef(operand);
                    if (!defOpId.valid())
                    {
                        continue;
                    }
                    const uint32_t fromNode = supernodeOfOp[defOpId.index];
                    if (fromNode == kInvalidActivitySupernodeId || fromNode == toNode || operand.index == 0 ||
                        operand.index > build.valueFanout.size())
                    {
                        continue;
                    }
                    build.valueFanout[operand.index - 1].push_back(toNode);
                }
            }
            for (auto &succs : build.valueFanout)
            {
                std::sort(succs.begin(), succs.end());
                succs.erase(std::unique(succs.begin(), succs.end()), succs.end());
            }
            if (perf)
            {
                perf->buildValueFanoutMs = elapsedMs(buildValueFanoutStart);
            }

            const auto buildStateReadSetsStart = std::chrono::steady_clock::now();
            for (const auto opId : graph.operations())
            {
                const auto op = graph.getOperation(opId);
                if (!isStateReadOpKind(op.kind()))
                {
                    continue;
                }
                const auto stateSymbol = stateSymbolForReadOp(op);
                if (!stateSymbol || stateSymbol->empty())
                {
                    continue;
                }
                if (opId.index >= supernodeOfOp.size())
                {
                    continue;
                }
                const uint32_t supernodeId = supernodeOfOp[opId.index];
                if (supernodeId == kInvalidActivitySupernodeId)
                {
                    continue;
                }
                build.stateReadSupernodes[*stateSymbol].push_back(supernodeId);
            }
            for (auto &[stateSymbol, supernodes] : build.stateReadSupernodes)
            {
                (void)stateSymbol;
                std::sort(supernodes.begin(), supernodes.end());
                supernodes.erase(std::unique(supernodes.begin(), supernodes.end()), supernodes.end());
            }
            if (perf)
            {
                perf->buildStateReadSetsMs = elapsedMs(buildStateReadSetsStart);
            }

            const auto finalTopoStart = std::chrono::steady_clock::now();
            wolvrix::lib::toposort::TopoDag<uint32_t> finalTopoDag;
            finalTopoDag.reserveNodes(build.supernodeToOps.size());
            for (uint32_t supernodeId = 0; supernodeId < build.supernodeToOps.size(); ++supernodeId)
            {
                finalTopoDag.addNode(supernodeId);
            }
            for (uint32_t supernodeId = 0; supernodeId < build.supernodeToOps.size(); ++supernodeId)
            {
                for (const auto succ : build.dag[supernodeId])
                {
                    finalTopoDag.addEdge(supernodeId, succ);
                }
            }

            try
            {
                const auto layers = finalTopoDag.toposort();
                for (const auto &layer : layers)
                {
                    build.topoOrder.insert(build.topoOrder.end(), layer.begin(), layer.end());
                }
            }
            catch (const std::exception &ex)
            {
                error = std::string("activity-schedule final topo failed: ") + ex.what() + " " +
                        describeCyclePath(findCyclePath(build.dag), liveClusters);
                return false;
            }
            if (perf)
            {
                perf->finalTopoMs = elapsedMs(finalTopoStart);
            }
            return true;
        }

    } // namespace

    ActivitySchedulePass::ActivitySchedulePass()
        : Pass("activity-schedule",
               "activity-schedule",
               "Build activity-schedule hypergraph for a single graph"),
          options_({})
    {
    }

    ActivitySchedulePass::ActivitySchedulePass(ActivityScheduleOptions options)
        : Pass("activity-schedule",
               "activity-schedule",
               "Build activity-schedule hypergraph for a single graph"),
          options_(std::move(options))
    {
    }

    PassResult ActivitySchedulePass::run()
    {
        PassResult result;
        const auto totalStart = std::chrono::steady_clock::now();
        if (options_.path.empty())
        {
            error("activity-schedule requires -path");
            result.failed = true;
            return result;
        }
        if (options_.supernodeMaxSize == 0)
        {
            error("activity-schedule supernode_max_size must be >= 1");
            result.failed = true;
            return result;
        }
        if (options_.refineMaxIter == 0)
        {
            error("activity-schedule refine_max_iter must be >= 1");
            result.failed = true;
            return result;
        }
        if (options_.costModel != "edge-cut")
        {
            error("activity-schedule only supports -cost-model=edge-cut");
            result.failed = true;
            return result;
        }
        const std::size_t maxSinkSupernodeOp =
            options_.maxSinkSupernodeOp == 0 ? options_.supernodeMaxSize : options_.maxSinkSupernodeOp;

        std::string resolveError;
        const std::optional<std::string> targetGraphName =
            resolveTargetGraphName(design(), options_.path, resolveError);
        if (!targetGraphName)
        {
            error(resolveError);
            result.failed = true;
            return result;
        }

        auto *graph = design().findGraph(*targetGraphName);
        if (graph == nullptr)
        {
            error("activity-schedule target graph not found: " + *targetGraphName);
            result.failed = true;
            return result;
        }

        bool graphChanged = false;
        std::vector<wolvrix::lib::grh::OperationId> opsNeedingSymbol;
        opsNeedingSymbol.reserve(graph->operations().size());
        for (const auto opId : graph->operations())
        {
            const auto op = graph->getOperation(opId);
            if (isHierLikeOpKind(op.kind()))
            {
                error(*graph,
                      op,
                      "activity-schedule guard: target graph must not contain hierarchical ops kind=" +
                          std::string(wolvrix::lib::grh::toString(op.kind())));
                result.failed = true;
            }
            if (graph->operationSymbol(opId).valid())
            {
                continue;
            }
            if (!isPartitionableOpKind(op.kind()))
            {
                continue;
            }
            opsNeedingSymbol.push_back(opId);
        }
        if (result.failed)
        {
            return result;
        }

        for (const auto opId : opsNeedingSymbol)
        {
            graph->setOpSymbol(opId, graph->makeInternalOpSym());
            graphChanged = true;
        }

        const auto seedBuildStart = std::chrono::steady_clock::now();
        graph->freeze();
        std::string buildError;
        ActivityOpData opData = buildActivityOpData(*graph, buildError);
        const std::uint64_t buildOpDataMs = elapsedMs(seedBuildStart);
        if (!buildError.empty())
        {
            error(*graph, buildError);
            result.failed = true;
            return result;
        }

        const auto sinkPartitionStart = std::chrono::steady_clock::now();
        const std::vector<uint32_t> sinkTopoPositions = collectSinkTopoPositions(*graph, opData);
        WorkingPartition sinkPartition = buildChunkedPartitionFromTopoPositions(sinkTopoPositions, maxSinkSupernodeOp);
        sinkPartition = canonicalizePartition(sinkPartition, opData);
        const std::uint64_t sinkPartitionMs = elapsedMs(sinkPartitionStart);
        std::vector<uint8_t> sinkTopoMask(opData.topoOps.size(), 0U);
        markCoveredTopoPositions(sinkPartition, sinkTopoMask);

        const auto tailPartitionStart = std::chrono::steady_clock::now();
        TailPartitionStats tailStats;
        WorkingPartition tailPartition =
            buildTailPartition(*graph, opData, sinkPartition, &tailStats);
        tailPartition = splitPartitionIntoContiguousTopoRuns(tailPartition);
        tailPartition = canonicalizePartition(tailPartition, opData);
        const std::uint64_t tailPartitionMs = elapsedMs(tailPartitionStart);
        const std::size_t tailCoveredOpCount =
            std::accumulate(tailPartition.clusters.begin(),
                            tailPartition.clusters.end(),
                            std::size_t{0},
                            [](std::size_t acc, const std::vector<uint32_t> &cluster)
                            {
                                return acc + cluster.size();
                            });
        const std::size_t sinkCoveredOpCount =
            std::accumulate(sinkPartition.clusters.begin(),
                            sinkPartition.clusters.end(),
                            std::size_t{0},
                            [](std::size_t acc, const std::vector<uint32_t> &cluster)
                            {
                                return acc + cluster.size();
                            });
        const std::vector<uint8_t> residualTopoMask =
            buildResidualTopoMask(opData.topoOps.size(), sinkPartition, tailPartition);
        const std::size_t residualOpCount =
            std::count(residualTopoMask.begin(), residualTopoMask.end(), static_cast<uint8_t>(1U));

        logInfo("activity-schedule timing: special_partition sink_supernodes=" +
                std::to_string(sinkPartition.clusters.size()) +
                " sink_ops=" + std::to_string(sinkCoveredOpCount) +
                " sink_chunk_limit=" + std::to_string(maxSinkSupernodeOp) +
                " tail_supernodes=" + std::to_string(tailPartition.clusters.size()) +
                " tail_initial_seed_ops=" + std::to_string(tailStats.initialSeedOps) +
                " tail_shared_seed_ops=" + std::to_string(tailStats.sharedSeedOps) +
                " tail_absorbed_ops=" + std::to_string(tailStats.absorbedOps) +
                " tail_ops=" + std::to_string(tailCoveredOpCount) +
                " residual_ops=" + std::to_string(residualOpCount) +
                " sink_elapsed_ms=" + std::to_string(sinkPartitionMs) +
                " tail_elapsed_ms=" + std::to_string(tailPartitionMs));

        const auto seedPartitionStart = std::chrono::steady_clock::now();
        WorkingPartition partition = buildInitialPartition(opData, sinkPartition, tailPartition);
        partition = canonicalizePartition(partition, opData);
        if (const std::string cycle = describeWorkingPartitionCycle(partition, opData); !cycle.empty())
        {
            logInfo("activity-schedule debug: initial_partition_cycle " + cycle);
        }
        const std::uint64_t seedPartitionMs = elapsedMs(seedPartitionStart);
        const std::size_t seedSupernodeCount = partition.clusters.size();

        StateReadTailAbsorbStats stateReadTailAbsorbStats;
        std::uint64_t stateReadTailAbsorbMs = 0;
        std::uint64_t rebuildAfterStateReadTailAbsorbMs = 0;
        std::size_t stateReadTailAbsorbTargetSupernodes = 0;
        if (options_.enableStateReadTailAbsorb && !partition.clusters.empty())
        {
            const auto absorbStart = std::chrono::steady_clock::now();
            const WorkingPartition targetPartition =
                buildStateReadTailAbsorbTargetPartition(partition, opData, options_);
            stateReadTailAbsorbTargetSupernodes = targetPartition.clusters.size();
            SymbolPartition targetSymbolPartition = buildSymbolPartition(targetPartition, opData);
            if (runStateReadTailAbsorbPhase(*graph,
                                            targetSymbolPartition,
                                            options_.stateReadTailAbsorbMaxTargets,
                                            stateReadTailAbsorbStats,
                                            buildError))
            {
                graphChanged = true;
                graph->freeze();
                const auto rebuildStart = std::chrono::steady_clock::now();
                opData = buildActivityOpData(*graph, buildError);
                rebuildAfterStateReadTailAbsorbMs = elapsedMs(rebuildStart);
                if (!buildError.empty())
                {
                    error(*graph, buildError);
                    result.failed = true;
                    return result;
                }
                partition = rebuildWorkingPartitionFromSymbolPartition(targetSymbolPartition, opData);
                partition = canonicalizePartition(partition, opData);
                if (const std::string cycle = describeWorkingPartitionCycle(partition, opData); !cycle.empty())
                {
                    logInfo("activity-schedule debug: post_state_read_tail_absorb_cycle " + cycle);
                }
            }
            stateReadTailAbsorbMs = elapsedMs(absorbStart);
        }

        std::uint64_t coarsenMs = 0;
        std::size_t coarsenIterations = 0;
        if (!partition.clusters.empty() && options_.enableCoarsen)
        {
            const auto coarsenStart = std::chrono::steady_clock::now();
            std::size_t tailIterations = 0;
            bool changed = true;
            while (changed)
            {
                ++coarsenIterations;
                changed = false;
                const auto iterStart = std::chrono::steady_clock::now();
                const std::size_t clustersBeforeIter = partition.clusters.size();
                bool out1Changed = false;
                bool in1Changed = false;
                bool siblingChanged = false;
                bool forwardChanged = false;
                if (options_.enableChainMerge)
                {
                    out1Changed = tryMergeOut1(partition, opData, options_.supernodeMaxSize);
                    in1Changed = tryMergeIn1(partition, opData, options_.supernodeMaxSize);
                    changed = out1Changed || in1Changed || changed;
                }
                if (options_.enableSiblingMerge)
                {
                    siblingChanged = tryMergeSiblings(partition, opData, options_.supernodeMaxSize);
                    changed = siblingChanged || changed;
                }
                if (options_.enableForwardMerge)
                {
                    forwardChanged = tryMergeForwarders(partition, opData, options_.supernodeMaxSize);
                    changed = forwardChanged || changed;
                }
                const std::size_t clustersAfterIter = partition.clusters.size();
                const std::size_t clusterDelta =
                    clustersBeforeIter >= clustersAfterIter ? (clustersBeforeIter - clustersAfterIter) : 0;
                logInfo("activity-schedule timing: coarsen_iter=" + std::to_string(coarsenIterations) +
                        " clusters=" + std::to_string(partition.clusters.size()) +
                        " cluster_delta=" + std::to_string(clusterDelta) +
                        " changed=" + (changed ? std::string("true") : std::string("false")) +
                        " out1=" + (out1Changed ? std::string("1") : std::string("0")) +
                        " in1=" + (in1Changed ? std::string("1") : std::string("0")) +
                        " siblings=" + (siblingChanged ? std::string("1") : std::string("0")) +
                        " forward=" + (forwardChanged ? std::string("1") : std::string("0")) +
                        " elapsed_ms=" + std::to_string(elapsedMs(iterStart)));

                const bool smallDeltaTail =
                    clustersBeforeIter >= kCoarsenTailLargeClusterThreshold &&
                    clusterDelta < kCoarsenTailMaxClusterDeltaExclusive;
                if (smallDeltaTail)
                {
                    ++tailIterations;
                }
                else
                {
                    tailIterations = 0;
                }
                if (tailIterations >= kCoarsenTailMaxConsecutiveIters)
                {
                    logInfo("activity-schedule timing: coarsen_tail_stop clusters=" +
                            std::to_string(partition.clusters.size()) +
                            " tail_iters=" + std::to_string(tailIterations) +
                            " cluster_delta=" + std::to_string(clusterDelta) +
                            " threshold_exclusive=" + std::to_string(kCoarsenTailMaxClusterDeltaExclusive));
                    break;
                }
            }
            coarsenMs = elapsedMs(coarsenStart);
        }

        const auto dpPrepStart = std::chrono::steady_clock::now();
        partition = canonicalizePartition(partition, opData);
        if (const std::string cycle = describeWorkingPartitionCycle(partition, opData); !cycle.empty())
        {
            logInfo("activity-schedule debug: coarsen_partition_cycle " + cycle);
        }
        const std::size_t coarseSupernodeCount = partition.clusters.size();
        ClusterView coarseView = buildClusterView(partition, opData);
        ClusterView dpView = buildTopoOrderedClusterView(coarseView);
        const std::uint64_t dpPrepMs = elapsedMs(dpPrepStart);
        const auto dpStart = std::chrono::steady_clock::now();
        std::vector<std::vector<uint32_t>> segments = buildDpSegments(dpView, options_.supernodeMaxSize);
        const std::uint64_t dpMs = elapsedMs(dpStart);
        const std::size_t dpSupernodeCount = segments.size();
        std::uint64_t refineMs = 0;
        if (options_.enableRefine)
        {
            const auto refineStart = std::chrono::steady_clock::now();
            segments = refineSegments(dpView, std::move(segments), options_.supernodeMaxSize, options_.refineMaxIter);
            refineMs = elapsedMs(refineStart);
        }
        const auto materializeSegmentsStart = std::chrono::steady_clock::now();
        partition = materializeSegments(dpView, segments);
        partition = canonicalizePartition(partition, opData);
        markSinkOnlyClustersFixedBoundary(partition, opData);
        if (const std::string cycle = describeWorkingPartitionCycle(partition, opData); !cycle.empty())
        {
            logInfo("activity-schedule debug: post_dp_partition_cycle " + cycle);
        }
        const std::uint64_t materializeSegmentsMs = elapsedMs(materializeSegmentsStart);

        const auto symbolPartitionStart = std::chrono::steady_clock::now();
        SymbolPartition symbolPartition = buildSymbolPartition(partition, opData);
        const std::uint64_t symbolPartitionMs = elapsedMs(symbolPartitionStart);
        ReplicationStats replicationStats;
        ReplicationPerfStats replicationPerf;
        std::uint64_t replicationMs = 0;
        if (options_.enableReplication)
        {
            const auto replicationStart = std::chrono::steady_clock::now();
            if (runReplicationPhase(*graph,
                                    symbolPartition,
                                    opData,
                                    options_.replicationMaxCost,
                                    options_.replicationMaxTargets,
                                    replicationStats,
                                    &replicationPerf,
                                    buildError))
            {
                graphChanged = true;
            }
            replicationMs = elapsedMs(replicationStart);
            if (!buildError.empty())
            {
                error(*graph, buildError);
                result.failed = true;
                return result;
            }
        }

        const auto freezeAfterReplicationStart = std::chrono::steady_clock::now();
        graph->freeze();
        const std::uint64_t freezeAfterReplicationMs = elapsedMs(freezeAfterReplicationStart);

        ActivityScheduleBuild build;
        ActivityOpData finalOpData;
        std::vector<uint32_t> supernodeOfOp;
        FinalMaterializePerfStats finalPerf;
        const auto finalMaterializeStart = std::chrono::steady_clock::now();
        if (!materializeFinalPartition(*graph, symbolPartition, finalOpData, build, supernodeOfOp, &finalPerf, buildError))
        {
            error(*graph, buildError);
            result.failed = true;
            return result;
        }
        const std::uint64_t finalMaterializeMs = elapsedMs(finalMaterializeStart);

        const std::string keyPrefix = options_.path + ".activity_schedule.";
        const auto exportStart = std::chrono::steady_clock::now();
        setSessionValue(keyPrefix + "supernode_to_ops",
                        build.supernodeToOps,
                        "activity-schedule.supernode-to-ops");
        const std::uint64_t exportSupernodeToOpsMs = elapsedMs(exportStart);
        const auto exportOpToSupernodeStart = std::chrono::steady_clock::now();
        setSessionValue(keyPrefix + "op_to_supernode",
                        build.opToSupernode,
                        "activity-schedule.op-to-supernode");
        const std::uint64_t exportOpToSupernodeMs = elapsedMs(exportOpToSupernodeStart);
        const auto exportDagStart = std::chrono::steady_clock::now();
        setSessionValue(keyPrefix + "dag", build.dag, "activity-schedule.dag");
        const std::uint64_t exportDagMs = elapsedMs(exportDagStart);
        const auto exportValueFanoutStart = std::chrono::steady_clock::now();
        setSessionValue(keyPrefix + "value_fanout", build.valueFanout, "activity-schedule.value-fanout");
        const std::uint64_t exportValueFanoutMs = elapsedMs(exportValueFanoutStart);
        const auto exportTopoOrderStart = std::chrono::steady_clock::now();
        setSessionValue(keyPrefix + "topo_order", build.topoOrder, "activity-schedule.topo-order");
        const std::uint64_t exportTopoOrderMs = elapsedMs(exportTopoOrderStart);
        const auto exportStateReadStart = std::chrono::steady_clock::now();
        setSessionValue(keyPrefix + "state_read_supernodes",
                        build.stateReadSupernodes,
                        "activity-schedule.state-read-supernodes");
        const std::uint64_t exportStateReadMs = elapsedMs(exportStateReadStart);
        const std::uint64_t exportTotalMs = elapsedMs(exportStart);

        logInfo("activity-schedule timing(ms): build_op_data=" + std::to_string(buildOpDataMs) +
                " sink_partition=" + std::to_string(sinkPartitionMs) +
                " tail_partition=" + std::to_string(tailPartitionMs) +
                " seed_partition=" + std::to_string(seedPartitionMs) +
                " state_read_tail_absorb=" + std::to_string(stateReadTailAbsorbMs) +
                " rebuild_after_state_read_tail_absorb=" + std::to_string(rebuildAfterStateReadTailAbsorbMs) +
                " coarsen=" + std::to_string(coarsenMs) +
                " dp_prep=" + std::to_string(dpPrepMs) +
                " dp=" + std::to_string(dpMs) +
                " refine=" + std::to_string(refineMs) +
                " materialize_segments=" + std::to_string(materializeSegmentsMs) +
                " symbol_partition=" + std::to_string(symbolPartitionMs) +
                " replication=" + std::to_string(replicationMs) +
                " freeze_after_replication=" + std::to_string(freezeAfterReplicationMs) +
                " final_materialize=" + std::to_string(finalMaterializeMs) +
                " export_session=" + std::to_string(exportTotalMs) +
                " total=" + std::to_string(elapsedMs(totalStart)));
        logInfo("activity-schedule timing detail: coarsen_iters=" + std::to_string(coarsenIterations) +
                " topo_edges=" + std::to_string(opData.topoEdges.size()) +
                " final_topo_edges=" + std::to_string(finalOpData.topoEdges.size()) +
                " graph_ops=" + std::to_string(graph->operations().size()) +
                " graph_values=" + std::to_string(graph->values().size()));
        logInfo("activity-schedule timing state_read_tail_absorb: target_supernodes=" +
                std::to_string(stateReadTailAbsorbTargetSupernodes) +
                " cloned=" + std::to_string(stateReadTailAbsorbStats.clonedOps) +
                " erased=" + std::to_string(stateReadTailAbsorbStats.erasedOps) +
                " kept_observable=" + std::to_string(stateReadTailAbsorbStats.keptObservableReads) +
                " kept_local=" + std::to_string(stateReadTailAbsorbStats.keptLocalReads) +
                " skipped_too_many_targets=" + std::to_string(stateReadTailAbsorbStats.skippedTooManyTargets));
        logInfo("activity-schedule timing replication(ms): collect_observable=" +
                std::to_string(replicationPerf.collectObservableMs) +
                " build_symbol_map=" + std::to_string(replicationPerf.buildSymbolMapMs) +
                " scan_and_clone=" + std::to_string(replicationPerf.scanAndCloneMs) +
                " scanned_ops=" + std::to_string(replicationPerf.scannedOps) +
                " candidate_ops=" + std::to_string(replicationPerf.candidateOps) +
                " user_edges_visited=" + std::to_string(replicationPerf.userEdgesVisited) +
                " target_buckets=" + std::to_string(replicationPerf.targetBuckets));
        logInfo("activity-schedule timing final_materialize(ms): rebuild_op_data=" +
                std::to_string(finalPerf.rebuildOpDataMs) +
                " map_live_ops=" + std::to_string(finalPerf.mapLiveOpsMs) +
                " collect_live_clusters=" + std::to_string(finalPerf.collectLiveClustersMs) +
                " build_supernode_maps=" + std::to_string(finalPerf.buildSupernodeMapsMs) +
                " build_dag=" + std::to_string(finalPerf.buildDagMs) +
                " build_value_fanout=" + std::to_string(finalPerf.buildValueFanoutMs) +
                " build_state_read_sets=" + std::to_string(finalPerf.buildStateReadSetsMs) +
                " final_topo=" + std::to_string(finalPerf.finalTopoMs));
        logInfo("activity-schedule timing export(ms): supernode_to_ops=" +
                std::to_string(exportSupernodeToOpsMs) +
                " op_to_supernode=" + std::to_string(exportOpToSupernodeMs) +
                " dag=" + std::to_string(exportDagMs) +
                " value_fanout=" + std::to_string(exportValueFanoutMs) +
                " topo_order=" + std::to_string(exportTopoOrderMs) +
                " state_read_supernodes=" + std::to_string(exportStateReadMs));

        std::ostringstream summary;
        summary << "activity-schedule: path=" << options_.path
                << " graph=" << graph->symbol()
                << " supernodes=" << build.supernodeToOps.size()
                << " seed_supernodes=" << seedSupernodeCount
                << " coarse_supernodes=" << coarseSupernodeCount
                << " dp_supernodes=" << dpSupernodeCount
                << " sink_supernodes=" << sinkPartition.clusters.size()
                << " sink_ops=" << sinkTopoPositions.size()
                << " tail_supernodes=" << tailPartition.clusters.size()
                << " tail_initial_seed_ops=" << tailStats.initialSeedOps
                << " tail_shared_seed_ops=" << tailStats.sharedSeedOps
                << " tail_absorbed_ops=" << tailStats.absorbedOps
                << " tail_ops=" << tailCoveredOpCount
                << " eligible_ops=" << finalOpData.topoOps.size()
                << " state_read_tail_absorb_target_supernodes=" << stateReadTailAbsorbTargetSupernodes
                << " state_read_tail_absorb_cloned=" << stateReadTailAbsorbStats.clonedOps
                << " state_read_tail_absorb_erased=" << stateReadTailAbsorbStats.erasedOps
                << " replication_cloned=" << replicationStats.clonedOps
                << " replication_erased=" << replicationStats.erasedOps
                << " state_read_sets=" << build.stateReadSupernodes.size()
                << " graph_changed=" << (graphChanged ? "true" : "false");
        logInfo(summary.str());

        result.changed = graphChanged;
        result.failed = false;
        return result;
    }

} // namespace wolvrix::lib::transform
