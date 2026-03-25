#include "transform/repcut_port_merge.hpp"

#include "core/grh.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
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
        using wolvrix::lib::grh::Design;
        using wolvrix::lib::grh::Graph;
        using wolvrix::lib::grh::Operation;
        using wolvrix::lib::grh::OperationId;
        using wolvrix::lib::grh::OperationKind;
        using wolvrix::lib::grh::SymbolId;
        using wolvrix::lib::grh::Value;
        using wolvrix::lib::grh::ValueId;
        using wolvrix::lib::grh::ValueIdHash;
        using wolvrix::lib::grh::ValueType;

        struct InstanceInfo
        {
            OperationId opId = OperationId::invalid();
            std::string instanceName;
            std::string moduleName;
            Graph *moduleGraph = nullptr;
            std::vector<std::string> inputNames;
            std::vector<std::string> outputNames;
            std::vector<ValueId> operands;
            std::vector<ValueId> results;
        };

        struct NamedValue
        {
            std::string name;
            ValueId value = ValueId::invalid();
        };

        struct SinkPort
        {
            std::string instanceName;
            std::string portName;

            friend bool operator<(const SinkPort &lhs, const SinkPort &rhs)
            {
                return std::tie(lhs.instanceName, lhs.portName) < std::tie(rhs.instanceName, rhs.portName);
            }
        };

        struct LinkDesc
        {
            ValueId topValue = ValueId::invalid();
            std::string symbol;
            int32_t width = 0;
            bool isSigned = false;
            ValueType type = ValueType::Logic;
            std::string producerInstance;
            std::string producerPortName;
            std::vector<SinkPort> sinks;
        };

        struct GroupKey
        {
            std::string producerInstance;
            std::vector<std::string> sinkInstances;

            friend bool operator==(const GroupKey &lhs, const GroupKey &rhs)
            {
                return lhs.producerInstance == rhs.producerInstance && lhs.sinkInstances == rhs.sinkInstances;
            }
        };

        struct GroupKeyHash
        {
            std::size_t operator()(const GroupKey &key) const noexcept
            {
                std::size_t seed = std::hash<std::string>{}(key.producerInstance);
                for (const auto &sink : key.sinkInstances)
                {
                    seed ^= std::hash<std::string>{}(sink) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                }
                return seed;
            }
        };

        struct GroupMember
        {
            const LinkDesc *link = nullptr;
            std::size_t offset = 0;
        };

        struct GroupPlan
        {
            std::size_t index = 0;
            GroupKey key;
            std::vector<GroupMember> members;
            std::size_t totalWidth = 0;
            std::string producerMergedPortName;
            std::unordered_map<std::string, std::string> consumerMergedPortNameByInstance;
            ValueId mergedTopValue = ValueId::invalid();
        };

        struct InstanceRewrite
        {
            std::unordered_set<std::string> removedInputs;
            std::unordered_set<std::string> removedOutputs;
            std::vector<std::pair<std::string, ValueId>> addedInputs;
            std::vector<std::pair<std::string, ValueId>> addedOutputs;
        };

        std::optional<std::string> getAttrString(const Operation &op, std::string_view key)
        {
            auto attr = op.attr(key);
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

        std::optional<std::vector<std::string>> getAttrStrings(const Operation &op, std::string_view key)
        {
            auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *values = std::get_if<std::vector<std::string>>(&*attr))
            {
                return *values;
            }
            return std::nullopt;
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

        OperationId findUniqueInstance(const Graph &graph, std::string_view instanceName)
        {
            OperationId found = OperationId::invalid();
            for (const auto opId : graph.operations())
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

        std::optional<std::string> resolveTargetGraphName(Design &design, std::string_view path, std::string &error)
        {
            const std::vector<std::string> segments = splitPath(path);
            if (segments.empty())
            {
                error = "repcut-port-merge path must not be empty";
                return std::nullopt;
            }
            if (segments.size() == 1)
            {
                if (design.findGraph(segments.front()) == nullptr)
                {
                    error = "repcut-port-merge graph not found: " + segments.front();
                    return std::nullopt;
                }
                return segments.front();
            }

            Graph *current = design.findGraph(segments.front());
            if (current == nullptr)
            {
                error = "repcut-port-merge root graph not found: " + segments.front();
                return std::nullopt;
            }
            for (std::size_t i = 1; i < segments.size(); ++i)
            {
                const OperationId instOp = findUniqueInstance(*current, segments[i]);
                if (!instOp.valid())
                {
                    error = "repcut-port-merge instance not found or not unique: " + segments[i];
                    return std::nullopt;
                }
                const Operation op = current->getOperation(instOp);
                const auto moduleName = getAttrString(op, "moduleName");
                if (!moduleName || moduleName->empty())
                {
                    error = "repcut-port-merge instance missing moduleName: " + segments[i];
                    return std::nullopt;
                }
                current = design.findGraph(*moduleName);
                if (current == nullptr)
                {
                    error = "repcut-port-merge child graph not found: " + *moduleName;
                    return std::nullopt;
                }
            }
            return current->symbol();
        }

        SymbolId internUniqueSymbol(Graph &graph, std::string base)
        {
            if (base.empty())
            {
                base = "value";
            }
            std::string candidate = std::move(base);
            const std::string root = candidate;
            int suffix = 0;
            while (true)
            {
                const SymbolId sym = graph.internSymbol(candidate);
                if (sym.valid() && !graph.findValue(sym).valid() && !graph.findOperation(sym).valid())
                {
                    return sym;
                }
                candidate = root + "_" + std::to_string(++suffix);
            }
        }

        std::unordered_set<std::string> collectUsedPortNames(const Graph &graph)
        {
            std::unordered_set<std::string> used;
            used.reserve(graph.inputPorts().size() + graph.outputPorts().size() + graph.inoutPorts().size() + 8);
            for (const auto &port : graph.inputPorts())
            {
                used.insert(port.name);
            }
            for (const auto &port : graph.outputPorts())
            {
                used.insert(port.name);
            }
            for (const auto &port : graph.inoutPorts())
            {
                used.insert(port.name);
            }
            return used;
        }

        bool isRepcutUnitInstanceName(std::string_view instanceName)
        {
            return instanceName == "debug_part" || instanceName.starts_with("part_");
        }

        std::string makeUniquePortName(std::unordered_set<std::string> &used, std::string base)
        {
            std::string candidate = std::move(base);
            const std::string root = candidate;
            int suffix = 0;
            while (!used.insert(candidate).second)
            {
                candidate = root + "_" + std::to_string(++suffix);
            }
            return candidate;
        }

        OperationId buildInstance(Graph &parent,
                                  std::string_view moduleName,
                                  std::string_view instanceBase,
                                  const Graph &target,
                                  const std::unordered_map<std::string, ValueId> &inputMapping,
                                  const std::unordered_map<std::string, ValueId> &outputMapping)
        {
            const std::string opBase = std::string("inst_") + std::string(instanceBase);
            const SymbolId opSym = internUniqueSymbol(parent, opBase);
            const OperationId inst = parent.createOperation(OperationKind::kInstance, opSym);
            parent.setAttr(inst, "moduleName", std::string(moduleName));
            parent.setAttr(inst, "instanceName", std::string(instanceBase));

            std::vector<std::string> inputNames;
            inputNames.reserve(target.inputPorts().size());
            for (const auto &port : target.inputPorts())
            {
                inputNames.push_back(port.name);
            }
            std::vector<std::string> outputNames;
            outputNames.reserve(target.outputPorts().size());
            for (const auto &port : target.outputPorts())
            {
                outputNames.push_back(port.name);
            }

            parent.setAttr(inst, "inputPortName", inputNames);
            parent.setAttr(inst, "outputPortName", outputNames);

            for (const auto &portName : inputNames)
            {
                auto it = inputMapping.find(portName);
                if (it == inputMapping.end())
                {
                    throw std::runtime_error("repcut-port-merge missing input mapping for " + std::string(instanceBase) +
                                             "." + portName);
                }
                parent.addOperand(inst, it->second);
            }
            for (const auto &portName : outputNames)
            {
                auto it = outputMapping.find(portName);
                if (it == outputMapping.end())
                {
                    throw std::runtime_error("repcut-port-merge missing output mapping for " + std::string(instanceBase) +
                                             "." + portName);
                }
                parent.addResult(inst, it->second);
            }
            return inst;
        }

        bool collectInstanceInfo(Graph &graph,
                                 Design &design,
                                 std::unordered_map<std::string, InstanceInfo> &instanceByName,
                                 std::string &error)
        {
            instanceByName.clear();
            for (const auto opId : graph.operations())
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

                const auto instanceName = getAttrString(op, "instanceName");
                const auto moduleName = getAttrString(op, "moduleName");
                const auto inputNames = getAttrStrings(op, "inputPortName");
                const auto outputNames = getAttrStrings(op, "outputPortName");
                if (!instanceName || !moduleName || !inputNames || !outputNames)
                {
                    error = "repcut-port-merge instance is missing required attributes";
                    return false;
                }
                if (!isRepcutUnitInstanceName(*instanceName))
                {
                    continue;
                }
                if (instanceByName.find(*instanceName) != instanceByName.end())
                {
                    error = "repcut-port-merge duplicate instanceName in target graph: " + *instanceName;
                    return false;
                }

                Graph *moduleGraph = design.findGraph(*moduleName);
                if (moduleGraph == nullptr)
                {
                    error = "repcut-port-merge child graph not found: " + *moduleName;
                    return false;
                }

                const auto operands = graph.opOperands(opId);
                const auto results = graph.opResults(opId);
                if (operands.size() < inputNames->size() || results.size() < outputNames->size())
                {
                    error = "repcut-port-merge instance port counts do not match operands/results: " + *instanceName;
                    return false;
                }

                InstanceInfo info;
                info.opId = opId;
                info.instanceName = *instanceName;
                info.moduleName = *moduleName;
                info.moduleGraph = moduleGraph;
                info.inputNames = *inputNames;
                info.outputNames = *outputNames;
                info.operands.assign(operands.begin(), operands.begin() + static_cast<std::ptrdiff_t>(inputNames->size()));
                info.results.assign(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(outputNames->size()));
                instanceByName.emplace(info.instanceName, std::move(info));
            }
            return true;
        }

        std::vector<std::string> uniqueSinkInstances(const LinkDesc &link)
        {
            std::vector<std::string> out;
            out.reserve(link.sinks.size());
            std::string last;
            for (const auto &sink : link.sinks)
            {
                if (out.empty() || sink.instanceName != last)
                {
                    out.push_back(sink.instanceName);
                    last = sink.instanceName;
                }
            }
            return out;
        }
    } // namespace

    RepcutPortMergePass::RepcutPortMergePass()
        : Pass("repcut-port-merge", "repcut-port-merge", "Merge identical repcut unit-to-unit port topologies")
    {
    }

    RepcutPortMergePass::RepcutPortMergePass(RepcutPortMergeOptions options)
        : Pass("repcut-port-merge", "repcut-port-merge", "Merge identical repcut unit-to-unit port topologies"),
          options_(std::move(options))
    {
    }

    PassResult RepcutPortMergePass::run()
    {
        PassResult result;
        if (options_.path.empty())
        {
            error("repcut-port-merge requires -path");
            result.failed = true;
            return result;
        }

        std::string resolveError;
        const auto targetGraphName = resolveTargetGraphName(design(), options_.path, resolveError);
        if (!targetGraphName)
        {
            error(resolveError);
            result.failed = true;
            return result;
        }

        Graph *graph = design().findGraph(*targetGraphName);
        if (graph == nullptr)
        {
            error("repcut-port-merge target graph missing after resolve: " + *targetGraphName);
            result.failed = true;
            return result;
        }

        std::unordered_map<std::string, InstanceInfo> instanceByName;
        std::string collectError;
        if (!collectInstanceInfo(*graph, design(), instanceByName, collectError))
        {
            error(collectError, graph->symbol());
            result.failed = true;
            return result;
        }

        if (instanceByName.empty())
        {
            info(*graph, "repcut-port-merge: target graph has no repcut unit instances, nothing to do");
            return result;
        }

        std::unordered_set<ValueId, ValueIdHash> topTouchedValues;
        topTouchedValues.reserve(graph->inputPorts().size() + graph->outputPorts().size() + graph->inoutPorts().size() * 3 + 8);
        for (const auto &port : graph->inputPorts())
        {
            topTouchedValues.insert(port.value);
        }
        for (const auto &port : graph->outputPorts())
        {
            topTouchedValues.insert(port.value);
        }
        for (const auto &port : graph->inoutPorts())
        {
            topTouchedValues.insert(port.in);
            topTouchedValues.insert(port.out);
            topTouchedValues.insert(port.oe);
        }

        struct DriverDesc
        {
            std::string instanceName;
            std::string portName;
        };

        std::unordered_map<ValueId, DriverDesc, ValueIdHash> driverByValue;
        std::unordered_map<ValueId, std::vector<SinkPort>, ValueIdHash> sinksByValue;
        driverByValue.reserve(graph->values().size());
        sinksByValue.reserve(graph->values().size());
        std::unordered_set<OperationId, wolvrix::lib::grh::OperationIdHash> repcutInstanceOps;
        repcutInstanceOps.reserve(instanceByName.size());

        for (const auto &[instanceName, instance] : instanceByName)
        {
            (void)instanceName;
            repcutInstanceOps.insert(instance.opId);
            for (std::size_t i = 0; i < instance.outputNames.size(); ++i)
            {
                driverByValue.emplace(instance.results[i], DriverDesc{instance.instanceName, instance.outputNames[i]});
            }
            for (std::size_t i = 0; i < instance.inputNames.size(); ++i)
            {
                sinksByValue[instance.operands[i]].push_back(SinkPort{instance.instanceName, instance.inputNames[i]});
            }
        }

        std::vector<LinkDesc> links;
        links.reserve(driverByValue.size());
        for (const auto &[valueId, driver] : driverByValue)
        {
            if (topTouchedValues.find(valueId) != topTouchedValues.end())
            {
                continue;
            }

            const auto sinksIt = sinksByValue.find(valueId);
            if (sinksIt == sinksByValue.end() || sinksIt->second.empty())
            {
                continue;
            }

            const Value value = graph->getValue(valueId);
            if (value.type() != ValueType::Logic || value.width() <= 0)
            {
                continue;
            }

            bool hasOnlyInstanceUsers = true;
            for (const auto &user : value.users())
            {
                if (!user.operation.valid())
                {
                    hasOnlyInstanceUsers = false;
                    break;
                }
                const Operation userOp = graph->getOperation(user.operation);
                if (userOp.kind() != OperationKind::kInstance)
                {
                    hasOnlyInstanceUsers = false;
                    break;
                }
                if (repcutInstanceOps.find(user.operation) == repcutInstanceOps.end())
                {
                    hasOnlyInstanceUsers = false;
                    break;
                }
            }
            if (!hasOnlyInstanceUsers)
            {
                continue;
            }

            LinkDesc link;
            link.topValue = valueId;
            link.symbol = std::string(value.symbolText());
            if (link.symbol.empty())
            {
                link.symbol = "value_" + std::to_string(valueId.index);
            }
            link.width = value.width();
            link.isSigned = value.isSigned();
            link.type = value.type();
            link.producerInstance = driver.instanceName;
            link.producerPortName = driver.portName;
            link.sinks = sinksIt->second;
            std::sort(link.sinks.begin(), link.sinks.end());
            link.sinks.erase(std::unique(link.sinks.begin(),
                                         link.sinks.end(),
                                         [](const SinkPort &lhs, const SinkPort &rhs) {
                                             return lhs.instanceName == rhs.instanceName && lhs.portName == rhs.portName;
                                         }),
                             link.sinks.end());
            links.push_back(std::move(link));
        }

        if (links.empty())
        {
            info(*graph, "repcut-port-merge: no eligible unit-to-unit links");
            return result;
        }

        std::unordered_map<GroupKey, std::vector<const LinkDesc *>, GroupKeyHash> rawGroups;
        rawGroups.reserve(links.size());
        for (const auto &link : links)
        {
            GroupKey key;
            key.producerInstance = link.producerInstance;
            key.sinkInstances = uniqueSinkInstances(link);
            rawGroups[key].push_back(&link);
        }

        std::vector<GroupPlan> groups;
        groups.reserve(rawGroups.size());
        for (auto &[key, members] : rawGroups)
        {
            if (members.size() < 2)
            {
                continue;
            }
            std::sort(members.begin(),
                      members.end(),
                      [](const LinkDesc *lhs, const LinkDesc *rhs) {
                          return std::tie(lhs->symbol, lhs->topValue.index) < std::tie(rhs->symbol, rhs->topValue.index);
                      });
            GroupPlan group;
            group.key = key;
            std::size_t offset = 0;
            for (const auto *member : members)
            {
                group.members.push_back(GroupMember{member, offset});
                offset += static_cast<std::size_t>(member->width);
            }
            group.totalWidth = offset;
            groups.push_back(std::move(group));
        }

        if (groups.empty())
        {
            info(*graph, "repcut-port-merge: no merge groups with at least two links");
            return result;
        }

        info(*graph,
             "repcut-port-merge: repcut_instances=" + std::to_string(instanceByName.size()) +
                 " eligible_links=" + std::to_string(links.size()) +
                 " merge_groups=" + std::to_string(groups.size()));

        std::sort(groups.begin(),
                  groups.end(),
                  [](const GroupPlan &lhs, const GroupPlan &rhs) {
                      if (lhs.key.producerInstance != rhs.key.producerInstance)
                      {
                          return lhs.key.producerInstance < rhs.key.producerInstance;
                      }
                      if (lhs.key.sinkInstances != rhs.key.sinkInstances)
                      {
                          return lhs.key.sinkInstances < rhs.key.sinkInstances;
                      }
                      return lhs.members.front().link->symbol < rhs.members.front().link->symbol;
                  });
        for (std::size_t i = 0; i < groups.size(); ++i)
        {
            groups[i].index = i;
        }

        std::unordered_map<std::string, std::vector<std::string>> affectedInstancesByModule;
        for (const auto &group : groups)
        {
            const auto producerIt = instanceByName.find(group.key.producerInstance);
            if (producerIt == instanceByName.end())
            {
                error("repcut-port-merge internal error: missing producer instance " + group.key.producerInstance);
                result.failed = true;
                return result;
            }
            affectedInstancesByModule[producerIt->second.moduleName].push_back(producerIt->second.instanceName);
            for (const auto &sinkInstance : group.key.sinkInstances)
            {
                const auto sinkIt = instanceByName.find(sinkInstance);
                if (sinkIt == instanceByName.end())
                {
                    error("repcut-port-merge internal error: missing sink instance " + sinkInstance);
                    result.failed = true;
                    return result;
                }
                affectedInstancesByModule[sinkIt->second.moduleName].push_back(sinkIt->second.instanceName);
            }
        }
        for (auto &[moduleName, instances] : affectedInstancesByModule)
        {
            std::sort(instances.begin(), instances.end());
            instances.erase(std::unique(instances.begin(), instances.end()), instances.end());
            if (instances.size() > 1)
            {
                error("repcut-port-merge does not support shared module graph across affected instances: module=" +
                      moduleName);
                result.failed = true;
                return result;
            }
        }

        std::unordered_map<std::string, InstanceRewrite> rewritesByInstance;
        rewritesByInstance.reserve(instanceByName.size());
        std::unordered_map<Graph *, std::unordered_set<std::string>> usedPortNamesByGraph;
        usedPortNamesByGraph.reserve(instanceByName.size());
        auto usedPortNamesFor = [&](Graph &g) -> std::unordered_set<std::string> & {
            auto it = usedPortNamesByGraph.find(&g);
            if (it != usedPortNamesByGraph.end())
            {
                return it->second;
            }
            auto [insertedIt, inserted] = usedPortNamesByGraph.emplace(&g, collectUsedPortNames(g));
            (void)inserted;
            return insertedIt->second;
        };

        try
        {
            for (auto &group : groups)
            {
                const InstanceInfo &producer = instanceByName.at(group.key.producerInstance);
                Graph &producerGraph = *producer.moduleGraph;

                auto &usedProducerPortNames = usedPortNamesFor(producerGraph);
                group.producerMergedPortName = makeUniquePortName(usedProducerPortNames,
                                                                 "pm_out_" + std::to_string(group.index));
                const OperationId concatOp =
                    producerGraph.createOperation(OperationKind::kConcat,
                                                 internUniqueSymbol(producerGraph,
                                                                    "pm_concat_" + std::to_string(group.index)));
                for (auto it = group.members.rbegin(); it != group.members.rend(); ++it)
                {
                    const ValueId oldPortValue = producerGraph.outputPortValue(it->link->producerPortName);
                    if (!oldPortValue.valid())
                    {
                        throw std::runtime_error("repcut-port-merge missing producer output port " +
                                                 producer.instanceName + "." + it->link->producerPortName);
                    }
                    producerGraph.addOperand(concatOp, oldPortValue);
                }
                const ValueId mergedProducerValue =
                    producerGraph.createValue(internUniqueSymbol(producerGraph,
                                                                 "pm_out_value_" + std::to_string(group.index)),
                                              static_cast<int32_t>(group.totalWidth),
                                              false,
                                              ValueType::Logic);
                producerGraph.addResult(concatOp, mergedProducerValue);
                producerGraph.bindOutputPort(group.producerMergedPortName, mergedProducerValue);
                for (const auto &member : group.members)
                {
                    producerGraph.removeOutputPort(member.link->producerPortName);
                    rewritesByInstance[producer.instanceName].removedOutputs.insert(member.link->producerPortName);
                }

                for (const auto &sinkInstanceName : group.key.sinkInstances)
                {
                    const InstanceInfo &sink = instanceByName.at(sinkInstanceName);
                    Graph &sinkGraph = *sink.moduleGraph;
                    auto &usedSinkPortNames = usedPortNamesFor(sinkGraph);
                    const std::string mergedInputPortName = makeUniquePortName(usedSinkPortNames,
                                                                              "pm_in_" + std::to_string(group.index));
                    group.consumerMergedPortNameByInstance.emplace(sink.instanceName, mergedInputPortName);

                    const ValueId mergedInputValue =
                        sinkGraph.createValue(internUniqueSymbol(sinkGraph,
                                                                 "pm_in_value_" + std::to_string(group.index)),
                                              static_cast<int32_t>(group.totalWidth),
                                              false,
                                              ValueType::Logic);
                    sinkGraph.bindInputPort(mergedInputPortName, mergedInputValue);

                    std::unordered_set<std::string> consumedInputPorts;
                    for (const auto &member : group.members)
                    {
                        for (const auto &sinkPort : member.link->sinks)
                        {
                            if (sinkPort.instanceName != sink.instanceName)
                            {
                                continue;
                            }
                            const ValueId oldInputValue = sinkGraph.inputPortValue(sinkPort.portName);
                            if (!oldInputValue.valid())
                            {
                                throw std::runtime_error("repcut-port-merge missing consumer input port " +
                                                         sink.instanceName + "." + sinkPort.portName);
                            }

                            const Value oldInput = sinkGraph.getValue(oldInputValue);
                            const OperationId sliceOp =
                                sinkGraph.createOperation(OperationKind::kSliceStatic,
                                                          internUniqueSymbol(sinkGraph,
                                                                             "pm_slice_" + std::to_string(group.index)));
                            sinkGraph.addOperand(sliceOp, mergedInputValue);
                            const int64_t sliceStart = static_cast<int64_t>(member.offset);
                            const int64_t sliceEnd = sliceStart + static_cast<int64_t>(member.link->width) - 1;
                            sinkGraph.setAttr(sliceOp, "sliceStart", sliceStart);
                            sinkGraph.setAttr(sliceOp, "sliceEnd", sliceEnd);
                            const ValueId slicedValue =
                                sinkGraph.createValue(internUniqueSymbol(sinkGraph,
                                                                         "pm_slice_value_" + std::to_string(group.index)),
                                                      oldInput.width(),
                                                      oldInput.isSigned(),
                                                      oldInput.type());
                            sinkGraph.addResult(sliceOp, slicedValue);
                            sinkGraph.replaceAllUses(oldInputValue, slicedValue);
                            consumedInputPorts.insert(sinkPort.portName);
                            rewritesByInstance[sink.instanceName].removedInputs.insert(sinkPort.portName);
                        }
                    }

                    for (const auto &portName : consumedInputPorts)
                    {
                        sinkGraph.removeInputPort(portName);
                    }
                }
            }

            for (auto &group : groups)
            {
                const std::string mergedTopBase = "repcut_pm_" + std::to_string(group.index);
                group.mergedTopValue =
                    graph->createValue(internUniqueSymbol(*graph, mergedTopBase),
                                       static_cast<int32_t>(group.totalWidth),
                                       false,
                                       ValueType::Logic);

                const InstanceInfo &producer = instanceByName.at(group.key.producerInstance);
                rewritesByInstance[producer.instanceName].addedOutputs.emplace_back(group.producerMergedPortName,
                                                                                    group.mergedTopValue);
                for (const auto &sinkInstanceName : group.key.sinkInstances)
                {
                    rewritesByInstance[sinkInstanceName].addedInputs.emplace_back(
                        group.consumerMergedPortNameByInstance.at(sinkInstanceName), group.mergedTopValue);
                }
            }

            std::vector<std::string> affectedInstances;
            affectedInstances.reserve(rewritesByInstance.size());
            for (const auto &[instanceName, rewrite] : rewritesByInstance)
            {
                (void)rewrite;
                affectedInstances.push_back(instanceName);
            }
            std::sort(affectedInstances.begin(), affectedInstances.end());

            struct RebuildPlan
            {
                InstanceInfo info;
                std::unordered_map<std::string, ValueId> inputMapping;
                std::unordered_map<std::string, ValueId> outputMapping;
            };
            std::vector<RebuildPlan> rebuildPlans;
            rebuildPlans.reserve(affectedInstances.size());
            for (const auto &instanceName : affectedInstances)
            {
                const InstanceInfo &instance = instanceByName.at(instanceName);
                const InstanceRewrite &rewrite = rewritesByInstance.at(instanceName);

                std::unordered_map<std::string, ValueId> inputMapping;
                std::unordered_map<std::string, ValueId> outputMapping;
                inputMapping.reserve(instance.inputNames.size() + rewrite.addedInputs.size());
                outputMapping.reserve(instance.outputNames.size() + rewrite.addedOutputs.size());

                for (std::size_t i = 0; i < instance.inputNames.size(); ++i)
                {
                    inputMapping.emplace(instance.inputNames[i], instance.operands[i]);
                }
                for (std::size_t i = 0; i < instance.outputNames.size(); ++i)
                {
                    outputMapping.emplace(instance.outputNames[i], instance.results[i]);
                }
                for (const auto &portName : rewrite.removedInputs)
                {
                    inputMapping.erase(portName);
                }
                for (const auto &portName : rewrite.removedOutputs)
                {
                    outputMapping.erase(portName);
                }
                for (const auto &[portName, valueId] : rewrite.addedInputs)
                {
                    inputMapping.insert_or_assign(portName, valueId);
                }
                for (const auto &[portName, valueId] : rewrite.addedOutputs)
                {
                    outputMapping.insert_or_assign(portName, valueId);
                }

                rebuildPlans.push_back(RebuildPlan{instance, std::move(inputMapping), std::move(outputMapping)});
            }

            for (const auto &plan : rebuildPlans)
            {
                graph->eraseOpUnchecked(plan.info.opId);
            }
            for (const auto &plan : rebuildPlans)
            {
                buildInstance(*graph,
                              plan.info.moduleName,
                              plan.info.instanceName,
                              *plan.info.moduleGraph,
                              plan.inputMapping,
                              plan.outputMapping);
            }

            std::size_t erasedLinkCount = 0;
            for (const auto &group : groups)
            {
                for (const auto &member : group.members)
                {
                    if (graph->eraseValue(member.link->topValue))
                    {
                        ++erasedLinkCount;
                    }
                }
            }

            result.changed = true;
            info(*graph,
                 "repcut-port-merge: merged_groups=" + std::to_string(groups.size()) +
                     " affected_instances=" + std::to_string(rewritesByInstance.size()) +
                     " erased_links=" + std::to_string(erasedLinkCount));
            return result;
        }
        catch (const std::exception &ex)
        {
            error(std::string("repcut-port-merge failed: ") + ex.what(), graph->symbol());
            result.failed = true;
            return result;
        }
    }

} // namespace wolvrix::lib::transform
