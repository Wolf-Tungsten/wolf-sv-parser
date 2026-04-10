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
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kSystemTask:
            case wolvrix::lib::grh::OperationKind::kDpicCall:
                return true;
            default:
                return false;
            }
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
            std::vector<std::vector<uint32_t>> preds;
            std::vector<std::vector<uint32_t>> succs;
            std::vector<uint32_t> clusterOfTopoPos;
        };

        struct SymbolPartition
        {
            std::vector<std::vector<wolvrix::lib::grh::SymbolId>> clusters;
            std::vector<uint8_t> fixedBoundary;
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

        constexpr std::size_t kCoarsenSiblingTailLargeClusterThreshold = 100000;
        constexpr std::size_t kCoarsenSiblingTailMinClusterDelta = 8;
        constexpr std::size_t kCoarsenSiblingTailMaxConsecutiveIters = 2;

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
            for (std::size_t pos = 0; pos < data.topoOps.size(); ++pos)
            {
                const auto opId = data.topoOps[pos];
                data.topoPosByOpIndex[opId.index] = static_cast<uint32_t>(pos);
                data.topoSymbols.push_back(graph.operationSymbol(opId));
                data.topoKinds.push_back(graph.opKind(opId));
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

        WorkingPartition makeSeedPartition(const ActivityOpData &opData)
        {
            WorkingPartition partition;
            partition.clusters.reserve(opData.topoOps.size());
            partition.fixedBoundary.reserve(opData.topoOps.size());
            for (std::size_t pos = 0; pos < opData.topoOps.size(); ++pos)
            {
                partition.clusters.push_back(std::vector<uint32_t>{static_cast<uint32_t>(pos)});
                partition.fixedBoundary.push_back(isSideEffectBoundaryKind(opData.topoKinds[pos]) ? 1U : 0U);
            }
            return partition;
        }

        ClusterView buildClusterView(const WorkingPartition &partition, const ActivityOpData &opData)
        {
            ClusterView view;
            view.members = partition.clusters;
            view.fixedBoundary = partition.fixedBoundary;
            view.preds.resize(view.members.size());
            view.succs.resize(view.members.size());
            view.clusterOfTopoPos.assign(opData.topoOps.size(), kInvalidActivitySupernodeId);

            for (std::size_t clusterId = 0; clusterId < view.members.size(); ++clusterId)
            {
                auto &members = view.members[clusterId];
                std::sort(members.begin(), members.end());
                members.erase(std::unique(members.begin(), members.end()), members.end());
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

        WorkingPartition canonicalizePartition(const WorkingPartition &partition,
                                               const ActivityOpData &opData)
        {
            if (partition.clusters.empty())
            {
                return partition;
            }

            const ClusterView view = buildClusterView(partition, opData);
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

            WorkingPartition out;
            try
            {
                const auto layers = topoDag.toposort();
                for (const auto &layer : layers)
                {
                    for (const auto clusterId : layer)
                    {
                        out.clusters.push_back(view.members[clusterId]);
                        out.fixedBoundary.push_back(view.fixedBoundary[clusterId]);
                    }
                }
            }
            catch (const std::exception &)
            {
                return partition;
            }
            if (out.clusters.size() != partition.clusters.size())
            {
                return partition;
            }
            return out;
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
                if (view.fixedBoundary[succId] != 0)
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
                if (view.fixedBoundary[predId] != 0)
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
                for (std::size_t end = begin + 1; end <= count; ++end)
                {
                    const std::size_t clusterId = end - 1;
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

                    accumSize += view.members[clusterId].size();
                    if (accumSize > maxSize)
                    {
                        break;
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
                        (newCost == bestCost[end] && (backtrace[end] < 0 || static_cast<int>(begin) > backtrace[end])))
                    {
                        bestCost[end] = newCost;
                        backtrace[end] = static_cast<int>(begin);
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

            struct LiveCluster
            {
                uint32_t minTopoPos = kInvalidActivitySupernodeId;
                std::vector<uint32_t> topoPositions;
            };

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
                      [](const auto &lhs, const auto &rhs) { return lhs.minTopoPos < rhs.minTopoPos; });
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
                error = std::string("activity-schedule final topo failed: ") + ex.what();
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
        const ActivityOpData opData = buildActivityOpData(*graph, buildError);
        const std::uint64_t buildOpDataMs = elapsedMs(seedBuildStart);
        if (!buildError.empty())
        {
            error(*graph, buildError);
            result.failed = true;
            return result;
        }

        const auto seedPartitionStart = std::chrono::steady_clock::now();
        WorkingPartition partition = makeSeedPartition(opData);
        partition = canonicalizePartition(partition, opData);
        const std::uint64_t seedPartitionMs = elapsedMs(seedPartitionStart);
        const std::size_t seedSupernodeCount = partition.clusters.size();

        std::uint64_t coarsenMs = 0;
        std::size_t coarsenIterations = 0;
        if (!partition.clusters.empty() && options_.enableCoarsen)
        {
            const auto coarsenStart = std::chrono::steady_clock::now();
            std::size_t siblingTailIterations = 0;
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

                const bool siblingOnlyTail =
                    siblingChanged &&
                    !out1Changed &&
                    !in1Changed &&
                    !forwardChanged &&
                    clustersBeforeIter >= kCoarsenSiblingTailLargeClusterThreshold &&
                    clusterDelta <= kCoarsenSiblingTailMinClusterDelta;
                if (siblingOnlyTail)
                {
                    ++siblingTailIterations;
                }
                else
                {
                    siblingTailIterations = 0;
                }
                if (siblingTailIterations >= kCoarsenSiblingTailMaxConsecutiveIters)
                {
                    logInfo("activity-schedule timing: coarsen_tail_stop clusters=" +
                            std::to_string(partition.clusters.size()) +
                            " sibling_tail_iters=" + std::to_string(siblingTailIterations) +
                            " cluster_delta=" + std::to_string(clusterDelta) +
                            " threshold=" + std::to_string(kCoarsenSiblingTailMinClusterDelta));
                    break;
                }
            }
            coarsenMs = elapsedMs(coarsenStart);
        }

        const auto dpPrepStart = std::chrono::steady_clock::now();
        partition = canonicalizePartition(partition, opData);
        const std::size_t coarseSupernodeCount = partition.clusters.size();
        ClusterView coarseView = buildClusterView(partition, opData);
        const std::uint64_t dpPrepMs = elapsedMs(dpPrepStart);
        const auto dpStart = std::chrono::steady_clock::now();
        std::vector<std::vector<uint32_t>> segments = buildDpSegments(coarseView, options_.supernodeMaxSize);
        const std::uint64_t dpMs = elapsedMs(dpStart);
        const std::size_t dpSupernodeCount = segments.size();
        std::uint64_t refineMs = 0;
        if (options_.enableRefine)
        {
            const auto refineStart = std::chrono::steady_clock::now();
            segments = refineSegments(coarseView, std::move(segments), options_.supernodeMaxSize, options_.refineMaxIter);
            refineMs = elapsedMs(refineStart);
        }
        const auto materializeSegmentsStart = std::chrono::steady_clock::now();
        partition = materializeSegments(coarseView, segments);
        partition = canonicalizePartition(partition, opData);
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
                " seed_partition=" + std::to_string(seedPartitionMs) +
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
                << " eligible_ops=" << finalOpData.topoOps.size()
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
