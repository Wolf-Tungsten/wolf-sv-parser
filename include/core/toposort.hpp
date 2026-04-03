#ifndef WOLVRIX_TOPOSORT_HPP
#define WOLVRIX_TOPOSORT_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wolvrix::lib::toposort
{

    enum class DuplicateNodePolicy
    {
        kIgnore,
        kError,
    };

    struct TopoSortOptions
    {
        DuplicateNodePolicy duplicateNodePolicy = DuplicateNodePolicy::kIgnore;
        bool throwOnCycle = true;
    };

    [[noreturn]] void throwDuplicateNodeError();
    [[noreturn]] void throwCycleError();
    [[noreturn]] void throwNodeLimitError();
    [[noreturn]] void throwIndegreeOverflowError();

    template <typename NodeType,
              typename Hash = std::hash<NodeType>,
              typename Eq = std::equal_to<NodeType>>
    class TopoDag
    {
    public:
        static_assert(std::is_copy_constructible_v<NodeType>,
                      "TopoDag requires NodeType to be copy constructible");

        using NodeId = uint32_t;
        using EdgeOffset = uint64_t;

        explicit TopoDag(TopoSortOptions options = {})
            : options_(options)
        {
        }

        void reserveNodes(std::size_t count)
        {
            nodes_.reserve(count);
            nodeToId_.reserve(count);
        }

        void reserveEdges(std::size_t count)
        {
            rawEdges_.reserve(count);
        }

        NodeId addNode(const NodeType &node)
        {
            const auto it = nodeToId_.find(node);
            if (it != nodeToId_.end())
            {
                if (options_.duplicateNodePolicy == DuplicateNodePolicy::kError)
                {
                    throwDuplicateNodeError();
                }
                return it->second;
            }

            return createNode(node);
        }

        void addEdge(const NodeType &fromNode, const NodeType &toNode)
        {
            const NodeId fromId = ensureNode(fromNode);
            const NodeId toId = ensureNode(toNode);
            rawEdges_.push_back(packEdge(fromId, toId));
            uniqueEdgeCountValid_ = false;
        }

        std::size_t nodeCount() const noexcept
        {
            return nodes_.size();
        }

        std::size_t edgeCount() const
        {
            refreshUniqueEdgeCount();
            return uniqueEdgeCount_;
        }

        std::vector<std::vector<NodeType>> toposort() const
        {
            const std::size_t nodeCountValue = nodes_.size();
            if (nodeCountValue == 0)
            {
                return {};
            }

            std::vector<uint64_t> uniqueEdges = collectUniqueEdges();
            std::vector<EdgeOffset> rowOffsets(nodeCountValue + 1, 0);
            std::vector<uint32_t> indegree(nodeCountValue, 0);

            for (uint64_t packedEdge : uniqueEdges)
            {
                const NodeId from = unpackFrom(packedEdge);
                const NodeId to = unpackTo(packedEdge);
                ++rowOffsets[static_cast<std::size_t>(from) + 1];
                if (indegree[to] == std::numeric_limits<uint32_t>::max())
                {
                    throwIndegreeOverflowError();
                }
                ++indegree[to];
            }

            for (std::size_t i = 1; i < rowOffsets.size(); ++i)
            {
                rowOffsets[i] += rowOffsets[i - 1];
            }

            std::vector<EdgeOffset> writeOffsets = rowOffsets;
            std::vector<NodeId> colIndices(uniqueEdges.size(), 0);
            for (uint64_t packedEdge : uniqueEdges)
            {
                const NodeId from = unpackFrom(packedEdge);
                const NodeId to = unpackTo(packedEdge);
                colIndices[writeOffsets[from]++] = to;
            }

            std::vector<NodeId> frontier;
            frontier.reserve(nodeCountValue);
            for (NodeId id = 0; id < nodeCountValue; ++id)
            {
                if (indegree[id] == 0)
                {
                    frontier.push_back(id);
                }
            }

            std::vector<NodeId> nextFrontier;
            nextFrontier.reserve(nodeCountValue);
            std::vector<NodeId> orderedIds;
            orderedIds.reserve(nodeCountValue);
            std::vector<std::size_t> levelOffsets;
            levelOffsets.reserve(nodeCountValue + 1);
            levelOffsets.push_back(0);

            while (!frontier.empty())
            {
                nextFrontier.clear();
                orderedIds.insert(orderedIds.end(), frontier.begin(), frontier.end());

                for (NodeId nodeId : frontier)
                {
                    const EdgeOffset begin = rowOffsets[nodeId];
                    const EdgeOffset end = rowOffsets[static_cast<std::size_t>(nodeId) + 1];
                    for (EdgeOffset edgeIndex = begin; edgeIndex < end; ++edgeIndex)
                    {
                        const NodeId target = colIndices[edgeIndex];
                        const uint32_t nextIndegree = --indegree[target];
                        if (nextIndegree == 0)
                        {
                            nextFrontier.push_back(target);
                        }
                    }
                }

                levelOffsets.push_back(orderedIds.size());
                frontier.swap(nextFrontier);
            }

            if (orderedIds.size() != nodeCountValue && options_.throwOnCycle)
            {
                throwCycleError();
            }

            std::vector<std::vector<NodeType>> layers;
            layers.reserve(levelOffsets.size() - 1);
            for (std::size_t layerIndex = 1; layerIndex < levelOffsets.size(); ++layerIndex)
            {
                const std::size_t begin = levelOffsets[layerIndex - 1];
                const std::size_t end = levelOffsets[layerIndex];
                std::vector<NodeType> layer;
                layer.reserve(end - begin);
                for (std::size_t i = begin; i < end; ++i)
                {
                    layer.push_back(nodes_[orderedIds[i]]);
                }
                layers.push_back(std::move(layer));
            }
            return layers;
        }

    private:
        static constexpr uint64_t packEdge(NodeId from, NodeId to) noexcept
        {
            return (static_cast<uint64_t>(from) << 32) | static_cast<uint64_t>(to);
        }

        static constexpr NodeId unpackFrom(uint64_t packedEdge) noexcept
        {
            return static_cast<NodeId>(packedEdge >> 32);
        }

        static constexpr NodeId unpackTo(uint64_t packedEdge) noexcept
        {
            return static_cast<NodeId>(packedEdge & 0xffffffffULL);
        }

        NodeId createNode(const NodeType &node)
        {
            if (nodes_.size() > static_cast<std::size_t>(std::numeric_limits<NodeId>::max()))
            {
                throwNodeLimitError();
            }

            const NodeId nextId = static_cast<NodeId>(nodes_.size());
            nodeToId_.emplace(node, nextId);
            nodes_.push_back(node);
            return nextId;
        }

        NodeId ensureNode(const NodeType &node)
        {
            const auto it = nodeToId_.find(node);
            if (it != nodeToId_.end())
            {
                return it->second;
            }
            return createNode(node);
        }

        std::vector<uint64_t> collectUniqueEdges() const
        {
            std::vector<uint64_t> uniqueEdges = rawEdges_;
            std::sort(uniqueEdges.begin(), uniqueEdges.end());
            uniqueEdges.erase(std::unique(uniqueEdges.begin(), uniqueEdges.end()), uniqueEdges.end());
            uniqueEdgeCount_ = uniqueEdges.size();
            uniqueEdgeCountValid_ = true;
            return uniqueEdges;
        }

        void refreshUniqueEdgeCount() const
        {
            if (uniqueEdgeCountValid_)
            {
                return;
            }
            const auto uniqueEdges = collectUniqueEdges();
            (void)uniqueEdges;
        }

        TopoSortOptions options_;
        std::vector<NodeType> nodes_;
        std::unordered_map<NodeType, NodeId, Hash, Eq> nodeToId_;
        std::vector<uint64_t> rawEdges_;
        mutable bool uniqueEdgeCountValid_ = true;
        mutable std::size_t uniqueEdgeCount_ = 0;
    };

    template <typename NodeType,
              typename Hash = std::hash<NodeType>,
              typename Eq = std::equal_to<NodeType>>
    class TopoDagBuilder
    {
    public:
        using DagType = TopoDag<NodeType, Hash, Eq>;

        class LocalBuilder
        {
        public:
            void reserveNodes(std::size_t count)
            {
                data().localNodes.reserve(count);
            }

            void reserveEdges(std::size_t count)
            {
                data().localEdges.reserve(count);
            }

            void addNode(const NodeType &node)
            {
                data().localNodes.push_back(node);
            }

            void addEdge(const NodeType &fromNode, const NodeType &toNode)
            {
                data().localEdges.emplace_back(fromNode, toNode);
            }

        private:
            struct LocalData
            {
                std::vector<NodeType> localNodes;
                std::vector<std::pair<NodeType, NodeType>> localEdges;
            };

            friend class TopoDagBuilder;

            LocalBuilder(TopoDagBuilder *owner, std::size_t index)
                : owner_(owner), index_(index)
            {
            }

            LocalData &data() const
            {
                return *owner_->locals_.at(index_);
            }

            TopoDagBuilder *owner_ = nullptr;
            std::size_t index_ = 0;
        };

        explicit TopoDagBuilder(TopoSortOptions options = {})
            : options_(options)
        {
        }

        void reserveLocalBuilders(std::size_t count)
        {
            locals_.reserve(count);
        }

        LocalBuilder createLocalBuilder()
        {
            locals_.push_back(std::make_unique<typename LocalBuilder::LocalData>());
            return LocalBuilder(this, locals_.size() - 1);
        }

        DagType finalize() const
        {
            DagType dag(options_);

            std::size_t totalExplicitNodes = 0;
            std::size_t totalEdges = 0;
            for (const auto &local : locals_)
            {
                totalExplicitNodes += local->localNodes.size();
                totalEdges += local->localEdges.size();
            }

            dag.reserveNodes(totalExplicitNodes);
            dag.reserveEdges(totalEdges);

            for (const auto &local : locals_)
            {
                for (const auto &node : local->localNodes)
                {
                    dag.addNode(node);
                }
            }

            for (const auto &local : locals_)
            {
                for (const auto &[fromNode, toNode] : local->localEdges)
                {
                    dag.addEdge(fromNode, toNode);
                }
            }

            return dag;
        }

    private:
        TopoSortOptions options_;
        std::vector<std::unique_ptr<typename LocalBuilder::LocalData>> locals_;
    };

} // namespace wolvrix::lib::toposort

#endif // WOLVRIX_TOPOSORT_HPP
