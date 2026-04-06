#include "transform/activity_schedule.hpp"

#include "core/toposort.hpp"

#include <algorithm>
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

        bool isEventBoundarySinkKind(wolvrix::lib::grh::OperationKind kind) noexcept
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

        bool participatesInBackwardPropagation(wolvrix::lib::grh::OperationKind kind) noexcept
        {
            if (isHierLikeOpKind(kind) || kind == wolvrix::lib::grh::OperationKind::kDpicImport)
            {
                return false;
            }
            if (kind == wolvrix::lib::grh::OperationKind::kSystemTask)
            {
                return false;
            }
            if (kind == wolvrix::lib::grh::OperationKind::kDpicCall)
            {
                return false;
            }
            return isPartitionableOpKind(kind);
        }

        std::string valueSortKey(const wolvrix::lib::grh::Graph &graph,
                                 wolvrix::lib::grh::ValueId value)
        {
            const wolvrix::lib::grh::SymbolId sym = graph.valueSymbol(value);
            if (sym.valid())
            {
                return std::string(graph.symbolText(sym));
            }
            return std::string("_v") + std::to_string(value.index);
        }

        void normalizeSignature(const wolvrix::lib::grh::Graph &graph,
                                ActivityScheduleEventDomainSignature &signature)
        {
            std::sort(signature.terms.begin(), signature.terms.end(),
                      [&](const auto &lhs, const auto &rhs) {
                          const std::string lhsKey = valueSortKey(graph, lhs.value);
                          const std::string rhsKey = valueSortKey(graph, rhs.value);
                          if (lhsKey != rhsKey)
                          {
                              return lhsKey < rhsKey;
                          }
                          if (lhs.value != rhs.value)
                          {
                              return lhs.value.index < rhs.value.index;
                          }
                          return lhs.edge < rhs.edge;
                      });
            signature.terms.erase(std::unique(signature.terms.begin(), signature.terms.end()),
                                  signature.terms.end());
        }

        bool signatureLess(const wolvrix::lib::grh::Graph &graph,
                           const ActivityScheduleEventDomainSignature &lhs,
                           const ActivityScheduleEventDomainSignature &rhs)
        {
            if (lhs.terms.size() != rhs.terms.size())
            {
                return lhs.terms.size() < rhs.terms.size();
            }
            for (std::size_t i = 0; i < lhs.terms.size(); ++i)
            {
                const std::string lhsKey = valueSortKey(graph, lhs.terms[i].value);
                const std::string rhsKey = valueSortKey(graph, rhs.terms[i].value);
                if (lhsKey != rhsKey)
                {
                    return lhsKey < rhsKey;
                }
                if (lhs.terms[i].value != rhs.terms[i].value)
                {
                    return lhs.terms[i].value.index < rhs.terms[i].value.index;
                }
                if (lhs.terms[i].edge != rhs.terms[i].edge)
                {
                    return lhs.terms[i].edge < rhs.terms[i].edge;
                }
            }
            return false;
        }

        struct ActivityScheduleBuild
        {
            ActivityScheduleSupernodes supernodes;
            ActivityScheduleSupernodeToOps supernodeToOps;
            ActivityScheduleSupernodeToOpSymbols supernodeToOpSymbols;
            ActivityScheduleOpToSupernode opToSupernode;
            ActivityScheduleOpSymbolToSupernode opSymbolToSupernode;
            ActivityScheduleDag dag;
            ActivityScheduleTopoOrder topoOrder;
            ActivityScheduleHeadEvalSupernodes headEvalSupernodes;
            ActivityScheduleOpEventDomains opEventDomains;
            ActivityScheduleValueEventDomains valueEventDomains;
            ActivityScheduleSupernodeEventDomains supernodeEventDomains;
            ActivityScheduleEventDomainSinks eventDomainSinks;
            ActivityScheduleEventDomainSinkGroups eventDomainSinkGroups;
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

        ActivityScheduleOpSymbolToSupernode buildSymbolToSupernodeMap(const SymbolPartition &partition)
        {
            ActivityScheduleOpSymbolToSupernode out;
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
                                 std::string &error)
        {
            if (partition.clusters.empty() || maxTargets == 0)
            {
                return false;
            }

            const auto observableValues = collectObservableBoundaryValues(graph);
            auto symbolToSupernode = buildSymbolToSupernodeMap(partition);
            bool changed = false;

            for (std::size_t revPos = opData.topoSymbols.size(); revPos > 0; --revPos)
            {
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
                const auto op = graph.getOperation(opId);
                if (!isReplicationCandidateKind(op.kind()) || op.results().size() != 1)
                {
                    continue;
                }
                const int cost = replicationCost(op.kind());
                if (cost < 0 || static_cast<std::size_t>(cost) > maxCost)
                {
                    continue;
                }

                const auto result = op.results().front();
                std::unordered_map<uint32_t, std::vector<wolvrix::lib::grh::ValueUser>> usersByTarget;
                usersByTarget.reserve(4);
                for (const auto user : graph.getValue(result).users())
                {
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

                const auto resultInfo = graph.getValue(result);
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
                                                               resultInfo.width(),
                                                               resultInfo.isSigned(),
                                                               resultInfo.type());
                    if (resultInfo.srcLoc())
                    {
                        graph.setValueSrcLoc(cloneResult, *resultInfo.srcLoc());
                    }
                    graph.addResult(cloneOp, cloneResult);

                    for (const auto user : users)
                    {
                        graph.replaceOperand(user.operation, user.operandIndex, cloneResult);
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

            return changed;
        }

        bool materializeFinalPartition(const wolvrix::lib::grh::Graph &graph,
                                       const SymbolPartition &partition,
                                       ActivityOpData &finalOpData,
                                       ActivityScheduleBuild &build,
                                       std::vector<uint32_t> &supernodeOfOp,
                                       std::string &error)
        {
            finalOpData = buildActivityOpData(graph, error);
            if (!error.empty())
            {
                return false;
            }

            std::unordered_map<wolvrix::lib::grh::SymbolId, std::pair<wolvrix::lib::grh::OperationId, uint32_t>,
                               ActivityScheduleSymbolIdHash>
                liveOpsBySymbol;
            liveOpsBySymbol.reserve(finalOpData.topoOps.size());
            for (std::size_t topoPos = 0; topoPos < finalOpData.topoOps.size(); ++topoPos)
            {
                liveOpsBySymbol.emplace(finalOpData.topoSymbols[topoPos],
                                        std::make_pair(finalOpData.topoOps[topoPos], static_cast<uint32_t>(topoPos)));
            }

            struct LiveCluster
            {
                uint32_t minTopoPos = kInvalidActivitySupernodeId;
                std::vector<uint32_t> topoPositions;
            };

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

            build = ActivityScheduleBuild{};
            build.supernodes.resize(liveClusters.size());
            std::iota(build.supernodes.begin(), build.supernodes.end(), 0);
            build.supernodeToOps.resize(liveClusters.size());
            build.supernodeToOpSymbols.resize(liveClusters.size());
            build.opToSupernode.assign(finalOpData.maxOpIndex, kInvalidActivitySupernodeId);
            build.opSymbolToSupernode.reserve(finalOpData.topoOps.size());
            build.dag.resize(liveClusters.size());
            build.opEventDomains.resize(finalOpData.maxOpIndex);
            if (finalOpData.maxOpIndex > 0)
            {
                build.valueEventDomains.resize(graph.values().empty() ? 0 : graph.values().back().index);
            }
            build.supernodeEventDomains.resize(liveClusters.size());
            supernodeOfOp.assign(finalOpData.maxOpIndex + 1, kInvalidActivitySupernodeId);

            for (std::size_t supernodeId = 0; supernodeId < liveClusters.size(); ++supernodeId)
            {
                for (const auto topoPos : liveClusters[supernodeId].topoPositions)
                {
                    const auto opId = finalOpData.topoOps[topoPos];
                    const auto opSym = finalOpData.topoSymbols[topoPos];
                    build.supernodeToOps[supernodeId].push_back(opId);
                    build.supernodeToOpSymbols[supernodeId].push_back(opSym);
                    build.opToSupernode[opId.index - 1] = static_cast<uint32_t>(supernodeId);
                    build.opSymbolToSupernode.emplace(opSym, static_cast<uint32_t>(supernodeId));
                    supernodeOfOp[opId.index] = static_cast<uint32_t>(supernodeId);
                }
            }

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

            wolvrix::lib::toposort::TopoDag<uint32_t> finalTopoDag;
            finalTopoDag.reserveNodes(build.supernodes.size());
            for (uint32_t supernodeId = 0; supernodeId < build.supernodes.size(); ++supernodeId)
            {
                finalTopoDag.addNode(supernodeId);
            }
            for (uint32_t supernodeId = 0; supernodeId < build.supernodes.size(); ++supernodeId)
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
            return true;
        }

        ActivityScheduleEventDomainSignature extractSinkSignature(
            const wolvrix::lib::grh::Graph &graph,
            const wolvrix::lib::grh::Operation &op,
            bool &valid)
        {
            valid = true;
            ActivityScheduleEventDomainSignature signature;
            const auto operands = op.operands();
            const auto eventEdges = getAttrValue<std::vector<std::string>>(op, "eventEdge");
            std::size_t eventStart = operands.size();

            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
                if (!eventEdges)
                {
                    valid = false;
                    return signature;
                }
                if (operands.size() < 3 || operands.size() < 3 + eventEdges->size())
                {
                    valid = false;
                    return signature;
                }
                eventStart = 3;
                break;
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
                if (!eventEdges)
                {
                    valid = false;
                    return signature;
                }
                if (operands.size() < 4 || operands.size() < 4 + eventEdges->size())
                {
                    valid = false;
                    return signature;
                }
                eventStart = 4;
                break;
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
                return signature;
            case wolvrix::lib::grh::OperationKind::kSystemTask:
            case wolvrix::lib::grh::OperationKind::kDpicCall:
                if (!eventEdges || eventEdges->empty())
                {
                    return signature;
                }
                if (operands.size() < eventEdges->size())
                {
                    valid = false;
                    return signature;
                }
                eventStart = operands.size() - eventEdges->size();
                break;
            default:
                return signature;
            }

            if (!eventEdges)
            {
                return signature;
            }
            for (std::size_t i = 0; i < eventEdges->size(); ++i)
            {
                signature.terms.push_back(ActivityScheduleEventTerm{operands[eventStart + i], (*eventEdges)[i]});
            }
            normalizeSignature(graph, signature);
            return signature;
        }

        bool hasReturnValue(const wolvrix::lib::grh::Operation &op) noexcept
        {
            const auto hasReturnAttr = getAttrValue<bool>(op, "hasReturn");
            if (hasReturnAttr)
            {
                return *hasReturnAttr;
            }
            return !op.results().empty();
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

        graph->freeze();
        std::string buildError;
        const ActivityOpData opData = buildActivityOpData(*graph, buildError);
        if (!buildError.empty())
        {
            error(*graph, buildError);
            result.failed = true;
            return result;
        }

        WorkingPartition partition = makeSeedPartition(opData);
        partition = canonicalizePartition(partition, opData);
        const std::size_t seedSupernodeCount = partition.clusters.size();

        if (!partition.clusters.empty() && options_.enableCoarsen)
        {
            bool changed = true;
            while (changed)
            {
                changed = false;
                if (options_.enableChainMerge)
                {
                    changed = tryMergeOut1(partition, opData, options_.supernodeMaxSize) || changed;
                    changed = tryMergeIn1(partition, opData, options_.supernodeMaxSize) || changed;
                }
                if (options_.enableSiblingMerge)
                {
                    changed = tryMergeSiblings(partition, opData, options_.supernodeMaxSize) || changed;
                }
                if (options_.enableForwardMerge)
                {
                    changed = tryMergeForwarders(partition, opData, options_.supernodeMaxSize) || changed;
                }
            }
        }

        partition = canonicalizePartition(partition, opData);
        const std::size_t coarseSupernodeCount = partition.clusters.size();
        ClusterView coarseView = buildClusterView(partition, opData);
        std::vector<std::vector<uint32_t>> segments = buildDpSegments(coarseView, options_.supernodeMaxSize);
        const std::size_t dpSupernodeCount = segments.size();
        if (options_.enableRefine)
        {
            segments = refineSegments(coarseView, std::move(segments), options_.supernodeMaxSize, options_.refineMaxIter);
        }
        partition = materializeSegments(coarseView, segments);
        partition = canonicalizePartition(partition, opData);

        SymbolPartition symbolPartition = buildSymbolPartition(partition, opData);
        ReplicationStats replicationStats;
        if (options_.enableReplication)
        {
            if (runReplicationPhase(*graph,
                                    symbolPartition,
                                    opData,
                                    options_.replicationMaxCost,
                                    options_.replicationMaxTargets,
                                    replicationStats,
                                    buildError))
            {
                graphChanged = true;
            }
            if (!buildError.empty())
            {
                error(*graph, buildError);
                result.failed = true;
                return result;
            }
        }

        graph->freeze();

        ActivityScheduleBuild build;
        ActivityOpData finalOpData;
        std::vector<uint32_t> supernodeOfOp;
        if (!materializeFinalPartition(*graph, symbolPartition, finalOpData, build, supernodeOfOp, buildError))
        {
            error(*graph, buildError);
            result.failed = true;
            return result;
        }

        std::vector<uint8_t> headEvalFlag(build.supernodes.size(), 0);
        for (const auto opId : finalOpData.topoOps)
        {
            bool isHeadEval = false;
            for (const auto operand : graph->opOperands(opId))
            {
                if (graph->valueIsInput(operand))
                {
                    isHeadEval = true;
                    break;
                }
                const auto defOp = graph->valueDef(operand);
                if (defOp.valid() && isStateReadOpKind(graph->opKind(defOp)))
                {
                    isHeadEval = true;
                    break;
                }
            }
            if (!isHeadEval)
            {
                continue;
            }
            const uint32_t supernodeId = supernodeOfOp[opId.index];
            if (supernodeId == kInvalidActivitySupernodeId || headEvalFlag[supernodeId] != 0)
            {
                continue;
            }
            headEvalFlag[supernodeId] = 1;
            build.headEvalSupernodes.push_back(supernodeId);
        }

        std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> sinkSet;
        sinkSet.reserve(graph->operations().size() / 8 + 8);
        for (const auto opId : graph->operations())
        {
            const auto op = graph->getOperation(opId);
            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kSystemTask:
                sinkSet.insert(opId);
                break;
            case wolvrix::lib::grh::OperationKind::kDpicCall:
                if (!hasReturnValue(op))
                {
                    sinkSet.insert(opId);
                }
                break;
            default:
                break;
            }
        }
        for (const auto &port : graph->outputPorts())
        {
            const auto defOp = graph->valueDef(port.value);
            if (defOp.valid())
            {
                sinkSet.insert(defOp);
            }
        }
        for (const auto &port : graph->inoutPorts())
        {
            const auto outDef = graph->valueDef(port.out);
            if (outDef.valid())
            {
                sinkSet.insert(outDef);
            }
            const auto oeDef = graph->valueDef(port.oe);
            if (oeDef.valid())
            {
                sinkSet.insert(oeDef);
            }
        }

        std::vector<wolvrix::lib::grh::OperationId> sinkOps(sinkSet.begin(), sinkSet.end());
        std::sort(sinkOps.begin(), sinkOps.end(),
                  [](const auto &lhs, const auto &rhs) { return lhs.index < rhs.index; });

        std::unordered_map<ActivityScheduleEventDomainSignature,
                           std::vector<wolvrix::lib::grh::OperationId>,
                           ActivityScheduleEventDomainSignatureHash>
            sinksBySignature;
        sinksBySignature.reserve(sinkOps.size());

        for (const auto sinkOpId : sinkOps)
        {
            const auto op = graph->getOperation(sinkOpId);
            bool signatureValid = false;
            auto signature = extractSinkSignature(*graph, op, signatureValid);
            if (!signatureValid)
            {
                error(*graph,
                      op,
                      "activity-schedule sink event-domain extraction failed for " + describeOp(*graph, sinkOpId));
                result.failed = true;
                continue;
            }
            build.eventDomainSinks.push_back(ActivityScheduleEventDomainSink{sinkOpId, signature});
            sinksBySignature[signature].push_back(sinkOpId);
        }
        if (result.failed)
        {
            return result;
        }

        std::sort(build.eventDomainSinks.begin(), build.eventDomainSinks.end(),
                  [](const auto &lhs, const auto &rhs) { return lhs.sinkOp.index < rhs.sinkOp.index; });

        std::vector<ActivityScheduleEventDomainSignature> signatureTable;
        signatureTable.reserve(sinksBySignature.size());
        for (const auto &entry : sinksBySignature)
        {
            signatureTable.push_back(entry.first);
        }
        std::sort(signatureTable.begin(), signatureTable.end(),
                  [&](const auto &lhs, const auto &rhs) { return signatureLess(*graph, lhs, rhs); });

        std::unordered_map<ActivityScheduleEventDomainSignature, uint32_t, ActivityScheduleEventDomainSignatureHash>
            signatureIds;
        signatureIds.reserve(signatureTable.size());
        build.eventDomainSinkGroups.reserve(signatureTable.size());
        for (uint32_t id = 0; id < signatureTable.size(); ++id)
        {
            signatureIds.emplace(signatureTable[id], id);
            auto sinks = sinksBySignature[signatureTable[id]];
            std::sort(sinks.begin(), sinks.end(),
                      [](const auto &lhs, const auto &rhs) { return lhs.index < rhs.index; });
            build.eventDomainSinkGroups.push_back(ActivityScheduleEventDomainSinkGroup{
                .signature = signatureTable[id],
                .sinkOps = std::move(sinks),
            });
        }

        std::vector<std::vector<uint32_t>> opSignatureIds(build.opEventDomains.size());
        std::vector<std::vector<uint32_t>> valueSignatureIds(build.valueEventDomains.size());
        std::vector<std::vector<uint32_t>> supernodeSignatureIds(build.supernodes.size());
        std::vector<uint8_t> isStopSinkByOpIndex(finalOpData.maxOpIndex + 1, 0);
        for (const auto sinkOpId : sinkOps)
        {
            if (isEventBoundarySinkKind(graph->opKind(sinkOpId)))
            {
                isStopSinkByOpIndex[sinkOpId.index] = 1;
            }
        }

        for (const auto &entry : sinksBySignature)
        {
            auto itSig = signatureIds.find(entry.first);
            if (itSig == signatureIds.end())
            {
                continue;
            }
            const uint32_t signatureId = itSig->second;

            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> groupSeeds(
                entry.second.begin(), entry.second.end());
            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> visited;
            visited.reserve(entry.second.size() * 4 + 16);
            std::vector<wolvrix::lib::grh::OperationId> stack(entry.second.begin(), entry.second.end());

            while (!stack.empty())
            {
                const auto opId = stack.back();
                stack.pop_back();
                if (!visited.insert(opId).second)
                {
                    continue;
                }

                if (opId.index > 0 && opId.index - 1 < opSignatureIds.size())
                {
                    opSignatureIds[opId.index - 1].push_back(signatureId);
                }
                if (opId.index < supernodeOfOp.size())
                {
                    const uint32_t supernodeId = supernodeOfOp[opId.index];
                    if (supernodeId != kInvalidActivitySupernodeId)
                    {
                        supernodeSignatureIds[supernodeId].push_back(signatureId);
                    }
                }

                const auto op = graph->getOperation(opId);
                for (const auto result : op.results())
                {
                    if (!result.valid() || result.index == 0 || result.index - 1 >= valueSignatureIds.size())
                    {
                        continue;
                    }
                    if (graph->valueIsOutput(result) || graph->valueIsInout(result))
                    {
                        valueSignatureIds[result.index - 1].push_back(signatureId);
                    }
                }
                for (const auto operand : op.operands())
                {
                    if (operand.valid() && operand.index > 0 && operand.index - 1 < valueSignatureIds.size())
                    {
                        valueSignatureIds[operand.index - 1].push_back(signatureId);
                    }
                    if (graph->valueIsInput(operand))
                    {
                        continue;
                    }
                    const auto defOp = graph->valueDef(operand);
                    if (!defOp.valid())
                    {
                        continue;
                    }
                    if (defOp.index >= isStopSinkByOpIndex.size())
                    {
                        continue;
                    }
                    if (isStopSinkByOpIndex[defOp.index] != 0 && groupSeeds.find(defOp) == groupSeeds.end())
                    {
                        continue;
                    }
                    const auto defKind = graph->opKind(defOp);
                    if (!participatesInBackwardPropagation(defKind))
                    {
                        if (isStateReadOpKind(defKind) && defOp.index < supernodeOfOp.size())
                        {
                            if (defOp.index > 0 && defOp.index - 1 < opSignatureIds.size())
                            {
                                opSignatureIds[defOp.index - 1].push_back(signatureId);
                            }
                            const uint32_t supernodeId = supernodeOfOp[defOp.index];
                            if (supernodeId != kInvalidActivitySupernodeId)
                            {
                                supernodeSignatureIds[supernodeId].push_back(signatureId);
                            }
                        }
                        continue;
                    }
                    stack.push_back(defOp);
                }
            }
        }

        for (std::size_t opIdx = 0; opIdx < opSignatureIds.size(); ++opIdx)
        {
            auto &ids = opSignatureIds[opIdx];
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            auto &domains = build.opEventDomains[opIdx];
            domains.reserve(ids.size());
            for (const auto signatureId : ids)
            {
                domains.push_back(signatureTable[signatureId]);
            }
        }

        for (std::size_t valueIdx = 0; valueIdx < valueSignatureIds.size(); ++valueIdx)
        {
            auto &ids = valueSignatureIds[valueIdx];
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            auto &domains = build.valueEventDomains[valueIdx];
            domains.reserve(ids.size());
            for (const auto signatureId : ids)
            {
                domains.push_back(signatureTable[signatureId]);
            }
        }

        for (std::size_t supernodeId = 0; supernodeId < supernodeSignatureIds.size(); ++supernodeId)
        {
            auto &ids = supernodeSignatureIds[supernodeId];
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            auto &domains = build.supernodeEventDomains[supernodeId];
            domains.reserve(ids.size());
            for (const auto signatureId : ids)
            {
                domains.push_back(signatureTable[signatureId]);
            }
            std::sort(domains.begin(), domains.end(),
                      [&](const auto &lhs, const auto &rhs) { return signatureLess(*graph, lhs, rhs); });
        }

        const std::string keyPrefix = options_.path + ".activity_schedule.";
        setSessionValue(keyPrefix + "supernodes", build.supernodes, "activity-schedule.supernodes");
        setSessionValue(keyPrefix + "supernode_to_ops",
                        build.supernodeToOps,
                        "activity-schedule.supernode-to-ops");
        setSessionValue(keyPrefix + "supernode_to_op_symbols",
                        build.supernodeToOpSymbols,
                        "activity-schedule.supernode-to-op-symbols");
        setSessionValue(keyPrefix + "op_to_supernode",
                        build.opToSupernode,
                        "activity-schedule.op-to-supernode");
        setSessionValue(keyPrefix + "op_symbol_to_supernode",
                        build.opSymbolToSupernode,
                        "activity-schedule.op-symbol-to-supernode");
        setSessionValue(keyPrefix + "dag", build.dag, "activity-schedule.dag");
        setSessionValue(keyPrefix + "topo_order", build.topoOrder, "activity-schedule.topo-order");
        setSessionValue(keyPrefix + "head_eval_supernodes",
                        build.headEvalSupernodes,
                        "activity-schedule.head-eval-supernodes");
        setSessionValue(keyPrefix + "op_event_domains",
                        build.opEventDomains,
                        "activity-schedule.op-event-domains");
        setSessionValue(keyPrefix + "value_event_domains",
                        build.valueEventDomains,
                        "activity-schedule.value-event-domains");
        setSessionValue(keyPrefix + "supernode_event_domains",
                        build.supernodeEventDomains,
                        "activity-schedule.supernode-event-domains");
        setSessionValue(keyPrefix + "event_domain_sinks",
                        build.eventDomainSinks,
                        "activity-schedule.event-domain-sinks");
        setSessionValue(keyPrefix + "event_domain_sink_groups",
                        build.eventDomainSinkGroups,
                        "activity-schedule.event-domain-sink-groups");

        std::ostringstream summary;
        summary << "activity-schedule: path=" << options_.path
                << " graph=" << graph->symbol()
                << " supernodes=" << build.supernodes.size()
                << " seed_supernodes=" << seedSupernodeCount
                << " coarse_supernodes=" << coarseSupernodeCount
                << " dp_supernodes=" << dpSupernodeCount
                << " eligible_ops=" << finalOpData.topoOps.size()
                << " replication_cloned=" << replicationStats.clonedOps
                << " replication_erased=" << replicationStats.erasedOps
                << " sink_count=" << build.eventDomainSinks.size()
                << " head_eval=" << build.headEvalSupernodes.size()
                << " graph_changed=" << (graphChanged ? "true" : "false");
        logInfo(summary.str());

        result.changed = graphChanged;
        result.failed = false;
        return result;
    }

} // namespace wolvrix::lib::transform
