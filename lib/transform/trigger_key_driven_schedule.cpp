#include "transform/trigger_key_driven_schedule.hpp"

#include "core/toposort.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wolvrix::lib::transform
{

    namespace
    {
        using wolvrix::lib::grh::AttributeValue;
        using wolvrix::lib::grh::Graph;
        using wolvrix::lib::grh::Operation;
        using wolvrix::lib::grh::OperationId;
        using wolvrix::lib::grh::OperationIdHash;
        using wolvrix::lib::grh::OperationKind;
        using wolvrix::lib::grh::Value;
        using wolvrix::lib::grh::ValueId;
        using wolvrix::lib::grh::ValueIdHash;

        template <typename T>
        std::optional<T> getAttr(const Operation &op, std::string_view key)
        {
            const std::optional<AttributeValue> attr = op.attr(key);
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

        std::optional<std::vector<std::string>> getAttrStrings(const Operation &op, std::string_view key)
        {
            return getAttr<std::vector<std::string>>(op, key);
        }

        std::optional<std::string> getAttrString(const Operation &op, std::string_view key)
        {
            return getAttr<std::string>(op, key);
        }

        bool lessValueId(ValueId lhs, ValueId rhs)
        {
            return std::tie(lhs.graph.index, lhs.graph.generation, lhs.index, lhs.generation) <
                   std::tie(rhs.graph.index, rhs.graph.generation, rhs.index, rhs.generation);
        }

        bool lessOperationId(OperationId lhs, OperationId rhs)
        {
            return std::tie(lhs.graph.index, lhs.graph.generation, lhs.index, lhs.generation) <
                   std::tie(rhs.graph.index, rhs.graph.generation, rhs.index, rhs.generation);
        }

        bool lessTriggerItem(const TkdTriggerEventItem &lhs, const TkdTriggerEventItem &rhs)
        {
            if (lhs.valueId != rhs.valueId)
            {
                return lessValueId(lhs.valueId, rhs.valueId);
            }
            return static_cast<uint8_t>(lhs.eventEdge) < static_cast<uint8_t>(rhs.eventEdge);
        }

        struct TriggerKeyItemsHash
        {
            std::size_t operator()(const std::vector<TkdTriggerEventItem> &items) const noexcept
            {
                std::size_t seed = items.size();
                for (const auto &item : items)
                {
                    seed = seed * 1315423911u + item.valueId.index;
                    seed = seed * 1315423911u + item.valueId.generation;
                    seed = seed * 1315423911u + item.valueId.graph.index;
                    seed = seed * 1315423911u + item.valueId.graph.generation;
                    seed = seed * 1315423911u + static_cast<uint8_t>(item.eventEdge);
                }
                return seed;
            }
        };

        struct TriggerKeyItemsEq
        {
            bool operator()(const std::vector<TkdTriggerEventItem> &lhs,
                            const std::vector<TkdTriggerEventItem> &rhs) const noexcept
            {
                if (lhs.size() != rhs.size())
                {
                    return false;
                }
                for (std::size_t i = 0; i < lhs.size(); ++i)
                {
                    if (lhs[i].valueId != rhs[i].valueId || lhs[i].eventEdge != rhs[i].eventEdge)
                    {
                        return false;
                    }
                }
                return true;
            }
        };

        struct SinkSetHash
        {
            std::size_t operator()(const std::vector<SinkTKDGroupId> &items) const noexcept
            {
                std::size_t seed = items.size();
                for (SinkTKDGroupId item : items)
                {
                    seed = seed * 1315423911u + item;
                }
                return seed;
            }
        };

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

        OperationId findUniqueInstance(const Graph &graph, std::string_view instanceName)
        {
            OperationId found = OperationId::invalid();
            for (const OperationId opId : graph.operations())
            {
                if (!opId.valid())
                {
                    continue;
                }
                const Operation op = graph.getOperation(opId);
                if (op.kind() != OperationKind::kInstance)
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
                    return OperationId::invalid();
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
                error = "trigger-key-driven-schedule path must not be empty";
                return std::nullopt;
            }
            if (segments.size() == 1)
            {
                if (design.findGraph(segments.front()) == nullptr)
                {
                    error = "trigger-key-driven-schedule graph not found: " + segments.front();
                    return std::nullopt;
                }
                return segments.front();
            }

            auto *current = design.findGraph(segments.front());
            if (current == nullptr)
            {
                error = "trigger-key-driven-schedule root graph not found: " + segments.front();
                return std::nullopt;
            }
            for (std::size_t i = 1; i < segments.size(); ++i)
            {
                const OperationId instOp = findUniqueInstance(*current, segments[i]);
                if (!instOp.valid())
                {
                    error = "trigger-key-driven-schedule instance not found or not unique: " + segments[i];
                    return std::nullopt;
                }
                const Operation op = current->getOperation(instOp);
                const auto moduleName = getAttrString(op, "moduleName");
                if (!moduleName || moduleName->empty())
                {
                    error = "trigger-key-driven-schedule instance missing moduleName: " + segments[i];
                    return std::nullopt;
                }
                current = design.findGraph(*moduleName);
                if (current == nullptr)
                {
                    error = "trigger-key-driven-schedule target graph not found: " + *moduleName;
                    return std::nullopt;
                }
            }
            return current->symbol();
        }

        bool isForbiddenHierarchicalOpKind(OperationKind kind)
        {
            switch (kind)
            {
            case OperationKind::kInstance:
            case OperationKind::kBlackbox:
            case OperationKind::kXMRRead:
            case OperationKind::kXMRWrite:
                return true;
            default:
                return false;
            }
        }

        bool isDeclarationCarrierOpKind(OperationKind kind)
        {
            switch (kind)
            {
            case OperationKind::kRegister:
            case OperationKind::kMemory:
            case OperationKind::kLatch:
            case OperationKind::kDpicImport:
                return true;
            default:
                return false;
            }
        }

        bool isIntrinsicSinkOp(const Operation &op)
        {
            switch (op.kind())
            {
            case OperationKind::kRegisterWritePort:
            case OperationKind::kMemoryWritePort:
            case OperationKind::kLatchWritePort:
            case OperationKind::kSystemTask:
                return true;
            case OperationKind::kDpicCall:
                return op.results().empty();
            default:
                return false;
            }
        }

        std::optional<TriggerEventEdge> parseTriggerEventEdge(std::string_view text)
        {
            if (text == "posedge")
            {
                return TriggerEventEdge::kPosedge;
            }
            if (text == "negedge")
            {
                return TriggerEventEdge::kNegedge;
            }
            return std::nullopt;
        }

        bool mergeSinkGroupSet(std::vector<SinkTKDGroupId> &dst,
                               const std::vector<SinkTKDGroupId> &src)
        {
            if (src.empty())
            {
                return false;
            }
            std::vector<SinkTKDGroupId> merged;
            merged.reserve(dst.size() + src.size());
            std::set_union(dst.begin(), dst.end(), src.begin(), src.end(), std::back_inserter(merged));
            if (merged == dst)
            {
                return false;
            }
            dst = std::move(merged);
            return true;
        }

        bool addSinkGroup(std::vector<SinkTKDGroupId> &dst, SinkTKDGroupId sinkGroupId)
        {
            const auto it = std::lower_bound(dst.begin(), dst.end(), sinkGroupId);
            if (it != dst.end() && *it == sinkGroupId)
            {
                return false;
            }
            dst.insert(it, sinkGroupId);
            return true;
        }

        TriggerKeyId internTriggerKey(
            std::vector<TkdTriggerKeyRecord> &pool,
            std::unordered_map<std::vector<TkdTriggerEventItem>,
                               TriggerKeyId,
                               TriggerKeyItemsHash,
                               TriggerKeyItemsEq> &intern,
            std::vector<TkdTriggerEventItem> items)
        {
            std::sort(items.begin(), items.end(), lessTriggerItem);
            items.erase(std::unique(items.begin(),
                                    items.end(),
                                    [](const TkdTriggerEventItem &lhs, const TkdTriggerEventItem &rhs) {
                                        return lhs.valueId == rhs.valueId && lhs.eventEdge == rhs.eventEdge;
                                    }),
                        items.end());

            const auto found = intern.find(items);
            if (found != intern.end())
            {
                return found->second;
            }

            const TriggerKeyId id = static_cast<TriggerKeyId>(pool.size());
            pool.push_back(TkdTriggerKeyRecord{id, items});
            intern.emplace(pool.back().items, id);
            return id;
        }

        AffectedSinkSetId internAffectedSinkSet(
            std::vector<TkdAffectedSinkSetRecord> &pool,
            std::unordered_map<std::vector<SinkTKDGroupId>, AffectedSinkSetId, SinkSetHash> &intern,
            std::vector<SinkTKDGroupId> sinkGroupIds)
        {
            std::sort(sinkGroupIds.begin(), sinkGroupIds.end());
            sinkGroupIds.erase(std::unique(sinkGroupIds.begin(), sinkGroupIds.end()), sinkGroupIds.end());

            const auto found = intern.find(sinkGroupIds);
            if (found != intern.end())
            {
                return found->second;
            }

            const AffectedSinkSetId id = static_cast<AffectedSinkSetId>(pool.size());
            pool.push_back(TkdAffectedSinkSetRecord{id, sinkGroupIds});
            intern.emplace(pool.back().sinkGroupIds, id);
            return id;
        }

        std::string sessionKey(std::string_view modulePath, std::string_view suffix)
        {
            std::string key = "tkds/";
            key.append(modulePath);
            key.push_back('/');
            key.append(suffix);
            return key;
        }

        std::size_t maxOperationIndex(const Graph &graph)
        {
            std::size_t maxIndex = 0;
            for (const OperationId opId : graph.operations())
            {
                maxIndex = std::max(maxIndex, static_cast<std::size_t>(opId.index));
            }
            return maxIndex;
        }

        std::vector<TkdTriggerEventItem> extractTriggerKeyItems(const Operation &op, std::string &error)
        {
            std::vector<TkdTriggerEventItem> items;
            switch (op.kind())
            {
            case OperationKind::kLatchWritePort:
                return items;
            case OperationKind::kRegisterWritePort:
            {
                const auto edgeTexts = getAttrStrings(op, "eventEdge");
                if (!edgeTexts)
                {
                    error = "kRegisterWritePort missing eventEdge attribute";
                    return {};
                }
                if (op.operands().size() < 3 || edgeTexts->size() != op.operands().size() - 3)
                {
                    error = "kRegisterWritePort eventEdge count does not match event operands";
                    return {};
                }
                items.reserve(edgeTexts->size());
                for (std::size_t i = 0; i < edgeTexts->size(); ++i)
                {
                    const auto edge = parseTriggerEventEdge((*edgeTexts)[i]);
                    if (!edge)
                    {
                        error = "kRegisterWritePort contains unsupported eventEdge value";
                        return {};
                    }
                    items.push_back(TkdTriggerEventItem{op.operands()[i + 3], *edge});
                }
                return items;
            }
            case OperationKind::kMemoryWritePort:
            {
                const auto edgeTexts = getAttrStrings(op, "eventEdge");
                if (!edgeTexts)
                {
                    error = "kMemoryWritePort missing eventEdge attribute";
                    return {};
                }
                if (op.operands().size() < 4 || edgeTexts->size() != op.operands().size() - 4)
                {
                    error = "kMemoryWritePort eventEdge count does not match event operands";
                    return {};
                }
                items.reserve(edgeTexts->size());
                for (std::size_t i = 0; i < edgeTexts->size(); ++i)
                {
                    const auto edge = parseTriggerEventEdge((*edgeTexts)[i]);
                    if (!edge)
                    {
                        error = "kMemoryWritePort contains unsupported eventEdge value";
                        return {};
                    }
                    items.push_back(TkdTriggerEventItem{op.operands()[i + 4], *edge});
                }
                return items;
            }
            case OperationKind::kSystemTask:
            case OperationKind::kDpicCall:
            {
                const std::vector<std::string> edgeTexts = getAttrStrings(op, "eventEdge").value_or(
                    std::vector<std::string>{});
                if (edgeTexts.size() > op.operands().size())
                {
                    error = "effect sink eventEdge count exceeds operand count";
                    return {};
                }
                items.reserve(edgeTexts.size());
                const std::size_t firstEvent = op.operands().size() - edgeTexts.size();
                for (std::size_t i = 0; i < edgeTexts.size(); ++i)
                {
                    const auto edge = parseTriggerEventEdge(edgeTexts[i]);
                    if (!edge)
                    {
                        error = "effect sink contains unsupported eventEdge value";
                        return {};
                    }
                    items.push_back(TkdTriggerEventItem{op.operands()[firstEvent + i], *edge});
                }
                return items;
            }
            default:
                return items;
            }
        }

        std::size_t normalizeTopLevelObservableSinks(Graph &graph)
        {
            std::unordered_map<ValueId, ValueId, ValueIdHash> replacementByValue;
            replacementByValue.reserve(graph.outputPorts().size() + graph.inoutPorts().size() * 2);

            auto ensureReplacement = [&](ValueId valueId) {
                if (!valueId.valid())
                {
                    return;
                }
                if (replacementByValue.find(valueId) != replacementByValue.end())
                {
                    return;
                }

                const Value value = graph.getValue(valueId);
                if (!value.definingOp().valid() || value.users().empty())
                {
                    return;
                }

                const OperationId assignOp = graph.createOperation(OperationKind::kAssign);
                const ValueId assignResult = graph.createValue(value.width(), value.isSigned(), value.type());
                graph.addOperand(assignOp, valueId);
                graph.addResult(assignOp, assignResult);
                graph.setOpSrcLoc(assignOp, makeTransformSrcLoc("trigger-key-driven-schedule", "normalizeTopLevelSink"));
                graph.setValueSrcLoc(assignResult,
                                    makeTransformSrcLoc("trigger-key-driven-schedule", "normalizeTopLevelSink"));
                replacementByValue.emplace(valueId, assignResult);
            };

            for (const auto &port : graph.outputPorts())
            {
                ensureReplacement(port.value);
            }
            for (const auto &port : graph.inoutPorts())
            {
                ensureReplacement(port.out);
                ensureReplacement(port.oe);
            }

            if (replacementByValue.empty())
            {
                return 0;
            }

            for (const auto &port : graph.outputPorts())
            {
                const auto found = replacementByValue.find(port.value);
                if (found != replacementByValue.end())
                {
                    graph.bindOutputPort(port.name, found->second);
                }
            }
            for (const auto &port : graph.inoutPorts())
            {
                const ValueId outValue =
                    replacementByValue.contains(port.out) ? replacementByValue.at(port.out) : port.out;
                const ValueId oeValue =
                    replacementByValue.contains(port.oe) ? replacementByValue.at(port.oe) : port.oe;
                if (outValue != port.out || oeValue != port.oe)
                {
                    graph.bindInoutPort(port.name, port.in, outValue, oeValue);
                }
            }

            return replacementByValue.size();
        }

        std::unordered_set<OperationId, OperationIdHash> collectTopLevelObservableSinkOps(const Graph &graph)
        {
            std::unordered_set<OperationId, OperationIdHash> sinkOps;
            sinkOps.reserve(graph.outputPorts().size() + graph.inoutPorts().size() * 2);

            auto collect = [&](ValueId valueId) {
                if (!valueId.valid())
                {
                    return;
                }
                const OperationId defOp = graph.valueDef(valueId);
                if (defOp.valid())
                {
                    sinkOps.insert(defOp);
                }
            };

            for (const auto &port : graph.outputPorts())
            {
                collect(port.value);
            }
            for (const auto &port : graph.inoutPorts())
            {
                collect(port.out);
                collect(port.oe);
            }
            return sinkOps;
        }

        uint64_t packGroupEdge(TkdGroupId srcGroupId, TkdGroupId dstGroupId) noexcept
        {
            return (static_cast<uint64_t>(srcGroupId) << 32) | static_cast<uint64_t>(dstGroupId);
        }

    } // namespace

    TriggerKeyDrivenSchedulePass::TriggerKeyDrivenSchedulePass()
        : Pass("trigger-key-driven-schedule",
               "trigger-key-driven-schedule",
               "Build trigger-key-driven scheduling groups for one GRH graph"),
          options_({})
    {
    }

    TriggerKeyDrivenSchedulePass::TriggerKeyDrivenSchedulePass(TriggerKeyDrivenScheduleOptions options)
        : Pass("trigger-key-driven-schedule",
               "trigger-key-driven-schedule",
               "Build trigger-key-driven scheduling groups for one GRH graph"),
          options_(std::move(options))
    {
    }

    PassResult TriggerKeyDrivenSchedulePass::run()
    {
        PassResult result;
        if (options_.path.empty())
        {
            error("trigger-key-driven-schedule requires -path");
            result.failed = true;
            return result;
        }

        std::string resolveError;
        const std::optional<std::string> targetGraphName = resolveTargetGraphName(design(), options_.path, resolveError);
        if (!targetGraphName)
        {
            error(resolveError);
            result.failed = true;
            return result;
        }

        Graph *graph = design().findGraph(*targetGraphName);
        if (graph == nullptr)
        {
            error("trigger-key-driven-schedule target graph not found: " + *targetGraphName);
            result.failed = true;
            return result;
        }

        for (const OperationId opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            if (isForbiddenHierarchicalOpKind(op.kind()))
            {
                error(*graph,
                      op,
                      "trigger-key-driven-schedule requires a de-hierarchized GRH graph without instance/blackbox/XMR ops");
                result.failed = true;
                return result;
            }
        }

        const std::size_t normalizedTopLevelSinkAssignCount = normalizeTopLevelObservableSinks(*graph);
        graph->freeze();
        result.changed = normalizedTopLevelSinkAssignCount != 0;

        const auto topLevelObservableSinkOps = collectTopLevelObservableSinkOps(*graph);
        const std::size_t opIndexLimit = maxOperationIndex(*graph) + 1;

        std::vector<TkdTriggerKeyRecord> triggerKeyPool;
        std::unordered_map<std::vector<TkdTriggerEventItem>,
                           TriggerKeyId,
                           TriggerKeyItemsHash,
                           TriggerKeyItemsEq>
            triggerKeyIntern;
        triggerKeyPool.reserve(16);
        triggerKeyPool.push_back(TkdTriggerKeyRecord{0, {}});
        triggerKeyIntern.emplace(triggerKeyPool.front().items, triggerKeyPool.front().triggerKeyId);

        std::vector<TkdAffectedSinkSetRecord> affectedSinkSetPool;
        std::unordered_map<std::vector<SinkTKDGroupId>, AffectedSinkSetId, SinkSetHash> affectedSinkSetIntern;
        affectedSinkSetPool.reserve(16);
        affectedSinkSetPool.push_back(TkdAffectedSinkSetRecord{0, {}});
        affectedSinkSetIntern.emplace(affectedSinkSetPool.front().sinkGroupIds,
                                      affectedSinkSetPool.front().affectedSinkSetId);

        std::vector<SinkTKDGroupId> sinkGroupIdByOpIndex(opIndexLimit, kInvalidSinkTkdGroupId);
        std::vector<uint8_t> opRoleByIndex(opIndexLimit, 0);
        std::unordered_map<TriggerKeyId, SinkTKDGroupId> sinkGroupByTriggerKeyId;
        std::vector<TkdSinkGroupRecord> sinkGroups;
        sinkGroups.reserve(16);

        for (const OperationId opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            const bool isSink = isIntrinsicSinkOp(op) || topLevelObservableSinkOps.find(opId) != topLevelObservableSinkOps.end();
            if (!isSink)
            {
                continue;
            }

            std::vector<TkdTriggerEventItem> triggerItems;
            if (topLevelObservableSinkOps.find(opId) == topLevelObservableSinkOps.end() || isIntrinsicSinkOp(op))
            {
                std::string triggerError;
                triggerItems = extractTriggerKeyItems(op, triggerError);
                if (!triggerError.empty())
                {
                    error(*graph, op, triggerError);
                    result.failed = true;
                    return result;
                }
            }

            const TriggerKeyId triggerKeyId =
                internTriggerKey(triggerKeyPool, triggerKeyIntern, std::move(triggerItems));

            auto foundGroup = sinkGroupByTriggerKeyId.find(triggerKeyId);
            SinkTKDGroupId sinkGroupId = kInvalidSinkTkdGroupId;
            if (foundGroup == sinkGroupByTriggerKeyId.end())
            {
                sinkGroupId = static_cast<SinkTKDGroupId>(sinkGroups.size());
                sinkGroupByTriggerKeyId.emplace(triggerKeyId, sinkGroupId);
                sinkGroups.push_back(TkdSinkGroupRecord{
                    sinkGroupId,
                    kInvalidTkdGroupId,
                    triggerKeyId,
                    kInvalidAffectedSinkSetId,
                    {},
                });
            }
            else
            {
                sinkGroupId = foundGroup->second;
            }

            sinkGroups[sinkGroupId].memberOps.push_back(opId);
            sinkGroupIdByOpIndex[opId.index] = sinkGroupId;
            opRoleByIndex[opId.index] = 1;
        }

        for (auto &sinkGroup : sinkGroups)
        {
            sinkGroup.affectedSinkSetId =
                internAffectedSinkSet(affectedSinkSetPool, affectedSinkSetIntern, {sinkGroup.sinkGroupId});
        }

        std::vector<SinkTKDGroupId> triggerAffectedSinkGroups;
        triggerAffectedSinkGroups.reserve(sinkGroups.size());
        std::unordered_set<ValueId, ValueIdHash> triggerRootSeen;
        std::vector<ValueId> triggerRootValues;
        for (const auto &sinkGroup : sinkGroups)
        {
            if (sinkGroup.triggerKeyId == 0)
            {
                continue;
            }
            addSinkGroup(triggerAffectedSinkGroups, sinkGroup.sinkGroupId);
            for (const auto &item : triggerKeyPool[sinkGroup.triggerKeyId].items)
            {
                if (triggerRootSeen.insert(item.valueId).second)
                {
                    triggerRootValues.push_back(item.valueId);
                }
            }
        }
        std::sort(triggerRootValues.begin(), triggerRootValues.end(), lessValueId);

        std::unordered_set<ValueId, ValueIdHash> visitedTriggerValues;
        std::unordered_set<OperationId, OperationIdHash> visitedTriggerOps;
        std::deque<ValueId> triggerQueue;
        visitedTriggerValues.reserve(triggerRootValues.size() * 2 + 1);
        visitedTriggerOps.reserve(triggerRootValues.size() * 2 + 1);
        for (ValueId valueId : triggerRootValues)
        {
            if (visitedTriggerValues.insert(valueId).second)
            {
                triggerQueue.push_back(valueId);
            }
        }

        std::vector<OperationId> triggerMemberOps;
        while (!triggerQueue.empty())
        {
            const ValueId valueId = triggerQueue.front();
            triggerQueue.pop_front();

            const OperationId defOpId = graph->valueDef(valueId);
            if (!defOpId.valid())
            {
                continue;
            }
            if (opRoleByIndex[defOpId.index] == 1)
            {
                continue;
            }

            const Operation defOp = graph->getOperation(defOpId);
            if (isDeclarationCarrierOpKind(defOp.kind()) || isForbiddenHierarchicalOpKind(defOp.kind()))
            {
                continue;
            }
            if (!visitedTriggerOps.insert(defOpId).second)
            {
                continue;
            }

            triggerMemberOps.push_back(defOpId);
            opRoleByIndex[defOpId.index] = 2;
            for (ValueId operand : defOp.operands())
            {
                if (visitedTriggerValues.insert(operand).second)
                {
                    triggerQueue.push_back(operand);
                }
            }
        }
        std::sort(triggerMemberOps.begin(), triggerMemberOps.end(), lessOperationId);

        TkdTriggerGroupRecord triggerGroup;
        triggerGroup.triggerGroupId = kDefaultTriggerTkdGroupId;
        triggerGroup.affectedSinkSetId =
            internAffectedSinkSet(affectedSinkSetPool, affectedSinkSetIntern, std::move(triggerAffectedSinkGroups));
        triggerGroup.rootValues = std::move(triggerRootValues);
        triggerGroup.memberOps = std::move(triggerMemberOps);

        std::vector<std::vector<SinkTKDGroupId>> affectedSinkGroupsByOpIndex(opIndexLimit);
        std::vector<uint8_t> inQueue(opIndexLimit, 0);
        std::deque<OperationId> workQueue;

        auto isSimpleCandidateOp = [&](OperationId opId) {
            const uint8_t role = opRoleByIndex[opId.index];
            if (role != 0)
            {
                return false;
            }
            const Operation op = graph->getOperation(opId);
            return !isDeclarationCarrierOpKind(op.kind()) && !isForbiddenHierarchicalOpKind(op.kind());
        };

        for (const auto &sinkGroup : sinkGroups)
        {
            for (const OperationId sinkOpId : sinkGroup.memberOps)
            {
                const Operation sinkOp = graph->getOperation(sinkOpId);
                for (ValueId operand : sinkOp.operands())
                {
                    const OperationId defOpId = graph->valueDef(operand);
                    if (!defOpId.valid() || !isSimpleCandidateOp(defOpId))
                    {
                        continue;
                    }
                    if (addSinkGroup(affectedSinkGroupsByOpIndex[defOpId.index], sinkGroup.sinkGroupId) &&
                        !inQueue[defOpId.index])
                    {
                        inQueue[defOpId.index] = 1;
                        workQueue.push_back(defOpId);
                    }
                }
            }
        }

        while (!workQueue.empty())
        {
            const OperationId opId = workQueue.front();
            workQueue.pop_front();
            inQueue[opId.index] = 0;

            const std::vector<SinkTKDGroupId> sinkSet = affectedSinkGroupsByOpIndex[opId.index];
            const Operation op = graph->getOperation(opId);
            for (ValueId operand : op.operands())
            {
                const OperationId defOpId = graph->valueDef(operand);
                if (!defOpId.valid() || !isSimpleCandidateOp(defOpId))
                {
                    continue;
                }
                if (mergeSinkGroupSet(affectedSinkGroupsByOpIndex[defOpId.index], sinkSet) && !inQueue[defOpId.index])
                {
                    inQueue[defOpId.index] = 1;
                    workQueue.push_back(defOpId);
                }
            }
        }

        std::unordered_map<AffectedSinkSetId, SimpleTKDGroupId> simpleGroupByAffectedSinkSetId;
        std::vector<SimpleTKDGroupId> simpleGroupIdByOpIndex(opIndexLimit, kInvalidSimpleTkdGroupId);
        std::vector<TkdSimpleGroupRecord> simpleGroups;
        simpleGroups.reserve(16);
        for (const OperationId opId : graph->operations())
        {
            if (!isSimpleCandidateOp(opId))
            {
                continue;
            }

            const AffectedSinkSetId affectedSinkSetId =
                internAffectedSinkSet(affectedSinkSetPool,
                                      affectedSinkSetIntern,
                                      affectedSinkGroupsByOpIndex[opId.index]);
            auto foundGroup = simpleGroupByAffectedSinkSetId.find(affectedSinkSetId);
            SimpleTKDGroupId simpleGroupId = kInvalidSimpleTkdGroupId;
            if (foundGroup == simpleGroupByAffectedSinkSetId.end())
            {
                simpleGroupId = static_cast<SimpleTKDGroupId>(simpleGroups.size());
                simpleGroupByAffectedSinkSetId.emplace(affectedSinkSetId, simpleGroupId);
                simpleGroups.push_back(TkdSimpleGroupRecord{
                    simpleGroupId,
                    kInvalidTkdGroupId,
                    affectedSinkSetId,
                    {},
                });
            }
            else
            {
                simpleGroupId = foundGroup->second;
            }
            simpleGroups[simpleGroupId].memberOps.push_back(opId);
            simpleGroupIdByOpIndex[opId.index] = simpleGroupId;
        }

        for (std::size_t i = 0; i < sinkGroups.size(); ++i)
        {
            sinkGroups[i].tkdGroupId = static_cast<TkdGroupId>(i);
        }
        triggerGroup.tkdGroupId = static_cast<TkdGroupId>(sinkGroups.size());
        for (std::size_t i = 0; i < simpleGroups.size(); ++i)
        {
            simpleGroups[i].tkdGroupId = static_cast<TkdGroupId>(sinkGroups.size() + 1 + i);
        }

        const std::size_t tkdGroupCount = sinkGroups.size() + 1 + simpleGroups.size();
        std::vector<TkdGroupId> groupIdByOpIndex(opIndexLimit, kInvalidTkdGroupId);
        for (const auto &sinkGroup : sinkGroups)
        {
            for (OperationId opId : sinkGroup.memberOps)
            {
                groupIdByOpIndex[opId.index] = sinkGroup.tkdGroupId;
            }
        }
        for (OperationId opId : triggerGroup.memberOps)
        {
            groupIdByOpIndex[opId.index] = triggerGroup.tkdGroupId;
        }
        for (const auto &simpleGroup : simpleGroups)
        {
            for (OperationId opId : simpleGroup.memberOps)
            {
                groupIdByOpIndex[opId.index] = simpleGroup.tkdGroupId;
            }
        }

        std::unordered_map<uint64_t, TkdGroupEdgeKind> edgeKindByPacked;
        edgeKindByPacked.reserve(graph->operations().size() * 2 + tkdGroupCount);
        for (const OperationId opId : graph->operations())
        {
            if (!opId.valid())
            {
                continue;
            }
            const TkdGroupId srcGroupId = groupIdByOpIndex[opId.index];
            if (srcGroupId == kInvalidTkdGroupId)
            {
                continue;
            }

            const Operation op = graph->getOperation(opId);
            for (ValueId resultValueId : op.results())
            {
                const Value resultValue = graph->getValue(resultValueId);
                for (const auto &user : resultValue.users())
                {
                    const TkdGroupId dstGroupId = groupIdByOpIndex[user.operation.index];
                    if (dstGroupId == kInvalidTkdGroupId || dstGroupId == srcGroupId)
                    {
                        continue;
                    }
                    edgeKindByPacked.emplace(packGroupEdge(srcGroupId, dstGroupId), TkdGroupEdgeKind::kDataDependency);
                }
            }
        }
        for (TkdGroupId dstGroupId = 0; dstGroupId < static_cast<TkdGroupId>(tkdGroupCount); ++dstGroupId)
        {
            if (dstGroupId == triggerGroup.tkdGroupId)
            {
                continue;
            }
            edgeKindByPacked.emplace(packGroupEdge(triggerGroup.tkdGroupId, dstGroupId),
                                     TkdGroupEdgeKind::kTriggerPrecedence);
        }

        std::vector<TkdGroupEdge> edges;
        edges.reserve(edgeKindByPacked.size());
        for (const auto &[packedEdge, kind] : edgeKindByPacked)
        {
            edges.push_back(TkdGroupEdge{
                static_cast<TkdGroupId>(packedEdge >> 32),
                static_cast<TkdGroupId>(packedEdge & 0xffffffffULL),
                kind,
            });
        }
        std::sort(edges.begin(),
                  edges.end(),
                  [](const TkdGroupEdge &lhs, const TkdGroupEdge &rhs) {
                      if (lhs.srcGroupId != rhs.srcGroupId)
                      {
                          return lhs.srcGroupId < rhs.srcGroupId;
                      }
                      if (lhs.dstGroupId != rhs.dstGroupId)
                      {
                          return lhs.dstGroupId < rhs.dstGroupId;
                      }
                      return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
                  });

        std::vector<TkdGroupId> topoOrder;
        topoOrder.reserve(tkdGroupCount);
        try
        {
            wolvrix::lib::toposort::TopoDag<TkdGroupId> dag;
            dag.reserveNodes(tkdGroupCount);
            dag.reserveEdges(edges.size());
            for (TkdGroupId groupId = 0; groupId < static_cast<TkdGroupId>(tkdGroupCount); ++groupId)
            {
                dag.addNode(groupId);
            }
            for (const auto &edge : edges)
            {
                dag.addEdge(edge.srcGroupId, edge.dstGroupId);
            }

            const auto topoLayers = dag.toposort();
            for (const auto &layer : topoLayers)
            {
                topoOrder.insert(topoOrder.end(), layer.begin(), layer.end());
            }
        }
        catch (const std::exception &ex)
        {
            error(std::string("trigger-key-driven-schedule toposort failed: ") + ex.what());
            result.failed = true;
            return result;
        }

        const std::string modulePath = options_.path;
        TkdScheduleMeta meta;
        meta.modulePath = modulePath;
        meta.graphSymbol = graph->symbol();
        meta.valueCount = graph->values().size();
        meta.operationCount = graph->operations().size();
        meta.normalizedTopLevelSinkAssignCount = normalizedTopLevelSinkAssignCount;
        meta.triggerKeyCount = triggerKeyPool.size();
        meta.affectedSinkSetCount = affectedSinkSetPool.size();
        meta.sinkGroupCount = sinkGroups.size();
        meta.simpleGroupCount = simpleGroups.size();
        meta.tkdGroupCount = tkdGroupCount;
        meta.edgeCount = edges.size();

        const std::vector<TkdTriggerGroupRecord> triggerGroups{triggerGroup};
        const TkdOpToGroupIndex opToGroupIndex{kInvalidTkdGroupId, groupIdByOpIndex};
        if (!options_.metaKey.empty())
        {
            setSessionValue(options_.metaKey, meta, "tkd.meta");
        }
        if (!options_.groupsKey.empty())
        {
            setSessionValue(options_.groupsKey,
                          TkdGroupBundle{sinkGroups, triggerGroups, simpleGroups},
                          "tkd.groups");
        }
        if (!options_.resultKey.empty())
        {
            setSessionValue(options_.resultKey,
                          TkdScheduleResult{
                              meta,
                              triggerKeyPool,
                              affectedSinkSetPool,
                              TkdGroupBundle{sinkGroups, triggerGroups, simpleGroups},
                              opToGroupIndex,
                              edges,
                              topoOrder,
                          },
                          "tkd.result");
        }
        if (options_.resultKey.empty() && options_.groupsKey.empty() && options_.metaKey.empty())
        {
            setSessionValue(sessionKey(modulePath, "meta"), meta, "tkd.meta");
            setSessionValue(sessionKey(modulePath, "pools/triggerKeys"), triggerKeyPool, "tkd.trigger_keys");
            setSessionValue(sessionKey(modulePath, "pools/affectedSinkSets"), affectedSinkSetPool, "tkd.affected_sink_sets");
            setSessionValue(sessionKey(modulePath, "groups/sink"), sinkGroups, "tkd.sink_groups");
            setSessionValue(sessionKey(modulePath, "groups/trigger"), triggerGroups, "tkd.trigger_groups");
            setSessionValue(sessionKey(modulePath, "groups/simple"), simpleGroups, "tkd.simple_groups");
            setSessionValue(sessionKey(modulePath, "index/opToTkdGroup"), opToGroupIndex, "tkd.op_group_index");
            setSessionValue(sessionKey(modulePath, "plan/edges"), edges, "tkd.plan_edges");
            setSessionValue(sessionKey(modulePath, "plan/topoOrder"), topoOrder, "tkd.topo_order");
        }

        return result;
    }

} // namespace wolvrix::lib::transform
