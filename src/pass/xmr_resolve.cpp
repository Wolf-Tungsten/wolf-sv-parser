#include "pass/xmr_resolve.hpp"

#include "grh.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace wolf_sv_parser::transform
{

    namespace
    {
        using PortNameCache = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

        std::vector<std::string> splitPath(std::string_view path)
        {
            std::vector<std::string> out;
            std::string current;
            for (char ch : path)
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

        std::string sanitizePath(std::string_view path)
        {
            std::string out;
            out.reserve(path.size());
            for (char ch : path)
            {
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_')
                {
                    out.push_back(ch);
                }
                else
                {
                    out.push_back('_');
                }
            }
            if (out.empty())
            {
                out = "xmr";
            }
            return out;
        }

        std::string hashPath(std::string_view path)
        {
            const std::size_t hash = std::hash<std::string_view>{}(path);
            std::ostringstream oss;
            oss << std::hex << hash;
            return oss.str();
        }

        std::string makeUniqueSymbol(grh::ir::Graph &graph, std::string base)
        {
            std::string candidate = base;
            int suffix = 0;
            while (graph.symbols().contains(candidate))
            {
                candidate = base + "_" + std::to_string(++suffix);
            }
            return candidate;
        }

        std::string getPortName(grh::ir::Graph &graph, PortNameCache &cache,
                                std::string_view path, std::string_view prefix)
        {
            auto &graphMap = cache[graph.symbol()];
            auto it = graphMap.find(std::string(path));
            if (it != graphMap.end())
            {
                return it->second;
            }
            std::string base = "__";
            base.append(prefix);
            base.push_back('_');
            std::string sanitized = sanitizePath(path);
            if (sanitized.size() > 64)
            {
                sanitized = hashPath(path);
            }
            base.append(sanitized);
            std::string unique = makeUniqueSymbol(graph, base);
            graphMap.emplace(std::string(path), unique);
            return unique;
        }

        std::optional<std::string> getAttrString(const grh::ir::Operation &op,
                                                 std::string_view key)
        {
            auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (auto value = std::get_if<std::string>(&*attr))
            {
                return *value;
            }
            return std::nullopt;
        }

        std::vector<std::string> getAttrStrings(const grh::ir::Operation &op,
                                                std::string_view key)
        {
            auto attr = op.attr(key);
            if (!attr)
            {
                return {};
            }
            if (auto values = std::get_if<std::vector<std::string>>(&*attr))
            {
                return *values;
            }
            return {};
        }

        grh::ir::OperationId findInstanceOp(const grh::ir::Graph &graph,
                                            std::string_view instanceName)
        {
            for (const auto opId : graph.operations())
            {
                const grh::ir::Operation op = graph.getOperation(opId);
                if (op.kind() != grh::ir::OperationKind::kInstance &&
                    op.kind() != grh::ir::OperationKind::kBlackbox)
                {
                    continue;
                }
                auto name = getAttrString(op, "instanceName");
                if (name && *name == instanceName)
                {
                    return opId;
                }
            }
            return grh::ir::OperationId::invalid();
        }

        int32_t normalizeWidth(int32_t width)
        {
            return width > 0 ? width : 1;
        }

    } // namespace

    XmrResolvePass::XmrResolvePass()
        : Pass("xmr-resolve", "xmr-resolve",
               "Resolve hierarchical references by adding ports and instance connections")
    {
    }

    PassResult XmrResolvePass::run()
    {
        PassResult result;
        const std::size_t graphCount = netlist().graphs().size();
        logInfo("begin graphs=" + std::to_string(graphCount));
        PortNameCache readPortNames;
        PortNameCache writePortNames;
        std::size_t xmrOpCount = 0;
        std::size_t xmrReadCount = 0;
        std::size_t xmrWriteCount = 0;
        struct PendingPort
        {
            std::string moduleName;
            std::string portName;
            int32_t width = 0;
            bool isSigned = false;
        };
        std::vector<PendingPort> pendingOutputPorts;
        std::vector<PendingPort> pendingInputPorts;
        std::unordered_map<std::string, std::unordered_map<std::string, grh::ir::ValueId>> inputPadCache;

        auto getPadInput = [&](grh::ir::Graph &graph, int32_t width, bool isSigned) -> grh::ir::ValueId {
            const int32_t normalized = normalizeWidth(width);
            const std::string key = std::to_string(normalized) + (isSigned ? "s" : "u");
            auto &graphCache = inputPadCache[graph.symbol()];
            auto it = graphCache.find(key);
            if (it != graphCache.end())
            {
                return it->second;
            }
            std::string base = "__xmr_pad_in_" + key;
            std::string symName = makeUniqueSymbol(graph, base);
            grh::ir::SymbolId sym = graph.internSymbol(symName);
            grh::ir::ValueId value = graph.createValue(sym, normalized, isSigned);
            grh::ir::OperationId op =
                graph.createOperation(grh::ir::OperationKind::kConstant,
                                      grh::ir::SymbolId::invalid());
            graph.addResult(op, value);
            graph.setAttr(op, "constValue", std::to_string(normalized) + "'b0");
            graphCache.emplace(key, value);
            result.changed = true;
            return value;
        };

        auto ensureOutputPort = [&](grh::ir::Graph &graph,
                                    const std::string &portName,
                                    grh::ir::ValueId value,
                                    bool *added) -> grh::ir::ValueId {
            if (added)
            {
                *added = false;
            }
            grh::ir::SymbolId sym = graph.internSymbol(portName);
            grh::ir::ValueId existing = graph.outputPortValue(sym);
            if (existing.valid() && existing != value)
            {
                warning(graph, "XMR output port already bound; keeping existing binding");
                return existing;
            }
            if (!existing.valid())
            {
                graph.bindOutputPort(sym, value);
                result.changed = true;
                if (added)
                {
                    *added = true;
                }
            }
            return value;
        };

        auto ensureInputPort = [&](grh::ir::Graph &graph,
                                   const std::string &portName,
                                   int32_t width,
                                   bool isSigned,
                                   bool *added) -> grh::ir::ValueId {
            if (added)
            {
                *added = false;
            }
            grh::ir::SymbolId sym = graph.internSymbol(portName);
            grh::ir::ValueId existing = graph.inputPortValue(sym);
            if (existing.valid())
            {
                return existing;
            }
            grh::ir::ValueId value = graph.findValue(sym);
            if (!value.valid())
            {
                value = graph.createValue(sym, normalizeWidth(width), isSigned);
            }
            graph.bindInputPort(sym, value);
            result.changed = true;
            if (added)
            {
                *added = true;
            }
            return value;
        };

        auto ensureInstanceOutput = [&](grh::ir::Graph &graph,
                                        grh::ir::OperationId opId,
                                        const std::string &portName,
                                        int32_t width,
                                        bool isSigned) -> grh::ir::ValueId {
            const grh::ir::Operation op = graph.getOperation(opId);
            auto names = getAttrStrings(op, "outputPortName");
            const auto inoutNames = getAttrStrings(op, "inoutPortName");
            const auto resultsSpan = op.results();
            const std::size_t limit = std::min(names.size(), resultsSpan.size());
            for (std::size_t i = 0; i < limit; ++i)
            {
                if (names[i] == portName)
                {
                    return resultsSpan[i];
                }
            }
            std::string base = "__xmr_out_";
            base.append(portName);
            std::string symName = makeUniqueSymbol(graph, base);
            grh::ir::SymbolId sym = graph.internSymbol(symName);
            grh::ir::ValueId value = graph.createValue(sym, normalizeWidth(width), isSigned);
            const std::size_t inoutCount = inoutNames.size();
            const std::size_t resultCount = resultsSpan.size();
            const std::size_t outputLimit =
                resultCount > inoutCount ? resultCount - inoutCount : 0U;
            const std::size_t insertIndex = std::min(names.size(), outputLimit);
            graph.insertResult(opId, insertIndex, value);
            names.push_back(portName);
            graph.setAttr(opId, "outputPortName", names);
            result.changed = true;
            return value;
        };

        auto ensureInstanceInput = [&](grh::ir::Graph &graph,
                                       grh::ir::OperationId opId,
                                       const std::string &portName,
                                       grh::ir::ValueId value) -> grh::ir::ValueId {
            const grh::ir::Operation op = graph.getOperation(opId);
            auto names = getAttrStrings(op, "inputPortName");
            const auto inoutNames = getAttrStrings(op, "inoutPortName");
            const auto operandsSpan = op.operands();
            const std::size_t limit = std::min(names.size(), operandsSpan.size());
            for (std::size_t i = 0; i < limit; ++i)
            {
                if (names[i] == portName)
                {
                    if (operandsSpan[i] != value)
                    {
                        warning(graph, op, "XMR input port already connected; keeping existing operand");
                    }
                    return operandsSpan[i];
                }
            }
            const std::size_t inoutCount = inoutNames.size();
            const std::size_t operandCount = operandsSpan.size();
            const std::size_t inputLimit =
                operandCount > inoutCount * 2 ? operandCount - inoutCount * 2 : 0U;
            const std::size_t insertIndex = std::min(names.size(), inputLimit);
            graph.insertOperand(opId, insertIndex, value);
            names.push_back(portName);
            graph.setAttr(opId, "inputPortName", names);
            result.changed = true;
            return value;
        };

        auto forceSingleDriver = [&](grh::ir::Graph &graph,
                                     grh::ir::ValueId target,
                                     grh::ir::OperationId contextOp,
                                     std::string_view path) -> std::optional<grh::ir::ValueId> {
            if (!target.valid())
            {
                return std::nullopt;
            }
            const grh::ir::Value value = graph.getValue(target);
            if (value.isInout())
            {
                warning(graph, graph.getOperation(contextOp),
                        "XMR write to inout is not supported: " + std::string(path));
                return std::nullopt;
            }
            const std::string symbolText(value.symbolText());
            if (symbolText.empty())
            {
                warning(graph, graph.getOperation(contextOp),
                        "XMR write target missing symbol: " + std::string(path));
                return std::nullopt;
            }
            if (value.definingOp().valid())
            {
                warning(graph, graph.getOperation(contextOp),
                        "XMR write replaces existing driver for " + symbolText);
            }
            if (value.isInput())
            {
                warning(graph, graph.getOperation(contextOp),
                        "XMR write overrides input; leaving input port unconnected for " +
                            symbolText);
                std::string base = "__xmr_override_";
                base.append(symbolText);
                std::string symName = makeUniqueSymbol(graph, base);
                grh::ir::SymbolId sym = graph.internSymbol(symName);
                grh::ir::ValueId replacement =
                    graph.createValue(sym, normalizeWidth(value.width()), value.isSigned());
                graph.replaceAllUses(target, replacement);
                result.changed = true;
                return replacement;
            }
            std::vector<grh::ir::SymbolId> outputNames;
            if (value.isOutput())
            {
                for (const auto &port : graph.outputPorts())
                {
                    if (port.value == target)
                    {
                        outputNames.push_back(port.name);
                    }
                }
            }
            graph.clearValueSymbol(target);
            grh::ir::SymbolId sym = graph.internSymbol(symbolText);
            grh::ir::ValueId replacement =
                graph.createValue(sym, normalizeWidth(value.width()), value.isSigned());
            graph.replaceAllUses(target, replacement);
            for (const auto &name : outputNames)
            {
                graph.bindOutputPort(name, replacement);
            }
            result.changed = true;
            return replacement;
        };

        auto resolveRead = [&](grh::ir::Graph &root,
                               grh::ir::OperationId opId,
                               const std::string &path) -> std::optional<grh::ir::ValueId> {
            auto segments = splitPath(path);
            if (segments.empty())
            {
                warning(root, root.getOperation(opId), "XMR read has empty path");
                return std::nullopt;
            }
            if (!segments.empty() && segments.front() == root.symbol())
            {
                segments.erase(segments.begin());
                if (segments.empty())
                {
                    warning(root, root.getOperation(opId), "XMR read has empty path after trimming root");
                    return std::nullopt;
                }
            }
            if (segments.size() == 1)
            {
                grh::ir::ValueId local = root.findValue(segments.front());
                if (!local.valid())
                {
                    warning(root, root.getOperation(opId), "XMR read target not found in graph");
                    return std::nullopt;
                }
                return local;
            }

            struct Hop
            {
                grh::ir::Graph *parent = nullptr;
                grh::ir::OperationId instOp = grh::ir::OperationId::invalid();
                grh::ir::Graph *child = nullptr;
            };
            std::vector<Hop> hops;
            grh::ir::Graph *current = &root;
            for (std::size_t i = 0; i + 1 < segments.size(); ++i)
            {
                const std::string &instName = segments[i];
                grh::ir::OperationId instOp = findInstanceOp(*current, instName);
                if (!instOp.valid())
                {
                    warning(root, root.getOperation(opId),
                            "XMR read instance not found: " + instName);
                    return std::nullopt;
                }
                const grh::ir::Operation op = current->getOperation(instOp);
                auto moduleName = getAttrString(op, "moduleName");
                if (!moduleName || moduleName->empty())
                {
                    warning(root, root.getOperation(opId),
                            "XMR read instance missing moduleName");
                    return std::nullopt;
                }
                grh::ir::Graph *childGraph = netlist().findGraph(*moduleName);
                if (!childGraph)
                {
                    warning(root, root.getOperation(opId),
                            "XMR read module not found: " + *moduleName);
                    return std::nullopt;
                }
                hops.push_back(Hop{current, instOp, childGraph});
                current = childGraph;
            }

            grh::ir::Graph *leafGraph = current;
            const std::string &leafName = segments.back();
            grh::ir::ValueId leafValue = leafGraph->findValue(leafName);
            if (!leafValue.valid())
            {
                warning(root, root.getOperation(opId),
                        "XMR read target not found: " + leafName);
                return std::nullopt;
            }

            grh::ir::ValueId propagated = leafValue;
            for (std::size_t i = hops.size(); i-- > 0;)
            {
                grh::ir::Graph *childGraph = hops[i].child;
                grh::ir::Graph *parentGraph = hops[i].parent;
                const grh::ir::OperationId instOp = hops[i].instOp;
                const std::string portName = getPortName(*childGraph, readPortNames, path, "xmr_r");

                bool newPort = false;
                propagated = ensureOutputPort(*childGraph, portName, propagated, &newPort);

                const grh::ir::Value childValue = childGraph->getValue(propagated);
                if (newPort)
                {
                    pendingOutputPorts.push_back(
                        PendingPort{childGraph->symbol(), portName,
                                    childValue.width(), childValue.isSigned()});
                }
                grh::ir::ValueId parentValue =
                    ensureInstanceOutput(*parentGraph, instOp, portName,
                                         childValue.width(), childValue.isSigned());
                propagated = parentValue;
            }

            return propagated;
        };

        auto resolveWrite = [&](grh::ir::Graph &root,
                                grh::ir::OperationId opId,
                                const std::string &path,
                                grh::ir::ValueId data) -> bool {
            auto segments = splitPath(path);
            if (segments.empty())
            {
                warning(root, root.getOperation(opId), "XMR write has empty path");
                return false;
            }
            if (!segments.empty() && segments.front() == root.symbol())
            {
                segments.erase(segments.begin());
                if (segments.empty())
                {
                    warning(root, root.getOperation(opId), "XMR write has empty path after trimming root");
                    return false;
                }
            }
            if (segments.size() == 1)
            {
                grh::ir::ValueId target = root.findValue(segments.front());
                if (!target.valid())
                {
                    warning(root, root.getOperation(opId), "XMR write target not found in graph");
                    return false;
                }
                auto replacement = forceSingleDriver(root, target, opId, path);
                if (!replacement || !replacement->valid())
                {
                    return false;
                }
                grh::ir::OperationId assign =
                    root.createOperation(grh::ir::OperationKind::kAssign,
                                         grh::ir::SymbolId::invalid());
                root.addOperand(assign, data);
                root.addResult(assign, *replacement);
                result.changed = true;
                return true;
            }

            grh::ir::Graph *current = &root;
            grh::ir::ValueId driver = data;
            for (std::size_t i = 0; i + 1 < segments.size(); ++i)
            {
                const std::string &instName = segments[i];
                grh::ir::OperationId instOp = findInstanceOp(*current, instName);
                if (!instOp.valid())
                {
                    warning(root, root.getOperation(opId),
                            "XMR write instance not found: " + instName);
                    return false;
                }
                const grh::ir::Operation op = current->getOperation(instOp);
                auto moduleName = getAttrString(op, "moduleName");
                if (!moduleName || moduleName->empty())
                {
                    warning(root, root.getOperation(opId),
                            "XMR write instance missing moduleName");
                    return false;
                }
                grh::ir::Graph *childGraph = netlist().findGraph(*moduleName);
                if (!childGraph)
                {
                    warning(root, root.getOperation(opId),
                            "XMR write module not found: " + *moduleName);
                    return false;
                }
                const std::string portName = getPortName(*childGraph, writePortNames, path, "xmr_w");

                const grh::ir::Value driverValue = current->getValue(driver);
                bool newPort = false;
                grh::ir::ValueId childPort =
                    ensureInputPort(*childGraph, portName, driverValue.width(),
                                    driverValue.isSigned(), &newPort);
                if (newPort)
                {
                    pendingInputPorts.push_back(
                        PendingPort{childGraph->symbol(), portName,
                                    driverValue.width(), driverValue.isSigned()});
                }

                ensureInstanceInput(*current, instOp, portName, driver);

                current = childGraph;
                driver = childPort;
            }

            grh::ir::Graph *leafGraph = current;
            const std::string &leafName = segments.back();
            grh::ir::ValueId target = leafGraph->findValue(leafName);
            if (!target.valid())
            {
                warning(root, root.getOperation(opId),
                        "XMR write target not found: " + leafName);
                return false;
            }
            auto replacement = forceSingleDriver(*leafGraph, target, opId, path);
            if (!replacement || !replacement->valid())
            {
                return false;
            }
            grh::ir::OperationId assign =
                leafGraph->createOperation(grh::ir::OperationKind::kAssign,
                                           grh::ir::SymbolId::invalid());
            leafGraph->addOperand(assign, driver);
            leafGraph->addResult(assign, *replacement);
            result.changed = true;
            return true;
        };

        for (auto &graphEntry : netlist().graphs())
        {
            grh::ir::Graph &graph = *graphEntry.second;
            std::vector<grh::ir::OperationId> xmrOps;
            for (const auto opId : graph.operations())
            {
                const grh::ir::Operation op = graph.getOperation(opId);
                if (op.kind() == grh::ir::OperationKind::kXMRRead ||
                    op.kind() == grh::ir::OperationKind::kXMRWrite)
                {
                    xmrOps.push_back(opId);
                    ++xmrOpCount;
                    if (op.kind() == grh::ir::OperationKind::kXMRRead)
                    {
                        ++xmrReadCount;
                    }
                    else
                    {
                        ++xmrWriteCount;
                    }
                }
            }

            for (const auto opId : xmrOps)
            {
                const grh::ir::Operation op = graph.getOperation(opId);
                auto path = getAttrString(op, "xmrPath");
                {
                    std::string message = "xmr ";
                    message.append(op.kind() == grh::ir::OperationKind::kXMRRead ? "read" : "write");
                    message.append(" graph=");
                    message.append(graph.symbol());
                    message.append(" path=");
                    if (path)
                    {
                        message.append(*path);
                    }
                    else
                    {
                        message.append("<missing>");
                    }
                    log(LogLevel::Trace, std::move(message));
                }
                if (!path)
                {
                    warning(graph, op, "XMR op missing xmrPath attribute");
                    continue;
                }
                if (op.kind() == grh::ir::OperationKind::kXMRRead)
                {
                    const auto results = op.results();
                    if (results.empty())
                    {
                        warning(graph, op, "XMR read missing result");
                        graph.eraseOp(opId);
                        result.changed = true;
                        continue;
                    }
                    log(LogLevel::Trace, "xmr read resolve begin graph=" + graph.symbol());
                    auto replacement = resolveRead(graph, opId, *path);
                    log(LogLevel::Trace, "xmr read resolve end graph=" + graph.symbol());
                    if (!replacement || !replacement->valid())
                    {
                        continue;
                    }
                    graph.replaceAllUses(results.front(), *replacement);
                    graph.eraseOp(opId);
                    result.changed = true;
                    continue;
                }

                if (op.kind() == grh::ir::OperationKind::kXMRWrite)
                {
                    const auto operands = op.operands();
                    if (operands.empty())
                    {
                        warning(graph, op, "XMR write missing operand");
                        graph.eraseOp(opId);
                        result.changed = true;
                        continue;
                    }
                    logInfo("xmr write resolve begin graph=" + graph.symbol());
                    resolveWrite(graph, opId, *path, operands.front());
                    logInfo("xmr write resolve end graph=" + graph.symbol());
                    graph.eraseOp(opId);
                    result.changed = true;
                }
            }
        }

        if (!pendingOutputPorts.empty() || !pendingInputPorts.empty())
        {
            auto instanceHasInput = [&](const grh::ir::Operation &op,
                                        std::string_view portName) -> bool {
                const auto names = getAttrStrings(op, "inputPortName");
                for (const auto &name : names)
                {
                    if (name == portName)
                    {
                        return true;
                    }
                }
                return false;
            };

            struct InstanceRef
            {
                grh::ir::Graph *graph = nullptr;
                grh::ir::OperationId opId = grh::ir::OperationId::invalid();
            };

            std::unordered_map<std::string, std::vector<InstanceRef>> instancesByModule;
            for (auto &graphEntry : netlist().graphs())
            {
                grh::ir::Graph &graph = *graphEntry.second;
                for (const auto opId : graph.operations())
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    if (op.kind() != grh::ir::OperationKind::kInstance &&
                        op.kind() != grh::ir::OperationKind::kBlackbox)
                    {
                        continue;
                    }
                    const auto moduleName = getAttrString(op, "moduleName");
                    if (!moduleName || moduleName->empty())
                    {
                        continue;
                    }
                    instancesByModule[*moduleName].push_back(InstanceRef{&graph, opId});
                }
            }

            std::unordered_map<std::string, std::vector<const PendingPort *>> pendingOutputsByModule;
            pendingOutputsByModule.reserve(pendingOutputPorts.size());
            for (const auto &pending : pendingOutputPorts)
            {
                pendingOutputsByModule[pending.moduleName].push_back(&pending);
            }

            std::unordered_map<std::string, std::vector<const PendingPort *>> pendingInputsByModule;
            pendingInputsByModule.reserve(pendingInputPorts.size());
            for (const auto &pending : pendingInputPorts)
            {
                pendingInputsByModule[pending.moduleName].push_back(&pending);
            }

            logInfo("xmr pending ports begin outputs=" + std::to_string(pendingOutputPorts.size()) +
                    " inputs=" + std::to_string(pendingInputPorts.size()) +
                    " outputModules=" + std::to_string(pendingOutputsByModule.size()) +
                    " inputModules=" + std::to_string(pendingInputsByModule.size()) +
                    " instanceModules=" + std::to_string(instancesByModule.size()));

            std::size_t moduleIndex = 0;
            for (const auto &entry : pendingOutputsByModule)
            {
                if (moduleIndex % 10 == 0)
                {
                    logInfo("xmr pending output module progress " + std::to_string(moduleIndex) +
                            "/" + std::to_string(pendingOutputsByModule.size()));
                }
                auto instIt = instancesByModule.find(entry.first);
                if (instIt == instancesByModule.end())
                {
                    ++moduleIndex;
                    continue;
                }
                const auto &instances = instIt->second;
                const auto &pendingPorts = entry.second;
                for (const auto &inst : instances)
                {
                    for (const PendingPort *pending : pendingPorts)
                    {
                        ensureInstanceOutput(*inst.graph, inst.opId, pending->portName,
                                             pending->width, pending->isSigned);
                    }
                }
                ++moduleIndex;
            }

            moduleIndex = 0;
            for (const auto &entry : pendingInputsByModule)
            {
                if (moduleIndex % 10 == 0)
                {
                    logInfo("xmr pending input module progress " + std::to_string(moduleIndex) +
                            "/" + std::to_string(pendingInputsByModule.size()));
                }
                auto instIt = instancesByModule.find(entry.first);
                if (instIt == instancesByModule.end())
                {
                    ++moduleIndex;
                    continue;
                }
                const auto &instances = instIt->second;
                const auto &pendingPorts = entry.second;
                for (const auto &inst : instances)
                {
                    for (const PendingPort *pending : pendingPorts)
                    {
                        const grh::ir::Operation op = inst.graph->getOperation(inst.opId);
                        if (instanceHasInput(op, pending->portName))
                        {
                            continue;
                        }
                        grh::ir::ValueId padValue =
                            getPadInput(*inst.graph, pending->width, pending->isSigned);
                        ensureInstanceInput(*inst.graph, inst.opId, pending->portName, padValue);
                    }
                }
                ++moduleIndex;
            }
            logInfo("xmr pending ports end");
        }

        std::string message = "xmr ops=" + std::to_string(xmrOpCount);
        message.append(", reads=");
        message.append(std::to_string(xmrReadCount));
        message.append(", writes=");
        message.append(std::to_string(xmrWriteCount));
        message.append(", newInputPorts=");
        message.append(std::to_string(pendingInputPorts.size()));
        message.append(", newOutputPorts=");
        message.append(std::to_string(pendingOutputPorts.size()));
        message.append(result.changed ? ", changed=true" : ", changed=false");
        logInfo(std::move(message));
        return result;
    }

} // namespace wolf_sv_parser::transform
